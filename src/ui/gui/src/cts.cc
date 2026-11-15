// cts: the GUI's CTS model (L6 interface, Qt-free).
#include "eos/ui/cts.h"

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

bool CtsModel::LoadPayload(const std::string& payload) {
  std::istringstream in(payload);
  std::string line;
  if (!std::getline(in, line)) return false;
  const std::size_t space = line.find(' ');
  if (space == std::string::npos) return false;
  if (line.substr(0, space) != "cts") return false;
  std::map<std::string, std::string> header;
  if (!LineFields(line.substr(space + 1), &header) ||
      !OnlyKeys(header, {"nets"})) {
    return false;
  }
  std::size_t net_count = 0;
  if (!ParseCount(header["nets"], &net_count)) return false;

  CtsModel next;
  std::set<std::string> net_names;
  while (std::getline(in, line)) {
    if (line.empty()) return false;
    const std::size_t tag_end = line.find(' ');
    if (tag_end == std::string::npos) return false;
    const std::string kind = line.substr(0, tag_end);
    if (kind == "sta_note") {
      // The note is the rest of the line verbatim: it is the
      // cts payload's unavailable statement, spaces and all,
      // so it is not tokenized like the other lines. It
      // belongs to the net line above it, and a net carries
      // slack lines or one note, never both.
      if (next.nets_.empty()) return false;
      CtsNet& net = next.nets_.back();
      if (!net.sta_note.empty() || !net.slacks.empty()) return false;
      net.sta_note = line.substr(tag_end + 1);
      if (net.sta_note.empty()) return false;
      continue;
    }
    std::map<std::string, std::string> fields;
    if (!LineFields(line.substr(tag_end + 1), &fields)) return false;
    if (kind == "net") {
      if (!OnlyKeys(fields, {"name", "roots", "buffers", "sinks",
                             "clock_sinks", "subnets",
                             "liberty_corner"})) {
        return false;
      }
      if (fields["name"].empty() || fields["liberty_corner"].empty()) {
        return false;
      }
      if (!net_names.insert(fields["name"]).second) return false;
      CtsNet net;
      net.name = fields["name"];
      net.liberty_corner = fields["liberty_corner"];
      if (!ParseCount(fields["roots"], &net.roots) ||
          !ParseCount(fields["buffers"], &net.buffers) ||
          !ParseCount(fields["sinks"], &net.sinks) ||
          !ParseCount(fields["clock_sinks"], &net.clock_sinks) ||
          !ParseCount(fields["subnets"], &net.subnets)) {
        return false;
      }
      next.nets_.push_back(std::move(net));
    } else if (kind == "slack") {
      if (!OnlyKeys(fields, {"scene", "worst_slack_ns"})) return false;
      if (fields["scene"].empty()) return false;
      // A slack line belongs to the net line above it; one
      // before any net line is malformed, and a net with a
      // sta_note carries no slack lines.
      if (next.nets_.empty()) return false;
      CtsNet& net = next.nets_.back();
      if (!net.sta_note.empty()) return false;
      for (const CtsSlack& existing : net.slacks) {
        if (existing.scene == fields["scene"]) return false;
      }
      CtsSlack slack;
      slack.scene = fields["scene"];
      if (!ParseDouble(fields["worst_slack_ns"],
                       &slack.worst_slack_ns)) {
        return false;
      }
      net.slacks.push_back(std::move(slack));
    } else {
      return false;
    }
  }
  if (next.nets_.size() != net_count) return false;
  next.loaded_ = true;
  *this = std::move(next);
  return true;
}

void CtsModel::Clear() {
  loaded_ = false;
  nets_.clear();
}

}  // namespace eos::ui
