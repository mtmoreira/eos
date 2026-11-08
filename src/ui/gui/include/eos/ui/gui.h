// gui: the GUI adapter over the command registry (L6 interface).
//
// The GUI is thin over the one command registry: every GUI action
// issues a registry command and renders its machine-readable result,
// so the CLI and the GUI cannot drift. This header holds the
// Qt-free part of that adapter: the session view every widget
// reads, and the application entry point the eos-gui binary calls.
// Widget code lives in the module's internal/ and src/ directories
// and is built only when Qt6 is found at configure time.
#ifndef EOS_UI_GUI_H_
#define EOS_UI_GUI_H_

#include <functional>
#include <string>
#include <vector>

#include "eos/ui/diagram.h"
#include "eos/ui/console_entry.h"
#include "eos/ui/floorplan.h"
#include "eos/ui/hierarchy.h"
#include "eos/ui/issues.h"
#include "eos/ui/panels.h"
#include "eos/ui/registry.h"
#include "eos/ui/schematic.h"
#include "eos/ui/timing.h"

namespace eos::ui {

enum class StepStatus {
  kPending,
  kDone,
};

// The Elaboration stage as the GUI sees it: the registry and the
// shared command context, plus the console echo log, the
// hierarchy model, the diagram model with its level plus
// the schematic model for a leaf level, and the right panels
// model with the dispatch message log. Dispatching a command runs it on the shared
// context (so widgets reading the context see the effect) and
// records the echo line followed by its outcome in the console
// transcript: the result payload lines on success, the
// diagnostic line on failure. A rejected dispatch also records
// the diagnostic line in Messages, so the Messages panel shows
// the failure and its diagnostic, and the transcript shows it
// too: the console reports every outcome where the user typed
// the command, never only in another tab.
class SessionView {
 public:
  SessionView(CommandRegistry& registry, CommandContext& context)
      : registry_(registry), context_(context) {}

  CommandResult Dispatch(const std::string& name,
                         const std::vector<std::string>& args);

  // Dispatches one submitted console text (U6): splits it into
  // command segments with SplitConsoleSegments and dispatches
  // each, so a multi-command paste runs every line instead of
  // jamming them into one command's arguments, and a lone
  // command runs exactly as before. A blank or comment segment
  // is a no-op and records nothing. A malformed segment echoes
  // the raw segment and records its diagnostic, exactly like a
  // rejected dispatch. Returns the first failure when any
  // segment fails, else the last segment's result; Ok with an
  // empty payload when no segment ran.
  CommandResult DispatchEntry(const std::string& line);

  // History sink: when set, every successfully parsed console
  // line is handed to it in normalized "eos::<name> ..." form
  // (the echo line), so the terminal REPL can recall it with
  // up-arrow. The GUI entry point sets it to the shell's shared
  // history; unset, the console keeps its own in-session history
  // only. Blank, comment, and malformed lines never reach the
  // sink: only a line parsed into a command is shared, whether
  // or not the dispatch itself succeeds.
  using HistorySink = std::function<void(const std::string&)>;
  void SetHistorySink(HistorySink sink) { history_sink_ = std::move(sink); }

  // Registry command names in registry order, for the console
  // entry's tab completion.
  std::vector<std::string> CommandNames() const {
    return registry_.Names();
  }

  // Current checkpoint path (U6 indicator): the path of the last
  // successful checkpoint or restore dispatch, empty when no
  // checkpoint is current. A failed checkpoint or restore leaves
  // it where it was; a successful close_design clears it, since
  // the design it belonged to is gone.
  const std::string& CheckpointName() const { return checkpoint_; }

  // The console transcript: one echo line per dispatched
  // command, each followed by its outcome lines (the result
  // payload lines on success, the diagnostic line on failure),
  // in dispatch order. A command with an empty payload shows
  // only its echo line. RefreshConsole renders this verbatim,
  // so the transcript is the record of what was attempted and
  // what came of it.
  const std::vector<std::string>& EchoLog() const { return echo_; }
  CommandContext& context() { return context_; }
  const CommandContext& context() const { return context_; }

  // Hierarchy model, refreshed from the live session (see
  // RefreshHierarchy). The block diagram (U3) consumes the
  // selection, so U2 exposes it here rather than in the widget:
  // the widget only displays the model and reports clicks.
  const HierarchyModel& Hierarchy() const { return hierarchy_; }

  // Re-reads the hierarchy from the live session: dispatches
  // synth_hierarchy WITHOUT an echo line (a model refresh is not
  // a user action) and rebuilds the model when elaborated;
  // clears it otherwise, or when the query or its payload
  // fails, so a stale tree never lingers.
  void RefreshHierarchy();

  // Selection the block diagram (U3) consumes. Only a path in
  // the model can be selected; Select returns false otherwise.
  bool SelectHierarchyPath(const std::string& path) {
    return hierarchy_.Select(path);
  }
  const std::string& SelectedHierarchyPath() const {
    return hierarchy_.SelectedPath();
  }

