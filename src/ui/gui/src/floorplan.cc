// floorplan: the GUI's floorplan model (L6 interface, Qt-free).
#include "eos/ui/floorplan.h"

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
bool ParseRect(const std::string& text, FloorplanRect* rect) {
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

bool RectHasArea(const FloorplanRect& rect) {
  return rect[2] > rect[0] && rect[3] > rect[1];
}

}  // namespace

bool FloorplanModel::LoadPayload(const std::string& payload) {
  std::istringstream in(payload);
  std::string line;
  if (!std::getline(in, line)) return false;
  const std::size_t space = line.find(' ');
  if (space == std::string::npos) return false;
  if (line.substr(0, space) != "floorplan") return false;
  std::map<std::string, std::string> header;
  if (!LineFields(line.substr(space + 1), &header) ||
      !OnlyKeys(header, {"die", "core", "rows", "track_grids",
                         "io_placed", "io_total"})) {
    return false;
  }
  FloorplanModel next;
  if (!ParseRect(header["die"], &next.die_) ||
      !ParseRect(header["core"], &next.core_) ||
      !RectHasArea(next.die_) || !RectHasArea(next.core_)) {
    return false;
  }
  std::size_t row_count = 0;
  std::size_t io_placed = 0;
  std::size_t io_total = 0;
  if (!ParseCount(header["rows"], &row_count) ||
      !ParseCount(header["track_grids"], &next.track_grids_) ||
      !ParseCount(header["io_placed"], &io_placed) ||
      !ParseCount(header["io_total"], &io_total)) {
    return false;
  }
  std::set<std::string> pin_names;
  std::size_t placed_seen = 0;
  while (std::getline(in, line)) {
    if (line.empty()) return false;
    const std::size_t tag_end = line.find(' ');
    if (tag_end == std::string::npos) return false;
    const std::string kind = line.substr(0, tag_end);
    std::map<std::string, std::string> fields;
    if (!LineFields(line.substr(tag_end + 1), &fields)) return false;
    if (kind == "row") {
      if (!OnlyKeys(fields, {"rect"})) return false;
      FloorplanRect rect = {0, 0, 0, 0};
      if (!ParseRect(fields["rect"], &rect) || !RectHasArea(rect)) {
        return false;
      }
      next.rows_.push_back(rect);
    } else if (kind == "pin") {
      if (!OnlyKeys(fields, {"name", "dir", "placed", "rect"})) {
        return false;
      }
      if (fields["name"].empty() ||
          (fields["dir"] != "in" && fields["dir"] != "out" &&
           fields["dir"] != "inout" && fields["dir"] != "feedthru") ||
          (fields["placed"] != "0" && fields["placed"] != "1")) {
        return false;
      }
      if (!pin_names.insert(fields["name"]).second) return false;
      FloorplanPin pin;
      pin.name = fields["name"];
      pin.direction = fields["dir"];
      pin.placed = fields["placed"] == "1";
      if (!ParseRect(fields["rect"], &pin.rect)) return false;
      if (pin.placed) ++placed_seen;
      next.pins_.push_back(std::move(pin));
    } else {
      return false;
    }
  }
  if (next.rows_.size() != row_count ||
      next.pins_.size() != io_total || placed_seen != io_placed) {
    return false;
  }
  next.io_placed_ = io_placed;
  next.loaded_ = true;
  *this = std::move(next);
  return true;
}

void FloorplanModel::Clear() {
  loaded_ = false;
  die_ = {0, 0, 0, 0};
  core_ = {0, 0, 0, 0};
  track_grids_ = 0;
  io_placed_ = 0;
  rows_.clear();
  pins_.clear();
}

}  // namespace eos::ui
