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
  echo_.push_back(EchoLine(name, args));
  return registry_.Dispatch(context_, name, args);
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

}  // namespace eos::ui
