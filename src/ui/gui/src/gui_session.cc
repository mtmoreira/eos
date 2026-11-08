// gui: SessionView over the command registry (L6 interface).
#include "eos/ui/gui.h"

#include <ctime>
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

// The local time as "%H:%M:%S" for a log entry.
std::string LogTimestamp() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_r(&now, &local);
  char buffer[16];
  std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
  return buffer;
}

// The severity of one engine log line, by its Yosys prefix:
// log_error writes "ERROR: ", log_warning writes "Warning: ",
// and every other line is informational. The prefix may sit
// after a time or source location, so this is a substring
// match, not an anchor: an informational line never carries
// either literal.
std::string ClassifyLogLine(const std::string& line) {
  if (line.find("ERROR:") != std::string::npos) return "error";
  if (line.find("Warning:") != std::string::npos) return "warning";
  return "info";
}

// The timing inputs as one string: everything the timing
// model shows depends on. A refresh whose key matches the
// last build skips the query, so a plain refresh never pays
// for fresh STA sessions. The key covers the last run's
// results (the summary and violators), the constraint set
// (the clock table), the synthesis CPU record (the QoR
// compile time), and the design source plus active scenarios
// (the paths, coverage, and QoR rows the query re-derives).
// A design source swapped without a new run keeps the last
// run's model, self-consistently, until run_timing runs
// again.
std::string TimingKey(const CommandContext& context) {
  std::string key;
  const auto add = [&key](const std::string& part) {
    key += part;
    key += '|';
  };
  add(context.timing.verilog_path);
  add(context.timing.top_cell);
  add(context.timing.sdc_path);
  add(context.timing.spef_path);
  add(context.timing.odb.has_value() ? "odb" : "verilog");
  for (const auto& entry : context.timing.liberty_by_corner) {
    add(entry.first + "=" + entry.second);
  }
  for (const db::Scenario* scenario :
       context.project.scenarios.ActiveScenarios()) {
    add(scenario->name + "/" + scenario->mode + "/" +
        scenario->corner + "/" +
        (scenario->parasitics == db::ParasiticsLevel::kExtracted
             ? "extracted"
             : "estimate"));
  }
  for (const db::Constraint& constraint :
       context.project.constraints.all()) {
    std::string row = std::to_string(
                          static_cast<int>(constraint.kind)) +
                      "," + constraint.name + "," +
                      constraint.target_spec + "," +
                      std::to_string(
                          static_cast<int>(constraint.status));
    for (const auto& param : constraint.params) {
      row += "," + param.first + "=" + param.second;
    }
    add(row);
  }
  add(context.timing.bottleneck_capture ? "capture" : "nocapture");
  add(context.timing.last_capture_enabled ? "captured" : "notcaptured");
  add(std::to_string(context.synth.mapped_cpu_s));
  add(std::to_string(context.synth.opt_cpu_s));
  const auto add_check = [&add](const analysis::ElectricalCheck& check) {
    add(check.worst_pin + "," + std::to_string(check.worst_value) +
        "," + std::to_string(check.worst_limit) + "," +
        std::to_string(check.worst_slack) + "," +
        std::to_string(check.violation_count) + "," +
        (check.has_limit ? "limit" : "nolimit"));
  };
  const auto add_violators = [&add](
      const std::vector<analysis::ElectricalViolation>& violators) {
    for (const analysis::ElectricalViolation& violation : violators) {
      add(violation.pin + "," + std::to_string(violation.value) + "," +
          std::to_string(violation.limit) + "," +
          std::to_string(violation.slack));
    }
  };
  const auto add_endpoints = [&add](
      const std::vector<analysis::EndpointSlack>& endpoints) {
    for (const analysis::EndpointSlack& endpoint : endpoints) {
      add(endpoint.pin + "," + std::to_string(endpoint.slack_ns));
    }
  };
  for (const analysis::SceneResult& result :
       context.timing.last_results) {
    add(result.scenario.name + "/" + result.scenario.mode + "/" +
        result.scenario.corner + "/" +
        (result.scenario.parasitics == db::ParasiticsLevel::kExtracted
             ? "extracted"
             : "estimate"));
    for (const auto& metric : result.metrics) {
      add(metric.first + "=" + std::to_string(metric.second.value));
    }
    add_endpoints(result.setup_violating_endpoints);
    add_endpoints(result.hold_violating_endpoints);
    add_check(result.max_transition_check);
    add_check(result.max_capacitance_check);
    add_check(result.max_fanout_check);
    add_violators(result.max_transition_violators);
    add_violators(result.max_capacitance_violators);
    add_violators(result.max_fanout_violators);
  }
  return key;
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
  log_.push_back(LogEntry{LogTimestamp(), "info", line});
  CommandResult result = registry_.Dispatch(context_, name, args);
  if (!result.ok) {
    const std::string error = line + ": EOS " + DiagName(result.diag) +
                              ": " + result.message;
    messages_.push_back(error);
    echo_.push_back(error);
    log_.push_back(LogEntry{LogTimestamp(), "error", error});
  } else {
    AppendPayloadLines(echo_, result.payload);
    if (name == "checkpoint" || name == "restore") {
      if (args.size() == 1) checkpoint_ = args[0];
    } else if (name == "close_design") {
      checkpoint_.clear();
    }
  }
  if (result.ok && (name == "read_verilog" || name == "read_filelist" ||
                    name == "elaborate" || name == "synth_generic" ||
                    name == "synth_map")) {
    // A design-changing command: the issues the last refresh
    // built describe the old design, so the next refresh
    // re-runs the check.
    issues_key_.clear();
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
      log_.push_back(LogEntry{LogTimestamp(), "info", segment});
      messages_.push_back(error);
      echo_.push_back(error);
      log_.push_back(LogEntry{LogTimestamp(), "error", error});
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
  // The mapped synthesis record (full-flow GUI unit F1): the
  // query fails fast until synth_map ran, which is the normal
  // "not run yet" state, so a failed query or a malformed
  // payload clears the model exactly like live_stats.
  const CommandResult summary =
      registry_.Dispatch(context_, "synth_summary", {});
  if (!summary.ok ||
      !panels_.mapped_synth.LoadPayload(summary.payload)) {
    panels_.mapped_synth.Clear();
  }
}

void SessionView::RefreshIssues() {
  if (!context_.synth.elaborated) {
    issues_.Clear();
    issues_key_.clear();
    return;
  }
  const std::string key =
      context_.synth.top_cell +
      (context_.synth.generic_optimized ? "|generic" : "|elaborated");
  if (key == issues_key_) return;
  const CommandResult result =
      registry_.Dispatch(context_, "synth_issues", {});
  if (!result.ok || !issues_.LoadPayload(result.payload)) {
    issues_.Clear();
  }
  issues_key_ = key;
}

void SessionView::RefreshTiming() {
  if (context_.timing.last_results.empty()) {
    timing_.Clear();
    timing_key_.clear();
    return;
  }
  const std::string key = TimingKey(context_);
  if (key == timing_key_) return;
  const CommandResult result =
      registry_.Dispatch(context_, "timing_summary", {});
  if (!result.ok || !timing_.LoadPayload(result.payload)) {
    timing_.Clear();
  }
  timing_key_ = key;
}

void SessionView::RefreshLog() {
  for (const std::string& line : context_.engine_log_lines) {
    log_.push_back(
        LogEntry{LogTimestamp(), ClassifyLogLine(line), line});
  }
  context_.engine_log_lines.clear();
}

}  // namespace eos::ui
