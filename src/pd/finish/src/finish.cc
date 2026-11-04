// finish: EOS-owned chip-finish driver. See include/eos/pd/finish.h.
#include "eos/pd/finish.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <string>
#include <vector>

#include "dpl/Opendp.h"
#include "fin/Finale.h"
#include "gds_stream.h"
#include "odb/db.h"
#include "pdn/PdnGen.hh"
#include "utl/Logger.h"

namespace eos::pd {

namespace {

using foundation::ErrorCode;
using foundation::Status;

Status InvalidState(const std::string& msg)
{
  return Status::Error(ErrorCode::kInvalidState, msg);
}

bool FileIsReadable(const std::string& path)
{
  std::ifstream in(path);
  return in.good();
}

long long FileSizeBytes(const std::string& path)
{
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in.good()) {
    return -1;
  }
  return static_cast<long long>(in.tellg());
}

// Shared fail-fast preconditions for finish and stream-out, in the
// order the extract runner checks them: database, technology,
// mapped design. Row, placement, and routed checks live in
// RunFinish only: stream-out judges the finish marker, not the
// flow stage, so a finished design that was reloaded still streams.
Status CheckDb(odb::dbDatabase* db, const char* command,
               odb::dbBlock** block_out)
{
  if (db == nullptr) {
    return InvalidState(std::string(command) +
                        ": no database (run eos::import_odb first)");
  }
  odb::dbTech* tech = db->getTech();
  if (tech == nullptr) {
    return InvalidState(std::string(command) +
                        ": no technology data (import a technology "
                        "file first)");
  }
  odb::dbBlock* block = db->getChip() ? db->getChip()->getBlock() : nullptr;
  if (block == nullptr) {
    return InvalidState(std::string(command) +
                        ": no mapped design (import a mapped netlist "
                        "first)");
  }
  if (block_out != nullptr) {
    *block_out = block;
  }
  return Status::Ok();
}

int CountFillers(odb::dbBlock* block)
{
  int count = 0;
  for (odb::dbInst* inst : block->getInsts()) {
    odb::dbMaster* master = inst->getMaster();
    if (master != nullptr && master->isFiller()) {
      ++count;
    }
  }
  return count;
}

// Special-wire boxes on the power and ground nets: the grid's
// written geometry, counted the same way before and after a run so
// a second run reports the same count instead of doubling it.
int CountGridShapes(odb::dbBlock* block)
{
  int count = 0;
  for (odb::dbNet* net : block->getNets()) {
    const odb::dbSigType::Value sig = net->getSigType().getValue();
    if (sig != odb::dbSigType::POWER && sig != odb::dbSigType::GROUND) {
      continue;
    }
    for (odb::dbSWire* swire : net->getSWires()) {
      count += static_cast<int>(swire->getWires().size());
    }
  }
  return count;
}

bool MarkerIsSet(odb::dbBlock* block, const char* name)
{
  odb::dbBoolProperty* prop = odb::dbBoolProperty::find(block, name);
  return prop != nullptr && prop->getValue();
}

void SetMarker(odb::dbBlock* block, const char* name, bool value)
{
  odb::dbBoolProperty* prop = odb::dbBoolProperty::find(block, name);
  if (prop != nullptr) {
    prop->setValue(value);
    return;
  }
  odb::dbBoolProperty::create(block, name, value);
}

// Routing layers of the technology, lowest routing level first.
std::vector<odb::dbTechLayer*> RoutingLayers(odb::dbTech* tech)
{
  std::vector<odb::dbTechLayer*> layers;
  for (odb::dbTechLayer* layer : tech->getLayers()) {
    if (layer->getType() == odb::dbTechLayerType::ROUTING &&
        layer->getRoutingLevel() > 0) {
      layers.push_back(layer);
    }
  }
  std::sort(layers.begin(), layers.end(),
            [](odb::dbTechLayer* a, odb::dbTechLayer* b) {
              return a->getRoutingLevel() < b->getRoutingLevel();
            });
  return layers;
}

}  // namespace

