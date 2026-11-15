// clock_network: the clock_network_summary payload parser.
// See clock_network.h for the model; the line grammar is the
// one the registry's clock_network_summary handler emits
// (clock-network design section 3.1): a header line, then
// clock, node, shared, and note lines, each space-separated
// key=value fields with brace groups for values that may
// contain spaces.

#include "eos/ui/clock_network.h"

#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace eos::ui {
namespace {

// Splits one line into key=value pairs. A value is a single
// token, or a {...} brace group, which may contain spaces; the
// braces ride with the value. Returns false on a token
// without '=', an unterminated brace group, a '}' outside a
// group, or a duplicate key.
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

// The value map of one line, or false when the line is
// malformed. Duplicate keys are malformed.
bool LineFields(const std::string& line,
                std::map<std::string, std::string>* fields) {
  std::vector<std::pair<std::string, std::string>> pairs;
  if (!SplitLine(line, &pairs)) return false;
  for (const auto& pair : pairs) (*fields)[pair.first] = pair.second;
  return true;
}

bool OnlyKeys(const std::map<std::string, std::string>& fields,
              const std::vector<const char*>& keys) {
  if (fields.size() != keys.size()) return false;
  for (const char* key : keys) {
    if (fields.find(key) == fields.end()) return false;
  }
  return true;
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

bool IsCountOrDash(const std::string& text) {
  if (text == "-") return true;
  std::size_t value = 0;
  return ParseCount(text, &value);
}

bool IsOneOf(const std::string& text,
             const std::vector<const char*>& options) {
  for (const char* option : options) {
    if (text == option) return true;
  }
  return false;
}

// Strips the braces of a brace-group value; a non-braced value
// passes through unchanged.
std::string Unbrace(const std::string& value) {
  if (value.size() >= 2 && value.front() == '{' && value.back() == '}') {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

}  // namespace

bool ClockNetworkModel::LoadPayload(const std::string& payload) {
  std::istringstream in(payload);
  std::string line;
  if (!std::getline(in, line)) return false;
  std::map<std::string, std::string> header;
  if (!LineFields(line, &header) ||
      !OnlyKeys(header, {"stage", "clocks"})) {
    return false;
  }
  if (!IsOneOf(header["stage"], {"constraints", "elaborated", "mapped"})) {
    return false;
  }
  std::size_t clock_count = 0;
  if (!ParseCount(header["clocks"], &clock_count)) return false;

  ClockNetworkModel next;
  next.stage_ = header["stage"];
  std::map<std::string, std::size_t> clock_index;
  while (std::getline(in, line)) {
    if (line.empty()) return false;
    const std::size_t space = line.find(' ');
    if (space == std::string::npos) return false;
    const std::string kind = line.substr(0, space);
    std::map<std::string, std::string> fields;
    if (!LineFields(line.substr(space + 1), &fields)) return false;
    if (kind == "clock") {
      if (!OnlyKeys(fields,
                    {"name", "generated", "period", "rise", "fall", "skew",
                     "source", "sinks", "latency", "master", "modification",
                     "tree"})) {
        return false;
      }
      double skew_value = 0.0;
      if (fields["name"].empty() ||
          (fields["generated"] != "0" && fields["generated"] != "1") ||
          !IsNumberOrDash(fields["period"]) ||
          !IsNumberOrDash(fields["rise"]) ||
          !IsNumberOrDash(fields["fall"]) ||
          (fields["skew"] != "none" &&
           !ParseDouble(fields["skew"], &skew_value)) ||
          fields["source"].empty() ||
          !IsCountOrDash(fields["sinks"]) ||
          fields["latency"] != "-" || fields["master"].empty() ||
          fields["modification"].empty() ||
          !IsOneOf(fields["tree"], {"none", "leaves", "pins"})) {
        return false;
      }
      if (clock_index.count(fields["name"]) != 0) return false;
      ClockNetworkClock row;
      row.name = fields["name"];
      row.generated = fields["generated"] == "1";
      row.period = fields["period"];
      row.rise = fields["rise"];
      row.fall = fields["fall"];
      row.skew = fields["skew"];
      row.sources = fields["source"];
      row.sinks = fields["sinks"];
      row.master = fields["master"];
      row.modification = fields["modification"];
      row.tree = fields["tree"];
      clock_index[row.name] = next.clocks_.size();
      next.clocks_.push_back(std::move(row));
    } else if (kind == "node") {
      if (!OnlyKeys(fields, {"clock", "depth", "kind", "name", "pin",
                             "count", "generates"})) {
        return false;
      }
      std::size_t depth = 0;
      if (!ParseCount(fields["depth"], &depth) ||
          !IsOneOf(fields["kind"],
                   {"source", "leaf", "divider", "buffer", "sink"}) ||
          fields["name"].empty() || fields["pin"].empty() ||
          !IsCountOrDash(fields["count"]) ||
          fields["generates"].empty()) {
        return false;
      }
      const auto owner = clock_index.find(fields["clock"]);
      if (owner == clock_index.end()) return false;
      ClockNetworkNode node;
      node.depth = static_cast<int>(depth);
      node.kind = fields["kind"];
      node.name = Unbrace(fields["name"]);
      node.pin = fields["pin"];
      node.count = fields["count"];
      node.generates = fields["generates"];
      next.clocks_[owner->second].nodes.push_back(std::move(node));
    } else if (kind == "shared") {
      if (!OnlyKeys(fields, {"clock", "other", "count"})) return false;
      std::size_t count = 0;
      if (fields["clock"].empty() || fields["other"].empty() ||
          !ParseCount(fields["count"], &count)) {
        return false;
      }
      ClockNetworkShared row;
      row.clock = fields["clock"];
      row.other = fields["other"];
      row.count = count;
      next.shared_.push_back(std::move(row));
    } else if (kind == "note") {
      if (!OnlyKeys(fields, {"text"})) return false;
      if (fields["text"].empty()) return false;
      next.notes_.push_back(Unbrace(fields["text"]));
    } else {
      return false;
    }
  }
  if (next.clocks_.size() != clock_count) return false;
  next.loaded_ = true;
  *this = std::move(next);
  return true;
}

}  // namespace eos::ui
