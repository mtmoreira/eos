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

#include <string>
#include <vector>

#include "eos/ui/hierarchy.h"
#include "eos/ui/registry.h"

namespace eos::ui {

enum class StepStatus {
  kPending,
  kDone,
};

// The Elaboration stage as the GUI sees it: the registry and the
// shared command context, plus the console echo log and the
// hierarchy model. Dispatching a command runs it on the shared
// context (so widgets reading the context see the effect) and
// records the echo line, including for commands the registry
// rejects: the console shows what was attempted, not only what
// succeeded.
class SessionView {
 public:
  SessionView(CommandRegistry& registry, CommandContext& context)
      : registry_(registry), context_(context) {}

  CommandResult Dispatch(const std::string& name,
                         const std::vector<std::string>& args);

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

 private:
  static std::string EchoLine(const std::string& name,
                              const std::vector<std::string>& args);

  CommandRegistry& registry_;
  CommandContext& context_;
  std::vector<std::string> echo_;
  HierarchyModel hierarchy_;
};

// Runs the Qt GUI application on the given registry and context.
// Defined only in a Qt build; the eos-gui app target is created
// only when Qt6 is found at configure time.
int RunGui(int argc, char** argv, CommandRegistry& registry,
           CommandContext& context);

}  // namespace eos::ui

#endif  // EOS_UI_GUI_H_
