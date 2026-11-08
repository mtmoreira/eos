// timing: the GUI's timing model (L6 interface, Qt-free).
#include "eos/ui/timing.h"

#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace eos::ui {

namespace {

// Splits one payload line into key=value pairs, in order. A
// value is a single token, or a {...} brace group, which may
// contain spaces; the braces ride with the value. Returns
// false on a token without '=', an unterminated brace group,
// a '}' outside a group, or a duplicate key.
bool SplitLine(const std::string& line,
               std::vector<std::pair<std::string, std::string>>* pairs) {
  std::size_t i = 0;
  const std::size_t n = line.size();
  std::set<std::string> seen;
  while (i < n) {
    while (i < n && line[i] == ' ') ++i;
    if (i >= n) break;
    const std::size_t key_start = i;
    while (i < n && line[i] != '=' && line[i] != ' ') ++i;
    if (i >= n || line[i] != '=') return false;
    const std::string key = line.substr(key_start, i - key_start);
    ++i;
    std::string value;
    if (i < n && line[i] == '{') {
      const std::size_t close = line.find('}', i);
      if (close == std::string::npos) return false;
      value = line.substr(i, close - i + 1);
      i = close + 1;
    } else {
      const std::size_t value_start = i;
      while (i < n && line[i] != ' ') ++i;
      value = line.substr(value_start, i - value_start);
    }
    if (key.empty() || !seen.insert(key).second) return false;
    pairs->push_back({key, value});
  }
  return true;
}

// The value map of one line, or nullptr when the line is
// malformed. Duplicate keys are malformed.
bool LineFields(const std::string& line,
                std::map<std::string, std::string>* fields) {
  std::vector<std::pair<std::string, std::string>> pairs;
  if (!SplitLine(line, &pairs)) return false;
  for (const auto& pair : pairs) (*fields)[pair.first] = pair.second;
  return true;
}

bool HasKeys(const std::map<std::string, std::string>& fields,
             const std::vector<const char*>& keys) {
  for (const char* key : keys) {
    if (fields.find(key) == fields.end()) return false;
  }
  return true;
}

bool OnlyKeys(const std::map<std::string, std::string>& fields,
              const std::vector<const char*>& keys) {
  if (fields.size() != keys.size()) return false;
  return HasKeys(fields, keys);
}

bool ParseDouble(const std::string& text, double* value) {
  if (text.empty()) return false;
  char* end = nullptr;
  *value = std::strtod(text.c_str(), &end);
  return end != nullptr && *end == '\0';
}

bool IsNumberOrDash(const std::string& text) {
  if (text == "-") return true;
  double value = 0.0;
  return ParseDouble(text, &value);
}

bool ParseCount(const std::string& text, std::size_t* value) {
  if (text.empty()) return false;
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') return false;
  *value = static_cast<std::size_t>(parsed);
  return true;
}

bool IsOneOf(const std::string& text,
             const std::vector<const char*>& options) {
  for (const char* option : options) {
    if (text == option) return true;
  }
  return false;
}

}  // namespace

