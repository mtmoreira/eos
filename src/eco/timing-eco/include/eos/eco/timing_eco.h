// timing-eco: the v1 timing ECO adapters (L4 engines).
//
// Two function-preserving edits, both meant to run inside an ECO
// transaction (src/eco/transactions), which supplies the
// checkpoint, the all-scene re-analysis and the commit or roll
// back decision. Called outside a transaction an adapter that
// fails midway can leave the edits it already made in place; the
// transaction is what makes an edit atomic.
//
// - ResizeInstance swaps one instance to another drive variant of
//   the same cell: the two masters must carry the same signal pins
//   (names and directions), so every connection carries over and
//   function is preserved by construction. Which variant is
//   stronger is the caller's knowledge (from the Liberty timing);
//   the adapter checks shape, not speed.
// - InsertBuffer splits one net: the original net keeps its driver
//   and gains the buffer's input; every load moves to a new net
//   driven by the buffer's output. Function is preserved by
//   construction when the buffer cell is a buffer (its output is
//   its input, per its Liberty function); the adapter checks the
//   cell's shape (exactly one input and one output signal pin),
//   not its logic function, and says so here.
//
// Disclosed simplifications (also for the phase exit report):
// - An inserted buffer is placed at its driver instance's
//   location. No placement legalization runs in v1; the designs in
//   scope have room at that location by construction, and a real
//   legalizer is physical-design work, not ECO work.
// - Buffering a net whose driver is a top-level port, or whose
//   loads include a top-level port, is refused (kUnsupported):
//   port connections live outside the instance-pin surface the
//   adapters move. Instance-driven, instance-loaded nets are the
//   v1 scope.
//
// This header is a stable surface.
#ifndef EOS_ECO_TIMING_ECO_H_
#define EOS_ECO_TIMING_ECO_H_

#include <string>
#include <vector>

#include "eos/db/odb.h"
#include "eos/foundation/errors.h"

