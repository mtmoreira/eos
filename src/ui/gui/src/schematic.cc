// schematic: the GUI's gate-level schematic model (L6 interface,
// Qt-free).
#include "eos/ui/schematic.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace eos::ui {

namespace {

// Normalizes a raw cell type for matching: strips one leading
// '$' and lowercases, so $AND, $and, and and all match.
std::string NormalizedType(const std::string& type) {
  std::string out;
  const std::size_t start = (!type.empty() && type[0] == '$') ? 1 : 0;
  for (std::size_t i = start; i < type.size(); ++i) {
    out += static_cast<char>(
        std::tolower(static_cast<unsigned char>(type[i])));
  }
  return out;
}

bool IsDigits(const std::string& text) {
  if (text.empty()) return false;
  for (const char c : text) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

// Parses a width value: digits only, at least 1, and small
// enough to be a real bus width. Returns false otherwise.
bool ParseWidth(const std::string& text, int& width) {
  if (!IsDigits(text)) return false;
  int value = 0;
  for (const char c : text) {
    value = value * 10 + (c - '0');
    if (value > 1000000) return false;
  }
  if (value < 1) return false;
  width = value;
  return true;
}

}  // namespace

std::string NetLabel(const SchematicNet& net) {
  if (net.width <= 1) return net.name;
  return net.name + " [" + std::to_string(net.width) + "]";
}

GateSymbol SymbolForType(const std::string& type) {
  const std::string normalized = NormalizedType(type);
  if (normalized == "and") return GateSymbol::kAnd;
  if (normalized == "nand") return GateSymbol::kNand;
  if (normalized == "or") return GateSymbol::kOr;
  if (normalized == "nor") return GateSymbol::kNor;
  if (normalized == "xor") return GateSymbol::kXor;
  if (normalized == "xnor") return GateSymbol::kXnor;
  if (normalized == "not" || normalized == "inv") return GateSymbol::kNot;
  if (normalized == "buf") return GateSymbol::kBuf;
  if (normalized == "mux" || normalized == "pmux")
    return GateSymbol::kMux;
  if (normalized == "dff" || normalized == "dffe" ||
      normalized == "dffsr" || normalized == "dffsre" ||
      normalized == "adff" || normalized == "adffe" ||
      normalized == "aldff" || normalized == "aldffe" ||
      normalized == "sdff" || normalized == "sdffce" ||
      normalized == "sdffe")
    return GateSymbol::kDff;
  if (normalized == "dlatch" || normalized == "dlatchsr" ||
      normalized == "sr")
    return GateSymbol::kLatch;
  if (normalized == "logic_and" || normalized == "reduce_and")
    return GateSymbol::kAnd;
  if (normalized == "logic_or" || normalized == "reduce_or")
    return GateSymbol::kOr;
  if (normalized == "logic_not") return GateSymbol::kNot;
  if (normalized == "reduce_xor") return GateSymbol::kXor;
  if (normalized == "reduce_xnor") return GateSymbol::kXnor;
  if (normalized == "tribuf" || normalized == "pos")
    return GateSymbol::kBuf;
  return GateSymbol::kGeneric;
}

std::string OperatorGlyph(const std::string& type) {
  const std::string normalized = NormalizedType(type);
  if (normalized == "add") return "+";
  if (normalized == "sub" || normalized == "neg") return "-";
  if (normalized == "mul") return "x";
  if (normalized == "div") return "/";
  if (normalized == "mod") return "%";
  if (normalized == "eq") return "=";
  if (normalized == "ne") return "!=";
  if (normalized == "lt") return "<";
  if (normalized == "gt") return ">";
  if (normalized == "le") return "<=";
  if (normalized == "ge") return ">=";
  if (normalized == "shl" || normalized == "sshl") return "<<";
  if (normalized == "shr" || normalized == "sshr") return ">>";
  return "";
}

std::string LogicEquation(const std::string& type) {
  const std::string normalized = NormalizedType(type);
  if (normalized == "mem" || normalized == "memrd") return "Q = mem[A]";
  if (normalized == "memwr") return "mem[A] = D";
  if (normalized == "fsm") return "Y = f(state, in)";
  if (normalized == "lut") return "Y = lut(A)";
  if (normalized == "pow") return "Y = A ** B";
  if (normalized == "shift") return "Y = shift(A, B)";
  return "";
}

std::string DisplayType(const std::string& type) {
  std::string out;
  const std::size_t start = (!type.empty() && type[0] == '$') ? 1 : 0;
  for (std::size_t i = start; i < type.size(); ++i) {
    out += static_cast<char>(
        std::toupper(static_cast<unsigned char>(type[i])));
  }
  return out;
}

std::string DisplayInstance(const std::string& instance) {
  // Plain identifiers show as-is: no '$', no path.
  if (instance.find('$') == std::string::npos &&
      instance.find('/') == std::string::npos) {
    return instance;
  }
  // Yosys auto names: $type$rest, where rest is file:line
  // with an optional $counter suffix. The file:line middle
  // is dropped in every case: it is a source location for a
  // real file and a temp path for a synth temp file, and the
  // widget shows neither. Only the counter survives, as
  // #counter, so gates from one location stay distinct.
  if (instance.size() >= 2 && instance[0] == '$') {
    const std::size_t second = instance.find('$', 1);
    if (second != std::string::npos) {
      const std::string rest = instance.substr(second + 1);
      const std::size_t counter = rest.rfind('$');
      if (counter != std::string::npos &&
          IsDigits(rest.substr(counter + 1))) {
        return "#" + rest.substr(counter + 1);
      }
      return "";
    }
  }
  // Any other path-bearing name: basename only, without a
  // trailing :line suffix, and nothing for a temp basename.
  // A leftover '$' means an unrecognized internal form, which
  // shows nothing rather than leaking internals.
  std::string base = instance;
  const std::size_t slash = base.rfind('/');
  if (slash != std::string::npos) base = base.substr(slash + 1);
  const std::size_t colon = base.rfind(':');
  if (colon != std::string::npos && IsDigits(base.substr(colon + 1))) {
    base = base.substr(0, colon);
  }
  if (base.find("eos_synth") != std::string::npos) return "";
  if (!base.empty() && base[0] == '$') base = base.substr(1);
  if (base.find('$') != std::string::npos) return "";
  return base;
}

namespace {

// Splits a "key=value" token list field. Every field is required
// exactly once, and no other key is allowed. Returns false on a
// token without '=', a duplicate key, or an unknown key.
bool ParseFields(const std::string& line, const std::string& prefix,
                 std::vector<std::pair<std::string, std::string>>& out) {
  if (line.rfind(prefix, 0) != 0) return false;
  std::istringstream in(line.substr(prefix.size()));
  std::string token;
  std::set<std::string> seen;
  while (in >> token) {
    const std::size_t equal = token.find('=');
    if (equal == std::string::npos) return false;
    const std::string key = token.substr(0, equal);
    const std::string value = token.substr(equal + 1);
    if (!seen.insert(key).second) return false;
    out.emplace_back(key, value);
  }
  return true;
}

bool FieldIs(const std::vector<std::pair<std::string, std::string>>& fields,
             const std::string& key, std::string& value) {
  for (const auto& field : fields) {
    if (field.first == key) {
      value = field.second;
      return true;
    }
  }
  return false;
}

bool IsDirection(const std::string& direction) {
  return direction == "in" || direction == "out" ||
         direction == "inout";
}

bool IsGateDirection(const std::string& direction) {
  return direction == "in" || direction == "out";
}

// Parses a comma list of "name:direction" port entries. An empty
// value means no ports.
bool ParsePorts(const std::string& value,
                std::vector<SchematicPort>& ports, bool gate) {
  if (value.empty()) return true;
  std::set<std::string> seen;
  std::istringstream in(value);
  std::string token;
  while (std::getline(in, token, ',')) {
    const std::size_t colon = token.find(':');
    if (colon == std::string::npos) return false;
    SchematicPort port;
    port.name = token.substr(0, colon);
    port.direction = token.substr(colon + 1);
    if (port.name.empty()) return false;
    if (gate ? !IsGateDirection(port.direction)
             : !IsDirection(port.direction)) {
      return false;
    }
    if (!seen.insert(port.name).second) return false;
    ports.push_back(std::move(port));
  }
  return true;
}

// Parses a comma list of "gate:port" endpoint entries. An empty
// value means no endpoints. The split is at the LAST colon:
// Yosys auto-generates primitive cell names that contain
// colons (file:line form), while port names are plain
// identifiers that never contain one, so the port is always
// the tail after the last colon.
bool ParseEndpoints(const std::string& value,
                    std::vector<SchematicNetEndpoint>& endpoints) {
  if (value.empty()) return true;
  std::set<std::string> seen;
  std::istringstream in(value);
  std::string token;
  while (std::getline(in, token, ',')) {
    const std::size_t colon = token.rfind(':');
    if (colon == std::string::npos) return false;
    SchematicNetEndpoint endpoint;
    endpoint.gate = token.substr(0, colon);
    endpoint.port = token.substr(colon + 1);
    if (endpoint.gate.empty() || endpoint.port.empty()) return false;
    if (!seen.insert(endpoint.gate + ":" + endpoint.port).second) {
      return false;
    }
    endpoints.push_back(std::move(endpoint));
  }
  return true;
}

// Parses a comma list of names. An empty value means no names.
bool ParseNames(const std::string& value, std::vector<std::string>& names) {
  if (value.empty()) return true;
  std::set<std::string> seen;
  std::istringstream in(value);
  std::string token;
  while (std::getline(in, token, ',')) {
    if (token.empty()) return false;
    if (!seen.insert(token).second) return false;
    names.push_back(token);
  }
  return true;
}

}  // namespace

bool SchematicModel::LoadPayload(const std::string& payload) {
  std::istringstream in(payload);
  std::string line;
  std::string module;
  bool module_seen = false;
  std::vector<SchematicPort> parent_ports;
  std::set<std::string> parent_names;
  std::vector<SchematicGate> gates;
  std::set<std::string> gate_names;
  std::vector<SchematicNet> nets;
  std::set<std::string> net_names;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    if (!module_seen) {
      if (line.rfind("module=", 0) != 0) return false;
      module = line.substr(7);
      if (module.empty()) return false;
      module_seen = true;
      continue;
    }
    std::vector<std::pair<std::string, std::string>> fields;
    if (line.rfind("parent ", 0) == 0) {
      if (!ParseFields(line, "parent ", fields)) return false;
      if (fields.size() != 2) return false;
      std::string name;
      std::string direction;
      if (!FieldIs(fields, "port", name) ||
          !FieldIs(fields, "direction", direction)) {
        return false;
      }
      if (name.empty() || !IsDirection(direction)) return false;
      if (!parent_names.insert(name).second) return false;
      parent_ports.push_back(SchematicPort{name, direction});
      continue;
    }
    if (line.rfind("gate ", 0) == 0) {
      if (!ParseFields(line, "gate ", fields)) return false;
      if (fields.size() != 3) return false;
      SchematicGate gate;
      std::string ports_value;
      if (!FieldIs(fields, "instance", gate.instance) ||
          !FieldIs(fields, "type", gate.type) ||
          !FieldIs(fields, "ports", ports_value)) {
        return false;
      }
      if (gate.instance.empty() || gate.type.empty()) return false;
      if (!ParsePorts(ports_value, gate.ports, true)) return false;
      if (!gate_names.insert(gate.instance).second) return false;
      gates.push_back(std::move(gate));
      continue;
    }
    if (line.rfind("net ", 0) == 0) {
      if (!ParseFields(line, "net ", fields)) return false;
      if (fields.size() != 3 && fields.size() != 4) return false;
      SchematicNet net;
      std::string gates_value;
      std::string terminals_value;
      std::string width_value;
      if (!FieldIs(fields, "name", net.name) ||
          !FieldIs(fields, "gates", gates_value) ||
          !FieldIs(fields, "terminals", terminals_value)) {
        return false;
      }
      if (net.name.empty()) return false;
      if (fields.size() == 4) {
        if (!FieldIs(fields, "width", width_value) ||
            !ParseWidth(width_value, net.width)) {
          return false;
        }
      }
      if (!ParseEndpoints(gates_value, net.endpoints)) return false;
      if (!ParseNames(terminals_value, net.terminals)) return false;
      if (net.endpoints.empty() && net.terminals.empty()) return false;
      if (!net_names.insert(net.name).second) return false;
      nets.push_back(std::move(net));
      continue;
    }
    return false;
  }
  if (!module_seen) return false;
  // Cross-checks: every endpoint names a known gate and one of
  // its ports, and every terminal names a known parent port.
  for (const SchematicNet& net : nets) {
    for (const SchematicNetEndpoint& endpoint : net.endpoints) {
      const SchematicGate* gate = nullptr;
      for (const SchematicGate& candidate : gates) {
        if (candidate.instance == endpoint.gate) {
          gate = &candidate;
          break;
        }
      }
      if (gate == nullptr) return false;
      bool port_known = false;
      for (const SchematicPort& port : gate->ports) {
        if (port.name == endpoint.port) {
          port_known = true;
          break;
        }
      }
      if (!port_known) return false;
    }
    for (const std::string& terminal : net.terminals) {
      if (parent_names.find(terminal) == parent_names.end()) {
        return false;
      }
    }
  }
  module_ = module;
  parent_ports_ = std::move(parent_ports);
  gates_ = std::move(gates);
  nets_ = std::move(nets);
  return true;
}

