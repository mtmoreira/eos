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
    messages_.push_back(line + ": EOS " + DiagName(result.diag) + ": " +
                        result.message);
  } else if (name == "checkpoint" || name == "restore") {
    if (args.size() == 1) checkpoint_ = args[0];
  } else if (name == "close_design") {
    checkpoint_.clear();
  }
  return result;
}

CommandResult SessionView::DispatchEntry(const std::string& line) {
  const ConsoleLine parsed = ParseConsoleLine(line);
  if (!parsed.ok) {
    echo_.push_back(line);
    messages_.push_back(line + ": EOS " + DiagName(Diag::kInvalidCommand) +
                        ": " + parsed.error);
    return CommandResult::Fail(Diag::kInvalidCommand, parsed.error);
  }
  if (parsed.name.empty()) return CommandResult::Ok("");
  return Dispatch(parsed.name, parsed.args);
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
