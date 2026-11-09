// placement: the GUI's placement model (L6 interface, Qt-free).
#include "eos/ui/placement.h"

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
// token: no sign, no trailing junk, no overflow past
// size_t.
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

// Parses a non-negative integer covering the whole token
// into a long long: HPWL can exceed the int range on a large
// placed design, so the count parser's int cap does not apply.
bool ParseHpwl(const std::string& text, long long* value) {
  if (text.empty()) return false;
  char* end = nullptr;
  const long long parsed = std::strtoll(text.c_str(), &end, 10);
  if (end == nullptr || *end != '\0' || parsed < 0) return false;
  *value = parsed;
  return true;
}

// Parses a signed decimal integer covering the whole token:
// an optional minus, digits only, no trailing junk, within
// int range.
bool ParseCoord(const std::string& text, int* value) {
  if (text.empty()) return false;
  std::size_t i = 0;
  bool negative = false;
  if (text[0] == '-') {
    negative = true;
    i = 1;
    if (text.size() == 1) return false;
  }
  long parsed = 0;
  for (; i < text.size(); ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
    parsed = parsed * 10 + (text[i] - '0');
    if (parsed > 2147483647L) return false;
  }
  *value = negative ? -static_cast<int>(parsed)
                    : static_cast<int>(parsed);
  return true;
}

// Parses a "lx,ly,ux,uy" rectangle: exactly four signed
// integers, comma separated, no spaces.
bool ParseRect(const std::string& text, PlacementRect* rect) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (true) {
    const std::size_t comma = text.find(',', start);
    if (comma == std::string::npos) {
      parts.push_back(text.substr(start));
      break;
    }
    parts.push_back(text.substr(start, comma - start));
    start = comma + 1;
  }
  if (parts.size() != 4) return false;
  for (int i = 0; i < 4; ++i) {
    if (!ParseCoord(parts[i], &(*rect)[i])) return false;
  }
  return true;
}

bool RectHasArea(const PlacementRect& rect) {
  return rect[2] > rect[0] && rect[3] > rect[1];
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

bool PlacementModel::LoadPayload(const std::string& payload) {
  std::istringstream in(payload);
  std::string line;
  if (!std::getline(in, line)) return false;
  const std::size_t space = line.find(' ');
  if (space == std::string::npos) return false;
  if (line.substr(0, space) != "placement") return false;
  std::map<std::string, std::string> header;
  if (!LineFields(line.substr(space + 1), &header) ||
      !OnlyKeys(header, {"die", "core", "placed", "total", "hpwl",
                         "overlaps"})) {
    return false;
  }
  PlacementModel next;
  if (!ParseRect(header["die"], &next.die_) ||
      !ParseRect(header["core"], &next.core_) ||
      !RectHasArea(next.die_) || !RectHasArea(next.core_)) {
    return false;
  }
  std::size_t total = 0;
  if (!ParseCount(header["placed"], &next.placed_) ||
      !ParseCount(header["total"], &total) ||
      !ParseCount(header["overlaps"], &next.overlaps_) ||
      !ParseHpwl(header["hpwl"], &next.hpwl_)) {
    return false;
  }
  if (next.placed_ > total) return false;
  std::set<std::string> instance_names;
  std::set<std::string> scene_names;
  while (std::getline(in, line)) {
    if (line.empty()) return false;
    const std::size_t tag_end = line.find(' ');
    if (tag_end == std::string::npos) return false;
    const std::string kind = line.substr(0, tag_end);
    std::map<std::string, std::string> fields;
    if (!LineFields(line.substr(tag_end + 1), &fields)) return false;
    if (kind == "inst") {
      if (!OnlyKeys(fields, {"name", "master", "x", "y", "w", "h"})) {
        return false;
      }
      if (fields["name"].empty() || fields["master"].empty()) {
        return false;
      }
      if (!instance_names.insert(fields["name"]).second) return false;
      int x = 0;
      int y = 0;
      int w = 0;
      int h = 0;
      if (!ParseCoord(fields["x"], &x) ||
          !ParseCoord(fields["y"], &y) ||
          !ParseCoord(fields["w"], &w) ||
          !ParseCoord(fields["h"], &h) || w <= 0 || h <= 0) {
        return false;
      }
      PlacementInstance instance;
      instance.name = fields["name"];
      instance.master = fields["master"];
      instance.rect = {x, y, x + w, y + h};
      next.instances_.push_back(std::move(instance));
    } else if (kind == "slack") {
      if (!OnlyKeys(fields, {"scene", "worst_slack_ns"})) return false;
      if (fields["scene"].empty()) return false;
      if (!scene_names.insert(fields["scene"]).second) return false;
      PlacementSlack slack;
      slack.scene = fields["scene"];
      if (!ParseDouble(fields["worst_slack_ns"],
                       &slack.worst_slack_ns)) {
        return false;
      }
      next.slacks_.push_back(std::move(slack));
    } else {
      return false;
    }
  }
  if (next.instances_.size() != total) return false;
  next.loaded_ = true;
  *this = std::move(next);
  return true;
}

void PlacementModel::Clear() {
  loaded_ = false;
  die_ = {0, 0, 0, 0};
  core_ = {0, 0, 0, 0};
  placed_ = 0;
  hpwl_ = 0;
  overlaps_ = 0;
  instances_.clear();
  slacks_.clear();
}

}  // namespace eos::ui