void SchematicModel::Clear() {
  module_.clear();
  parent_ports_.clear();
  gates_.clear();
  nets_.clear();
}

const SchematicGate* SchematicModel::FindGate(
    const std::string& instance) const {
  for (const SchematicGate& gate : gates_) {
    if (gate.instance == instance) return &gate;
  }
  return nullptr;
}

namespace {

// Whether a normalized cell type is a sequential cell: its
// output is a sequential output, a source for level purposes,
// whatever drives its data input.
bool IsSequentialType(const std::string& type) {
  const std::string normalized = NormalizedType(type);
  return normalized == "dff" || normalized == "adff" ||
         normalized == "dffe" || normalized == "adffe" ||
         normalized == "dffsr" || normalized == "adffsr" ||
         normalized == "ff" || normalized == "fsm" ||
         normalized == "mem" || normalized == "mem_v2" ||
         normalized == "dlatch" || normalized == "latch" ||
         normalized == "sr";
}

const SchematicNet* FindNet(const SchematicModel& model,
                            const std::string& name) {
  for (const SchematicNet& net : model.Nets()) {
    if (net.name == name) return &net;
  }
  return nullptr;
}

// The gates driving a net: endpoints on it whose port
// direction is "out". A net with no driver is driven by a
// parent terminal or is undriven.
std::vector<std::string> NetDrivers(const SchematicModel& model,
                                    const std::string& net_name) {
  std::vector<std::string> drivers;
  const SchematicNet* net = FindNet(model, net_name);
  if (net == nullptr) return drivers;
  for (const SchematicNetEndpoint& endpoint : net->endpoints) {
    const SchematicGate* gate = model.FindGate(endpoint.gate);
    if (gate == nullptr) continue;
    for (const SchematicPort& port : gate->ports) {
      if (port.name == endpoint.port && port.direction == "out") {
        drivers.push_back(endpoint.gate);
      }
    }
  }
  return drivers;
}

// The gates loaded by a net: endpoints on it whose port
// direction is "in".
std::vector<std::string> NetLoads(const SchematicModel& model,
                                  const std::string& net_name) {
  std::vector<std::string> loads;
  const SchematicNet* net = FindNet(model, net_name);
  if (net == nullptr) return loads;
  for (const SchematicNetEndpoint& endpoint : net->endpoints) {
    const SchematicGate* gate = model.FindGate(endpoint.gate);
    if (gate == nullptr) continue;
    for (const SchematicPort& port : gate->ports) {
      if (port.name == endpoint.port && port.direction == "in") {
        loads.push_back(endpoint.gate);
      }
    }
  }
  return loads;
}

// The nets touching one of a gate's input ports.
std::vector<std::string> GateInputNets(const SchematicModel& model,
                                       const SchematicGate& gate) {
  std::vector<std::string> nets;
  for (const SchematicPort& port : gate.ports) {
    if (port.direction != "in") continue;
    for (const SchematicNet& net : model.Nets()) {
      for (const SchematicNetEndpoint& endpoint : net.endpoints) {
        if (endpoint.gate == gate.instance &&
            endpoint.port == port.name) {
          nets.push_back(net.name);
          break;
        }
      }
    }
  }
  return nets;
}

// The nets touching one of a gate's output ports.
std::vector<std::string> GateOutputNets(const SchematicModel& model,
                                        const SchematicGate& gate) {
  std::vector<std::string> nets;
  for (const SchematicPort& port : gate.ports) {
    if (port.direction != "out") continue;
    for (const SchematicNet& net : model.Nets()) {
      for (const SchematicNetEndpoint& endpoint : net.endpoints) {
        if (endpoint.gate == gate.instance &&
            endpoint.port == port.name) {
          nets.push_back(net.name);
          break;
        }
      }
    }
  }
  return nets;
}

// Levels by depth-first search with memoization. A
// combinational loop breaks at the edge that re-enters a gate
// already being visited: the loop edge contributes nothing,
// so levels stay finite and deterministic.
class LevelComputer {
 public:
  explicit LevelComputer(const SchematicModel& model) : model_(model) {}