bool TimingModel::LoadPayload(const std::string& payload) {
  std::istringstream in(payload);
  std::string line;
  if (!std::getline(in, line)) return false;
  std::map<std::string, std::string> header;
  if (!LineFields(line, &header) ||
      !OnlyKeys(header, {"source", "scenes", "revision"})) {
    return false;
  }
  const std::string source = header["source"];
  if (source != "verilog" && source != "odb") return false;
  std::size_t scene_count = 0;
  std::size_t revision = 0;
  if (!ParseCount(header["scenes"], &scene_count) ||
      !ParseCount(header["revision"], &revision)) {
    return false;
  }

  TimingModel next;
  next.source_ = source;
  std::set<std::string> known_scenes;
  std::map<std::string, std::set<std::string>> classes_by_scene;
  std::set<std::string> coverage_totals;
  std::set<std::string> qor_scalars;
  static const std::vector<const char*> kClasses = {
      "max_delay", "min_delay", "max_transition", "max_capacitance",
      "max_fanout"};
  static const std::vector<const char*> kCheckTypes = {
      "setup", "hold", "out_setup", "out_hold",
      "recovery", "removal", "clock_gating_setup",
      "clock_gating_hold", "no-change", "min_pulse_width"};

  while (std::getline(in, line)) {
    if (line.empty()) return false;
    if (line.rfind("source=", 0) == 0 || line.rfind("scene=", 0) == 0) {
      std::map<std::string, std::string> fields;
      if (!LineFields(line, &fields)) return false;
      if (fields.count("class") != 0) {
        if (!OnlyKeys(fields,
                      {"scene", "class", "violator", "slack", "cost",
                       "weight", "violations"}) &&
            !OnlyKeys(fields,
                      {"scene", "class", "violator", "limit", "weight",
                       "violations"})) {
          return false;
        }
        if (known_scenes.count(fields["scene"]) == 0) return false;
        if (!IsOneOf(fields["class"], kClasses)) return false;
        if (fields["violator"].empty()) return false;
        if (fields["weight"] != "1") return false;
        if (fields.count("limit") != 0) {
          if (fields["limit"] != "none") return false;
        } else {
          if (!IsNumberOrDash(fields["slack"]) &&
              fields["slack"] != "none") {
            return false;
          }
          if (!IsNumberOrDash(fields["cost"]) &&
              fields["cost"] != "none") {
            return false;
          }
        }
        std::size_t violations = 0;
        if (!ParseCount(fields["violations"], &violations)) return false;
        if (!classes_by_scene[fields["scene"]]
                 .insert(fields["class"])
                 .second) {
          return false;
        }
        TimingViolator row;
        row.scene = fields["scene"];
        row.klass = fields["class"];
        row.violator = fields["violator"];
        row.has_limit = fields.count("limit") == 0;
        row.slack = row.has_limit ? fields["slack"] : "-";
        row.cost = row.has_limit ? fields["cost"] : "-";
        row.violations = violations;
        next.violators_.push_back(std::move(row));
        continue;
      }
      if (!OnlyKeys(fields,
                    {"scene", "mode", "corner", "parasitics",
                     "worst_slack_ns", "total_negative_slack_ns",
                     "worst_hold_slack_ns",
                     "total_negative_hold_slack_ns", "max_transition_ns",
                     "max_transition_violations", "max_capacitance_pf",
                     "max_capacitance_violations", "max_fanout",
                     "max_fanout_violations"})) {
        return false;
      }
      if (fields["scene"].empty() || fields["mode"].empty() ||
          fields["corner"].empty() || fields["parasitics"].empty()) {
        return false;
      }
      if (fields["parasitics"] != "estimate" &&
          fields["parasitics"] != "extracted") {
        return false;
      }
      double number = 0.0;
      if (!ParseDouble(fields["worst_slack_ns"], &number) ||
          !ParseDouble(fields["total_negative_slack_ns"], &number) ||
          !ParseDouble(fields["worst_hold_slack_ns"], &number) ||
          !ParseDouble(fields["total_negative_hold_slack_ns"],
                       &number) ||
          !ParseDouble(fields["max_transition_ns"], &number) ||
          !ParseDouble(fields["max_capacitance_pf"], &number) ||
          !ParseDouble(fields["max_fanout"], &number)) {
        return false;
      }
      std::size_t count = 0;
      if (!ParseCount(fields["max_transition_violations"], &count)) {
        return false;
      }
      const std::size_t transition_violations = count;
      if (!ParseCount(fields["max_capacitance_violations"], &count)) {
        return false;
      }
      const std::size_t capacitance_violations = count;
      if (!ParseCount(fields["max_fanout_violations"], &count)) {
        return false;
      }
      if (!known_scenes.insert(fields["scene"]).second) return false;
      TimingScene row;
      row.scene = fields["scene"];
      row.mode = fields["mode"];
      row.corner = fields["corner"];
      row.parasitics = fields["parasitics"];
      row.worst_slack_ns = fields["worst_slack_ns"];
      row.total_negative_slack_ns = fields["total_negative_slack_ns"];
      row.worst_hold_slack_ns = fields["worst_hold_slack_ns"];
      row.total_negative_hold_slack_ns =
          fields["total_negative_hold_slack_ns"];
      row.max_transition_ns = fields["max_transition_ns"];
      row.max_transition_violations = transition_violations;
      row.max_capacitance_pf = fields["max_capacitance_pf"];
      row.max_capacitance_violations = capacitance_violations;
      row.max_fanout = fields["max_fanout"];
      row.max_fanout_violations = count;
      next.scenes_.push_back(std::move(row));
      continue;
    }
    const std::size_t space = line.find(' ');
    if (space == std::string::npos) return false;
    const std::string kind = line.substr(0, space);
    std::map<std::string, std::string> fields;
    if (!LineFields(line.substr(space + 1), &fields)) return false;
    if (kind == "clock") {
      if (!OnlyKeys(fields,
                    {"name", "generated", "period", "rise", "fall",
                     "attrs", "skew", "sources"})) {
        return false;
      }
      if (fields["name"].empty() ||
          (fields["generated"] != "0" && fields["generated"] != "1") ||
          !IsNumberOrDash(fields["period"]) ||
          !IsNumberOrDash(fields["rise"]) ||
          !IsNumberOrDash(fields["fall"]) || fields["skew"] != "none" ||
          fields["sources"].empty()) {
        return false;
      }
      TimingClock row;
      row.name = fields["name"];
      row.generated = fields["generated"] == "1";
      row.period = fields["period"];
      row.rise = fields["rise"];
      row.fall = fields["fall"];
      row.attrs = fields["attrs"];
      row.skew = fields["skew"];
      row.sources = fields["sources"];
      next.clocks_.push_back(std::move(row));
    } else if (kind == "genclock") {
      if (!OnlyKeys(fields,
                    {"name", "master_clock", "modification",
                     "master_source", "sources"})) {
        return false;
      }
      if (fields["name"].empty() || fields["master_clock"].empty() ||
          fields["modification"].empty() ||
          fields["master_source"].empty() || fields["sources"].empty()) {
        return false;
      }
      TimingGeneratedClock row;
      row.name = fields["name"];
      row.master_clock = fields["master_clock"];
      row.master_source = fields["master_source"];
      row.modification = fields["modification"];
      row.sources = fields["sources"];
      next.generated_.push_back(std::move(row));
    } else if (kind == "bindclock") {
      if (!OnlyKeys(fields, {"name", "target"})) return false;
      if (fields["name"].empty() || fields["target"].empty()) {
        return false;
      }
      TimingBindClock row;
      row.name = fields["name"];
      row.target = fields["target"];
      next.bind_clocks_.push_back(std::move(row));
    } else if (kind == "path") {
      if (!OnlyKeys(fields,
                    {"scene", "check", "endpoint", "startpoint", "slack",
                     "arrival", "required"})) {
        return false;
      }
      if (known_scenes.count(fields["scene"]) == 0) return false;
      if (fields["check"] != "setup" && fields["check"] != "hold") {
        return false;
      }
      if (fields["endpoint"].empty() || fields["startpoint"].empty()) {
        return false;
      }
      double number = 0.0;
      if (!ParseDouble(fields["slack"], &number) ||
          !ParseDouble(fields["arrival"], &number) ||
          !ParseDouble(fields["required"], &number)) {
        return false;
      }
      TimingPath row;
      row.scene = fields["scene"];
      row.check = fields["check"];
      row.endpoint = fields["endpoint"];
      row.startpoint = fields["startpoint"];
      row.slack = fields["slack"];
      row.arrival = fields["arrival"];
      row.required = fields["required"];
      next.paths_.push_back(std::move(row));
    } else if (kind == "coverage") {
      if (known_scenes.count(fields["scene"]) == 0) return false;
      if (fields.count("type") != 0) {
        if (!OnlyKeys(fields,
                      {"scene", "type", "tested", "untested",
                       "percent"})) {
          return false;
        }
        if (!IsOneOf(fields["type"], kCheckTypes)) return false;
        double percent = 0.0;
        if (!ParseDouble(fields["percent"], &percent)) return false;
        std::size_t tested = 0;
        std::size_t untested = 0;
        if (!ParseCount(fields["tested"], &tested) ||
            !ParseCount(fields["untested"], &untested)) {
          return false;
        }
        TimingCoverageType row;
        row.scene = fields["scene"];
        row.type = fields["type"];
        row.tested = tested;
        row.untested = untested;
        row.percent = fields["percent"];
        next.coverage_types_.push_back(std::move(row));
      } else {
        if (!OnlyKeys(fields,
                      {"scene", "checks", "tested", "untested"})) {
          return false;
        }
        std::size_t checks = 0;
        std::size_t tested = 0;
        std::size_t untested = 0;
        if (!ParseCount(fields["checks"], &checks) ||
            !ParseCount(fields["tested"], &tested) ||
            !ParseCount(fields["untested"], &untested)) {
          return false;
        }
        if (!coverage_totals.insert(fields["scene"]).second) {
          return false;
        }
        TimingCoverageTotal row;
        row.scene = fields["scene"];
        row.checks = checks;
        row.tested = tested;
        row.untested = untested;
        next.coverage_totals_.push_back(std::move(row));
      }
    } else if (kind == "qor") {
      if (known_scenes.count(fields["scene"]) == 0) return false;
      if (fields.count("group") != 0) {
        if (!OnlyKeys(fields,
                      {"scene", "group", "check", "wns", "tns",
                       "violating_paths"})) {
          return false;
        }
        if (fields["group"].empty() ||
            (fields["check"] != "setup" && fields["check"] != "hold")) {
          return false;
        }
        double number = 0.0;
        if (!ParseDouble(fields["wns"], &number) ||
            !ParseDouble(fields["tns"], &number)) {
          return false;
        }
        std::size_t violating = 0;
        if (!ParseCount(fields["violating_paths"], &violating)) {
          return false;
        }
        TimingQorGroup row;
        row.scene = fields["scene"];
        row.group = fields["group"];
        row.check = fields["check"];
        row.wns = fields["wns"];
        row.tns = fields["tns"];
        row.violating_paths = violating;
        next.qor_groups_.push_back(std::move(row));
      } else if (fields.count("cell") != 0) {
        if (!OnlyKeys(fields, {"scene", "cell", "count", "area"})) {
          return false;
        }
        if (fields["cell"].empty()) return false;
        double area = 0.0;
        if (!ParseDouble(fields["area"], &area)) return false;
        std::size_t count = 0;
        if (!ParseCount(fields["count"], &count)) return false;
        TimingQorCell row;
        row.scene = fields["scene"];
        row.cell = fields["cell"];
        row.count = count;
        row.area = fields["area"];
        next.qor_cells_.push_back(std::move(row));
      } else if (fields.count("drc") != 0) {
        if (!OnlyKeys(fields,
                      {"scene", "drc", "violations", "worst_slack"})) {
          return false;
        }
        if (!IsOneOf(fields["drc"],
                     {"max_transition", "max_capacitance", "max_fanout"})) {
          return false;
        }
        if (!IsNumberOrDash(fields["worst_slack"]) &&
            fields["worst_slack"] != "none") {
          return false;
        }
        std::size_t violations = 0;
        if (!ParseCount(fields["violations"], &violations)) return false;
        TimingQorDrc row;
        row.scene = fields["scene"];
        row.drc = fields["drc"];
        row.violations = violations;
        row.worst_slack = fields["worst_slack"];
        next.qor_drcs_.push_back(std::move(row));
      } else {
        if (!OnlyKeys(fields,
                      {"scene", "setup_groups", "hold_groups",
                       "union_tns", "union_endpoints", "every_group_tns",
                       "every_group_endpoints", "hold_union_tns",
                       "hold_union_endpoints", "hold_every_group_tns",
                       "hold_every_group_endpoints", "cells", "area",
                       "compile_cpu_s"})) {
          return false;
        }
        double number = 0.0;
        if (!ParseDouble(fields["union_tns"], &number) ||
            !ParseDouble(fields["every_group_tns"], &number) ||
            !ParseDouble(fields["hold_union_tns"], &number) ||
            !ParseDouble(fields["hold_every_group_tns"], &number) ||
            !ParseDouble(fields["area"], &number)) {
          return false;
        }
        if (fields["compile_cpu_s"] != "n/a" &&
            !ParseDouble(fields["compile_cpu_s"], &number)) {
          return false;
        }
        std::size_t count = 0;
        if (!ParseCount(fields["setup_groups"], &count)) return false;
        const std::size_t setup_groups = count;
        if (!ParseCount(fields["hold_groups"], &count)) return false;
        const std::size_t hold_groups = count;
        if (!ParseCount(fields["union_endpoints"], &count)) return false;
        const std::size_t union_endpoints = count;
        if (!ParseCount(fields["every_group_endpoints"], &count)) {
          return false;
        }
        const std::size_t every_group_endpoints = count;
        if (!ParseCount(fields["hold_union_endpoints"], &count)) {
          return false;
        }
        const std::size_t hold_union_endpoints = count;
        if (!ParseCount(fields["hold_every_group_endpoints"], &count)) {
          return false;
        }
        const std::size_t hold_every_group_endpoints = count;
        if (!ParseCount(fields["cells"], &count)) return false;
        if (!qor_scalars.insert(fields["scene"]).second) return false;
        TimingQor row;
        row.scene = fields["scene"];
        row.setup_groups = setup_groups;
        row.hold_groups = hold_groups;
        row.union_tns = fields["union_tns"];
        row.union_endpoints = union_endpoints;
        row.every_group_tns = fields["every_group_tns"];
        row.every_group_endpoints = every_group_endpoints;
        row.hold_union_tns = fields["hold_union_tns"];
        row.hold_union_endpoints = hold_union_endpoints;
        row.hold_every_group_tns = fields["hold_every_group_tns"];
        row.hold_every_group_endpoints = hold_every_group_endpoints;
        row.cells = count;
        row.area = fields["area"];
        row.compile_cpu_s = fields["compile_cpu_s"];
        next.qor_.push_back(std::move(row));
      }
    } else {
      return false;
    }
  }
  if (next.scenes_.size() != scene_count) return false;
  for (const TimingScene& scene : next.scenes_) {
    if (classes_by_scene[scene.scene].size() != kClasses.size()) {
      return false;
    }
    if (coverage_totals.count(scene.scene) == 0) return false;
    if (qor_scalars.count(scene.scene) == 0) return false;
  }
  *this = std::move(next);
  loaded_ = true;
  return true;
}

void TimingModel::Clear() {
  *this = TimingModel();
}

}  // namespace eos::ui
