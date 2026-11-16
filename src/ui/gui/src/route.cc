// route: the GUI's route model (L6 interface, Qt-free).
#include "eos/ui/route.h"

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

// Parses a signed decimal integer covering the whole token:
// an optional leading '-', then digits, no trailing junk.
// Coordinates in database units are non-negative in
// practice, but the wire points list parses signed so a
// negative origin never rejects a real payload.
bool ParseInt(const std::string& text, int* value) {
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

// Parses a double covering the whole token, the same rule the
// timing model uses: strtod with no trailing junk.
bool ParseDouble(const std::string& text, double* value) {
  if (text.empty()) return false;
  char* end = nullptr;
  *value = std::strtod(text.c_str(), &end);
  return end != nullptr && *end == '\0';
}

// Parses a rectangle "x1,y1,x2,y2" into (lx, ly, ux, uy).
// Returns false on any malformed part or a degenerate
// rectangle (ux <= lx or uy <= ly).
bool ParseRect(const std::string& text, RouteRect* rect) {
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
    if (!ParseInt(parts[i], &(*rect)[i])) return false;
  }
  return (*rect)[2] > (*rect)[0] && (*rect)[3] > (*rect)[1];
}

}  // namespace

bool RouteModel::LoadPayload(const std::string& payload) {
  std::istringstream in(payload);
  std::string line;
  if (!std::getline(in, line)) return false;
  const std::size_t space = line.find(' ');
  if (space == std::string::npos) return false;
  if (line.substr(0, space) != "route") return false;
  std::map<std::string, std::string> header;
  if (!LineFields(line.substr(space + 1), &header) ||
      !OnlyKeys(header, {"routed", "total", "guides", "overflow",
                         "violations", "detailed_iterations",
                         "detailed_bound", "stopped_at_bound",
                         "liberty_corner", "die", "core"})) {
    return false;
  }
  RouteModel next;
  if (!ParseCount(header["routed"], &next.routed_) ||
      !ParseCount(header["total"], &next.total_) ||
      !ParseCount(header["guides"], &next.guides_) ||
      !ParseCount(header["violations"], &next.violations_) ||
      !ParseCount(header["detailed_iterations"],
                  &next.detailed_iterations_) ||
      !ParseCount(header["detailed_bound"], &next.detailed_bound_)) {
    return false;
  }
  // Overflow is a summed per-tile quantity and can exceed
  // the int range the other counts use, so it parses as a
  // non-negative long long, not a size_t count.
  {
    const std::string& text = header["overflow"];
    if (text.empty()) return false;
    long long parsed = 0;
    for (char c : text) {
      if (c < '0' || c > '9') return false;
      parsed = parsed * 10 + (c - '0');
      if (parsed > 9223372036854775807LL / 10) return false;
    }
    next.overflow_ = parsed;
  }
  if (header["stopped_at_bound"] == "true") {
    next.stopped_at_bound_ = true;
  } else if (header["stopped_at_bound"] == "false") {
    next.stopped_at_bound_ = false;
  } else {
    return false;
  }
  if (header["liberty_corner"].empty()) return false;
  next.liberty_corner_ = header["liberty_corner"];
  if (!ParseRect(header["die"], &next.die_) ||
      !ParseRect(header["core"], &next.core_)) {
    return false;
  }
  if (next.routed_ > next.total_) return false;

  std::set<std::string> layer_names;
  bool seen_geometry = false;
  bool seen_sta_note = false;
  while (std::getline(in, line)) {
    if (line.empty()) return false;
    const std::size_t tag_end = line.find(' ');
    if (tag_end == std::string::npos) return false;
    const std::string kind = line.substr(0, tag_end);
    if (kind == "sta_note") {
      // The note is the rest of the line verbatim: it is the
      // route payload's unavailable statement, spaces and all,
      // so it is not tokenized like the other lines. It
      // belongs to the run, and a run carries slack lines or
      // one note, never both.
      if (seen_sta_note || !next.slacks_.empty()) return false;
      next.sta_note_ = line.substr(tag_end + 1);
      if (next.sta_note_.empty()) return false;
      seen_sta_note = true;
      continue;
    }
    std::map<std::string, std::string> fields;
    if (!LineFields(line.substr(tag_end + 1), &fields)) return false;
    if (kind == "layer") {
      // Layer lines come first, in number order: a wire or
      // via line naming a layer the header never listed is
      // malformed, and the canvas filter reads the layer
      // list in the order it was given.
      if (seen_geometry || seen_sta_note || !next.slacks_.empty()) {
        return false;
      }
      if (!OnlyKeys(fields, {"number", "name", "capacity", "usage",
                             "overflow"})) {
        return false;
      }
      if (fields["name"].empty()) return false;
      if (!layer_names.insert(fields["name"]).second) return false;
      RouteLayer layer;
      layer.name = fields["name"];
      std::size_t number = 0;
      if (!ParseCount(fields["number"], &number) || number == 0) {
        return false;
      }
      layer.number = static_cast<int>(number);
      if (!next.layers_.empty() &&
          layer.number <= next.layers_.back().number) {
        return false;
      }
      long long capacity = 0;
      long long usage = 0;
      long long overflow = 0;
      auto parse_long = [](const std::string& text, long long* value) {
        if (text.empty()) return false;
        long long parsed = 0;
        for (char c : text) {
          if (c < '0' || c > '9') return false;
          parsed = parsed * 10 + (c - '0');
          if (parsed > 9223372036854775807LL / 10) return false;
        }
        *value = parsed;
        return true;
      };
      if (!parse_long(fields["capacity"], &capacity) ||
          !parse_long(fields["usage"], &usage) ||
          !parse_long(fields["overflow"], &overflow)) {
        return false;
      }
      layer.capacity = capacity;
      layer.usage = usage;
      layer.overflow = overflow;
      next.layers_.push_back(std::move(layer));
    } else if (kind == "wire") {
      if (seen_sta_note || !next.slacks_.empty()) return false;
      if (!OnlyKeys(fields, {"net", "layer", "width", "points"})) {
        return false;
      }
      if (fields["net"].empty() || fields["layer"].empty()) return false;
      if (layer_names.find(fields["layer"]) == layer_names.end()) {
        return false;
      }
      RouteSegment segment;
      segment.net = fields["net"];
      segment.layer = fields["layer"];
      std::size_t width = 0;
      if (!ParseCount(fields["width"], &width) || width == 0) {
        return false;
      }
      segment.width = static_cast<int>(width);
      // The points list is a single token of comma-separated
      // coordinates: an even count, at least two points.
      std::vector<std::string> parts;
      std::size_t start = 0;
      const std::string& points_text = fields["points"];
      while (true) {
        const std::size_t comma = points_text.find(',', start);
        if (comma == std::string::npos) {
          parts.push_back(points_text.substr(start));
          break;
        }
        parts.push_back(points_text.substr(start, comma - start));
        start = comma + 1;
      }
      if (parts.size() < 4 || parts.size() % 2 != 0) return false;
      for (std::size_t i = 0; i < parts.size(); i += 2) {
        int x = 0;
        int y = 0;
        if (!ParseInt(parts[i], &x) || !ParseInt(parts[i + 1], &y)) {
          return false;
        }
        segment.points.push_back({x, y});
      }
      seen_geometry = true;
      next.segments_.push_back(std::move(segment));
    } else if (kind == "via") {
      if (seen_sta_note || !next.slacks_.empty()) return false;
      if (!OnlyKeys(fields, {"net", "layers", "x", "y"})) return false;
      if (fields["net"].empty()) return false;
      const std::string& layers_text = fields["layers"];
      const std::size_t dash = layers_text.find('-');
      if (dash == std::string::npos || dash == 0 ||
          dash + 1 >= layers_text.size() ||
          layers_text.find('-', dash + 1) != std::string::npos) {
        return false;
      }
      RouteViaPoint via;
      via.net = fields["net"];
      via.lower_layer = layers_text.substr(0, dash);
      via.upper_layer = layers_text.substr(dash + 1);
      if (layer_names.find(via.lower_layer) == layer_names.end() ||
          layer_names.find(via.upper_layer) == layer_names.end()) {
        return false;
      }
      if (!ParseInt(fields["x"], &via.x) ||
          !ParseInt(fields["y"], &via.y)) {
        return false;
      }
      seen_geometry = true;
      next.vias_.push_back(std::move(via));
    } else if (kind == "unwired") {
      if (seen_sta_note || !next.slacks_.empty()) return false;
      if (!OnlyKeys(fields, {"net"})) return false;
      if (fields["net"].empty()) return false;
      next.unwired_nets_.push_back(fields["net"]);
    } else if (kind == "slack") {
      if (seen_sta_note) return false;
      if (!OnlyKeys(fields, {"scene", "worst_slack_ns"})) return false;
      if (fields["scene"].empty()) return false;
      for (const RouteSlack& existing : next.slacks_) {
        if (existing.scene == fields["scene"]) return false;
      }
      RouteSlack slack;
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
  // The header's routed-versus-total gap is the unwired net
  // count: every unrouted signal net is named by one unwired
  // line, and no other net is.
  if (next.total_ - next.routed_ != next.unwired_nets_.size()) {
    return false;
  }
  next.loaded_ = true;
  *this = std::move(next);
  return true;
}

void RouteModel::Clear() {
  loaded_ = false;
  routed_ = 0;
  total_ = 0;
  guides_ = 0;
  overflow_ = 0;
  violations_ = 0;
  detailed_iterations_ = 0;
  detailed_bound_ = 0;
  stopped_at_bound_ = false;
  liberty_corner_.clear();
  die_ = {0, 0, 0, 0};
  core_ = {0, 0, 0, 0};
  layers_.clear();
  segments_.clear();
  vias_.clear();
  unwired_nets_.clear();
  slacks_.clear();
  sta_note_.clear();
}

std::vector<const RouteSegment*> RouteModel::WiresForLayer(
    const std::string& layer) const {
  std::vector<const RouteSegment*> out;
  for (const RouteSegment& segment : segments_) {
    if (segment.layer == layer) out.push_back(&segment);
  }
  return out;
}

std::vector<const RouteViaPoint*> RouteModel::ViasForLayer(
    const std::string& layer) const {
  std::vector<const RouteViaPoint*> out;
  for (const RouteViaPoint& via : vias_) {
    if (via.lower_layer == layer || via.upper_layer == layer) {
      out.push_back(&via);
    }
  }
  return out;
}

}  // namespace eos::ui
