// extract: EOS-owned driver over the imported OpenROAD parasitic
// extractor (OpenRCX). See include/eos/pv/extract.h.
#include "eos/pv/extract.h"

#include <exception>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

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

// Snapshot/restore of routed wire geometry around one extraction
// run (extract-fix design sections 3.2 and 3.3).
//
// rcx::Ext::extract opens with odb::orderWires, whose
// connectivity analyzer (tmg_conn::treeReorder) re-encodes every
// non-supply net's dbWire in place as a traversal tree of its
// point-connectivity graph: segments off the chosen tree are
// dropped, layers change, and via stubs can be left
// geometrically disconnected. The reordered encoding is the
// extractor's input contract, so the reorder itself stays; the
// defect is that its product replaces the routed wires in the
// session database. This guard snapshots each affected net's
// wire stream before the extractor runs and restores it after
// the SPEF is written, so the database the command returns
// carries the routed wires the router wrote. The extraction
// computation runs on identical inputs through identical code,
// so every extracted count and the SPEF are unchanged.
//
// The copy is verbatim: dbWire::append copies the source wire's
// raw opcode/data vectors (dbWire.cpp). A holding wire is an
// unattached wire in the same block, created with
// dbWire::create(block, true); it lives only for the duration of
// the call and is destroyed by the restore. The extractor
// iterates nets, not block wires, so holding wires are invisible
// to the run between snapshot and restore.
//
// The snapshot set is exactly the set orderWires can touch:
// supply nets are skipped by orderWires and by this guard.
// Flags (is_disconnected, is_wire_ordered) are recorded for
// every non-supply net and written back verbatim: analyzeNet
// rewrites both on every net it processes. Streams are
// snapshotted for non-supply nets that are not already
// wire-ordered and carry a wire; already-ordered nets take the
// non-re-encoding branch, so their streams cannot change. For a
// non-ordered net with no wire, the absence is recorded, and a
// wire the reorder created for it (treeReorder creates one for
// a terminal-carrying wire-less net) is destroyed by the
// restore.
//
// Restore is unconditional and runs from the destructor, so it
// fires on the success path (after write_spef and the report
// counts, as the scope unwinds) and on every failure path after
// construction (the zero-coverage and empty-SPEF failures, an
// engine exception): a failed extract leaves the database's wire
// geometry and net flags exactly as it found them. Restoring a
// wire the engine happened not to rewrite is a byte-identical
// no-op in effect, so no per-net dirty tracking is needed.
// Restoring is_wire_ordered=false on reordered nets matters: a
// later extract or antenna repair must re-run the ordering on
// the restored (unordered) encoding, exactly as it would have
// before this command ever ran.
//
// Special wires are deliberately not restored (design section
// 3.4): odb offers no verbatim SWire stream copy at this pin,
// and analyzeNet's destruction of a processed net's SWires is
// pre-existing behavior this guard neither fixes nor worsens.
class WirePreservationGuard {
 public:
  explicit WirePreservationGuard(odb::dbBlock* block) : block_(block) {
    for (odb::dbNet* net : block_->getNets()) {
      if (net->getSigType().isSupply()) {
        continue;
      }
      NetSnapshot snap;
      snap.net = net;
      snap.was_disconnected = net->isDisconnected();
      snap.was_wire_ordered = net->isWireOrdered();
      snap.had_wire = net->getWire() != nullptr;
      if (!snap.was_wire_ordered && snap.had_wire) {
        snap.holding = odb::dbWire::create(block_, /*global_wire=*/true);
        snap.holding->append(net->getWire());
      }
      snapshots_.push_back(std::move(snap));
    }
  }

  WirePreservationGuard(const WirePreservationGuard&) = delete;
  WirePreservationGuard& operator=(const WirePreservationGuard&) = delete;

  ~WirePreservationGuard() {
    try {
      Restore();
    } catch (...) {
      // Restore runs during scope unwinding on the failure paths
      // too; a restore exception must never escape a destructor.
    }
  }

  // Total opcode/data entries held across all snapshot holding
  // wires (design section 8: the snapshotted stream volume is a
  // reported measurement, not a gate).
  std::size_t SnapshottedEntries() const {
    std::size_t total = 0;
    for (const NetSnapshot& snap : snapshots_) {
      if (snap.holding != nullptr) {
        total += snap.holding->length();
      }
    }
    return total;
  }

 private:
  struct NetSnapshot {
    odb::dbNet* net = nullptr;
    odb::dbWire* holding = nullptr;
    bool was_disconnected = false;
    bool was_wire_ordered = false;
    bool had_wire = false;
  };

  void Restore() {
    for (NetSnapshot& snap : snapshots_) {
      snap.net->setDisconnected(snap.was_disconnected);
      snap.net->setWireOrdered(snap.was_wire_ordered);
      if (snap.holding != nullptr) {
        if (snap.net->getWire() != nullptr) {
          odb::dbWire::destroy(snap.net->getWire());
        }
        odb::dbWire* fresh = odb::dbWire::create(snap.net);
        fresh->append(snap.holding);
        odb::dbWire::destroy(snap.holding);
        snap.holding = nullptr;
      } else if (!snap.had_wire && !snap.was_wire_ordered &&
                 snap.net->getWire() != nullptr) {
        // The reorder created a wire for a wire-less net; the
        // pre-run state had none.
        odb::dbWire::destroy(snap.net->getWire());
      }
    }
  }

  odb::dbBlock* block_;
  std::vector<NetSnapshot> snapshots_;
};

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

    // Snapshot the routed wire streams before the extractor's
    // orderWires pre-pass re-encodes them; the guard restores
    // them as this scope unwinds, after write_spef and the
    // report counts on the success path and on every failure
    // path alike (see the guard's comment above).
    WirePreservationGuard guard(block);

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
