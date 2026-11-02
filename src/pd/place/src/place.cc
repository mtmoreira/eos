// place: EOS-owned driver over the imported placement engines.
// See include/eos/pd/place.h.
#include "eos/pd/place.h"

#include <exception>
#include <iterator>
#include <memory>
#include <string>
#include <tcl.h>

#include "ant/AntennaChecker.hh"
#include "db_sta/dbNetwork.hh"
#include "db_sta/dbSta.hh"
#include "dpl/Opendp.h"
#include "gpl/Replace.h"
#include "grt/GlobalRouter.h"
#include "grt/MakeGlobalRouter.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "place_resizer.h"
#include "stt/SteinerTreeBuilder.h"
#include "utl/Logger.h"

namespace eos::pd {

namespace {

using foundation::ErrorCode;
using foundation::Status;

// ShimGuard mirrors the floorplan runner's guard: the engines read
// the database, network, and logger through the ord singleton, and
// the guard clears it on return so a later run in the same session
// cannot see stale pointers.
struct ShimGuard {
  ShimGuard(odb::dbDatabase* db, sta::dbNetwork* network,
            utl::Logger* logger)
  {
    ord::OpenRoad::openRoad()->setDb(db);
    ord::OpenRoad::openRoad()->setDbNetwork(network);
    ord::OpenRoad::openRoad()->setLogger(logger);
  }
  ~ShimGuard()
  {
    ord::OpenRoad::openRoad()->setDb(nullptr);
    ord::OpenRoad::openRoad()->setDbNetwork(nullptr);
    ord::OpenRoad::openRoad()->setLogger(nullptr);
  }
  ShimGuard(const ShimGuard&) = delete;
  ShimGuard& operator=(const ShimGuard&) = delete;
};

Status InvalidState(const std::string& msg)
{
  return Status::Error(ErrorCode::kInvalidState, msg);
}

// TclInterpGuard mirrors the resizer scratch-sta pattern (P-30):
// dbSta::init routes reporting through ReportTcl, whose setTclInterp
// and destructor touch the interp's channel stack, so a null interp
// segfaults in Tcl_GetThreadData (observed in the place unit test
// and in the registry place test run in isolation). The runner owns
// a private interp for its STA session; it is created before the
// session and destroyed after it (declared before sta below), and
// it is never shared with the caller's session. No STA Tcl commands
// are evaluated, so Sta_Init is not needed here.
struct TclInterpGuard {
  TclInterpGuard()
  {
    interp = Tcl_CreateInterp();
    if (interp != nullptr && Tcl_Init(interp) != TCL_OK) {
      Tcl_DeleteInterp(interp);
      interp = nullptr;
    }
  }
  ~TclInterpGuard()
  {
    if (interp != nullptr) {
      Tcl_DeleteInterp(interp);
    }
  }
  TclInterpGuard(const TclInterpGuard&) = delete;
  TclInterpGuard& operator=(const TclInterpGuard&) = delete;
  Tcl_Interp* interp = nullptr;
};

}  // namespace

Status RunPlace(odb::dbDatabase* db, const PlaceOptions& options,
                PlaceReport* report)
{
  if (db == nullptr) {
    return InvalidState("place: no database (run eos::import_odb first)");
  }
  odb::dbTech* tech = db->getTech();
  if (tech == nullptr) {
    return InvalidState(
        "place: no technology data (import a technology file first)");
  }
  odb::dbBlock* block = db->getChip() ? db->getChip()->getBlock() : nullptr;
  if (block == nullptr) {
    return InvalidState(
        "place: no mapped design (import a mapped netlist first)");
  }
  if (block->getRows().empty()) {
    return InvalidState(
        "place: design is not floorplanned (no rows; run eos::floorplan "
        "first)");
  }

  utl::Logger logger;
  TclInterpGuard tcl_guard;
  if (tcl_guard.interp == nullptr) {
    return InvalidState(
        "place: could not create a Tcl interpreter for the STA session");
  }
  std::unique_ptr<sta::dbSta> sta;
  try {
    sta.reset(new sta::dbSta());
    sta->init(tcl_guard.interp, db, &logger);
    sta::dbNetwork* network = sta->getDbNetwork();
    network->setBlock(block);

    // Timing-driven placement needs a Liberty-loaded STA session:
    // the resizer's slack path has no library to read otherwise.
    // This is a fail-fast, not a silent fallback to untimed
    // placement, because the user asked for timing-driven mode.
    if (options.timing_driven &&
        network->defaultLibertyLibrary() == nullptr) {
      sta::Sta::setSta(nullptr);
      return InvalidState(
          "place: timing-driven placement needs a Liberty library in "
          "the STA session (load Liberty first)");
    }

    ShimGuard guard(db, network, &logger);

    // The resizer is held through an opaque pointer (see
    // place_resizer.h): this unit must not include the resizer
    // header, or the pdr FLUTE definitions it pulls in collide
    // with the ead3236 Steiner-tree definitions above. The guard
    // frees it on every exit path, including engine exceptions.
    struct ResizerGuard {
      ResizerGuard() = default;
      rsz::Resizer* p = nullptr;
      ~ResizerGuard()
      {
        if (p != nullptr) {
          DeleteTimingResizer(p);
        }
      }
      ResizerGuard(const ResizerGuard&) = delete;
      ResizerGuard& operator=(const ResizerGuard&) = delete;
    };
    ResizerGuard resizer_guard;
    if (options.timing_driven) {
      resizer_guard.p = CreateTimingResizer(&logger, db, sta.get());
    }
    rsz::Resizer* resizer = resizer_guard.p;

    stt::SteinerTreeBuilder stt_builder;
    stt_builder.init(db, &logger);
    dpl::Opendp opendp;
    opendp.init(db, &logger);
    // The antenna checker is constructed first (uninitialized) so
    // the router can take its address; GlobalRouter::init only
    // stores pointers, so the checker is then initialized with the
    // router as its route source. The router's antenna repair path
    // is not exercised by placement, but the checker must exist
    // for the router to construct its repair object.
    ant::AntennaChecker antenna;
    grt::GlobalRouter* grouter = grt::MakeGlobalRouter(
        &logger, &stt_builder, db, sta.get(), resizer, &antenna,
        &opendp);
    antenna.init(db, grouter, &logger);

    gpl::Replace replace;
    replace.init(db, sta.get(), resizer, grouter, &logger);
    replace.setTimingDrivenMode(options.timing_driven);
    // Routability-driven placement runs the global router, whose
    // clock-net detection builds the STA graph and throws STA-2141
    // without a Liberty library. Without Liberty, routability is
    // therefore off and plain Nesterov plus detailed placement run
    // instead; the router path stays live whenever Liberty is
    // loaded (the gcd evidence run exercises it). This is a
    // runner-level mode selection, not an engine change: the
    // router still fails loudly on clock nets in its own flows.
    replace.setRoutabilityDrivenMode(
        network->defaultLibertyLibrary() != nullptr);
    replace.doInitialPlace();
    replace.doNesterovPlace();

    opendp.detailedPlacement(0, 0, "", false);

    PlaceReport out;
    out.timing_driven = options.timing_driven;
    out.instance_total_count = static_cast<int>(block->getInsts().size());
    int placed = 0;
    for (odb::dbInst* inst : block->getInsts()) {
      if (inst->isPlaced()) {
        ++placed;
      }
    }
    out.instance_placed_count = placed;
    out.hpwl_dbu = opendp.hpwl();

    // Pairwise bounding-box overlap count over placed instances.
    // The engine's own checkPlacement below must also pass; this
    // count is the reportable number, not a second opinion.
    int overlaps = 0;
    odb::dbSet<odb::dbInst> insts = block->getInsts();
    for (auto it1 = insts.begin(); it1 != insts.end(); ++it1) {
      odb::dbInst* a = *it1;
      if (!a->isPlaced()) {
        continue;
      }
      int ax, ay;
      a->getLocation(ax, ay);
      odb::dbMaster* ma = a->getMaster();
      unsigned int aw = ma->getWidth();
      unsigned int ah = ma->getHeight();
      for (auto it2 = std::next(it1); it2 != insts.end(); ++it2) {
        odb::dbInst* b = *it2;
        if (!b->isPlaced()) {
          continue;
        }
        int bx, by;
        b->getLocation(bx, by);
        odb::dbMaster* mb = b->getMaster();
        unsigned int bw = mb->getWidth();
        unsigned int bh = mb->getHeight();
        bool x_overlap = ax < bx + static_cast<int>(bw) &&
                         bx < ax + static_cast<int>(aw);
        bool y_overlap = ay < by + static_cast<int>(bh) &&
                         by < ay + static_cast<int>(ah);
        if (x_overlap && y_overlap) {
          ++overlaps;
        }
      }
    }
    out.overlap_count = overlaps;

    // The engine's own placement report: throws (Logger::error is
    // noreturn) on any placed, in-rows, overlap, or site-alignment
    // failure, so a failing check is a runner error, not a report
    // footnote.
    opendp.checkPlacement(false, false, "");

    grt::DeleteGlobalRouter(grouter);
    // Free the resizer before clearing the STA singleton (the
    // guard still covers the exception path, where this block is
    // skipped).
    if (resizer_guard.p != nullptr) {
      DeleteTimingResizer(resizer_guard.p);
      resizer_guard.p = nullptr;
    }
    sta::Sta::setSta(nullptr);

    if (report != nullptr) {
      *report = out;
    }
    return Status::Ok();
  } catch (const std::exception& e) {
    // The Sta singleton must never dangle on the destroyed dbSta:
    // clear it before the unique_ptr frees the session, so a later
    // run_timing or a second place in the same process starts clean.
    sta::Sta::setSta(nullptr);
    return InvalidState(std::string("place: engine failure: ") + e.what());
  }
}

}  // namespace eos::pd
