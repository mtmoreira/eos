// extract: EOS-owned driver over the imported OpenROAD parasitic
// extractor (OpenRCX). See include/eos/pv/extract.h.
#include "eos/pv/extract.h"

#include <exception>
#include <fstream>
#include <string>

#include "odb/db.h"
#include "rcx/ext.h"
#include "utl/Logger.h"

namespace eos::pv {

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

bool FileIsNonEmpty(const std::string& path)
{
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  return in.good() && in.tellg() > 0;
}

}  // namespace

Status ExtractParasitics(odb::dbDatabase* db, const ExtractOptions& options,
                         ExtractReport* report)
{
  if (db == nullptr) {
    return InvalidState(
        "extract_parasitics: no database (run eos::import_odb first)");
  }
  odb::dbTech* tech = db->getTech();
  if (tech == nullptr) {
    return InvalidState(
        "extract_parasitics: no technology data (import a technology "
        "file first)");
  }
  odb::dbBlock* block = db->getChip() ? db->getChip()->getBlock() : nullptr;
  if (block == nullptr) {
    return InvalidState(
        "extract_parasitics: no mapped design (import a mapped netlist "
        "first)");
  }
  if (block->getRows().empty()) {
    return InvalidState(
        "extract_parasitics: design is not floorplanned (no rows; run "
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
        "extract_parasitics: design is not placed (no placed instance; "
        "run eos::place first)");
  }
  // Routed means at least one signal net carries a wire. Guides
  // alone do not count: the global stage writes guides, and only
  // the detailed stage writes wires, so a guides-only design is
  // unrouted for extraction. Supply nets and special nets are never
  // extracted, so they do not count here either.
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
        "extract_parasitics: design is not routed (no signal net "
        "carries a wire; run eos::route first)");
  }
  if (options.spef_path.empty()) {
    return InvalidState(
        "extract_parasitics: no SPEF output path (pass the output file "
        "as the command argument)");
  }
  if (options.model_path.empty()) {
    return InvalidState(
        "extract_parasitics: no extraction model file (pass -model "
        "<rules file>; without one the extractor has no RC tables "
        "and extracts nothing)");
  }
  if (!FileIsReadable(options.model_path)) {
    return InvalidState("extract_parasitics: cannot read extraction "
                        "model file " +
                        options.model_path);
  }

  utl::Logger logger;
  try {
    rcx::Ext extractor;
    extractor.init(db, &logger);

    rcx::Ext::ExtractOptions ext_options;
    ext_options.ext_model_file = options.model_path.c_str();
    extractor.extract(ext_options);

    // The extractor warns and returns zero segments when the model
    // file covers none of the routed layers; that is a failed run,
    // not an empty report. The segment count below is the block
    // total the extractor left in the database.
    int total_nets = 0;
    int rseg_count = 0;
    int cap_node_count = 0;
    int cc_seg_count = 0;
    block->getExtCount(total_nets, rseg_count, cap_node_count,
                       cc_seg_count);
    if (rseg_count == 0 || cap_node_count == 0) {
      return InvalidState(
          "extract_parasitics: extraction left no RC segments in the "
          "design (the model file may not cover the routed layers)");
    }

    rcx::Ext::SpefOptions spef_options;
    spef_options.file = options.spef_path.c_str();
    extractor.write_spef(spef_options);

    if (!FileIsNonEmpty(options.spef_path)) {
      return InvalidState(
          "extract_parasitics: extractor wrote no SPEF file at " +
          options.spef_path);
    }

    ExtractReport out;
    out.spef_path = options.spef_path;
    out.rseg_count = rseg_count;
    out.cap_node_count = cap_node_count;
    out.cc_seg_count = cc_seg_count;
    for (odb::dbNet* net : block->getNets()) {
      if (net->getSigType().isSupply() || net->isSpecial()) {
        continue;
      }
      if (!net->getRSegs().empty()) {
        ++out.nets_extracted;
      }
    }
    if (report != nullptr) {
      *report = out;
    }
    return Status::Ok();
  } catch (const std::exception& e) {
    // Logger::error throws std::runtime_error with the engine's
    // message; surface it as a runner failure, never as a crash.
    return InvalidState(std::string("extract_parasitics: engine failure: ") +
                        e.what());
  }
}

}  // namespace eos::pv