  // Block diagram model, refreshed from the live session (see
  // RefreshDiagram). The diagram shows one hierarchy level: the
  // diagram level, which is a path in the model, not the
  // selection. Tree clicks, double-clicks, and breadcrumb
  // clicks move both the selection and the level together; a
  // single block click moves only the selection, so the diagram
  // keeps its level and highlights the block.
  const DiagramModel& Diagram() const { return diagram_; }
  const std::string& DiagramLevel() const { return diagram_level_; }

  // Sets the diagram level. Only a path in the model can become
  // the level; SetDiagramLevel returns false otherwise.
  bool SetDiagramLevel(const std::string& path) {
    if (hierarchy_.Find(path) == nullptr) return false;
    diagram_level_ = path;
    return true;
  }

  // Re-reads the block diagram from the live session: dispatches
  // synth_blocks WITHOUT an echo line (a model refresh is not a
  // user action) for the level node's module, and rebuilds the
  // model; clears it when not elaborated, when the level is
  // absent, or when the query or its payload fails. A level
  // that is empty or no longer in the tree falls back to the
  // root, so the diagram never shows a stale level.
  void RefreshDiagram();

  // Gate-level schematic model, refreshed from the live session
  // (see RefreshSchematic). The center panel shows the
  // schematic when the diagram level is a leaf; otherwise the
  // model is empty and the center shows the block diagram.
  const SchematicModel& Schematic() const { return schematic_; }

  // Whether the diagram level is a leaf: its hierarchy node
  // exists and has no children. A level with no node, or no
  // level at all, is not a leaf.
  bool DiagramLevelIsLeaf() const {
    const HierarchyNode* node =
        diagram_level_.empty() ? nullptr : hierarchy_.Find(diagram_level_);
    return node != nullptr && node->children.empty();
  }

  // Re-reads the gate-level schematic from the live session:
  // dispatches synth_schematic WITHOUT an echo line (a model
  // refresh is not a user action) for the level node's module,
  // but only when the level is a leaf; clears the model when
  // not elaborated, when the level is absent, when the level
  // is not a leaf, or when the query or its payload fails, so
  // a stale schematic never lingers beside a block diagram. A
  // level that is empty or no longer in the tree falls back to
  // the root, exactly as RefreshDiagram does, so a flat design
  // whose root is a leaf shows its schematic.
  void RefreshSchematic();

  // Right panels model, refreshed from the live session (see
  // RefreshPanels). The panels snapshot the session record:
  // sources from the Read step, elaborated totals from the
  // Elaborate step, the generic record from the Generic synth
  // step, the live cell-type breakdown from a synth_stats
  // query, and the mapped synthesis record from a synth_summary
  // query. The snapshot clears when no design is parsed, and
  // the live stats and mapped record clear when not elaborated
  // or mapped, or when the query or its payload fails, so stale
  // panels never linger.
  const PanelsModel& Panels() const { return panels_; }

  // Re-reads the right panels from the live session: copies
  // the session record and dispatches synth_stats and
  // synth_summary WITHOUT an echo line (a model refresh is not
  // a user action). Messages are dispatch history, not a
  // refresh, so they survive.
  void RefreshPanels();

  // Dispatch message log: one line per rejected dispatch, in
  // dispatch order, as "eos::<command> ...: EOS <diag>:
  // <message>". Successful dispatches record nothing: messages
  // are diagnostics, and the echo log is the transcript.
  const std::vector<std::string>& Messages() const { return messages_; }

  // Elaboration issues model, refreshed from the live session
  // (see RefreshIssues). The issues panel reads this: real
  // elaboration diagnostics, severity ordered, with the
  // instance path and source location of each problem. Issues
  // are design problems; dispatch history lives in Messages,
  // never here.
  const IssuesModel& Issues() const { return issues_; }

  // Re-reads the elaboration issues from the live session:
  // dispatches synth_issues WITHOUT an echo line (a model
  // refresh is not a user action) and rebuilds the model when
  // elaborated; clears it otherwise, or when the query or its
  // payload fails, so stale issues never linger. The check
  // pass runs only when the design changed (a new top, or a
  // new generic state, or a design-changing dispatch since
  // the last refresh), so a plain refresh never pays for a
  // check it already ran.
  void RefreshIssues();

  // Timing model, refreshed from the live session (see
  // RefreshTiming). The Timing panel reads this: the per-scene
  // slack summary from the last run_timing, the clock table,
  // the worst-violator table, the worst paths, the coverage
  // summary, and the QoR numbers.
  const TimingModel& Timing() const { return timing_; }

  // Re-reads the timing model from the live session:
  // dispatches timing_summary WITHOUT an echo line (a model
  // refresh is not a user action) and rebuilds the model when
  // a timing run exists; clears it otherwise, or when the
  // query or its payload fails, so stale timing never lingers.
  // The query runs fresh STA sessions for the paths, coverage,
  // and QoR rows, so it runs only when the timing inputs
  // changed since the last refresh (a new run, a new SDC, a
  // new scenario, or a new synthesis record), exactly like the
  // issues check-pass guard.
  void RefreshTiming();

