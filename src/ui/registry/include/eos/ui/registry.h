// registry: the one command registry (L6 interface).
//
// Every EOS operation exists exactly once, here. The Tcl dialect, the
// Python API and the GUI adapter bind to this registry and inherit the
// same semantics; no interface keeps design state of its own or
// duplicates an operation (Revised Plan, Section 12).
//
// Commands live in the EOS product namespace: interfaces expose them
// as eos::<name>, so orchestration commands can never silently change
// the meaning of a native engine command. A command returns a
// CommandResult: a machine-readable payload (key=value lines, units
// explicit), a diagnostic from the taxonomy below, and a message.
// Unsupported options are rejected, never silently ignored.
//
// Diagnostics (Plan Section 12): the taxonomy distinguishes at least
// tool crash, invalid configuration, unsupported feature, unclosed
// design, proof unknown, and verified pass. Command failures use the
// matching code; kVerifiedPass and kProofUnknown are how verification
// style commands report an outcome that is a result, not an error.
//
// Collections are a defined type here, distinct from plain lists: a
// named, live handle over persistent object IDs. Listing a collection
// resolves names at call time; an object that no longer exists reads
// as <deleted>. Wildcard rules: '*' matches any run of characters,
// '?' exactly one; matching is anchored at both ends; the hierarchy
// separator is '/'; backslash escapes the next character literally.
//
// This header is a stable surface.
#ifndef EOS_UI_REGISTRY_H_
#define EOS_UI_REGISTRY_H_

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "eos/analysis/timing.h"
#include "eos/db/constraints.h"
#include "eos/db/design.h"
#include "eos/db/odb.h"
#include "eos/db/scenarios.h"
#include "eos/foundation/errors.h"
#include "eos/foundation/ids.h"
#include "eos/session/ledger.h"
#include "eos/session/project.h"
#include "eos/session/reports.h"

namespace eos::ui {

using foundation::ObjectId;
using foundation::Status;

enum class Diag : std::int32_t {
  kOk = 0,
  kInvalidCommand = 1,
  kInvalidConfiguration = 2,
  kUnsupportedFeature = 3,
  kUnclosedDesign = 4,
  kProofUnknown = 5,
  kVerifiedPass = 6,
  kToolCrash = 7,
  kIoError = 8,
};

const char* DiagName(Diag diag);

struct CommandResult {
  bool ok = false;
  Diag diag = Diag::kOk;
  std::string message;
  std::string payload;

  static CommandResult Ok(std::string payload);
  static CommandResult Fail(Diag diag, std::string message);
};

// The state commands operate on: the L5 Project (design, scenarios,
// constraints and ledger as one owned unit), plus the collections
// this interface session has minted. Owned by whoever hosts the
// session; interfaces borrow it. There is at most one open design,
// the "current design" of traditional tools.
struct CommandContext {
  session::Project project;

  // The report registry: stateless logic over the project's report
  // bindings. The bindings themselves are project state (Phase 2
  // unit 2.0): a checkpoint restore replaces them with the restored
  // project's bindings, and a design close clears them with the
  // rest of the project.
  session::ReportRegistry reports;

  // Collections by handle ("eos_coll_1", ...). Handles are never
  // reused within a context.
  std::map<std::string, std::vector<ObjectId>> collections;
  std::uint32_t next_collection = 1;

  // Timing session state (Phase 2 unit 2.3): the design source and
  // the analysis inputs the timing commands accumulate, plus the
  // most recent run's per-scene results. Like the collections,
  // this is interface session state, not project state: it names
  // files and a physical database the project checkpoint does not
  // hold, it is cleared when the design closes, and a restore does
  // not bring it back. The results are the L3 service's own metric
  // maps, kept as returned.
  struct TimingSession {
    std::optional<db::OdbDatabase> odb;
    std::string verilog_path;
    std::string top_cell;
    std::string sdc_path;
    std::map<std::string, std::string> liberty_by_corner;
    std::string spef_path;
    std::vector<analysis::SceneResult> last_results;
  };
  TimingSession timing;

