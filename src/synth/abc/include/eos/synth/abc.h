// abc: the EOS facade over the imported ABC logic synthesis and
// verification engine (L4 engines).
//
// ABC is adopted as EOS's logic optimization and equivalence
// engine: its code lives in this module as EOS's own code (see
// PROVENANCE.md) and builds as the eda_synth_abc target. This
// header is the only surface other modules may use. It exposes no
// upstream ABC types: callers hand the engine gate-level Verilog
// netlists and a genlib library (the cell format ABC maps to), and
// get back a delay-mapped netlist or an equivalence verdict. The
// upstream frame, its command interpreter and every upstream object
// stay behind the implementation in src/abc_facade.cc.
//
// The operations run in process against the imported code, through
// ABC's own command interpreter (the same embedding upstream's
// library mode, libabc, exists for); no external process is ever
// started. ABC keeps a single global frame, so only one AbcEngine
// may be alive at a time; constructing a second while one is alive
// is reported by its operations failing with kInvalidState, and
// engines used strictly one after another work as expected.
//
// This header is a stable surface.
#ifndef EOS_SYNTH_ABC_H_
#define EOS_SYNTH_ABC_H_

#include <memory>
#include <string>
#include <vector>

#include "eos/foundation/errors.h"

namespace eos::synth {

using foundation::Result;
using foundation::Status;

// The verdict of an equivalence proof. kUnknown is a first-class
// outcome, never a pass: the prover finished without a conclusion
// (timeout, resource limit, an unreadable input), and callers must
// treat an unknown proof exactly like a failed one when deciding
// whether an edit may stand.
enum class EquivalenceVerdict {
  kEquivalent,
  kNotEquivalent,
  kUnknown,
};

// One technology-mapping target: delay-driven mapping (ABC's
// plain `map`) or area-driven mapping (ABC's `map -a`, which
// sets an infinite delay target and recovers area). The two are
// strategies of one command, not two commands.
enum class MapTarget {
  kDelay,
  kArea,
};

// One network as ABC's print_stats reports it: the AIG size in
// AND gates (inverters are free in an AIG and are not counted)
// and the logic depth in AND levels. These are the generic
// synthesis metrics: the "generic gate count" and "logic depth"
// a generic script is judged by, before any technology mapping.
struct AbcNetworkStats {
  int and_count = 0;
  int levels = 0;
};

// The result of one generic optimization: the network as read and
// hashed, and the network after the script ran, both in the
// print_stats terms above.
struct AbcGenericResult {
  AbcNetworkStats before;
  AbcNetworkStats after;
};

// One ABC session. Move-only; the destructor releases the upstream
// frame.
class AbcEngine {
 public:
  AbcEngine();
  ~AbcEngine();

  AbcEngine(const AbcEngine&) = delete;
  AbcEngine& operator=(const AbcEngine&) = delete;
  AbcEngine(AbcEngine&&) noexcept;
  AbcEngine& operator=(AbcEngine&&) noexcept;

  // Delay-driven technology mapping of one gate-level Verilog
  // netlist onto the cells of a genlib library: reads the library
  // and the netlist, converts the network to an AIG, optimizes it
  // (structural hashing, AIG rewriting, balancing; the double
  // inversions and redundant logic a sizing-only flow cannot
  // remove disappear here), maps it for delay with ABC's mapper,
  // and writes the mapped netlist as Verilog. The output module
  // keeps the input module's name and port names; internal net and
  // instance names are ABC's. Fails with kIoError when an input
  // file cannot be read or the output cannot be written, and with
  // kInvalidState when the engine does not own the frame.
  Status MapForDelay(const std::string& input_verilog,
                     const std::string& genlib_path,
                     const std::string& output_verilog);