  std::map<std::string, int> Compute() {
    for (const SchematicGate& gate : model_.Gates()) {
      LevelOf(gate.instance);
    }
    return levels_;
  }

 private:
  int LevelOf(const std::string& instance) {
    const auto found = levels_.find(instance);
    if (found != levels_.end()) return found->second;
    if (visiting_.count(instance) != 0) return 0;
    const SchematicGate* gate = model_.FindGate(instance);
    if (gate == nullptr) return 0;
    if (IsSequentialType(gate->type)) {
      levels_[instance] = 0;
      return 0;
    }
    visiting_.insert(instance);
    int level = 0;
    for (const std::string& net : GateInputNets(model_, *gate)) {
      for (const std::string& driver : NetDrivers(model_, net)) {
        if (visiting_.count(driver) != 0) continue;
        level = std::max(level, LevelOf(driver) + 1);
      }
    }
    visiting_.erase(instance);
    levels_[instance] = level;
    return level;
  }

  const SchematicModel& model_;
  std::map<std::string, int> levels_;
  std::set<std::string> visiting_;
};

double RowPitch() {
  return kSchematicGateHeight + kSchematicGateGap;
}

double ColumnX(int level, double gate_width) {
  return kSchematicGateX +
         static_cast<double>(level) *
             (gate_width + kSchematicColumnGap);
}

double RowY(int row) {
  return kSchematicGateTop + static_cast<double>(row) * RowPitch();
}

// The horizontal channel line between two gate rows that a
// skip-level net uses to cross columns: the gap above the
// source row, or below row 0 when there is no row above.
double ChannelY(int row) {
  if (row > 0) {
    return kSchematicGateTop + static_cast<double>(row) * RowPitch() -
           kSchematicGateGap / 2;
  }
  return kSchematicGateTop + kSchematicGateHeight +
         kSchematicGateGap / 2;
}

void AppendPoint(std::vector<SchematicPoint>& path, SchematicPoint point) {
  if (!path.empty() && path.back().x == point.x &&
      path.back().y == point.y) {
    return;
  }
  // Drop the middle point when three in a row are collinear,
  // so a path keeps only its corners.
  while (path.size() >= 2) {
    const SchematicPoint& a = path[path.size() - 2];
    const SchematicPoint& b = path.back();
    if ((a.x == b.x && b.x == point.x) ||
        (a.y == b.y && b.y == point.y)) {
      path.pop_back();
    } else {
      break;
    }
  }
  path.push_back(point);
}

bool SamePoint(const SchematicPoint& a, const SchematicPoint& b) {
  return a.x == b.x && a.y == b.y;
}

}  // namespace