namespace eos::eco {

using foundation::Result;
using foundation::Status;

// Swaps instance to the master new_master in database. Fails with
// kNotFound for an unknown instance or master, with
// kInvalidArgument when new_master is the instance's current
// master or the two masters' signal pin sets differ.
Status ResizeInstance(db::OdbDatabase& database, const std::string& instance,
                      const std::string& new_master);

// Inserts a buffer of master buffer_master on net net_name: the
// new instance new_instance_name is placed at the driver
// instance's location, its input connects to net_name, its output
// drives the new net new_net_name, and every load of net_name
// moves to new_net_name. Fails with kNotFound for an unknown net
// or buffer master, kInvalidArgument for a taken instance or net
// name or a buffer master without exactly one input and one output
// signal pin, and kUnsupported when the net's driver is not
// exactly one instance output pin or any load is a top-level port.
Status InsertBuffer(db::OdbDatabase& database, const std::string& net_name,
                    const std::string& buffer_master,
                    const std::string& new_instance_name,
                    const std::string& new_net_name);

// ---------------------------------------------------------------------------
// Resynthesis (Phase 2 unit 2.6): the third adapter, and the first
// one that changes logic. A sizing swap or a buffer insertion
// preserves function by construction; a remapped cone does not, so
// this adapter carries its own proof gate, stated in the Revised
// Plan, Section 9: any logic change requires an equivalence proof,
// and an inconclusive proof is unknown, never pass.
//
// The cone behind target_net (the transitive fanin of the net's
// driver, through combinational instances only) is extracted with
// explicit boundary conditions: the walk stops at top-level ports
// and at instances whose master is named in the request's
// sequential_masters list; those boundary nets are the cone's
// inputs, and every cone net that also drives something outside
// the cone ( plus the target net itself) is a cone output. The
// cone is remapped for delay with ABC (src/synth/abc) onto the
// cells of the request's Liberty library, generated as a genlib
// from the Liberty cells' functions and scalar delays, and the
// mapped cone is spliced back: the cone's instances are deleted,
// the mapped instances are created at the first cone instance's
// location (the same disclosed placement simplification as buffer
// insertion), cone-internal nets are recreated under fresh names,
// and the boundary and output nets, with every outside load, are
// never touched.
//
// The proof: before and after the splice, the adapter writes a
// combinational model of the FULL design, not just the cone: the
// design's combinational instances verbatim, with every sequential
// instance cut at its boundaries (its output nets become model
// inputs, its input nets become model outputs; the registers
// themselves are untouched by the splice, so combinational
// equivalence of the two models is function preservation of the
// design). ABC's own cec (in tree at the pin) proves the two
// models equivalent, or the splice is rolled back from a binary
// checkpoint of the database and the outcome records the verdict:
// kNotEquivalent and kUnknown both leave the design exactly as it
// was, and only kEquivalent lets the edit stand. Used inside an
// ECO transaction (the intended use), a rolled-back resynthesis
// presents as an unchanged design, which the transaction's
// all-scene verdict then rejects as no improvement; the proof
// verdict itself is in the outcome for the caller's record.
//
// v1 scope, stated rather than implied:
// - The Liberty timing must be scalar (one cell_rise and
//   cell_fall value per arc): a table-model cell cannot be given
//   an honest genlib delay, so a cone containing one is refused
//   with kUnsupported, as is a cone instance whose master carries
//   more than one output signal pin or a function over pins other
//   than its own inputs.
// - A cone output the remap reduces to a bare wire (assign to a
//   boundary input) is spliced by moving its instance loads to
//   that boundary net; a port load on such a net is refused.
// - The proof models name nets and instances as Verilog
//   identifiers, so a design whose names need escaping is refused
//   with kUnsupported instead of being proved under other names.
// - Only the masters in the Liberty are mappable; a cone instance
//   whose master is missing there is refused with kUnsupported.

// The verdict of the mandatory equivalence proof, mirroring the
// ABC facade's EquivalenceVerdict one module over.
enum class ProofVerdict {
  kEquivalent,
  kNotEquivalent,
  kUnknown,
};

// Everything one resynthesis needs beyond the database.
struct ResynthesisRequest {
  // The net whose driving cone is resynthesized: the endpoint of
  // the violating path, named by its net (the timing service
  // reports no per-endpoint path detail in this version, so the
  // caller names the cone by the net it drives).
  std::string target_net;
  // Masters treated as sequential boundaries: instances of these
  // masters are never in a cone and are cut at their pins in the
  // proof models. The boundary conditions are the caller's
  // explicit statement, per the unit's terms.
  std::vector<std::string> sequential_masters;
  // The Liberty library the cone is timed and mapped against:
  // cell functions and scalar delays for the genlib, one library
  // for the whole design in v1.
  std::string liberty_path;
  // A directory the adapter writes its working files into (cone
  // and mapped netlists, proof models, genlib, and the binary
  // checkpoint the rollback restores from). Must name an existing
  // directory; the files are left there for inspection.
  std::string scratch_dir;
};

// The outcome of one resynthesis attempt. applied is true only
// when the proof passed and the splice stands. proof is the
// proof's verdict (kUnknown when the proof could not conclude).
// reason states the verdict or the deciding fact in words, for a
// ledger record or a report. cone_instances names the instances
// the splice deleted; created_instances the ones it created
// (empty unless applied).
struct ResynthesisOutcome {
  bool applied = false;
  ProofVerdict proof = ProofVerdict::kUnknown;
  std::string reason;
  std::vector<std::string> cone_instances;
  std::vector<std::string> created_instances;
};

// Extracts the cone behind request.target_net, remaps it for
// delay with ABC, splices the mapped cone back, and proves the
// full design equivalent before and after, per the banner above.
// A proof that fails or cannot conclude rolls the design back and
// is returned as an outcome with applied false; it is not a Status
// failure. A Status failure means the attempt itself could not be
// carried out (an unknown net, an unmappable cone, a checkpoint or
// splice failure); when a splice failure happens after edits were
// made, the design is rolled back first, and a failure of that
// rollback is the Status returned, saying so plainly.
Result<ResynthesisOutcome> ResynthesizeCone(
    db::OdbDatabase& database, const ResynthesisRequest& request);

// The splice half of ResynthesizeCone, with the replacement cone
// supplied as a gate-level Verilog netlist instead of produced by
// ABC: same extraction, same boundary conditions, same mandatory
// proof, same rollback. ResynthesizeCone is this function over
// ABC's mapped output. It exists as its own surface so the proof
// gate can be exercised directly (a replacement that computes the
// wrong function must be caught and rolled back), and so a later
// remapping engine can feed the same gated splice.
Result<ResynthesisOutcome> SpliceReplacementCone(
    db::OdbDatabase& database, const ResynthesisRequest& request,
    const std::string& replacement_verilog_path);

}  // namespace eos::eco

#endif  // EOS_ECO_TIMING_ECO_H_
