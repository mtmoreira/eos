// extract: the GUI's extract model (L6 interface, Qt-free).
#include "eos/ui/extract.h"

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

}  // namespace

bool ExtractModel::LoadPayload(const std::string& payload) {
  std::istringstream in(payload);
  std::string line;
  if (!std::getline(in, line)) return false;
  const std::size_t space = line.find(' ');
  if (space == std::string::npos) return false;
  if (line.substr(0, space) != "extract") return false;
  std::map<std::string, std::string> header;
  if (!LineFields(line.substr(space + 1), &header) ||
      !OnlyKeys(header, {"nets", "rsegs", "cap_nodes", "cc_segs",
                         "spef", "model"})) {
    return false;
  }
  if (header["spef"].empty() || header["model"].empty()) return false;

  ExtractModel next;
  if (!ParseCount(header["nets"], &next.nets_) ||
      !ParseCount(header["rsegs"], &next.rsegs_) ||
      !ParseCount(header["cap_nodes"], &next.cap_nodes_) ||
      !ParseCount(header["cc_segs"], &next.cc_segs_)) {
    return false;
  }
  next.spef_path_ = header["spef"];
  next.model_path_ = header["model"];

  std::set<std::string> slack_scenes;
  std::set<std::pair<std::string, std::string>> compare_pairs;
  bool saw_compare = false;
  while (std::getline(in, line)) {
    if (line.empty()) return false;
    const std::size_t tag_end = line.find(' ');
    if (tag_end == std::string::npos) return false;
    const std::string kind = line.substr(0, tag_end);
    if (kind == "sta_note") {
      // The note is the rest of the line verbatim: it is the
      // extract payload's unavailable statement, spaces and
      // all, so it is not tokenized like the other lines. A
      // record carries slack lines or one note, never both,
      // and the note precedes any compare line.
      if (saw_compare || !next.slacks_.empty() ||
          !next.sta_note_.empty()) {
        return false;
      }
      next.sta_note_ = line.substr(tag_end + 1);
      if (next.sta_note_.empty()) return false;
      continue;
    }
    std::map<std::string, std::string> fields;
    if (!LineFields(line.substr(tag_end + 1), &fields)) return false;
    if (kind == "slack") {
      if (saw_compare) return false;
      if (!OnlyKeys(fields, {"scene", "worst_slack_ns"})) {
        return false;
      }
      if (fields["scene"].empty()) return false;
      if (!next.sta_note_.empty()) return false;
      if (!slack_scenes.insert(fields["scene"]).second) return false;
      ExtractSlack slack;
      slack.scene = fields["scene"];
      if (!ParseDouble(fields["worst_slack_ns"],
                       &slack.worst_slack_ns)) {
        return false;
      }
      next.slacks_.push_back(std::move(slack));
    } else if (kind == "compare") {
      if (!OnlyKeys(fields,
                    {"mode", "corner", "estimate_scene",
                     "extracted_scene", "estimated_worst_slack_ns",
                     "extracted_worst_slack_ns",
                     "estimated_worst_hold_slack_ns",
                     "extracted_worst_hold_slack_ns"})) {
        return false;
      }
      if (fields["mode"].empty() || fields["corner"].empty() ||
          fields["estimate_scene"].empty() ||
          fields["extracted_scene"].empty()) {
        return false;
      }
      if (!compare_pairs
               .insert({fields["mode"], fields["corner"]})
               .second) {
        return false;
      }
      ExtractCompare compare;
      compare.mode = fields["mode"];
      compare.corner = fields["corner"];
      compare.estimate_scene = fields["estimate_scene"];
      compare.extracted_scene = fields["extracted_scene"];
      if (!ParseDouble(fields["estimated_worst_slack_ns"],
                       &compare.estimated_worst_slack_ns) ||
          !ParseDouble(fields["extracted_worst_slack_ns"],
                       &compare.extracted_worst_slack_ns) ||
          !ParseDouble(fields["estimated_worst_hold_slack_ns"],
                       &compare.estimated_worst_hold_slack_ns) ||
          !ParseDouble(fields["extracted_worst_hold_slack_ns"],
                       &compare.extracted_worst_hold_slack_ns)) {
        return false;
      }
      saw_compare = true;
      next.compares_.push_back(std::move(compare));
    } else {
      return false;
    }
  }
  next.loaded_ = true;
  *this = std::move(next);
  return true;
}

void ExtractModel::Clear() {
  loaded_ = false;
  nets_ = 0;
  rsegs_ = 0;
  cap_nodes_ = 0;
  cc_segs_ = 0;
  spef_path_.clear();
  model_path_.clear();
  slacks_.clear();
  sta_note_.clear();
  compares_.clear();
}

}  // namespace eos::ui
