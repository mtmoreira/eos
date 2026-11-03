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
#include "triton_route/TritonRoute.h"
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
  if (options.max_detailed_iterations < 1) {
    return InvalidState(
        "route: detailed iteration bound starts at 1 (the loop must "
        "run at least one rip-up and reroute pass)");
  }
  for (const std::string* name :
       {&options.bottom_routing_layer, &options.top_routing_layer}) {
    if (name->empty()) {
      continue;
    }
    odb::dbTechLayer* layer = tech->findLayer(name->c_str());
    if (layer == nullptr ||
        layer->getType() != odb::dbTechLayerType::ROUTING) {
      return InvalidState("route: detailed layer " + *name +
                          " is not a routing layer in the technology");
    }
  }
  if (options.liberty_path.empty()) {
    return InvalidState(
        "route: no Liberty library (the engine's clock-net "
        "detection builds the STA graph and fails without one)");
  }

  // Re-route starts from the placed state: a previous run leaves
  // guides from the global stage and wires from the detailed
  // stage in the database, and the global router treats a net
  // that already has a wire as routed, so it would report zero
  // new routes on a second run. Strip both before the engine
  // runs, so every run routes the same placed design. Supply
  // and special nets are never routed here, so they keep their
  // wires and guides.
  for (odb::dbNet* net : block->getNets()) {
    if (net->getSigType().isSupply() || net->isSpecial()) {
      continue;
    }
    net->clearGuides();
    odb::dbWire* wire = net->getWire();
    if (wire != nullptr) {
      odb::dbWire::destroy(wire);
    }
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

    if (out.net_total_count > 0 && out.net_routed_count == 0) {
      sta::Sta::setSta(nullptr);
      return InvalidState(
          "route: engine routed zero nets on a design that holds nets");
    }

    // Detailed stage (P5): TritonRoute in single-step mode, driven
    // by the EOS-owned rip-up and reroute loop. main() runs pin
    // access, guide reading, region preparation, and track
    // assignment, then initializes (but does not run) the detailed
    // router; each stepDR is one rip-up and reroute pass. The first
    // pass rips up everything and follows the guides; later passes
    // repair DRC markers only, with the engine strategy's DRC
    // entry parameters. The loop stops early at zero violations
    // and stops at the bound otherwise; a nonzero count is
    // reported, never failed on, and the report states whether
    // the loop stopped at the bound. Distributed routing stays
    // off: the runner passes a null distributed backend, and the
    // drt port copies fail loudly on any distributed call (see
    // src/pd/drt/PATCHES.md). The detailed router reads the
    // technology, design, and guides from the database directly,
    // so it needs no STA session of its own; the private STA
    // session above stays alive until the end of this scope.
    triton_route::TritonRoute detailed;
    triton_route::ParamStruct params;
    params.doPa = true;
    params.singleStepDR = true;
    params.verbose = 1;
    params.enableViaGen = true;
    if (!options.bottom_routing_layer.empty()) {
      params.bottomRoutingLayer = options.bottom_routing_layer;
    }
    if (!options.top_routing_layer.empty()) {
      params.topRoutingLayer = options.top_routing_layer;
    }
    detailed.setParams(params);
    detailed.init(tcl_guard.interp, db, &logger, nullptr, &stt_builder);
    detailed.main();
    int iterations = 0;
    while (iterations < options.max_detailed_iterations) {
      if (iterations == 0) {
        detailed.stepDR(7, 0, 3, 8, 0, 8, 0.95f, 1, true);
      } else {
        detailed.stepDR(7, 0, 8, 8, 32, 16, 0.95f, 0, false);
      }
      ++iterations;
      if (detailed.getNumDRVs() == 0) {
        break;
      }
    }
    detailed.endFR();
    // endFR recounts the markers after writing the routed design
    // back to the database (FlexDR::end changes no markers, it only
    // reports), so this count is the count of the written design.
    out.violation_count = detailed.getNumDRVs();
    out.detailed_iterations_used = iterations;
    out.detailed_iteration_bound = options.max_detailed_iterations;
    out.detailed_stopped_at_bound =
        (out.violation_count > 0 &&
         iterations >= options.max_detailed_iterations);

    sta::Sta::setSta(nullptr);
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