Status RunFinish(odb::dbDatabase* db, const FinishOptions& options,
                 FinishReport* report)
{
  odb::dbBlock* block = nullptr;
  if (Status status = CheckDb(db, "finish", &block); !status.ok()) {
    return status;
  }
  odb::dbTech* tech = db->getTech();
  if (block->getRows().empty()) {
    return InvalidState(
        "finish: design is not floorplanned (no rows; run "
        "eos::floorplan first)");
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
        "finish: design is not placed (no placed instance; run "
        "eos::place first)");
  }
  // Routed means at least one signal net carries a wire, exactly as
  // for extraction: guides alone do not count, and supply nets and
  // special nets never count.
  bool any_routed = false;
  for (odb::dbNet* net : block->getNets()) {
    if (net->getSigType().isSupply() || net->isSpecial()) {
      continue;
    }
    if (net->getWire() != nullptr) {
      any_routed = true;
      break;
    }
  }
  if (!any_routed) {
    return InvalidState(
        "finish: design is not routed (no signal net carries a "
        "wire; run eos::route first)");
  }
  if (!options.fill_config_path.empty() &&
      !FileIsReadable(options.fill_config_path)) {
    return InvalidState("finish: cannot read metal-fill rules file " +
                        options.fill_config_path);
  }

  // Filler masters come from the library, never from a name list:
  // a master is a filler master exactly when OpenDB says so (LEF
  // CLASS CORE SPACER). A library with none fails fast here, before
  // any step mutates the design.
  std::vector<odb::dbMaster*> filler_masters;
  for (odb::dbLib* lib : db->getLibs()) {
    for (odb::dbMaster* master : lib->getMasters()) {
      if (master->isFiller()) {
        filler_masters.push_back(master);
      }
    }
  }
  if (filler_masters.empty()) {
    return InvalidState(
        "finish: library holds no filler cell master (no CORE "
        "SPACER master; add filler cells to the technology first)");
  }

  // The power and ground nets, when the caller asks for the grid:
  // the first net of each signal type. The failure message below
  // names the missing net class.
  odb::dbNet* power = nullptr;
  odb::dbNet* ground = nullptr;
  for (odb::dbNet* net : block->getNets()) {
    if (net->getSigType().getValue() == odb::dbSigType::POWER &&
        power == nullptr) {
      power = net;
    }
    if (net->getSigType().getValue() == odb::dbSigType::GROUND &&
        ground == nullptr) {
      ground = net;
    }
  }
  if (options.with_pdn && (power == nullptr || ground == nullptr)) {
    return InvalidState(
        "finish: -pdn needs a power net and a ground net in the "
        "design (signal types POWER and GROUND); the design holds "
        "neither or only one");
  }

  const bool grid_already_built = MarkerIsSet(block, "eos_finish_pdn");

  utl::Logger logger;
  try {
    // Filler insertion, through the in-tree detailed placer's
    // fillerPlacement: it fills only the gaps that remain, so a
    // second finish run inserts nothing new and the count below
    // comes back identical.
    {
      dpl::Opendp opendp;
      opendp.init(db, &logger);
      dpl::dbMasterSeq masters = filler_masters;
      opendp.fillerPlacement(&masters, "eos_fill_");
    }

    // Basic power grid (only when asked, and only once: a second
    // -pdn run counts the existing grid instead of rebuilding it,
    // because PdnGen writes a fresh set of special wires on every
    // build and a rebuild would duplicate the grid).
    if (options.with_pdn && !grid_already_built) {
      std::vector<odb::dbTechLayer*> routing = RoutingLayers(tech);
      if (routing.empty()) {
        return InvalidState(
            "finish: -pdn needs at least one routing layer in the "
            "technology");
      }
      odb::dbTechLayer* follow_layer = routing.front();
      odb::dbTechLayer* strap_layer =
          routing.size() > 1 ? routing[1] : routing.front();
      // The via between the two grid layers, when the technology
      // defines one: the grid's connect step needs it, and a grid
      // without it would leave followpins and straps unconnected.
      std::vector<odb::dbTechVia*> grid_vias;
      for (odb::dbTechVia* via : tech->getVias()) {
        if (via->getBottomLayer() == follow_layer &&
            via->getTopLayer() == strap_layer) {
          grid_vias.push_back(via);
        }
        if (via->getBottomLayer() == strap_layer &&
            via->getTopLayer() == follow_layer) {
          grid_vias.push_back(via);
        }
      }

      pdn::PdnGen pdngen;
      pdngen.init(db, &logger);
      pdngen.setCoreDomain(power, nullptr, ground, {});
      pdn::VoltageDomain* domain = pdngen.findDomain("Core");
      if (domain == nullptr) {
        return InvalidState(
            "finish: -pdn could not create the core voltage domain");
      }
      pdngen.makeCoreGrid(domain, "eos_core_grid", pdn::GRID,
                          {follow_layer}, {}, nullptr, nullptr, nullptr);
      std::vector<pdn::Grid*> grids = pdngen.findGrid("eos_core_grid");
      if (grids.empty()) {
        return InvalidState(
            "finish: -pdn could not create the core grid");
      }
      pdn::Grid* grid = grids.front();
      pdngen.makeFollowpin(grid, follow_layer,
                           static_cast<int>(follow_layer->getWidth()),
                           pdn::CORE);
      if (routing.size() > 1) {
        const int strap_width =
            3 * static_cast<int>(strap_layer->getWidth());
        const int strap_pitch =
            20 * static_cast<int>(strap_layer->getWidth());
        pdngen.makeStrap(grid, strap_layer, strap_width,
                         static_cast<int>(strap_layer->getWidth()),
                         strap_pitch, 0, 0, true, pdn::GRID, pdn::CORE,
                         {power, ground});
        if (!grid_vias.empty()) {
          pdngen.makeConnect(grid, follow_layer, strap_layer, 0, 0,
                             {}, grid_vias, 0, 0, {}, {}, "");
        }
      }
      pdngen.checkSetup();
      pdngen.buildGrids(true);
      pdngen.writeToDb(true);
    }

    // Metal fill, only with an explicit rules file. The fill area
    // is the die area; an empty die area fails here, not inside
    // the engine.
    if (!options.fill_config_path.empty()) {
      const odb::Rect die = block->getDieArea();
      if (die.dx() <= 0 || die.dy() <= 0) {
        return InvalidState(
            "finish: design has no die area (run eos::floorplan "
            "first)");
      }
      fin::Finale finale;
      finale.init(db, &logger);
      finale.densityFill(options.fill_config_path.c_str(), die);
    }
  } catch (const std::exception& e) {
    // Logger::error throws std::runtime_error with the engine's
    // message; surface it as a runner failure, never as a crash.
    return InvalidState(std::string("finish: engine failure: ") +
                        e.what());
  }

  SetMarker(block, "eos_finish", true);
  SetMarker(block, "eos_finish_pdn",
            options.with_pdn || grid_already_built);
  if (report != nullptr) {
    FinishReport out;
    out.filler_count = CountFillers(block);
    out.grid_shape_count = CountGridShapes(block);
    out.metal_fill_count = static_cast<int>(block->getFills().size());
    out.with_pdn = MarkerIsSet(block, "eos_finish_pdn");
    *report = out;
  }
  return Status::Ok();
}

Status WriteGds(odb::dbDatabase* db, const WriteGdsOptions& options,
                WriteGdsReport* report)
{
  odb::dbBlock* block = nullptr;
  if (Status status = CheckDb(db, "write_gds", &block); !status.ok()) {
    return status;
  }
  if (!MarkerIsSet(block, "eos_finish")) {
    return InvalidState(
        "write_gds: finish has not run on this design (run "
        "eos::finish first)");
  }
  if (options.path.empty()) {
    return InvalidState("write_gds: no output path (pass the GDSII "
                        "file as the command argument)");
  }
  gds::GdsStreamReport stream_report;
  std::string error;
  if (!gds::WriteGdsStream(db, options.path, &stream_report, &error)) {
    return InvalidState("write_gds: " + error);
  }
  const long long size = FileSizeBytes(options.path);
  if (size <= 0) {
    return InvalidState("write_gds: wrote an empty file at " +
                        options.path);
  }
  if (report != nullptr) {
    WriteGdsReport out;
    out.path = options.path;
    out.size_bytes = size;
    out.instance_count = stream_report.instance_count;
    out.structure_count = stream_report.structure_count;
    *report = out;
  }
  return Status::Ok();
}

}  // namespace eos::pd
