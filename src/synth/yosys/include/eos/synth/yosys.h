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
//   Unit S1 resolves this with the documented rule the S0 note
//   allowed: a malformed file aborts the calling process with
//   exit 1, exactly as it aborts the yosys executable, and that
//   abort is not a Status. Everything else is a Status:
//   ParseVerilog fails fast on an unreadable file (kIoError,
//   before Yosys is touched); a command-level rejection (an
//   unknown top in hierarchy, a pass that refuses the design)
//   comes back through the command-error throw scope as
//   kInvalidArgument; and Elaborate validates the top name
//   before any pass runs, so a bad top name never reaches a pass
//   at all. A malformed file is therefore not a tested failure
//   mode in process (testing it would kill the test runner); its
//   evidence is a separate-process run of the eos shell, recorded
//   in the S1 unit report.
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

// One cell type in a netlist, with how many cells of that type the
// netlist holds. Sorted by type name, so two runs of the same
// design report the same order.
struct YosysCellTypeCount {
  std::string type;
  int count = 0;
};

// The shape of one elaborated module, as the elaboration passes
// left it: its cell and wire counts, the cell count per type, and
// its logic depth. Logic depth is the longest chain of
// combinational cells from a primary input or a sequential output
// to any wire, counted in cells: a primary input and a sequential
// output have depth 0, a combinational cell's output has depth
// one more than the deepest of its inputs, and sequential cells
// (flip-flops, memories, FSMs) break the chain. A design with no
// combinational cell has depth 0. sequential_cells counts the
// cells whose type the facade treats as sequential (the same
// types that break the depth chain); synthesis unit S2 uses it
// to refuse mapping a sequential design, whose latches ABC's
// mapped Verilog writer renders as behavioural blocks rather
// than mapped gates.
struct YosysNetlistStats {
  std::string module;
  int cell_count = 0;
  int wire_count = 0;
  int logic_depth = 0;
  int sequential_cells = 0;
  std::vector<YosysCellTypeCount> cells_by_type;
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
  // modules into the next one. The parsed design stays live in
  // the Yosys session after the call returns, so Elaborate can
  // run on it; a later ParseVerilog replaces it. Fails with
  // kIoError when the file cannot be read, and with
  // kInvalidState when the engine does not own the Yosys
  // session. A malformed file aborts the process (see the header
  // note); it is not a Status.
  Result<YosysDesignSummary> ParseVerilog(const std::string& path);

  // Parses several Verilog files into one design, in order, with
  // the given include directories and preprocessor defines
  // applied to every file. This is the engine half of
  // read_filelist: the caller owns the list-file format, resolves
  // each entry, and checks readability first, so this call only
  // validates its own arguments and parses. The design is fresh
  // for the call (a previous parse does not leak modules in),
  // and stays live afterwards, so Elaborate can run on it. Each
  // file is read with one read_verilog pass carrying every -I
  // and -D flag, so a define or an include directory applies to
  // the whole list, not to one file. Fails with kInvalidArgument
  // when the file list is empty, an include directory is empty,
  // or a define token is malformed (its name, before any '=',
  // must be a plain Verilog identifier); with kIoError when a
  // file cannot be read; and with kInvalidState when the engine
  // does not own the Yosys session. A define token or an
  // include directory containing whitespace or ';' is rejected
  // for the same reason a top name must be a plain identifier:
  // the values are interpolated into a Yosys pass command
  // string, which Yosys tokenizes on whitespace and splits on
  // ';', so such a value could smuggle a second command in. A
  // malformed file aborts the process, exactly as for
  // ParseVerilog; a missing `include target is the same class
  // of upstream abort, not a Status.
  Result<YosysDesignSummary> ParseVerilogFiles(
      const std::vector<std::string>& paths,
      const std::vector<std::string>& include_dirs,
      const std::vector<std::string>& defines);

  // Elaborates the parsed design's top module: hierarchy
  // resolution, process lowering, FSM and memory identification.
  // This is the unoptimized netlist: no optimization pass runs
  // here, so a later generic script has something to improve.
  // The top name is validated before any Yosys pass runs (it must
  // be a plain Verilog identifier), so a bad top name is a Status
  // and never a pass command string. Fails with
  // kInvalidArgument when the top module is not among the parsed
  // modules or a pass rejects the design, and with kInvalidState
  // when the engine does not own the Yosys session. The
  // elaborated design stays live in the session, so AigMap and
  // WriteVerilog can run on it.
  Result<YosysNetlistStats> Elaborate(const std::string& top);

  // Recomputes the stats of the design currently live in the
  // session, for the named module, without running any pass.
  // Fails with kInvalidArgument when the module is not in the
  // live design, and with kInvalidState when the engine does not
  // own the Yosys session.
  Result<YosysNetlistStats> Stats(const std::string& top);

  // Maps the live design onto the AIG form ABC reads: techmap
  // first, which converts sequential cells to their simple
  // forms ($dff to $_DFF_P_, so the BLIF backend writes them
  // as .latch lines) using the staged share techmap.v, then
  // aigmap for the combinational cells. Sequential cells stay
  // sequential, only in the simple form. Fails with
  // kInvalidArgument when a pass rejects the design, and with
  // kInvalidState when the engine does not own the session.
  Status AigMap();

  // Writes the live design as Verilog to the given path, in the
  // form the Verilog backend produces (simple gates as continuous
  // assignments), without attributes: ABC's Verilog reader
  // rejects Yosys attribute lines, so the bridge form omits
  // them. Fails with kIoError when the file cannot be
  // written, with kInvalidArgument when a pass rejects the
  // design, and with kInvalidState when the engine does not own
  // the session.
  Status WriteVerilog(const std::string& path);

  // Writes the live design as BLIF to the given path, for the
  // named top module (validated as a plain identifier before
  // any pass runs, like Elaborate). This is the bridge to ABC:
  // the Verilog backend renders a sequential cell as a vector
  // always block, and ABC's Verilog reader cannot parse a
  // vector flop (it looks up the whole bus name as one net and
  // reports the output wire undefined), while the BLIF backend
  // bit-blasts buses and writes each flip-flop as a .latch
  // line, which ABC's BLIF reader handles natively. Fails with
  // kIoError when the file cannot be written, with
  // kInvalidArgument when the top is not a plain identifier or
  // a pass rejects the design, and with kInvalidState when the
  // engine does not own the session.
  Status WriteBlif(const std::string& path, const std::string& top);

 private:
  bool owns_ = false;
};

}  // namespace eos::synth

#endif  // EOS_SYNTH_YOSYS_H_
