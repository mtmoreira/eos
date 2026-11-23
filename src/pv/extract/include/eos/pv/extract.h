// extract: the EOS-owned driver over the imported OpenROAD parasitic
// extractor (OpenRCX, L4 engines).
//
// The runner operates on the live OpenDB database: the caller hands
// it the database the session owns, and the extractor annotates
// that database in place (RC segments and capacitance nodes on
// every extracted net) and writes a SPEF file for it. It never
// copies the design through a file. The imported engine code itself
// lives in this module (P6 import); this header is the only surface
// other modules use.
//
// Unlike the place, cts, and route runners, this runner needs
// neither the ord singleton nor a private Tcl interpreter: rcx::Ext
// takes the database and logger directly and has no Tcl surface in
// EOS (the upstream Tcl factory is not imported), so there is no
// singleton to guard and nothing stale a later run could see.
//
// Preconditions, defined precisely: the block must hold rows (a
// floorplanned design, as for place, cts, and route), at least one
// placed instance, and at least one signal net carrying a wire.
// Routed means exactly that last condition: a design with guides
// only, or with no wires at all, fails fast as "not routed" with a
// message saying to run eos::route first. Supply nets and special
// nets are never extracted, so they do not count toward routed.
//
// Extraction needs an RC model file: the extractor's tables come
// from the rules file (the upstream -ext_model_file), and without
// one it logs a warning and extracts nothing. The runner therefore
// fails fast when no model path is given or the file cannot be
// read, instead of writing an empty SPEF. The model file is an
// input to the run, not a product of it: EOS ships no default
// model, because a default would silently stand in for the
// process data the signoff numbers depend on.
//
// The SPEF the extractor writes is judged by the existing
// extracted STA path (set_spef plus extracted mode), not by a
// second STA engine in this module: the runner reports the extract
// counts, and the tests compare estimated STA against extracted
// STA on the written SPEF.
//
// Wire preservation contract (extract-fix unit): the extractor's
// pre-pass (odb::orderWires) re-encodes every routed net's wire
// stream in place as a connectivity traversal tree before the RC
// builder reads it. That reordered encoding is the extractor's
// input, not a product of the run: the runner snapshots each
// affected net's wire stream before the extractor runs and
// restores it after the SPEF is written, so on success the only
// change the command leaves in the database is the RC annotation
// (segments and capacitance nodes on the nets), and on failure
// the wire geometry and the nets' wire-ordered/disconnected
// flags are left exactly as they were found. Downstream geometry
// consumers (finish, the GDS writer, the DRC and LVS walks) read
// the routed wires the router wrote, never the traversal trees.
// One named exception: a processed net's special wires are not
// preserved (odb offers no verbatim SWire copy; their
// destruction inside the ordering pass is pre-existing engine
// behavior, unchanged by the runner).
//
// This header is a stable surface.
#ifndef EOS_PV_EXTRACT_H_
#define EOS_PV_EXTRACT_H_

#include <string>

#include "eos/foundation/errors.h"

namespace odb {
class dbDatabase;
}  // namespace odb

namespace eos::pv {

using foundation::Status;

// Options for one extraction run. spef_path names the SPEF file
// the run writes; model_path names the extraction rules file the
// extractor's RC tables come from. Both are required: an empty
// spef_path or model_path fails fast before the engine runs.
struct ExtractOptions {
  std::string spef_path;
  std::string model_path;
};

// Measured outcome of a run. nets_extracted counts the signal nets
// that carry at least one RC segment after the run; rseg_count,
// cap_node_count, and cc_seg_count are the block totals the
// extractor left in the database (see dbBlock::getExtCount).
// spef_path echoes the file the run wrote.
struct ExtractReport {
  int nets_extracted = 0;
  int rseg_count = 0;
  int cap_node_count = 0;
  int cc_seg_count = 0;
  std::string spef_path;
};

// Runs parasitic extraction (rcx::Ext::extract with the model file,
// then rcx::Ext::write_spef) on the block of db, and fills report.
// Fails with kInvalidState when the database holds no technology
// data or no mapped design (no top block), when the block holds no
// rows (an un-floorplanned design), when no instance is placed (an
// unplaced design), when no signal net carries a wire (an unrouted
// design), when the SPEF or model path is empty or the model file
// cannot be read, and when the run leaves no RC segments or writes
// no SPEF file. Engine logger errors surface as kInvalidState with
// the engine's message; on any failure after the engine runs, the
// database's wire geometry and net flags are restored to their
// pre-run state (see the wire preservation contract above), while
// any RC annotation the engine left is reported rather than
// hidden.
// A second run on the same database re-extracts and rewrites the
// SPEF; it never fails on the first run's data.
Status ExtractParasitics(odb::dbDatabase* db, const ExtractOptions& options,
                         ExtractReport* report);

}  // namespace eos::pv

#endif  // EOS_PV_EXTRACT_H_