std::map<std::string, int> SchematicLevels(const SchematicModel& model) {
  LevelComputer computer(model);
  return computer.Compute();
}

std::set<std::string> FaninCone(const SchematicModel& model,
                                const std::string& instance) {
  std::set<std::string> cone;
  const SchematicGate* gate = model.FindGate(instance);
  if (gate == nullptr) return cone;
  std::vector<std::string> work;
  for (const std::string& net : GateInputNets(model, *gate)) {
    for (const std::string& driver : NetDrivers(model, net)) {
      if (driver != instance && cone.insert(driver).second) {
        work.push_back(driver);
      }
    }
  }
  while (!work.empty()) {
    const std::string current = work.back();
    work.pop_back();
    const SchematicGate* next = model.FindGate(current);
    if (next == nullptr) continue;
    for (const std::string& net : GateInputNets(model, *next)) {
      for (const std::string& driver : NetDrivers(model, net)) {
        if (driver != instance && cone.insert(driver).second) {
          work.push_back(driver);
        }
      }
    }
  }
  return cone;
}

std::set<std::string> FanoutCone(const SchematicModel& model,
                                 const std::string& instance) {
  std::set<std::string> cone;
  const SchematicGate* gate = model.FindGate(instance);
  if (gate == nullptr) return cone;
  std::vector<std::string> work;
  for (const std::string& net : GateOutputNets(model, *gate)) {
    for (const std::string& load : NetLoads(model, net)) {
      if (load != instance && cone.insert(load).second) {
        work.push_back(load);
      }
    }
  }
  while (!work.empty()) {
    const std::string current = work.back();
    work.pop_back();
    const SchematicGate* next = model.FindGate(current);
    if (next == nullptr) continue;
    for (const std::string& net : GateOutputNets(model, *next)) {
      for (const std::string& load : NetLoads(model, net)) {
        if (load != instance && cone.insert(load).second) {
          work.push_back(load);
        }
      }
    }
  }
  return cone;
}

