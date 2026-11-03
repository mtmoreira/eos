// route: EOS-owned driver over the imported OpenROAD global router.
// See include/eos/pd/route.h.
#include "eos/pd/route.h"

#include <exception>
#include <memory>
#include <string>
#include <tcl.h>
#include <vector>

#include "ant/AntennaChecker.hh"
#include "db_sta/dbNetwork.hh"
#include "db_sta/dbSta.hh"
#include "dpl/Opendp.h"
#include "grt/GlobalRouter.h"
#include "grt/MakeGlobalRouter.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "sta/MinMax.hh"
#include "sta/Scene.hh"
#include "stt/SteinerTreeBuilder.h"
#include "utl/Logger.h"

namespace eos::pd {

namespace {

using foundation::ErrorCode;
using foundation::Status;

// ShimGuard mirrors the place and cts runners' guard: the engines
// read the database, network, and logger through the ord singleton,
// and the guard clears it on return so a later run in the same
// session cannot see stale pointers.
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

// TclInterpGuard mirrors the place and cts runners' guard:
// dbSta::init routes reporting through ReportTcl, whose
// setTclInterp and destructor touch the interp's channel stack, so
// a null interp segfaults in Tcl_GetThreadData. The runner owns a
// private interp for its STA session; it is created before the
// session and destroyed after it (declared before sta below), and
// it is never shared with the caller's session. No STA Tcl
// commands are evaluated, so Sta_Init is not needed here.
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

Status RunRoute(odb::dbDatabase* db, const RouteOptions& options,
                RouteReport* report)
{
  if (db == nullptr) {
    return InvalidState("route: no database (run eos::import_odb first)");
  }
  odb::dbTech* tech = db->getTech();
  if (tech == nullptr) {
    return InvalidState(
        "route: no technology data (import a technology file first)");
  }
  odb::dbBlock* block = db->getChip() ? db->getChip()->getBlock() : nullptr;
  if (block == nullptr) {
    return InvalidState(
        "route: no mapped design (import a mapped netlist first)");
  }
  if (block->getRows().empty()) {
    return InvalidState(
        "route: design is not floorplanned (no rows; run eos::floorplan "
        "first)");
  }
  bool any_placed = false;
  for (odb::dbInst* inst : block->getInsts()) {
    if (inst->isPlaced()) {
      any_placed = true;
      break;
    }
  }
  if (!any_placed) {
    return InvalidState(
        "route: design is not placed (no placed instance; run eos::place "
        "first)");
  }
  if (options.min_routing_layer < 0 || options.max_routing_layer < 0) {
    return InvalidState(
        "route: routing layer numbers start at 1 (0 means the engine "
        "default)");
  }
  if (options.min_routing_layer > 0 && options.max_routing_layer > 0 &&
      options.min_routing_layer > options.max_routing_layer) {
    return InvalidState(
        "route: minimum routing layer is above the maximum routing "
        "layer");
  }
  if (options.liberty_path.empty()) {
    return InvalidState(
        "route: no Liberty library (the engine's clock-net "
        "detection builds the STA graph and fails without one)");
  }

  utl::Logger logger;
  TclInterpGuard tcl_guard;
  if (tcl_guard.interp == nullptr) {
    return InvalidState(
        "route: could not create a Tcl interpreter for the STA session");
  }
  std::unique_ptr<sta::dbSta> sta;
  try {
    sta.reset(new sta::dbSta());
    sta->init(tcl_guard.interp, db, &logger);
    sta::dbNetwork* network = sta->getDbNetwork();
    network->setBlock(block);

    // Same step as the cts runner: the database was loaded before
    // this STA session existed, so no postReadDb callback ever
    // fired for it; readDbAfter builds the ConcreteCell per master
    // so the engine's network lookups resolve.
    network->readDbAfter(db);

    // Liberty first: the engine's clock-net detection builds the
    // STA graph and throws STA-2141 without a library, so a
    // missing file must fail here with a readable message, not
    // inside the engine.
    sta::Scene* scene = sta->cmdScene();
    if (scene == nullptr) {
      sta::Sta::setSta(nullptr);
      return InvalidState("route: STA session has no default scene");
    }
    if (sta->readLiberty(options.liberty_path, scene,
                         sta::MinMaxAll::all(), false) == nullptr) {
      sta::Sta::setSta(nullptr);
      return InvalidState("route: could not read Liberty file " +
                          options.liberty_path);
    }

    ShimGuard guard(db, network, &logger);

    stt::SteinerTreeBuilder stt_builder;
    stt_builder.init(db, &logger);
    dpl::Opendp opendp;
    opendp.init(db, &logger);
    // The antenna checker is constructed first (uninitialized) so
    // the router can take its address; GlobalRouter::init only
    // stores pointers, so the checker is then initialized with the
    // router as its route source. The router's antenna repair path
    // is not exercised by global routing, but the checker must
    // exist for the router to construct its repair object, exactly
    // as in the place runner.
    ant::AntennaChecker antenna;
    grt::GlobalRouter* grouter = grt::MakeGlobalRouter(
        &logger, &stt_builder, db, sta.get(), nullptr, &antenna,
        &opendp);
    antenna.init(db, grouter, &logger);

    // Congestion is reported, never failed on: the global route
    // stage's job is to produce the congestion map; the detailed
    // route stage (P5) owns violations. The critical-nets
    // percentage is pinned to 0 because the wire-parasitics
    // builder it needs is still deferred (see grt PATCHES.md),
    // even though a Liberty library is now loaded.
    grouter->setAllowCongestion(true);
    grouter->setCriticalNetsPercentage(0);
    if (options.min_routing_layer > 0) {
      grouter->setMinRoutingLayer(options.min_routing_layer);
    }
    if (options.max_routing_layer > 0) {
      grouter->setMaxRoutingLayer(options.max_routing_layer);
    }

    grouter->globalRoute(true);

    RouteReport out;
    out.net_total_count = static_cast<int>(block->getNets().size());
    out.net_routed_count =
        static_cast<int>(grouter->getRoutes().size());
    int guides = 0;
    for (odb::dbNet* net : block->getNets()) {
      guides += static_cast<int>(net->getGuides().size());
    }
    out.guide_count = guides;

    // Congestion map from the block GCellGrid the engine wrote
    // (updateDbCongestion runs inside globalRoute). One entry per
    // routing layer in the engine's range; a missing grid is a
    // runner failure, not an empty map.
    odb::dbGCellGrid* grid = block->getGCellGrid();
    if (grid == nullptr) {
      grt::DeleteGlobalRouter(grouter);
      sta::Sta::setSta(nullptr);
      return InvalidState(
          "route: engine wrote no congestion grid for the block");
    }
    int min_layer = options.min_routing_layer > 0
                        ? options.min_routing_layer
                        : 1;
    int max_layer = options.max_routing_layer > 0
                        ? options.max_routing_layer
                        : tech->getRoutingLayerCount();
    for (int layer_no = min_layer; layer_no <= max_layer; ++layer_no) {
      odb::dbTechLayer* layer = tech->findRoutingLayer(layer_no);
      if (layer == nullptr) {
        continue;
      }
      odb::dbMatrix<odb::dbGCellGrid::GCellData> map =
          grid->getCongestionMap(layer);
      LayerCongestion entry;
      entry.layer = layer_no;
      entry.name = layer->getName();
      const bool horizontal =
          layer->getDirection() == odb::dbTechLayerDir::HORIZONTAL;
      for (unsigned int i = 0; i < map.numRows(); ++i) {
        for (unsigned int j = 0; j < map.numCols(); ++j) {
          const odb::dbGCellGrid::GCellData& cell = map(i, j);
          const unsigned int capacity =
              horizontal ? cell.horizontal_capacity : cell.vertical_capacity;
          const unsigned int usage =
              horizontal ? cell.horizontal_usage : cell.vertical_usage;
          entry.capacity += static_cast<long long>(capacity);
          entry.usage += static_cast<long long>(usage);
          if (usage > capacity) {
            entry.overflow +=
                static_cast<long long>(usage - capacity);
          }
        }
      }
      out.total_overflow += entry.overflow;
      out.per_layer.push_back(entry);
    }

    grt::DeleteGlobalRouter(grouter);
    sta::Sta::setSta(nullptr);

    if (out.net_total_count > 0 && out.net_routed_count == 0) {
      return InvalidState(
          "route: engine routed zero nets on a design that holds nets");
    }
    if (report != nullptr) {
      *report = out;
    }
    return Status::Ok();
  } catch (const std::exception& e) {
    // The Sta singleton must never dangle on the destroyed dbSta:
    // clear it before the unique_ptr frees the session, so a later
    // run_timing or a second route in the same process starts clean.
    sta::Sta::setSta(nullptr);
    return InvalidState(std::string("route: engine failure: ") + e.what());
  }
}

}  // namespace eos::pd
