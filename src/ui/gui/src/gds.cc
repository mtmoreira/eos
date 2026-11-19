// gds: the GUI's GDS export model (L6 interface, Qt-free).
#include "eos/ui/gds.h"

#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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
// an optional minus, digits, no trailing junk, no overflow
// past int.
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
    const char c = text[i];
    if (c < '0' || c > '9') return false;
    parsed = parsed * 10 + (c - '0');
    if (parsed > 2147483647L) return false;
  }
  *value = static_cast<int>(negative ? -parsed : parsed);
  return true;
}

// Parses a double covering the whole token: strtod with no
// trailing junk.
bool ParseDouble(const std::string& text, double* value) {
  if (text.empty()) return false;
  char* end = nullptr;
  *value = std::strtod(text.c_str(), &end);
  return end != nullptr && *end == '\0';
}

// Parses a points token ("x,y[,x,y...]") into coordinate
// pairs. Returns false on an odd coordinate count, a bad
// number, or fewer than the minimum point count.
bool ParsePoints(const std::string& text, std::size_t min_points,
                 std::vector<std::array<int, 2>>* points) {
  std::vector<int> coords;
  std::size_t begin = 0;
  while (true) {
    const std::size_t comma = text.find(',', begin);
    const std::string token = text.substr(
        begin, comma == std::string::npos ? comma : comma - begin);
    int value = 0;
    if (!ParseInt(token, &value)) return false;
    coords.push_back(value);
    if (comma == std::string::npos) break;
    begin = comma + 1;
  }
  if (coords.size() % 2 != 0 || coords.size() / 2 < min_points) {
    return false;
  }
  for (std::size_t i = 0; i < coords.size(); i += 2) {
    points->push_back({coords[i], coords[i + 1]});
  }
  return true;
}

// The payload's layer name token for a census name: the
// name itself, or "-" when the layer is unnamed.
std::string LayerToken(const std::string& name) {
  return name.empty() ? "-" : name;
}

}  // namespace