std::set<std::pair<std::string, std::size_t>> ConeNetPaths(
    const SchematicModel& model, const SchematicLayout& layout,
    const std::string& instance) {
  std::set<std::pair<std::string, std::size_t>> in_cone;
  if (model.FindGate(instance) == nullptr) return in_cone;
  const std::set<std::string> fanin = FaninCone(model, instance);
  const std::set<std::string> fanout = FanoutCone(model, instance);
  std::set<std::string> cone = fanin;
  cone.insert(fanout.begin(), fanout.end());
  cone.insert(instance);
  std::set<std::string> fanin_side = fanin;
  fanin_side.insert(instance);
  std::set<std::string> fanout_side = fanout;
  fanout_side.insert(instance);
  for (const SchematicNetRoute& route : layout.nets) {
    for (std::size_t i = 0; i < route.paths.size(); ++i) {
      if (i >= route.path_ends.size()) continue;
      const SchematicNetPathEnds& ends = route.path_ends[i];
      bool hit = false;
      if (!ends.load_gate.empty() && !ends.driver_gate.empty()) {
        // Gate to gate: in the cone only when both ends are
        // cone gates, so a cone gate's branch to a sibling
        // load stays out.
        hit = cone.count(ends.load_gate) != 0 &&
              cone.count(ends.driver_gate) != 0;
      } else if (!ends.load_gate.empty() &&
                 !ends.driver_terminal.empty()) {
        // A primary input feeds the cone only on the fanin
        // side: an input to a fanout gate comes from outside
        // the cone.
        hit = fanin_side.count(ends.load_gate) != 0;
      } else if (!ends.load_terminal.empty() &&
                 !ends.driver_gate.empty()) {
        // A cone gate drives the cone only on the fanout side:
        // a fanin gate's side output to a parent port is not
        // in the cone.
        hit = fanout_side.count(ends.driver_gate) != 0;
      }
      if (hit) in_cone.insert({route.name, i});
    }
  }
  return in_cone;
}

