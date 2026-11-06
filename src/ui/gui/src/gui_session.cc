// gui: SessionView over the command registry (L6 interface).
#include "eos/ui/gui.h"

#include <string>
#include <vector>

namespace eos::ui {

namespace {

// Quotes one echo argument for display when it needs it: empty, or
// containing whitespace, a double quote, or a backslash. This is
// display quoting for the console log only; command entry parsing
// (unit U6) owns real replay quoting.
std::string QuoteArg(const std::string& arg) {
  bool needs = arg.empty();
  for (char c : arg) {
    if (c == ' ' || c == '\t' || c == '"' || c == '\\') needs = true;
  }
  if (!needs) return arg;
  std::string out = "\"";
  for (char c : arg) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  out += '"';
  return out;
}

// Appends one result payload to the echo log, one line per
// payload line. A payload is key=value lines; a trailing
// newline leaves no empty line behind, and an empty payload
// appends nothing: a command with nothing to report shows
// only its echo line.
void AppendPayloadLines(std::vector<std::string>& echo,
                        const std::string& payload) {
  std::size_t i = 0;
  const std::size_t n = payload.size();
  while (i < n) {
    std::size_t j = payload.find('\n', i);
    if (j == std::string::npos) j = n;
    if (j > i) echo.push_back(payload.substr(i, j - i));
    i = j + 1;
  }
}

}  // namespace

std::string SessionView::EchoLine(const std::string& name,
                                  const std::vector<std::string>& args) {
  std::string line = "eos::" + name;
  for (const std::string& arg : args) {
    line += ' ';
    line += QuoteArg(arg);
  }
  return line;
}

CommandResult SessionView::Dispatch(const std::string& name,
                                    const std::vector<std::string>& args) {
  const std::string line = EchoLine(name, args);
  echo_.push_back(line);
  CommandResult result = registry_.Dispatch(context_, name, args);
  if (!result.ok) {
    const std::string error = line + ": EOS " + DiagName(result.diag) +
                              ": " + result.message;
    messages_.push_back(error);
    echo_.push_back(error);
  } else {
    AppendPayloadLines(echo_, result.payload);
    if (name == "checkpoint" || name == "restore") {
      if (args.size() == 1) checkpoint_ = args[0];
    } else if (name == "close_design") {
      checkpoint_.clear();
    }
  }
  return result;
}

CommandResult SessionView::DispatchEntry(const std::string& line) {
  const std::vector<std::string> segments =
      SplitConsoleSegments(line, registry_.Names());
  CommandResult aggregate = CommandResult::Ok("");
  bool saw_failure = false;
  for (const std::string& segment : segments) {
    const ConsoleLine parsed = ParseConsoleLine(segment);
    if (!parsed.ok) {
      const std::string error =
          segment + ": EOS " + DiagName(Diag::kInvalidCommand) + ": " +
          parsed.error;
      echo_.push_back(segment);
      messages_.push_back(error);
      echo_.push_back(error);
      if (!saw_failure) {
        aggregate =
            CommandResult::Fail(Diag::kInvalidCommand, parsed.error);
        saw_failure = true;
      }
      continue;
    }
    if (parsed.name.empty()) continue;
    // Feed the shared interactive history before dispatch, so a
    // command is recallable in the terminal REPL even when the
    // registry rejects it. Blank, comment, and malformed lines
    // never reach here.
    if (history_sink_) history_sink_(EchoLine(parsed.name, parsed.args));
    CommandResult result = Dispatch(parsed.name, parsed.args);
    if (!result.ok) {
      if (!saw_failure) {
        aggregate = result;
        saw_failure = true;
      }
    } else if (!saw_failure) {
      aggregate = result;
    }
  }
  return aggregate;
}

void SessionView::RefreshHierarchy() {
  if (!context_.synth.elaborated) {
    hierarchy_.Clear();
    return;
  }
  const CommandResult result =
      registry_.Dispatch(context_, "synth_hierarchy", {});
  if (!result.ok || !hierarchy_.LoadPayload(result.payload)) {
    hierarchy_.Clear();
  }
}

void SessionView::RefreshDiagram() {
  if (!context_.synth.elaborated || hierarchy_.Empty()) {
    diagram_.Clear();
    diagram_level_.clear();
    return;
  }
  if (diagram_level_.empty() ||
      hierarchy_.Find(diagram_level_) == nullptr) {
    diagram_level_ = hierarchy_.Root()->path;
  }
  const HierarchyNode* node = hierarchy_.Find(diagram_level_);
  if (node == nullptr) {
    diagram_.Clear();
    return;
  }
  const CommandResult result =
      registry_.Dispatch(context_, "synth_blocks", {node->module});
  if (!result.ok || !diagram_.LoadPayload(result.payload)) {
    diagram_.Clear();
  }
}

void SessionView::RefreshSchematic() {
  if (!context_.synth.elaborated || hierarchy_.Empty()) {
    schematic_.Clear();
    diagram_level_.clear();
    return;
  }
  if (diagram_level_.empty() ||
      hierarchy_.Find(diagram_level_) == nullptr) {
    diagram_level_ = hierarchy_.Root()->path;
  }
  const HierarchyNode* node = hierarchy_.Find(diagram_level_);
  if (node == nullptr || !node->children.empty()) {
    schematic_.Clear();
    return;
  }
  const CommandResult result =
      registry_.Dispatch(context_, "synth_schematic", {node->module});
  if (!result.ok || !schematic_.LoadPayload(result.payload)) {
    schematic_.Clear();
  }
}

void SessionView::RefreshPanels() {
  panels_.Clear();
  if (!context_.synth.parsed) return;
  panels_.parsed = true;
  panels_.top = context_.synth.top_cell;
  panels_.verilog_path = context_.synth.verilog_path;
  panels_.filelist_path = context_.synth.filelist_path;
  panels_.filelist_files = context_.synth.filelist_files;
  if (!context_.synth.elaborated) return;
  panels_.elaborated = true;
  panels_.elaborated_cells = context_.synth.elaborated_cells;
  panels_.elaborated_wires = context_.synth.elaborated_wires;
  panels_.elaborated_depth = context_.synth.elaborated_depth;
  if (context_.synth.generic_optimized) {
    panels_.generic_run = true;
    panels_.generic_script = context_.synth.generic_script;
    panels_.generic_before_and = context_.synth.generic_before_and;
    panels_.generic_before_levels = context_.synth.generic_before_levels;
    panels_.generic_after_and = context_.synth.generic_after_and;
    panels_.generic_after_levels = context_.synth.generic_after_levels;
    panels_.generic_verilog = context_.synth.generic_verilog;
  }
  const CommandResult result =
      registry_.Dispatch(context_, "synth_stats", {});
  if (!result.ok || !panels_.live_stats.LoadPayload(result.payload)) {
    panels_.live_stats.Clear();
  }
}

}  // namespace eos::ui