  // Converts a Liberty library to ABC's genlib format and writes
  // it to the given path, returning the emitted cell names in
  // alphabetical order. The conversion covers the Liberty subset
  // a genlib needs: a cell qualifies when it is combinational,
  // has exactly one output pin, that pin carries a function over
  // the cell's own input pins, and its delay is scalar (the
  // maximum of the pin's cell_rise and cell_fall scalar values).
  // Sequential cells (an ff or latch block), cells with a table
  // delay, and cells whose function cannot be expressed in
  // genlib syntax are skipped, never guessed at. The emitted
  // genlib line per cell is the same format the ECO cone
  // resynthesis uses internally (one PIN * line carrying the
  // scalar delay); this facade keeps its own copy because that
  // converter is internal to the timing-eco module, which
  // already depends on this facade, so sharing it would be a
  // circular link. Fails with kIoError when the Liberty file
  // cannot be read, holds no cells, or the genlib cannot be
  // written; and with kUnsupported when no cell qualifies.
  // The conversion itself does not touch the upstream frame,
  // so it does not fail on frame ownership: it is a method of
  // this engine only because genlib is ABC's format.
  Result<std::vector<std::string>> LibertyToGenlib(
      const std::string& liberty_path, const std::string& genlib_path);

  // Technology mapping of one unmapped network (the BLIF form
  // Yosys hands over, or an assign-form Verilog netlist) onto
  // the cells of a genlib library: reads the library and the
  // network, hashes it to an AIG, optimizes it at mapping time
  // (structural hashing, rewriting, balancing, the same steps
  // MapForDelay runs), maps it for the requested target, and
  // writes the mapped netlist as Verilog. The output module
  // keeps the input module's name and port names; internal net
  // and instance names are ABC's. Unlike MapForDelay, the input
  // is read unmapped: a mapped gate netlist belongs to
  // MapForDelay, not here. Fails with kIoError when an input
  // file cannot be read or the output cannot be written, and
  // with kInvalidState when the engine does not own the frame.
  Status MapTechnology(const std::string& input_network,
                       const std::string& genlib_path,
                       const std::string& output_verilog,
                       MapTarget target);

  // Generic (technology-agnostic) optimization of one Verilog
  // or BLIF netlist, with the classic ABC script: the network
  // is read unmapped (ABC's read dispatches on the file
  // extension), hashed to an AIG, optimized with a named
  // script, and written back out as Verilog. Scripts are data,
  // not code:
  // "default" is balance, rewrite, refactor, resub, balance;
  // "balance" is balance alone; "rewrite" is rewrite alone; and
  // "none" hashes only, which is the baseline the other scripts
  // are judged against. The returned stats are print_stats
  // before the script and after it, so a caller can judge the
  // script by its own numbers. An unknown script name fails with
  // kInvalidArgument before ABC is touched; a netlist ABC
  // cannot read fails with kIoError and writes nothing. Fails
  // with kInvalidState when the engine does not own the frame.
  Result<AbcGenericResult> OptimizeGeneric(
      const std::string& input_verilog, const std::string& script,
      const std::string& output_verilog);

  // Combinational equivalence proof of two gate-level Verilog
  // netlists whose cells come from the same genlib library, run
  // with ABC's own cec (fraiging plus SAT, in tree at the pin).
  // Each netlist is converted to an AIGER intermediate inside the
  // call (cec's own reader cannot read a mapped Verilog netlist;
  // the AIGER symbol table carries the names), and combinational
  // inputs and outputs are matched by name. The
  // verdict is read from the prover's own conclusion: equivalent
  // and not-equivalent are the prover's words; anything else (a
  // limit hit, a name mismatch, an unreadable file) is kUnknown.
  // Fails with kInvalidState only when the engine does not own the
  // frame; an unreadable netlist is a kUnknown verdict, not a
  // failure of the call, because "the proof did not run" must
  // never be confusable with "the proof passed".
  Result<EquivalenceVerdict> ProveEquivalent(const std::string& first_verilog,
                                             const std::string& second_verilog,
                                             const std::string& genlib_path);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace eos::synth

#endif  // EOS_SYNTH_ABC_H_