  // Synthesis session state (unit S1): the design the synthesis
  // commands have parsed, elaborated and generically optimized,
  // as one advancing state machine. Like the timing session,
  // this is interface session state, not project state: it names
  // files and a live engine design the project checkpoint does
  // not hold, it is cleared when the design closes, and a
  // restore does not bring it back. The Yosys design itself
  // lives in the engine session; these fields are the record of
  // which stage it is at, so a command that needs a stage the
  // session has not reached fails here instead of operating on
  // whatever design a previous session left in the engine.
  struct SynthSession {
    bool parsed = false;
    std::string verilog_path;
    std::string top_cell;
    // Synthesis unit S4: the file list a parsed design came
    // from, when it came from read_filelist rather than
    // read_verilog. filelist_path is the list as given on the
    // command line; filelist_files are the entries resolved
    // against the list file's directory, in list order.
    // read_verilog leaves both empty (the session reset clears
    // them); the timing session is untouched by read_filelist,
    // because it names a single Verilog file and a list file is
    // not one.
    std::string filelist_path;
    std::vector<std::string> filelist_files;
    bool elaborated = false;
    int elaborated_cells = 0;
    int elaborated_wires = 0;
    int elaborated_depth = 0;
    bool generic_optimized = false;
    std::string generic_script;
    int generic_before_and = 0;
    int generic_before_levels = 0;
    int generic_after_and = 0;
    int generic_after_levels = 0;
    std::string generic_verilog;
    // Synthesis unit S2: the technology-mapping record. The
    // mapped netlist is the file synth_map wrote (its cells are
    // Liberty cells, so the timing session is pointed at it on
    // success); mapped_genlib is the Liberty-to-genlib
    // conversion the run produced, kept as evidence; mapped_types
    // is the "Type:count" list scanned from ABC's mapped Verilog.
    // The STA record is the synth_map-time judgement when the
    // timing session could build scenes, and is otherwise
    // untouched: a mapping without timing setup has no record
    // to carry, and the next run_timing supplies it.
    bool mapped = false;
    std::string mapped_target;
    std::string mapped_liberty;
    std::string mapped_genlib;
    std::string mapped_verilog;
    std::string mapped_genlib_cells;
    int mapped_cells = 0;
    std::string mapped_types;
    bool mapped_timing_judged = false;
    double mapped_worst_slack_ns = 0.0;
    double mapped_total_negative_slack_ns = 0.0;
    int mapped_electrical_violations = 0;
    // Synthesis unit S3: the every-path optimization loop's
    // record. The loop starts from the mapped netlist, or from
    // the previous loop's winner when the loop runs again, and
    // on success the timing session is pointed at the winner,
    // so the next run_timing is the STA on the optimized
    // netlist. The mapped record above is untouched: it
    // describes synth_map's output, not the loop's. opt_winner
    // is the winning strategy's name, or empty when no strategy
    // improved the incumbent; opt_still_violating lists the
    // working-set endpoints that still violate at the end of
    // the run, as "pin:slack_ns" entries, and is empty when the
    // loop cleared every working-set endpoint (or when the
    // working set was empty to begin with).
    bool opt_run = false;
    std::string opt_winner;
    int opt_iterations = 0;
    std::string opt_verilog;
    std::string opt_types;
    int opt_cells = 0;
    double opt_worst_slack_ns = 0.0;
    double opt_total_negative_slack_ns = 0.0;
    std::vector<std::string> opt_still_violating;
  };
  SynthSession synth;

  bool HasDesign() const { return project.HasDesign(); }
};

using CommandHandler = std::function<CommandResult(
    CommandContext&, const std::vector<std::string>& args)>;

struct CommandInfo {
  std::string name;   // bare name, exposed by interfaces as eos::<name>
  std::string usage;  // one line, shown by help
  CommandHandler handler;
};

class CommandRegistry {
 public:
  // Registers a command. Fails with kInvalidArgument on a duplicate
  // name or an empty one.
  Status Register(std::string name, std::string usage,
                  CommandHandler handler);

  const CommandInfo* Find(const std::string& name) const;
  std::vector<std::string> Names() const;  // sorted

  // Runs a command by bare name. Unknown names fail with
  // kInvalidCommand; handler results pass through unchanged.
  CommandResult Dispatch(CommandContext& context, const std::string& name,
                         const std::vector<std::string>& args) const;

 private:
  std::map<std::string, CommandInfo> commands_;
};

// Registers the core command set: design lifecycle and construction,
// scenarios, constraints and coverage, collections, ledger
// transactions and artifacts, the export freshness gate, and the
// project-level checkpoint and restore commands (the full project
// file: design, scenarios, constraints and ledger).
void RegisterCoreCommands(CommandRegistry& registry);

}  // namespace eos::ui

#endif  // EOS_UI_REGISTRY_H_
