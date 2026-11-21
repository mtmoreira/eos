// eco: the GUI's ECO model (L6 interface, Qt-free).
#include "eos/ui/eco.h"

#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace eos::ui {

namespace {

// Splits one payload line into key=value pairs, in order. A
// value is a single token. Returns false on a token without
// '=', or a duplicate key.
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
    const std::size_t value_start = i;
    while (i < n && line[i] != ' ') ++i;
    const std::string value = line.substr(value_start, i - value_start);
    if (key.empty() || value.empty() || !seen.insert(key).second) {
      return false;
    }
    pairs->push_back({key, value});
  }
  return true;
}

// The value map of one line, or false when the line is
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

// Parses a non-negative decimal integer covering the whole
// token: no sign, no trailing junk, no overflow past int.
bool ParseCount(const std::string& text, std::size_t* value) {
  if (text.empty()) return false;
  long parsed = 0;
  for (char c : text) {
    if (c < '0' || c > '9') return false;
    parsed = parsed * 10 + (c - '0');
    if (parsed > 2147483647L) return false;
  }
  *value = static_cast<std::size_t>(parsed);
  return true;
}

// Parses a double covering the whole token, the same rule the
// timing model uses: strtod with no trailing junk.
bool ParseDouble(const std::string& text, double* value) {
  if (text.empty()) return false;
  char* end = nullptr;
  *value = std::strtod(text.c_str(), &end);
  return end != nullptr && *end == '\0';
}

// Parses one scene metric: the payload's "none" is an absent
// metric, anything else must be a whole-token double.
bool ParseMetric(const std::string& text, std::optional<double>* value) {
  if (text == "none") {
    *value = std::nullopt;
    return true;
  }
  double parsed = 0.0;
  if (!ParseDouble(text, &parsed)) return false;
  *value = parsed;
  return true;
}

}  // namespace

bool EcoModel::LoadPayload(const std::string& payload) {
  std::istringstream in(payload);
  std::string line;
  if (!std::getline(in, line)) return false;
  const std::size_t space = line.find(' ');
  if (space == std::string::npos) return false;
  if (line.substr(0, space) != "eco") return false;
  std::map<std::string, std::string> header;
  if (!LineFields(line.substr(space + 1), &header) ||
      !OnlyKeys(header, {"closed", "iterations", "candidates_tried",
                         "candidates_rejected", "candidates_skipped_stale",
                         "candidates_skipped_memory", "max_iterations"})) {
    return false;
  }

  EcoModel next;
  if (header["closed"] == "1") {
    next.closed_ = true;
  } else if (header["closed"] == "0") {
    next.closed_ = false;
  } else {
    return false;
  }
  if (!ParseCount(header["iterations"], &next.iterations_) ||
      !ParseCount(header["candidates_tried"], &next.candidates_tried_) ||
      !ParseCount(header["candidates_rejected"],
                  &next.candidates_rejected_) ||
      !ParseCount(header["candidates_skipped_stale"],
                  &next.candidates_skipped_stale_) ||
      !ParseCount(header["candidates_skipped_memory"],
                  &next.candidates_skipped_memory_) ||
      !ParseCount(header["max_iterations"], &next.max_iterations_)) {
    return false;
  }

  std::set<std::string> scene_names;
  bool saw_step = false;
  bool saw_stop_reason = false;
  bool saw_note = false;
  while (std::getline(in, line)) {
    if (line.empty()) return false;
    const std::size_t tag_end = line.find(' ');
    if (tag_end == std::string::npos) return false;
    const std::string kind = line.substr(0, tag_end);
    if (kind == "step" || kind == "stop_reason" || kind == "note") {
      // These lines carry their values verbatim: an edit
      // label and the run's closing texts are sentences,
      // spaces and all, so they are not tokenized like the
      // other lines. Their order is the grammar's: steps
      // follow the scenes, then the stop reason, then the
      // note, and the note closes the payload.
      const std::string text = line.substr(tag_end + 1);
      if (text.empty()) return false;
      if (kind == "step") {
        if (saw_stop_reason || saw_note) return false;
        saw_step = true;
        next.steps_.push_back(text);
      } else if (kind == "stop_reason") {
        if (saw_stop_reason || saw_note) return false;
        saw_stop_reason = true;
        next.stop_reason_ = text;
      } else {
        if (!saw_stop_reason || saw_note) return false;
        saw_note = true;
        next.note_ = text;
      }
      continue;
    }
    if (saw_stop_reason || saw_note) return false;
    std::map<std::string, std::string> fields;
    if (!LineFields(line.substr(tag_end + 1), &fields)) return false;
    if (kind == "scene") {
      if (saw_step) return false;
      if (!OnlyKeys(fields,
                    {"name", "initial_worst_slack_ns",
                     "initial_worst_hold_slack_ns", "worst_slack_ns",
                     "total_negative_slack_ns", "worst_hold_slack_ns",
                     "total_negative_hold_slack_ns"})) {
        return false;
      }
      if (fields["name"].empty()) return false;
      if (!scene_names.insert(fields["name"]).second) return false;
      EcoSceneLine scene;
      scene.name = fields["name"];
      if (!ParseMetric(fields["initial_worst_slack_ns"],
                       &scene.initial_worst_slack_ns) ||
          !ParseMetric(fields["initial_worst_hold_slack_ns"],
                       &scene.initial_worst_hold_slack_ns) ||
          !ParseMetric(fields["worst_slack_ns"],
                       &scene.worst_slack_ns) ||
          !ParseMetric(fields["total_negative_slack_ns"],
                       &scene.total_negative_slack_ns) ||
          !ParseMetric(fields["worst_hold_slack_ns"],
                       &scene.worst_hold_slack_ns) ||
          !ParseMetric(fields["total_negative_hold_slack_ns"],
                       &scene.total_negative_hold_slack_ns)) {
        return false;
      }
      next.scenes_.push_back(std::move(scene));
    } else {
      return false;
    }
  }
  if (!saw_stop_reason || !saw_note) return false;
  next.loaded_ = true;
  *this = std::move(next);
  return true;
}

void EcoModel::Clear() {
  loaded_ = false;
  closed_ = false;
  iterations_ = 0;
  candidates_tried_ = 0;
  candidates_rejected_ = 0;
  candidates_skipped_stale_ = 0;
  candidates_skipped_memory_ = 0;
  max_iterations_ = 0;
  scenes_.clear();
  steps_.clear();
  stop_reason_.clear();
  note_.clear();
}

}  // namespace eos::ui