SchematicLayout ComputeSchematicLayout(const SchematicModel& model,
                                       double gate_width) {
  SchematicLayout layout;
  const std::map<std::string, int> levels = SchematicLevels(model);
  int max_level = -1;
  for (const auto& entry : levels) {
    max_level = std::max(max_level, entry.second);
  }
  const int columns = max_level + 1;

  // Rows within a level by barycenter ordering: a few sweeps
  // left to right against driver rows, then right to left
  // against load rows, so gates fed by the same drivers line
  // up and avoidable crossings drop out. Stable sorts keep the
  // model order on ties, so the result is deterministic.
  std::map<int, std::vector<std::string>> by_level;
  for (const SchematicGate& gate : model.Gates()) {
    by_level[levels.at(gate.instance)].push_back(gate.instance);
  }
  std::map<std::string, int> row_of;
  auto rebuild_rows = [&]() {
    row_of.clear();
    for (const auto& entry : by_level) {
      for (std::size_t i = 0; i < entry.second.size(); ++i) {
        row_of[entry.second[i]] = static_cast<int>(i);
      }
    }
  };
  rebuild_rows();
  auto mean_neighbour_row = [&](const std::string& instance,
                                bool drivers, int level) {
    const SchematicGate* gate = model.FindGate(instance);
    if (gate == nullptr) return -1.0;
    double sum = 0;
    int count = 0;
    const std::vector<std::string> nets =
        drivers ? GateInputNets(model, *gate)
                : GateOutputNets(model, *gate);
    for (const std::string& net : nets) {
      const std::vector<std::string> neighbours =
          drivers ? NetDrivers(model, net) : NetLoads(model, net);
      for (const std::string& neighbour : neighbours) {
        if (neighbour == instance) continue;
        const auto found = levels.find(neighbour);
        if (found == levels.end()) continue;
        if (drivers && found->second >= level) continue;
        if (!drivers && found->second <= level) continue;
        const auto row = row_of.find(neighbour);
        if (row == row_of.end()) continue;
        sum += row->second;
        ++count;
      }
    }
    if (count == 0) return -1.0;
    return sum / count;
  };
  for (int sweep = 0; sweep < 3; ++sweep) {
    for (int level = 1; level <= max_level; ++level) {
      std::vector<std::string>& order = by_level[level];
      std::map<std::string, double> key;
      for (const std::string& instance : order) {
        const double mean = mean_neighbour_row(instance, true, level);
        key[instance] =
            mean < 0 ? static_cast<double>(row_of[instance]) : mean;
      }
      std::stable_sort(order.begin(), order.end(),
                       [&](const std::string& a, const std::string& b) {
                         return key[a] < key[b];
                       });
      rebuild_rows();
    }
    for (int level = max_level - 1; level >= 0; --level) {
      std::vector<std::string>& order = by_level[level];
      std::map<std::string, double> key;
      for (const std::string& instance : order) {
        const double mean = mean_neighbour_row(instance, false, level);
        key[instance] =
            mean < 0 ? static_cast<double>(row_of[instance]) : mean;
      }
      std::stable_sort(order.begin(), order.end(),
                       [&](const std::string& a, const std::string& b) {
                         return key[a] < key[b];
                       });
      rebuild_rows();
    }
  }

  // Gate placements with port anchors: input and inout ports
  // on the left edge, output ports on the right, spread by
  // port index among the ports on the same side.
  std::map<std::string, const SchematicGatePlacement*> placement_of;
  for (const auto& entry : by_level) {
    for (std::size_t i = 0; i < entry.second.size(); ++i) {
      const SchematicGate* gate = model.FindGate(entry.second[i]);
      SchematicGatePlacement placement;
      placement.instance = entry.second[i];
      placement.level = entry.first;
      placement.row = static_cast<int>(i);
      placement.x = ColumnX(entry.first, gate_width);
      placement.y = RowY(static_cast<int>(i));
      placement.width = gate_width;
      placement.height = kSchematicGateHeight;
      if (gate != nullptr) {
        for (const SchematicPort& port : gate->ports) {
          std::vector<const SchematicPort*> side_ports;
          for (const SchematicPort& candidate : gate->ports) {
            const bool candidate_left =
                candidate.direction == "in" ||
                candidate.direction == "inout";
            const bool port_left = port.direction == "in" ||
                                   port.direction == "inout";
            if (candidate_left == port_left) {
              side_ports.push_back(&candidate);
            }
          }
          std::size_t index = 0;
          for (std::size_t k = 0; k < side_ports.size(); ++k) {
            if (side_ports[k]->name == port.name) {
              index = k;
              break;
            }
          }
          SchematicPortAnchor anchor;
          anchor.name = port.name;
          anchor.left_side = port.direction == "in" ||
                             port.direction == "inout";
          anchor.is_output = port.direction == "out";
          anchor.x = anchor.left_side ? placement.x
                                      : placement.x + gate_width;
          anchor.y = placement.y +
                     (static_cast<double>(index) + 1) *
                         placement.height /
                         (static_cast<double>(side_ports.size()) + 1);
          placement.ports.push_back(anchor);
        }
      }
      layout.gates.push_back(std::move(placement));
    }
  }
  // Keep gate placements in model order, so the widget draws
  // them the way the model lists them.
  std::sort(layout.gates.begin(), layout.gates.end(),
            [&](const SchematicGatePlacement& a,
                const SchematicGatePlacement& b) {
              std::size_t ai = model.Gates().size();
              std::size_t bi = model.Gates().size();
              for (std::size_t i = 0; i < model.Gates().size(); ++i) {
                if (model.Gates()[i].instance == a.instance) ai = i;
                if (model.Gates()[i].instance == b.instance) bi = i;
              }
              return ai < bi;
            });
  for (const SchematicGatePlacement& placement : layout.gates) {
    placement_of[placement.instance] = &placement;
  }

  // Output terminals sit one column gap past the last gate
  // column; with no gates they sit at the old fixed spot.
  layout.output_x =
      columns > 0
          ? ColumnX(max_level, gate_width) + gate_width +
                kSchematicColumnGap
          : kSchematicGateX + kSchematicColumnGap + kSchematicMinGateWidth;
  std::size_t input_row = 0;
  std::size_t output_row = 0;
  for (const SchematicPort& port : model.ParentPorts()) {
    const bool input_side =
        port.direction == "in" || port.direction == "inout";
    const std::size_t row = input_side ? input_row++ : output_row++;
    SchematicTerminalPlacement terminal;
    terminal.name = port.name;
    terminal.input_side = input_side;
    terminal.y = RowY(static_cast<int>(row)) + kSchematicTerminalSize / 2;
    terminal.x = input_side
                     ? kSchematicInputX + kSchematicTerminalSize
                     : layout.output_x;
    layout.terminals.push_back(terminal);
  }
  std::map<std::string, const SchematicTerminalPlacement*> terminal_of;
  for (const SchematicTerminalPlacement& terminal : layout.terminals) {
    terminal_of[terminal.name] = &terminal;
  }

  // Routes: one polyline per load, from the driving anchor to
  // one load anchor, all segments orthogonal.
  struct RouteAnchor {
    SchematicPoint point;
    bool faces_left;
    bool is_output;
    bool is_terminal;
    bool terminal_input;
    int column;
    int row;
    // Identity for the path-ends record: a gate anchor names
    // its gate and port, a terminal anchor its parent port.
    // The other field stays empty.
    std::string gate;
    std::string port;
    std::string terminal;
  };
  auto column_left = [&](int column) {
    if (column < 0) return kSchematicInputX;
    if (column >= columns) return layout.output_x;
    return ColumnX(column, gate_width);
  };
  auto column_right = [&](int column) {
    if (column < 0) return kSchematicInputX + kSchematicTerminalSize;
    if (column >= columns) {
      return layout.output_x + kSchematicTerminalSize;
    }
    return ColumnX(column, gate_width) + gate_width;
  };
  auto gate_anchor = [&](const std::string& instance,
                         const std::string& port_name,
                         RouteAnchor& anchor) {
    const auto found = placement_of.find(instance);
    if (found == placement_of.end()) return false;
    for (const SchematicPortAnchor& port : found->second->ports) {
      if (port.name != port_name) continue;
      anchor.point = SchematicPoint{port.x, port.y};
      anchor.faces_left = port.left_side;
      anchor.is_output = port.is_output;
      anchor.is_terminal = false;
      anchor.terminal_input = false;
      anchor.column = found->second->level;
      anchor.row = found->second->row;
      anchor.gate = instance;
      anchor.port = port_name;
      return true;
    }
    return false;
  };
  for (const SchematicNet& net : model.Nets()) {
    std::vector<RouteAnchor> anchors;
    for (const SchematicNetEndpoint& endpoint : net.endpoints) {
      RouteAnchor anchor;
      if (gate_anchor(endpoint.gate, endpoint.port, anchor)) {
        anchors.push_back(anchor);
      }
    }
    for (const std::string& terminal_name : net.terminals) {
      const auto found = terminal_of.find(terminal_name);
      if (found == terminal_of.end()) continue;
      RouteAnchor anchor;
      anchor.point = SchematicPoint{found->second->x, found->second->y};
      anchor.is_output = !found->second->input_side;
      anchor.faces_left = !found->second->input_side;
      anchor.is_terminal = true;
      anchor.terminal_input = found->second->input_side;
      anchor.column =
          found->second->input_side ? -1 : columns;
      anchor.row = 0;
      // Terminal rows are declaration order on each side; the
      // row feeds the channel choice below.
      std::size_t seen = 0;
      for (const SchematicPort& port : model.ParentPorts()) {
        const bool input_side =
            port.direction == "in" || port.direction == "inout";
        if (input_side != found->second->input_side) continue;
        if (port.name == terminal_name) break;
        ++seen;
      }
      anchor.row = static_cast<int>(seen);
      anchor.terminal = terminal_name;
      anchors.push_back(anchor);
    }
    if (anchors.empty()) continue;
    SchematicNetRoute route;
    route.name = net.name;
    route.label = NetLabel(net);
    // The endpoints of one path, from its driving anchor to
    // its load anchor, recorded beside the polyline.
    auto make_ends = [](const RouteAnchor& from, const RouteAnchor& to) {
      SchematicNetPathEnds ends;
      ends.driver_gate = from.gate;
      ends.driver_terminal = from.terminal;
      ends.load_gate = to.gate;
      ends.load_terminal = to.terminal;
      return ends;
    };
    if (anchors.size() == 1) {
      const RouteAnchor& anchor = anchors[0];
      std::vector<SchematicPoint> path;
      AppendPoint(path, anchor.point);
      AppendPoint(path, SchematicPoint{
                            anchor.point.x +
                                (anchor.faces_left
                                     ? -kSchematicStubLength
                                     : kSchematicStubLength),
                            anchor.point.y});
      route.paths.push_back(std::move(path));
      route.path_ends.push_back(make_ends(anchor, anchor));
      layout.nets.push_back(std::move(route));
      continue;
    }
    // The source is the driver: a gate output endpoint first,
    // else an input-side terminal (a parent input drives the
    // net), else the first anchor. Picking a gate input port
    // as the source would route the net backwards.
    std::size_t source = anchors.size();
    for (std::size_t i = 0; i < anchors.size(); ++i) {
      if (anchors[i].is_output && !anchors[i].is_terminal) {
        source = i;
        break;
      }
    }
    if (source == anchors.size()) {
      for (std::size_t i = 0; i < anchors.size(); ++i) {
        if (anchors[i].is_terminal && anchors[i].terminal_input) {
          source = i;
          break;
        }
      }
    }
    if (source == anchors.size()) source = 0;
    const RouteAnchor& start = anchors[source];
    for (std::size_t i = 0; i < anchors.size(); ++i) {
      if (i == source) continue;
      const RouteAnchor& load = anchors[i];
      std::vector<SchematicPoint> path;
      AppendPoint(path, start.point);
      if (load.column == start.column + 1) {
        // Adjacent columns: one trunk in the gap between
        // them, so no segment crosses a gate.
        const double trunk =
            (column_right(start.column) + column_left(load.column)) / 2;
        AppendPoint(path, SchematicPoint{trunk, start.point.y});
        AppendPoint(path, SchematicPoint{trunk, load.point.y});
        AppendPoint(path, load.point);
      } else {
        // Skip-level or feedback: out to the gap right of
        // the source column, across a between-rows channel,
        // then down the gap left of the load column. The
        // channel line crosses intermediate columns between
        // rows, where no gate sits.
        const double x0 =
            column_right(start.column) + kSchematicColumnGap / 2;
        const double x1 =
            column_left(load.column) - kSchematicColumnGap / 2;
        const double channel = ChannelY(start.row);
        AppendPoint(path, SchematicPoint{x0, start.point.y});
        AppendPoint(path, SchematicPoint{x0, channel});
        AppendPoint(path, SchematicPoint{x1, channel});
        AppendPoint(path, SchematicPoint{x1, load.point.y});
        AppendPoint(path, load.point);
      }
      route.paths.push_back(std::move(path));
      route.path_ends.push_back(make_ends(start, load));
    }
    // Branch dots: a point where three or more wire ends
    // meet. Count rays per candidate point over the deduplicated
    // segments: a through segment counts two, an endpoint one.
    // The source and the load points themselves are pins, not
    // branch points, so they never get a dot.
    std::set<std::pair<SchematicPoint, SchematicPoint>> segments;
    auto ordered = [](SchematicPoint a, SchematicPoint b) {
      if (a.x < b.x || (a.x == b.x && a.y < b.y)) {
        return std::pair<SchematicPoint, SchematicPoint>(a, b);
      }
      return std::pair<SchematicPoint, SchematicPoint>(b, a);
    };
    for (const auto& path : route.paths) {
      for (std::size_t i = 1; i < path.size(); ++i) {
        segments.insert(ordered(path[i - 1], path[i]));
      }
    }
    std::set<std::pair<double, double>> candidates;
    for (const auto& segment : segments) {
      candidates.insert({segment.first.x, segment.first.y});
      candidates.insert({segment.second.x, segment.second.y});
    }
    for (const auto& vertical : segments) {
      if (vertical.first.x != vertical.second.x) continue;
      for (const auto& horizontal : segments) {
        if (horizontal.first.y != horizontal.second.y) continue;
        const double x = vertical.first.x;
        const double y = horizontal.first.y;
        const double hx0 =
            std::min(horizontal.first.x, horizontal.second.x);
        const double hx1 =
            std::max(horizontal.first.x, horizontal.second.x);
        const double vy0 =
            std::min(vertical.first.y, vertical.second.y);
        const double vy1 =
            std::max(vertical.first.y, vertical.second.y);
        if (x > hx0 && x < hx1 && y > vy0 && y < vy1) {
          candidates.insert({x, y});
        }
      }
    }
    if (route.paths.size() > 1) {
      for (const auto& candidate : candidates) {
        const SchematicPoint point{candidate.first, candidate.second};
        if (SamePoint(point, start.point)) continue;
        bool is_load = false;
        for (std::size_t i = 0; i < anchors.size(); ++i) {
          if (i != source && SamePoint(point, anchors[i].point)) {
            is_load = true;
          }
        }
        if (is_load) continue;
        int rays = 0;
        for (const auto& segment : segments) {
          const SchematicPoint& a = segment.first;
          const SchematicPoint& b = segment.second;
          if (SamePoint(point, a) || SamePoint(point, b)) {
            ++rays;
          } else if (a.x == b.x && point.x == a.x &&
                     point.y > std::min(a.y, b.y) &&
                     point.y < std::max(a.y, b.y)) {
            rays += 2;
          } else if (a.y == b.y && point.y == a.y &&
                     point.x > std::min(a.x, b.x) &&
                     point.x < std::max(a.x, b.x)) {
            rays += 2;
          }
        }
        if (rays >= 3) route.branch_dots.push_back(point);
      }
    }
    std::sort(route.branch_dots.begin(), route.branch_dots.end(),
              [](const SchematicPoint& a, const SchematicPoint& b) {
                if (a.x != b.x) return a.x < b.x;
                return a.y < b.y;
              });
    layout.nets.push_back(std::move(route));
  }
  return layout;
}

}  // namespace eos::ui
