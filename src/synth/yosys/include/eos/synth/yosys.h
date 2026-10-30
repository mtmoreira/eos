// yosys: the EOS facade over the imported Yosys synthesis engine
// (L4 engines).
//
// Yosys is adopted as EOS's front end and generic synthesis engine:
// its code lives in this module as EOS's own code (see
// PROVENANCE.md) and builds as the eda_synth_yosys target. This
// header is the only surface other modules may use. It exposes no
// upstream Yosys types: callers hand the engine a Verilog file and
// get back a summary of the modules the front end parsed. The
// upstream design, its pass registry and every upstream object
// stay behind the implementation in src/yosys_facade.cc.
//
// The engine runs in process against the imported code, through
// Yosys's own embedding entry points (yosys_setup, run_pass,
// yosys_shutdown); no external process is ever started. Yosys
// keeps a single global design, so only one YosysEngine may be
// alive at a time; constructing a second while one is alive is
// reported by its operations failing with kInvalidState, and
// engines used strictly one after another work as expected.
//
// Linking rule for every consumer of this module: link
// eda_synth_yosys whole-archive
// ("$<LINK_LIBRARY:WHOLE_ARCHIVE,eda_synth_yosys>"). Yosys
// registers its passes and frontends through global constructors,
// and a plain static-archive pull never includes an object
// nothing references, so without whole-archive no pass is
// registered and every call fails with "No such command".
//
// Two upstream behaviours shape this facade and are recorded here
// because they are load-bearing:
// - A Yosys log_error (a syntax error in the input, an
//   unsupported construct in a pass) terminates the process with
//   _Exit: that is upstream's behaviour for its executable, and
//   there is no library hook that turns it into a return value.
//   ParseVerilog therefore fails fast only on an unreadable file
//   (kIoError, before Yosys is touched); a malformed file is a
//   documented limitation of this unit, not a tested failure
//   mode, because testing it would kill the test runner. Unit S1
//   must resolve this before read_verilog changes semantics to
//   parse: either a containment for the abort, or a documented
//   rule that a malformed file aborts the shell the way a
//   malformed file aborts the yosys executable.
// - Yosys's log sinks are installed by its executable's main,
//   which this library does not compile, so the facade installs
//   upstream's non-tty default sink (ConsoleLogSink) at first
//   setup; without it, all Yosys output, including command
//   errors, would be silently discarded.
//
// This header is a stable surface.
#ifndef EOS_SYNTH_YOSYS_H_
#define EOS_SYNTH_YOSYS_H_

#include <string>
#include <vector>

#include "eos/foundation/errors.h"

namespace eos::synth {

using foundation::Result;
using foundation::Status;

// One module of a parsed design, as the front end saw it: its name,
// its ports in declaration order, and how many wires the module
// declares. This is a summary, not the design: S1's elaborate step
// owns the full netlist representation.
struct YosysModuleSummary {
  std::string name;
  std::vector<std::string> ports;
  int wire_count = 0;
};

// The summary of one ParseVerilog call: every module the front end
// parsed, in the order Yosys stores them.
struct YosysDesignSummary {
  std::vector<YosysModuleSummary> modules;
};

// One Yosys session. Move-only; the destructor releases only
// the single-engine guard. The Yosys session itself lives for
// the process: pass registration is one-shot per process (a
// shutdown clears the registries and a later setup registers
// nothing), so an engine must never shut Yosys down. Each parse
// still gets a fresh design, by replacing the design object.
class YosysEngine {
 public:
  YosysEngine();
  ~YosysEngine();

  YosysEngine(const YosysEngine&) = delete;
  YosysEngine& operator=(const YosysEngine&) = delete;
  YosysEngine(YosysEngine&&) noexcept;
  YosysEngine& operator=(YosysEngine&&) noexcept;

  // The version string of the imported Yosys (the pin's version
  // and commit, baked in at build time by the module's CMake).
  static std::string Version();

  // Parses one Verilog file with the Yosys Verilog front end and
  // returns the module summary above. The design is fresh for
  // every call: a previous parse on this engine does not leak
  // modules into the next one. Fails with kIoError when the file
  // cannot be read, and with kInvalidState when the engine does
  // not own the Yosys session. A malformed file aborts the
  // process (see the header note); it is not a Status.
  Result<YosysDesignSummary> ParseVerilog(const std::string& path);

 private:
  bool owns_ = false;
};

}  // namespace eos::synth

#endif  // EOS_SYNTH_YOSYS_H_