  // Floorplan model, refreshed from the live session (see
  // RefreshFloorplan). The floorplan canvas reads this: the
  // die and core rectangles, one rectangle per row, and one
  // entry per IO pin, plus the counts the floorplan payload
  // reports.
  const FloorplanModel& Floorplan() const { return floorplan_; }

  // Re-reads the floorplan model from the live session:
  // dispatches floorplan_summary WITHOUT an echo line (a model
  // refresh is not a user action) and rebuilds the model when
  // the database holds a floorplan; clears it otherwise, or
  // when the query or its payload fails, so a stale floorplan
  // never lingers. The query fails fast with no technology, no
  // mapped design, or no floorplan, so the refresh needs no
  // gate of its own; and the query itself is a cheap database
  // walk (rows, track grids, block terminals), so it runs on
  // every refresh, like the hierarchy refresh.
  void RefreshFloorplan();

  // Whether the center shows the floorplan canvas instead of
  // the hierarchy views. A successful floorplan dispatch takes
  // the user there; a hierarchy navigation or a successful
  // close_design takes them back to the hierarchy views. The
  // center shows the canvas only while the floorplan model is
  // loaded, so a stale flag never shows a floorplan that no
  // longer exists.
  bool ShowingFloorplan() const { return center_floorplan_; }
  void ShowFloorplan(bool show) { center_floorplan_ = show; }

  // One engine log line for the log window: the local time the
  // line was drained, its severity ("error" for a line
  // carrying "ERROR:", "warning" for one carrying "Warning:",
  // "info" otherwise), and the line itself. The log window
  // colors lines by severity and filters on it.
  struct LogEntry {
    std::string timestamp;
    std::string severity;
    std::string text;
  };

  // Log window entries, in drain order. The window appends
  // them as RefreshLog drains the context's engine lines; a
  // refresh never re-reads lines already shown.
  const std::vector<LogEntry>& Log() const { return log_; }

  // Drains the context's engine log lines into log entries
  // (see RefreshLog's note on the context field) and clears
  // the context field, so the next refresh only shows new
  // lines. The drain is the single choke point: Dispatch
  // never drains, so a command's lines appear exactly once,
  // on the next refresh after the command.
  void RefreshLog();

  // Stepper state, read straight from the synthesis session record:
  // Read is done once a design is parsed, Elaborate once it is
  // elaborated, Generic synth once generic optimization ran. A
  // failed command leaves the flags where they were, so a step
  // never reports Done for work that did not happen.
  StepStatus ReadStep() const {
    return context_.synth.parsed ? StepStatus::kDone : StepStatus::kPending;
  }
  StepStatus ElaborateStep() const {
    return context_.synth.elaborated ? StepStatus::kDone
                                     : StepStatus::kPending;
  }
  StepStatus GenericStep() const {
    return context_.synth.generic_optimized ? StepStatus::kDone
                                            : StepStatus::kPending;
  }
  // Mapped synthesis (full-flow GUI unit F1): Done once a
  // mapped design exists. The stepper widget itself is F11's;
  // this is the state contract it wires.
  StepStatus SynthesisStep() const {
    return context_.synth.mapped ? StepStatus::kDone
                                 : StepStatus::kPending;
  }

 private:
  static std::string EchoLine(const std::string& name,
                              const std::vector<std::string>& args);

  CommandRegistry& registry_;
  CommandContext& context_;
  HistorySink history_sink_;
  std::vector<std::string> echo_;
  std::vector<std::string> messages_;
  std::vector<LogEntry> log_;
  std::string checkpoint_;
  HierarchyModel hierarchy_;
  DiagramModel diagram_;
  SchematicModel schematic_;
  PanelsModel panels_;
  IssuesModel issues_;
  // The design the issues model was last built from: the top
  // plus the generic state, as "top|elaborated" or
  // "top|generic". Empty when no issues are loaded. Cleared
  // by a design-changing dispatch (see Dispatch), so the next
  // refresh re-runs the check on the new design.
  std::string issues_key_;
  TimingModel timing_;
  // The timing inputs the timing model was last built from
  // (see RefreshTiming). Empty when no timing is loaded.
  std::string timing_key_;
  FloorplanModel floorplan_;
  // The center view the user last chose: true for the
  // floorplan canvas, false for the hierarchy views. Set by a
  // successful floorplan dispatch and by explicit stage
  // navigation; cleared by a successful close_design.
  bool center_floorplan_ = false;
  std::string diagram_level_;
};

// Runs the Qt GUI application on the given registry and context.
// Defined only in a Qt build; the eos-gui app target is created
// only when Qt6 is found at configure time.
//
// The shell argument (U6) is the terminal REPL: when non-null,
// the same process keeps a REPL on stdin while the window is
// open, and every terminal line is evaluated on the GUI thread
// through that shell, so terminal and GUI console commands can
// never race on the session. A null shell means no terminal
// REPL (a build without Tcl); the GUI console still works, since
// it dispatches registry commands directly. The window owns no
// session lifetime: closing it hides it, and the terminal
// reopen command shows it again.
class TclShell;
int RunGui(int argc, char** argv, CommandRegistry& registry,
           CommandContext& context, TclShell* shell);

}  // namespace eos::ui

#endif  // EOS_UI_GUI_H_
