// schematic: the GUI's gate-level schematic model (L6 interface,
// Qt-free).
#include "eos/ui/schematic.h"

#include <cctype>
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

}  // namespace

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
  return GateSymbol::kGeneric;
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
      if (fields.size() != 3) return false;
      SchematicNet net;
      std::string gates_value;
      std::string terminals_value;
      if (!FieldIs(fields, "name", net.name) ||
          !FieldIs(fields, "gates", gates_value) ||
          !FieldIs(fields, "terminals", terminals_value)) {
        return false;
      }
      if (net.name.empty()) return false;
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

}  // namespace eos::ui
