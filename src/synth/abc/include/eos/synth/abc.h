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