bool GdsModel::LoadPayload(const std::string& payload) {
  std::istringstream in(payload);
  std::string line;
  if (!std::getline(in, line)) return false;
  const std::size_t space = line.find(' ');
  if (space == std::string::npos) return false;
  if (line.substr(0, space) != "gds") return false;
  std::map<std::string, std::string> header;
  if (!LineFields(line.substr(space + 1), &header) ||
      !OnlyKeys(header, {"path", "size", "instances", "structures",
                         "library", "dbu_per_micron"})) {
    return false;
  }
  if (header["path"].empty() || header["library"].empty()) {
    return false;
  }

  GdsModel next;
  if (!ParseCount(header["size"], &next.size_) ||
      !ParseCount(header["instances"], &next.instances_) ||
      !ParseCount(header["structures"], &next.structures_) ||
      !ParseDouble(header["dbu_per_micron"], &next.dbu_per_micron_)) {
    return false;
  }
  next.path_ = header["path"];
  next.library_ = header["library"];

  std::set<std::string> structure_names;
  std::map<std::pair<int, int>, std::string> layer_names;
  int phase = 0;  // 0 structures, 1 layers, 2 geometry.
  bool have_layer_key = false;
  std::pair<int, int> last_layer_key;
  std::string sref_structure;
  std::size_t sref_lines = 0;
  while (std::getline(in, line)) {
    if (line.empty()) return false;
    const std::size_t tag_end = line.find(' ');
    if (tag_end == std::string::npos) return false;
    const std::string tag = line.substr(0, tag_end);
    std::map<std::string, std::string> fields;
    if (!LineFields(line.substr(tag_end + 1), &fields)) return false;
    if (tag == "structure") {
      if (phase != 0) return false;
      if (!OnlyKeys(fields,
                    {"name", "top", "boundaries", "paths", "srefs"})) {
        return false;
      }
      GdsStructureInfo info;
      info.name = fields["name"];
      if (info.name.empty() ||
          !structure_names.insert(info.name).second) {
        return false;
      }
      if (fields["top"] == "1") {
        info.top = true;
      } else if (fields["top"] != "0") {
        return false;
      }
      if (!ParseCount(fields["boundaries"], &info.boundaries) ||
          !ParseCount(fields["paths"], &info.paths) ||
          !ParseCount(fields["srefs"], &info.srefs)) {
        return false;
      }
      next.structure_infos_.push_back(info);
      continue;
    }
    if (tag == "layer") {
      if (phase > 1) return false;
      phase = 1;
      if (!OnlyKeys(fields, {"gds", "name", "datatype", "boundaries",
                             "paths"})) {
        return false;
      }
      GdsLayerInfo info;
      if (!ParseInt(fields["gds"], &info.gds_layer) ||
          !ParseInt(fields["datatype"], &info.datatype) ||
          !ParseCount(fields["boundaries"], &info.boundaries) ||
          !ParseCount(fields["paths"], &info.paths)) {
        return false;
      }
      const std::pair<int, int> key = {info.gds_layer, info.datatype};
      if (have_layer_key && !(last_layer_key < key)) return false;
      have_layer_key = true;
      last_layer_key = key;
      info.name = fields["name"] == "-" ? "" : fields["name"];
      layer_names[key] = info.name;
      next.layer_infos_.push_back(info);
      continue;
    }
    if (tag == "boundary" || tag == "path") {
      phase = 2;
      if (!OnlyKeys(fields, {"structure", "layer", "gds_layer",
                             "datatype", "points"}) &&
          !(tag == "path" &&
            OnlyKeys(fields, {"structure", "layer", "gds_layer",
                              "datatype", "width", "points"}))) {
        return false;
      }
      const std::string& structure = fields["structure"];
      if (structure_names.find(structure) == structure_names.end()) {
        return false;
      }
      int gds_layer = 0;
      int datatype = 0;
      if (!ParseInt(fields["gds_layer"], &gds_layer) ||
          !ParseInt(fields["datatype"], &datatype)) {
        return false;
      }
      const auto layer = layer_names.find({gds_layer, datatype});
      if (layer == layer_names.end() ||
          fields["layer"] != LayerToken(layer->second)) {
        return false;
      }
      if (tag == "boundary") {
        GdsBoundaryShape shape;
        shape.structure = structure;
        shape.layer_name = layer->second;
        shape.gds_layer = gds_layer;
        shape.datatype = datatype;
        if (!ParsePoints(fields["points"], 3, &shape.points)) {
          return false;
        }
        next.boundaries_.push_back(shape);
      } else {
        GdsPathShape shape;
        shape.structure = structure;
        shape.layer_name = layer->second;
        shape.gds_layer = gds_layer;
        shape.datatype = datatype;
        if (fields.find("width") != fields.end()) {
          if (fields["width"] == "none") {
            shape.has_width = false;
          } else if (!ParseInt(fields["width"], &shape.width)) {
            return false;
          } else {
            shape.has_width = true;
          }
        }
        if (!ParsePoints(fields["points"], 2, &shape.points)) {
          return false;
        }
        next.paths_.push_back(shape);
      }
      continue;
    }
    if (tag == "sref") {
      phase = 2;
      if (!OnlyKeys(fields, {"structure", "target", "x", "y",
                             "reflect", "angle"})) {
        return false;
      }
      GdsSrefInfo info;
      info.structure = fields["structure"];
      info.target = fields["target"];
      if (structure_names.find(info.structure) ==
              structure_names.end() ||
          structure_names.find(info.target) == structure_names.end()) {
        return false;
      }
      if (!sref_structure.empty() &&
          sref_structure != info.structure) {
        return false;
      }
      sref_structure = info.structure;
      if (!ParseInt(fields["x"], &info.x) ||
          !ParseInt(fields["y"], &info.y)) {
        return false;
      }
      if (fields["reflect"] == "1") {
        info.reflect = true;
      } else if (fields["reflect"] != "0") {
        return false;
      }
      if (fields["angle"] == "none") {
        info.has_angle = false;
      } else if (!ParseDouble(fields["angle"], &info.angle_degrees)) {
        return false;
      } else {
        info.has_angle = true;
      }
      ++sref_lines;
      next.srefs_.push_back(info);
      continue;
    }
    return false;
  }

  // The recount invariants: the structure lines recount the
  // header's structures, and the sref lines recount the
  // header's instances (the top structure's SREF count,
  // which the top's structure line must agree with).
  if (next.structure_infos_.size() != next.structures_) return false;
  if (sref_lines != next.instances_) return false;
  if (sref_lines > 0) {
    bool agrees = false;
    for (const GdsStructureInfo& info : next.structure_infos_) {
      if (info.name == sref_structure) {
        agrees = info.top && info.srefs == sref_lines;
      }
    }
    if (!agrees) return false;
  }

  bool have_box = false;
  std::array<int, 4> box = {0, 0, 0, 0};
  const auto extend = [&have_box, &box](const std::array<int, 2>& p) {
    if (!have_box) {
      box = {p[0], p[1], p[0], p[1]};
      have_box = true;
      return;
    }
    if (p[0] < box[0]) box[0] = p[0];
    if (p[1] < box[1]) box[1] = p[1];
    if (p[0] > box[2]) box[2] = p[0];
    if (p[1] > box[3]) box[3] = p[1];
  };
  for (const GdsBoundaryShape& shape : next.boundaries_) {
    for (const auto& point : shape.points) extend(point);
  }
  for (const GdsPathShape& shape : next.paths_) {
    for (const auto& point : shape.points) extend(point);
  }
  next.has_bounding_box_ = have_box;
  next.bounding_box_ = box;
  next.loaded_ = true;
  *this = next;
  return true;
}

void GdsModel::Clear() { *this = GdsModel(); }

std::vector<const GdsBoundaryShape*> GdsModel::BoundariesForLayer(
    const std::string& layer_name) const {
  std::vector<const GdsBoundaryShape*> out;
  for (const GdsBoundaryShape& shape : boundaries_) {
    if (shape.layer_name == layer_name) out.push_back(&shape);
  }
  return out;
}

std::vector<const GdsPathShape*> GdsModel::PathsForLayer(
    const std::string& layer_name) const {
  std::vector<const GdsPathShape*> out;
  for (const GdsPathShape& shape : paths_) {
    if (shape.layer_name == layer_name) out.push_back(&shape);
  }
  return out;
}

}  // namespace eos::ui
