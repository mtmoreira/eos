// cts: EOS-owned driver over the imported TritonCTS engine.
// See include/eos/pd/cts.h.
#include "eos/pd/cts.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <tcl.h>

#include "CtsOptions.h"
#include "cts/TritonCTS.h"
#include "cts_resizer.h"
#include "db_sta/dbNetwork.hh"
#include "db_sta/dbSta.hh"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "sta/MinMax.hh"
#include "sta/Scene.hh"
#include "sta/Sdc.hh"
#include "stt/SteinerTreeBuilder.h"
#include "utl/Logger.h"

namespace eos::pd {

namespace {

using foundation::ErrorCode;
using foundation::Status;

// Legalize the buffer instances the engine inserted: TritonCTS
// places buffers at computed tree points, not on the site grid
// and not necessarily inside the die (on the clocked demo one
// buffer landed with its pins at negative x, outside the die).
// A buffer off the grid or off the die gets no pin access from
// the detailed router (its via check fails), so the run fails
// with DRT-0073. The fix snaps each new buffer to the nearest
// site column and row, clamps it inside the die, and walks
// nearby grid points when the snapped spot overlaps another
// instance. Only unrotated (R0, LEF N) buffers are moved: the
// engine places its buffers unrotated, and any other
// orientation is left alone rather than guessed at. The place record is a snapshot
// of the pre-CTS placement, so this does not touch it; the
// CTS report is counts only, so it is unaffected too.
void LegalizeCtsBuffers(odb::dbBlock* block,
                        const std::set<odb::dbInst*>& before)
{
  odb::dbSet<odb::dbRow> rows = block->getRows();
  if (rows.empty()) {
    return;
  }
  const odb::Rect die = block->getDieArea();
  odb::dbRow* first_row = *rows.begin();
  const int site_width = first_row->getSite()->getWidth();
  const int row_height = first_row->getSite()->getHeight();
  if (site_width <= 0 || row_height <= 0) {
    return;
  }
  int grid_x0 = 0;
  int grid_y0 = 0;
  first_row->getOrigin(grid_x0, grid_y0);
  std::vector<int> row_y;
  for (odb::dbRow* row : rows) {
    int rx = 0;
    int ry = 0;
    row->getOrigin(rx, ry);
    row_y.push_back(ry);
  }
  std::sort(row_y.begin(), row_y.end());

  // Occupied boxes: every instance except the one being moved.
  // Rebuilt per buffer so already-legalized buffers count too.
  auto overlaps = [](int x0, int y0, int x1, int y1,
                     const std::vector<odb::Rect>& boxes) {
    for (const odb::Rect& b : boxes) {
      if (x0 < b.xMax() && b.xMin() < x1 && y0 < b.yMax() &&
          b.yMin() < y1) {
        return true;
      }
    }
    return false;
  };

  for (odb::dbInst* inst : block->getInsts()) {
    if (before.count(inst) != 0) {
      continue;
    }
    if (inst->getOrient() != odb::dbOrientType::R0) {
      continue;
    }
    odb::dbMaster* master = inst->getMaster();
    if (master == nullptr) {
      continue;
    }
    const int width = master->getWidth();
    const int height = master->getHeight();
    int x = 0;
    int y = 0;
    inst->getLocation(x, y);

    // Nearest site column and row, then clamp the grid indices
    // so the whole cell stays inside the die.
    const int max_col =
        (die.xMax() - width - grid_x0) / site_width;
    const int max_row = static_cast<int>(row_y.size()) - 1;
    if (max_col < 0 || max_row < 0) {
      continue;
    }
    int col = static_cast<int>(
        std::llround(static_cast<double>(x - grid_x0) / site_width));
    col = std::max(0, std::min(max_col, col));
    int row = 0;
    int best = std::abs(y - row_y[0]);
    for (int i = 1; i <= max_row; i++) {
      const int d = std::abs(y - row_y[i]);
      if (d < best) {
        best = d;
        row = i;
      }
    }

    // Walk nearby grid points (increasing Manhattan distance)
    // for the first spot that does not overlap another cell.
    bool placed = false;
    for (int radius = 0; radius <= 8 && !placed; radius++) {
      for (int dc = -radius; dc <= radius && !placed; dc++) {
        for (int dr = -radius; dr <= radius && !placed; dr++) {
          if (std::abs(dc) + std::abs(dr) != radius) {
            continue;
          }
          const int c = col + dc;
          const int r = row + dr;
          if (c < 0 || c > max_col || r < 0 || r > max_row) {
            continue;
          }
          const int nx = grid_x0 + c * site_width;
          const int ny = row_y[r];
          if (nx < die.xMin() || nx + width > die.xMax() ||
              ny < die.yMin() || ny + height > die.yMax()) {
            continue;
          }
          std::vector<odb::Rect> boxes;
          for (odb::dbInst* other : block->getInsts()) {
            if (other == inst) {
              continue;
            }
            boxes.push_back(other->getBBox()->getBox());
          }
          if (!overlaps(nx, ny, nx + width, ny + height, boxes)) {
            inst->setLocation(nx, ny);
            placed = true;
          }
        }
      }
    }
    // No free spot nearby: keep the snapped position anyway;
    // the router will fail on it the same way it did before.
    if (!placed) {
      inst->setLocation(grid_x0 + col * site_width, row_y[row]);
    }
  }
}

// ShimGuard mirrors the place runner's guard: the engines read
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

// TclInterpGuard mirrors the place runner's guard: dbSta::init
// routes reporting through ReportTcl, whose setTclInterp and
// destructor touch the interp's channel stack, so a null interp
// segfaults in Tcl_GetThreadData. The runner owns a private interp
// for its STA session; it is created before the session and
// destroyed after it (declared before sta below), and it is never
// shared with the caller's session. No STA Tcl commands are
// evaluated, so Sta_Init is not needed here.
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

std::string JoinSpace(const std::vector<std::string>& words)
{
  std::string out;
  for (const std::string& word : words) {
    if (!out.empty()) {
      out += " ";
    }
    out += word;
  }
  return out;
}

}  // namespace

Status RunCts(odb::dbDatabase* db, const CtsOptions& options,
              CtsReport* report)
{
  if (db == nullptr) {
    return InvalidState("cts: no database (run eos::import_odb first)");
  }
  odb::dbTech* tech = db->getTech();
  if (tech == nullptr) {
    return InvalidState(
        "cts: no technology data (import a technology file first)");
  }
  odb::dbBlock* block = db->getChip() ? db->getChip()->getBlock() : nullptr;
  if (block == nullptr) {
    return InvalidState(
        "cts: no mapped design (import a mapped netlist first)");
  }
  if (block->getRows().empty()) {
    return InvalidState(
        "cts: design is not floorplanned (no rows; run eos::floorplan "
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
        "cts: design is not placed (no placed instance; run eos::place "
        "first)");
  }
  if (options.liberty_path.empty()) {
    return InvalidState(
        "cts: no Liberty library (CTS characterization needs a "
        "Liberty library for its buffer cells)");
  }

  utl::Logger logger;
  TclInterpGuard tcl_guard;
  if (tcl_guard.interp == nullptr) {
    return InvalidState(
        "cts: could not create a Tcl interpreter for the STA session");
  }
  std::unique_ptr<sta::dbSta> sta;
  try {
    sta.reset(new sta::dbSta());
    sta->init(tcl_guard.interp, db, &logger);
    sta::dbNetwork* network = sta->getDbNetwork();
    network->setBlock(block);

    // The database was loaded before this STA session existed, so
    // no postReadDb callback ever fired for it: without this call
    // no master has a staCell and the engine's Liberty lookup for
    // a buffer master dereferences null. readDbAfter builds the
    // ConcreteCell per master (same step the normal LEF/DEF flow
    // performs); the Liberty read below then links each cell to
    // its Liberty cell through readLibertyAfter. Same pattern as
    // the repair flow (repair_commands.cc).
    network->readDbAfter(db);

    // Liberty first: the resizer and the engine's characterization
    // both read Liberty cells, and a missing file must fail here
    // with a readable message, not inside the engine.
    sta::Scene* scene = sta->cmdScene();
    if (scene == nullptr) {
      sta::Sta::setSta(nullptr);
      return InvalidState("cts: STA session has no default scene");
    }
    if (sta->readLiberty(options.liberty_path, scene,
                         sta::MinMaxAll::all(), false) == nullptr) {
      sta::Sta::setSta(nullptr);
      return InvalidState("cts: could not read Liberty file " +
                          options.liberty_path);
    }

    ShimGuard guard(db, network, &logger);

    // The resizer is held through an opaque pointer (see
    // cts_resizer.h): this unit must not include the resizer
    // header, or the pdr FLUTE definitions it pulls in collide
    // with the ead3236 Steiner-tree definitions above. The guard
    // frees it on every exit path, including engine exceptions.
    struct ResizerGuard {
      ResizerGuard() = default;
      rsz::Resizer* p = nullptr;
      ~ResizerGuard()
      {
        if (p != nullptr) {
          DeleteCtsResizer(p);
        }
      }
      ResizerGuard(const ResizerGuard&) = delete;
      ResizerGuard& operator=(const ResizerGuard&) = delete;
    };
    ResizerGuard resizer_guard;
    resizer_guard.p = CreateCtsResizer(&logger, db, sta.get());
    rsz::Resizer* resizer = resizer_guard.p;

    stt::SteinerTreeBuilder stt_builder;
    stt_builder.init(db, &logger);

    cts::TritonCTS triton;
    triton.init(&logger, db, network, sta.get(), &stt_builder, resizer);
    if (!options.buffer_list.empty()) {
      triton.setBufferList(JoinSpace(options.buffer_list).c_str());
    } else {
      // Empty input triggers Liberty inference inside the engine.
      triton.setBufferList("");
    }
    if (!options.root_buffer.empty()) {
      triton.setRootBuffer(options.root_buffer.c_str());
    }
    if (!options.sink_buffer.empty()) {
      triton.setSinkBuffer(options.sink_buffer.c_str());
    }

    // Clock selection, fail-fast before the engine runs: the
    // engine only warns on a design with no clock nets, so an
    // empty clock list here, an unresolvable clock net name, and
    // zero clock roots after the run are all runner failures.
    // An explicitly named net that the engine cannot resolve
    // returns nonzero from setClockNets.
    bool have_clock = false;
    if (!options.clock_nets.empty()) {
      if (triton.setClockNets(JoinSpace(options.clock_nets).c_str()) !=
          0) {
        sta::Sta::setSta(nullptr);
        return InvalidState(
            "cts: clock net not found in the design (check the "
            "-clock argument)");
      }
      have_clock = true;
    } else if (scene->sdc() != nullptr &&
               !scene->sdc()->clocks().empty()) {
      have_clock = true;
    }
    if (!have_clock) {
      sta::Sta::setSta(nullptr);
      return InvalidState(
          "cts: design has no clock (name a clock net with -clock)");
    }

    // Snapshot the instances before the engine runs, so the
    // legalization below can tell the buffers it inserted from
    // the gates that were already placed.
    std::set<odb::dbInst*> before;
    for (odb::dbInst* inst : block->getInsts()) {
      before.insert(inst);
    }

    triton.runTritonCts();
    triton.reportCtsMetrics();

    // The engine places its buffers off the site grid and
    // sometimes off the die; snap them legal so the router can
    // reach their pins (see LegalizeCtsBuffers).
    LegalizeCtsBuffers(block, before);

    cts::CtsOptions* parms = triton.getParms();
    CtsReport out;
    out.clock_root_count = parms->getNumClockRoots();
    out.buffers_inserted = parms->getNumBuffersInserted();
    out.sink_count = parms->getNumSinks();
    out.clock_subnet_count = parms->getNumClockSubnets();
    // Skew and insertion delay in ns are not engine outputs (see
    // include/eos/pd/cts.h); the flags stay false by design.

    // Free the resizer before clearing the STA singleton (the
    // guard still covers the exception path, where this block is
    // skipped).
    if (resizer_guard.p != nullptr) {
      DeleteCtsResizer(resizer_guard.p);
      resizer_guard.p = nullptr;
    }
    sta::Sta::setSta(nullptr);

    if (out.clock_root_count == 0) {
      return InvalidState(
          "cts: design has no clock (no clock tree was built; "
          "check the -clock argument)");
    }
    if (report != nullptr) {
      *report = out;
    }
    return Status::Ok();
  } catch (const std::exception& e) {
    // The Sta singleton must never dangle on the destroyed dbSta:
    // clear it before the unique_ptr frees the session, so a later
    // run_timing or a second cts in the same process starts clean.
    sta::Sta::setSta(nullptr);
    return InvalidState(std::string("cts: engine failure: ") + e.what());
  }
}

}  // namespace eos::pd
