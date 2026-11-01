// floorplan runner: EOS-owned driver over InitFloorplan.
// See include/eos/pd/floorplan.h for the contract.
#include "eos/pd/floorplan.h"

#include <exception>
#include <vector>

#include "db_sta/dbNetwork.hh"
#include "ifp/InitFloorplan.hh"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"

namespace eos::pd {
namespace {

using foundation::ErrorCode;

odb::Rect ToRect(const std::array<int, 4>& area)
{
  return odb::Rect(area[0], area[1], area[2], area[3]);
}

std::array<int, 4> FromRect(const odb::Rect& rect)
{
  return {rect.xMin(), rect.yMin(), rect.xMax(), rect.yMax()};
}

bool IsEmpty(const std::array<int, 4>& area)
{
  return area[2] <= area[0] || area[3] <= area[1];
}

// Sets the ord shim from this run's session state and clears it
// when the run ends, so the shim never dangles on the stack-owned
// logger and network after RunFloorplan returns. Later engines
// set it again from their own run's session state.
class ShimGuard
{
 public:
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

}  // namespace

Status RunFloorplan(odb::dbDatabase* db, const FloorplanOptions& options,
                    FloorplanReport* report)
{
  if (db == nullptr) {
    return Status::Error(ErrorCode::kInvalidState,
                         "no technology data loaded: no database");
  }
  if (db->getTech() == nullptr) {
    return Status::Error(ErrorCode::kInvalidState,
                         "no technology data loaded: the database "
                         "holds no technology");
  }
  if (db->getChip() == nullptr || db->getChip()->getBlock() == nullptr) {
    return Status::Error(ErrorCode::kInvalidState,
                         "no mapped design: the database holds no top "
                         "block (eos::import_odb first)");
  }
  odb::dbBlock* block = db->getChip()->getBlock();

  std::array<int, 4> die = options.die_area;
  if (!options.has_die_area) {
    die = FromRect(block->getDieArea());
  }
  if (IsEmpty(die)) {
    return Status::Error(
        ErrorCode::kInvalidArgument,
        "no die area: the design holds no die area and none was "
        "given (eos::floorplan -die_area <lx> <ly> <ux> <uy>)");
  }
  std::array<int, 4> core = options.core_area;
  if (!options.has_core_area) {
    core = die;
  }
  if (IsEmpty(core)) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "empty core area: -core_area must have "
                         "positive width and height");
  }

  utl::Logger logger;
  sta::dbNetwork network;
  try {
    network.init(db, &logger);
    network.setBlock(block);
  } catch (const std::exception& error) {
    return Status::Error(ErrorCode::kInvalidState,
                         std::string("dbNetwork setup failed: ") +
                             error.what());
  }

  // The ord shim lets engines read the session state this run is
  // built on; P1 engines read it only through the dbNetwork passed
  // to InitFloorplan, but the shim is set here so the surface is
  // live from the first engine import on.
  ShimGuard shim(db, &network, &logger);

  try {
    ifp::InitFloorplan floorplan(block, &logger, &network);
    std::vector<odb::dbSite*> extra_sites;
    if (!options.site.empty()) {
      odb::dbSite* site = floorplan.findSite(options.site.c_str());
      if (site == nullptr) {
        return Status::Error(ErrorCode::kNotFound,
                             "unknown site: " + options.site);
      }
      extra_sites.push_back(site);
    }
    floorplan.initFloorplan(ToRect(die), ToRect(core), extra_sites);
    floorplan.makeTracks();
  } catch (const std::exception& error) {
    return Status::Error(ErrorCode::kInvalidState,
                         std::string("floorplan failed: ") + error.what());
  }

  if (report != nullptr) {
    report->die_area = FromRect(block->getDieArea());
    report->core_area = core;
    int rows = 0;
    for (odb::dbRow* row : block->getRows()) {
      (void)row;
      ++rows;
    }
    report->row_count = rows;
    int grids = 0;
    for (odb::dbTrackGrid* grid : block->getTrackGrids()) {
      (void)grid;
      ++grids;
    }
    report->track_grid_count = grids;
    int total = 0;
    int placed = 0;
    for (odb::dbBTerm* bterm : block->getBTerms()) {
      ++total;
      if (bterm->getFirstPinPlacementStatus().isPlaced()) {
        ++placed;
      }
    }
    report->io_total_count = total;
    report->io_placed_count = placed;
  }
  return Status::Ok();
}

}  // namespace eos::pd
