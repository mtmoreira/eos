// resynthesis implementation: cone extraction, genlib generation
// from the Liberty, the gated splice, and the full-design
// equivalence proof, over the ODB facade and the ABC facade. See
// timing_eco.h for the contract, the boundary conditions and the
// v1 scope; this file is the mechanism those words describe.
#include "eos/eco/timing_eco.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

#include "eos/synth/abc.h"

namespace eos::eco {
namespace {

using foundation::ErrorCode;

Status Invalid(const std::string& message) {
  return Status::Error(ErrorCode::kInvalidArgument, message);
}
Status Unsupported(const std::string& message) {
  return Status::Error(ErrorCode::kUnsupported, message);
}
Status NotFound(const std::string& what, const std::string& name) {
  return Status::Error(ErrorCode::kNotFound, what + " not found: " + name);
}

bool IsPlainIdentifier(const std::string& name) {
  if (name.empty()) {
    return false;
  }
  const auto first_ok = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  };
  const auto rest_ok = [&first_ok](char c) {
    return first_ok(c) || (c >= '0' && c <= '9') || c == '$';
  };
  if (!first_ok(name[0])) {
    return false;
  }
  for (std::size_t i = 1; i < name.size(); ++i) {
    if (!rest_ok(name[i])) {
      return false;
    }
  }
  return true;
}

std::string ReadTextFile(const std::string& path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

Status WriteTextFile(const std::string& path, const std::string& text) {
  std::ofstream out(path);
  if (!out) {
    return Status::Error(ErrorCode::kIoError, "cannot write file: " + path);
  }
  out << text;
  out.close();
  if (!out) {
    return Status::Error(ErrorCode::kIoError, "cannot write file: " + path);
  }
  return Status::Ok();
}

std::string Trim(const std::string& text) {
  const std::size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  const std::size_t last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

// ---------------------------------------------------------------------------
// The Liberty subset the genlib needs: per cell, its area, whether
// it is sequential (an ff or latch block), and per pin the
// direction, capacitance, combinational function, and scalar delay
// (the maximum of the pin's cell_rise and cell_fall scalar values,
// with table arcs flagged so the caller can refuse them).

struct LibPin {
  bool is_input = false;
  bool is_output = false;
  double capacitance = 0.0;
  std::string function;
  double delay = -1.0;
  bool delay_is_table = false;
};

struct LibCell {
  std::string name;
  double area = 0.0;
  bool sequential = false;
  std::map<std::string, LibPin> pins;
};

// Returns the index just past the closing brace that matches the
// opening brace at open, or std::string::npos.
std::size_t MatchBrace(const std::string& text, std::size_t open) {
  int depth = 0;
  for (std::size_t i = open; i < text.size(); ++i) {
    if (text[i] == '{') {
      ++depth;
    } else if (text[i] == '}') {
      if (--depth == 0) {
        return i + 1;
      }
    }
  }
  return std::string::npos;
}

// Finds the blocks opened by headers of the form keyword ( name ) {
// inside text, calling fn(name, block_text_without_outer_braces).
template <typename Fn>
void ForEachBlock(const std::string& text, const std::string& keyword,
                  Fn&& fn) {
  std::size_t pos = 0;
  while (true) {
    const std::size_t at = text.find(keyword, pos);
    if (at == std::string::npos) {
      return;
    }
    std::size_t i = at + keyword.size();
    while (i < text.size() &&
           std::isspace(static_cast<unsigned char>(text[i]))) {
      ++i;
    }
    if (i >= text.size() || text[i] != '(') {
      pos = at + keyword.size();
      continue;
    }
    const std::size_t close_paren = text.find(')', i);
    if (close_paren == std::string::npos) {
      return;
    }
    std::string name = Trim(text.substr(i + 1, close_paren - i - 1));
    if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
      name = name.substr(1, name.size() - 2);
    }
    const std::size_t open = text.find('{', close_paren);
    if (open == std::string::npos) {
      return;
    }
    const std::size_t end = MatchBrace(text, open);
    if (end == std::string::npos) {
      return;
    }
    fn(name, text.substr(open + 1, end - open - 2));
    pos = end;
  }
}

bool FindScalar(const std::string& text, const std::string& key,
                double* out) {
  const std::size_t at = text.find(key);
  if (at == std::string::npos) {
    return false;
  }
  const std::size_t colon = text.find(':', at);
  if (colon == std::string::npos) {
    return false;
  }
  const char* begin = text.c_str() + colon + 1;
  char* end = nullptr;
  const double value = std::strtod(begin, &end);
  if (end == begin) {
    return false;
  }
  *out = value;
  return true;
}

std::string FindQuoted(const std::string& text, const std::string& key) {
  const std::size_t at = text.find(key);
  if (at == std::string::npos) {
    return "";
  }
  const std::size_t open = text.find('"', at);
  if (open == std::string::npos) {
    return "";
  }
  const std::size_t close = text.find('"', open + 1);
  if (close == std::string::npos) {
    return "";
  }
  return text.substr(open + 1, close - open - 1);
}

// One scalar delay value out of a pin's timing blocks: the maximum
// cell_rise / cell_fall scalar; table arcs set delay_is_table.
void ParsePinTiming(const std::string& pin_block, LibPin* pin) {
  for (const char* arc : {"cell_rise", "cell_fall"}) {
    std::size_t pos = 0;
    while (true) {
      const std::size_t at = pin_block.find(arc, pos);
      if (at == std::string::npos) {
        break;
      }
      const std::size_t open_paren = pin_block.find('(', at);
      const std::size_t close_paren = pin_block.find(')', at);
      if (open_paren == std::string::npos ||
          close_paren == std::string::npos) {
        break;
      }
      const std::string model =
          Trim(pin_block.substr(open_paren + 1, close_paren - open_paren - 1));
      if (model != "scalar") {
        pin->delay_is_table = true;
        pos = close_paren;
        continue;
      }
      const std::size_t values = pin_block.find("values", close_paren);
      if (values == std::string::npos) {
        break;
      }
      const std::size_t quote = pin_block.find('"', values);
      const std::size_t quote_end =
          quote == std::string::npos
              ? std::string::npos
              : pin_block.find('"', quote + 1);
      if (quote_end == std::string::npos) {
        break;
      }
      const double value = std::strtod(
          pin_block.substr(quote + 1, quote_end - quote - 1).c_str(),
          nullptr);
      pin->delay = std::max(pin->delay, value);
      pos = quote_end;
    }
  }
}

Result<std::map<std::string, LibCell>> ParseLiberty(
    const std::string& path) {
  const std::string text = ReadTextFile(path);
  if (text.empty()) {
    return Status::Error(ErrorCode::kIoError,
                         "Liberty file is empty or unreadable: " + path);
  }
  std::map<std::string, LibCell> cells;
  ForEachBlock(text, "cell", [&](const std::string& name,
                                 const std::string& block) {
    LibCell cell;
    cell.name = name;
    FindScalar(block, "area", &cell.area);
    cell.sequential = block.find("ff (") != std::string::npos ||
                      block.find("ff(") != std::string::npos ||
                      block.find("latch (") != std::string::npos ||
                      block.find("latch(") != std::string::npos;
    ForEachBlock(block, "pin", [&](const std::string& pin_name,
                                   const std::string& pin_block) {
      LibPin pin;
      const std::string direction = FindQuoted(pin_block, "direction");
      std::string dir_word;
      {
        const std::size_t at = pin_block.find("direction");
        if (at != std::string::npos) {
          const std::size_t colon = pin_block.find(':', at);
          const std::size_t semi = pin_block.find(';', at);
          if (colon != std::string::npos && semi != std::string::npos) {
            dir_word = Trim(pin_block.substr(colon + 1, semi - colon - 1));
          }
        }
      }
      pin.is_input = dir_word == "input" || dir_word == "inout";
      pin.is_output = dir_word == "output" || dir_word == "inout";
      FindScalar(pin_block, "capacitance", &pin.capacitance);
      pin.function = FindQuoted(pin_block, "function");
      ParsePinTiming(pin_block, &pin);
      cell.pins[pin_name] = pin;
      (void)direction;
    });
    cells[name] = cell;
  });
  if (cells.empty()) {
    return Status::Error(ErrorCode::kIoError,
                         "no cells found in Liberty file: " + path);
  }
  return cells;
}

// ---------------------------------------------------------------------------
// Genlib generation. A cell qualifies when it is combinational,
// has exactly one output pin, that pin carries a function over the
// cell's own input pins, and its delay is scalar. The function is
// rewritten from Liberty syntax (&, |, juxtaposition) to genlib
// syntax (*, +).

std::optional<std::string> LibertyFunctionToGenlib(
    const std::string& function, const LibCell& cell) {
  std::vector<std::string> tokens;
  for (std::size_t i = 0; i < function.size();) {
    const char c = function[i];
    if (std::isspace(static_cast<unsigned char>(c))) {
      ++i;
      continue;
    }
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
      std::size_t j = i;
      while (j < function.size() &&
             (std::isalnum(static_cast<unsigned char>(function[j])) ||
              function[j] == '_')) {
        ++j;
      }
      tokens.push_back(function.substr(i, j - i));
      i = j;
      continue;
    }
    if (c == '&' || c == '|' || c == '!' || c == '~' || c == '^' ||
        c == '(' || c == ')' || c == '*') {
      tokens.push_back(std::string(1, c == '~' ? '!' : c == '&' ? '*' : c == '|' ? '+' : c));
      ++i;
      continue;
    }
    return std::nullopt;  // quotes, apostrophes, brackets: out of scope
  }
  const auto is_operand_end = [](const std::string& token) {
    return token == ")" ||
           (!token.empty() &&
            (std::isalnum(static_cast<unsigned char>(token[0])) ||
             token[0] == '_'));
  };
  const auto is_operand_start = [](const std::string& token) {
    return token == "(" || token == "!" ||
           (!token.empty() &&
            (std::isalnum(static_cast<unsigned char>(token[0])) ||
             token[0] == '_'));
  };
  std::string out;
  std::string previous;
  for (const std::string& token : tokens) {
    if (!previous.empty() && is_operand_end(previous) &&
        is_operand_start(token)) {
      out += "*";  // Liberty juxtaposition is AND
    }
    if (is_operand_end(token) || token == "(" || token == ")") {
      // A bare identifier must be one of the cell's input pins, or
      // a constant.
      if (is_operand_end(token) && token != ")") {
        if (token != "0" && token != "1" &&
            cell.pins.find(token) == cell.pins.end()) {
          return std::nullopt;
        }
        const auto pin = cell.pins.find(token);
        if (pin != cell.pins.end() && !pin->second.is_input) {
          return std::nullopt;  // a state variable, not an input
        }
      }
    }
    out += token;
    previous = token;
  }
  if (out.empty()) {
    return std::nullopt;
  }
  return out;
}

// Writes the genlib for every qualifying cell; returns the set of
// cell names emitted.
Result<std::set<std::string>> WriteGenlib(
    const std::map<std::string, LibCell>& cells, const std::string& path) {
  std::set<std::string> emitted;
  std::stringstream text;
  for (const auto& entry : cells) {
    const LibCell& cell = entry.second;
    if (cell.sequential) {
      continue;
    }
    const LibPin* output = nullptr;
    std::string output_name;
    std::size_t outputs = 0;
    for (const auto& pin_entry : cell.pins) {
      if (pin_entry.second.is_output && !pin_entry.second.is_input) {
        ++outputs;
        output = &pin_entry.second;
        output_name = pin_entry.first;
      }
    }
    if (outputs != 1 || output == nullptr || output->function.empty() ||
        output->delay_is_table || output->delay < 0.0) {
      continue;
    }
    const auto function = LibertyFunctionToGenlib(output->function, cell);
    if (!function.has_value()) {
      continue;
    }
    const double area = cell.area > 0.0 ? cell.area : 1.0;
    std::ostringstream line;
    line << "GATE " << cell.name << " " << area << " " << output_name << "="
         << *function << ";\n";
    line << "PIN * INV 1 999 " << output->delay << " 0.0 " << output->delay
         << " 0.0;\n";
    text << line.str();
    emitted.insert(cell.name);
  }
  if (emitted.empty()) {
    return Unsupported(
        "no Liberty cell qualifies for genlib mapping (combinational, "
        "one output pin, function over its inputs, scalar delay)");
  }
  const Status written = WriteTextFile(path, text.str());
  if (!written.ok()) {
    return written;
  }
  return emitted;
}

// ---------------------------------------------------------------------------
// A read-only snapshot of the design through the ODB facade, with
// the lookups extraction and the proof models need.

struct DesignView {
  std::vector<db::OdbInstanceInfo> instances;
  std::map<std::string, const db::OdbInstanceInfo*> instance_by_name;
  std::vector<std::string> nets;
  std::map<std::string, std::vector<db::OdbNetConnection>> connections;
  // (instance, pin) -> net, for instance pins only.
  std::map<std::pair<std::string, std::string>, std::string> pin_net;
  std::map<std::string, std::vector<db::OdbMasterPin>> master_pins;
};

DesignView BuildView(db::OdbDatabase& database,
                     const std::set<std::string>& masters) {
  DesignView view;
  view.instances = database.Instances();
  for (const db::OdbInstanceInfo& info : view.instances) {
    view.instance_by_name[info.name] = &info;
  }
  for (const std::string& master : masters) {
    view.master_pins[master] = database.MasterPins(master);
  }
  view.nets = database.NetNames();
  for (const std::string& net : view.nets) {
    std::vector<db::OdbNetConnection> conns = database.NetConnections(net);
    for (const db::OdbNetConnection& conn : conns) {
      if (!conn.is_port) {
        view.pin_net[{conn.instance, conn.pin}] = net;
      }
    }
    view.connections[net] = std::move(conns);
  }
  return view;
}

// Only the connections the master declares as signal pins: the
// facade's net view also lists supply connections, which are not
// logic and never take part in a cone or a proof model.
std::vector<db::OdbNetConnection> SignalConnections(
    const DesignView& view, const std::string& net) {
  std::vector<db::OdbNetConnection> out;
  const auto it = view.connections.find(net);
  if (it == view.connections.end()) {
    return out;
  }
  for (const db::OdbNetConnection& conn : it->second) {
    if (conn.is_port) {
      out.push_back(conn);
      continue;
    }
    const auto inst = view.instance_by_name.find(conn.instance);
    if (inst == view.instance_by_name.end()) {
      continue;
    }
    const auto pins = view.master_pins.find(inst->second->master);
    if (pins == view.master_pins.end()) {
      out.push_back(conn);
      continue;
    }
    bool signal = false;
    for (const db::OdbMasterPin& pin : pins->second) {
      if (pin.name == conn.pin) {
        signal = true;
        break;
      }
    }
    if (signal) {
      out.push_back(conn);
    }
  }
  return out;
}

struct Cone {
  std::vector<std::string> instances;      // sorted
  std::vector<std::string> input_nets;     // sorted boundary nets
  std::vector<std::string> output_nets;    // sorted, includes target
  std::vector<std::string> internal_nets;  // sorted
  std::string target_net;
};

Result<Cone> ExtractCone(const DesignView& view,
                         const ResynthesisRequest& request,
                         const std::set<std::string>& mappable,
                         const std::set<std::string>& sequential) {
  if (!view.connections.count(request.target_net)) {
    return NotFound("net", request.target_net);
  }
  const std::vector<db::OdbNetConnection> target_conns =
      SignalConnections(view, request.target_net);
  const db::OdbNetConnection* driver = nullptr;
  for (const db::OdbNetConnection& conn : target_conns) {
    if (conn.is_driver) {
      if (driver != nullptr) {
        return Invalid("net " + request.target_net +
                       " has more than one driver");
      }
      driver = &conn;
    }
  }
  if (driver == nullptr) {
    return Invalid("net " + request.target_net + " has no driver");
  }
  if (driver->is_port) {
    return Unsupported("net " + request.target_net +
                       " is driven by a top-level port; there is no "
                       "cone behind it to resynthesize");
  }
  const auto driver_inst = view.instance_by_name.find(driver->instance);
  if (driver_inst == view.instance_by_name.end()) {
    return NotFound("instance", driver->instance);
  }
  if (sequential.count(driver_inst->second->master)) {
    return Invalid("net " + request.target_net +
                   " is driven by sequential instance " + driver->instance +
                   " (" + driver_inst->second->master +
                   "); a register output has no combinational cone");
  }

  std::set<std::string> in_cone;
  std::set<std::string> boundary;
  std::vector<std::string> worklist = {driver->instance};
  in_cone.insert(driver->instance);
  while (!worklist.empty()) {
    const std::string name = worklist.back();
    worklist.pop_back();
    const db::OdbInstanceInfo& info = *view.instance_by_name.at(name);
    if (!mappable.count(info.master)) {
      return Unsupported(
          "cone instance " + name + " has master " + info.master +
          ", which the Liberty does not offer as a mappable "
          "combinational cell (missing, sequential in the library, "
          "more than one output pin, a non-scalar delay, or a "
          "function over more than its input pins)");
    }
    const std::vector<db::OdbMasterPin>& pins =
        view.master_pins.at(info.master);
    std::size_t outputs = 0;
    for (const db::OdbMasterPin& pin : pins) {
      if (pin.is_output && !pin.is_input) {
        ++outputs;
      }
      if (!pin.is_input || pin.is_output) {
        continue;
      }
      const auto net = view.pin_net.find({name, pin.name});
      if (net == view.pin_net.end()) {
        return Unsupported("cone instance " + name + " pin " + pin.name +
                           " is unconnected; a floating cone input has "
                           "no boundary condition");
      }
      const std::vector<db::OdbNetConnection> conns =
          SignalConnections(view, net->second);
      const db::OdbNetConnection* net_driver = nullptr;
      for (const db::OdbNetConnection& conn : conns) {
        if (conn.is_driver) {
          net_driver = &conn;
          break;
        }
      }
      if (net_driver == nullptr) {
        return Unsupported("net " + net->second +
                           " has no driver; its value is not a cone "
                           "boundary condition");
      }
      if (net_driver->is_port) {
        boundary.insert(net->second);
        continue;
      }
      const db::OdbInstanceInfo& upstream =
          *view.instance_by_name.at(net_driver->instance);
      if (sequential.count(upstream.master)) {
        boundary.insert(net->second);
        continue;
      }
      if (in_cone.insert(upstream.name).second) {
        worklist.push_back(upstream.name);
      }
    }
    if (outputs != 1) {
      return Unsupported("cone instance " + name + " master " + info.master +
                         " does not have exactly one output signal pin");
    }
  }

  Cone cone;
  cone.target_net = request.target_net;
  cone.instances.assign(in_cone.begin(), in_cone.end());
  cone.input_nets.assign(boundary.begin(), boundary.end());
  std::set<std::string> outputs;
  std::set<std::string> internal;
  outputs.insert(request.target_net);
  for (const std::string& name : in_cone) {
    const db::OdbInstanceInfo& info = *view.instance_by_name.at(name);
    for (const db::OdbMasterPin& pin : view.master_pins.at(info.master)) {
      if (!pin.is_output || pin.is_input) {
        continue;
      }
      const auto net = view.pin_net.find({name, pin.name});
      if (net == view.pin_net.end()) {
        continue;  // an unconnected output drives nothing
      }
      bool leaves = false;
      for (const db::OdbNetConnection& conn :
           SignalConnections(view, net->second)) {
        if (!conn.is_driver &&
            (conn.is_port || !in_cone.count(conn.instance))) {
          leaves = true;
          break;
        }
      }
      if (leaves) {
        outputs.insert(net->second);
      } else {
        internal.insert(net->second);
      }
    }
  }
  internal.erase(request.target_net);
  cone.output_nets.assign(outputs.begin(), outputs.end());
  cone.internal_nets.assign(internal.begin(), internal.end());
  return cone;
}

// One instance line in the netlist form both the cone writer and
// the proof-model writer emit, and the ODB Verilog writer uses:
// "  MASTER inst (.PIN(net), ...);" over the instance's connected
// signal pins, in master pin order.
std::string InstanceLine(const DesignView& view,
                         const db::OdbInstanceInfo& info) {
  std::string line = "  " + info.master + " " + info.name + " (";
  bool first = true;
  for (const db::OdbMasterPin& pin : view.master_pins.at(info.master)) {
    const auto net = view.pin_net.find({info.name, pin.name});
    if (net == view.pin_net.end()) {
      continue;
    }
    if (!first) {
      line += ", ";
    }
    first = false;
    line += "." + pin.name + "(" + net->second + ")";
  }
  line += ");\n";
  return line;
}

// The cone as a standalone module: ports are the boundary input
// nets and the output nets, by name.
Result<std::string> ConeVerilog(const DesignView& view, const Cone& cone) {
  std::vector<std::string> ports = cone.input_nets;
  ports.insert(ports.end(), cone.output_nets.begin(), cone.output_nets.end());
  for (const std::string& name : ports) {
    if (!IsPlainIdentifier(name)) {
      return Unsupported("cone net name " + name +
                         " is not a plain Verilog identifier; the "
                         "resynthesis netlists name nets directly");
    }
  }
  std::string text = "module resynth_cone (";
  for (std::size_t i = 0; i < ports.size(); ++i) {
    if (i) text += ", ";
    text += ports[i];
  }
  text += ");\n";
  const auto decl = [&text](const char* kind,
                            const std::vector<std::string>& names) {
    if (names.empty()) {
      return;
    }
    text += std::string("  ") + kind + " ";
    for (std::size_t i = 0; i < names.size(); ++i) {
      if (i) text += ", ";
      text += names[i];
    }
    text += ";\n";
  };
  decl("input", cone.input_nets);
  decl("output", cone.output_nets);
  decl("wire", cone.internal_nets);
  const std::set<std::string> in_cone(cone.instances.begin(),
                                      cone.instances.end());
  for (const db::OdbInstanceInfo& info : view.instances) {
    if (in_cone.count(info.name)) {
      text += InstanceLine(view, info);
    }
  }
  text += "endmodule\n";
  return text;
}

// The combinational model of the full design for the equivalence
// proof: sequential instances are cut at their boundaries (their
// output nets become model inputs, their input nets model
// outputs), everything else is the design verbatim. The registers
// are untouched by the splice, so equivalence of these models is
// function preservation of the design. Both models are built by
// this one function from the design before and after, so any
// naming choice is identical on both sides.
Result<std::string> CecModel(const DesignView& view,
                             const std::set<std::string>& sequential) {
  std::set<std::string> inputs;
  std::set<std::string> outputs;
  std::set<std::string> wires;
  for (const std::string& net : view.nets) {
    if (!IsPlainIdentifier(net)) {
      return Unsupported("net name " + net +
                         " is not a plain Verilog identifier; the "
                         "equivalence model names nets directly, so "
                         "the proof cannot run under these names");
    }
    bool driven_by_boundary = false;
    bool drives_boundary = false;
    bool driven_inside = false;
    for (const db::OdbNetConnection& conn : SignalConnections(view, net)) {
      const bool boundary =
          conn.is_port ||
          sequential.count(
              view.instance_by_name.at(conn.instance)->master);
      if (conn.is_driver) {
        if (boundary) {
          driven_by_boundary = true;
        } else {
          driven_inside = true;
        }
      } else if (boundary) {
        drives_boundary = true;
      }
    }
    if (driven_by_boundary) {
      inputs.insert(net);
    } else if (drives_boundary) {
      outputs.insert(net);
    } else if (driven_inside) {
      wires.insert(net);
    }
  }
  std::vector<std::string> ports(inputs.begin(), inputs.end());
  ports.insert(ports.end(), outputs.begin(), outputs.end());
  std::string text = "module cec_model (";
  for (std::size_t i = 0; i < ports.size(); ++i) {
    if (i) text += ", ";
    text += ports[i];
  }
  text += ");\n";
  const auto decl = [&text](const char* kind,
                            const std::set<std::string>& names) {
    if (names.empty()) {
      return;
    }
    text += std::string("  ") + kind + " ";
    bool first = true;
    for (const std::string& name : names) {
      if (!first) text += ", ";
      first = false;
      text += name;
    }
    text += ";\n";
  };
  decl("input", inputs);
  decl("output", outputs);
  decl("wire", wires);
  for (const db::OdbInstanceInfo& info : view.instances) {
    if (sequential.count(info.master)) {
      continue;
    }
    if (!IsPlainIdentifier(info.name)) {
      return Unsupported("instance name " + info.name +
                         " is not a plain Verilog identifier; the "
                         "equivalence model cannot name it");
    }
    // Skip instances with no signal connection at all (filler,
    // tap and decap cells), exactly as the ODB Verilog writer
    // does: they carry no logic.
    bool connected = false;
    for (const db::OdbMasterPin& pin : view.master_pins.at(info.master)) {
      if (view.pin_net.count({info.name, pin.name})) {
        connected = true;
        break;
      }
    }
    if (connected) {
      text += InstanceLine(view, info);
    }
  }
  text += "endmodule\n";
  return text;
}

// ---------------------------------------------------------------------------
// The replacement cone netlist, in the restricted form ABC's
// write_verilog produces: declarations, gate instantiations with
// named pin connections, and assigns. Anything outside that form
// is refused before any edit is made.

struct ReplacementInstance {
  std::string cell;
  std::string name;
  std::map<std::string, std::string> pin_net;  // pin -> net
};

struct ReplacementNetlist {
  std::vector<ReplacementInstance> instances;
  std::map<std::string, std::string> assigns;  // lhs -> rhs (identifiers)
  std::set<std::string> inputs;
  std::set<std::string> outputs;
  std::set<std::string> nets;  // every net named anywhere
};

Result<ReplacementNetlist> ParseReplacement(
    const std::string& path, const std::set<std::string>& masters) {
  const std::string raw = ReadTextFile(path);
  if (raw.empty()) {
    return Status::Error(ErrorCode::kIoError,
                         "replacement netlist is empty or unreadable: " +
                             path);
  }
  // Strip // comments, then split into semicolon statements.
  std::string text;
  {
    std::stringstream in(raw);
    std::string line;
    while (std::getline(in, line)) {
      const std::size_t comment = line.find("//");
      if (comment != std::string::npos) {
        line = line.substr(0, comment);
      }
      text += line + "\n";
    }
  }
  ReplacementNetlist netlist;
  std::stringstream stream(text);
  std::string stmt;
  while (std::getline(stream, stmt, ';')) {
    stmt = Trim(stmt);
    if (stmt.empty() || stmt == "endmodule") {
      continue;
    }
    if (stmt.rfind("module", 0) == 0) {
      continue;  // the header; ports come from the declarations
    }
    const auto word_list = [&stmt]() {
      std::vector<std::string> words;
      std::stringstream words_stream(stmt);
      std::string word;
      while (words_stream >> word) {
        words.push_back(word);
      }
      return words;
    };
    if (stmt.rfind("input", 0) == 0 || stmt.rfind("output", 0) == 0 ||
        stmt.rfind("wire", 0) == 0) {
      if (stmt.find('[') != std::string::npos) {
        return Unsupported(
            "replacement netlist declares a bus; the splice handles "
            "scalar nets only");
      }
      const std::string kind = stmt.substr(0, stmt.find(' '));
      const std::string rest = stmt.substr(stmt.find(' ') + 1);
      std::stringstream names(rest);
      std::string name;
      while (std::getline(names, name, ',')) {
        name = Trim(name);
        if (!IsPlainIdentifier(name)) {
          return Unsupported("replacement netlist names " + name +
                             ", which is not a plain identifier");
        }
        netlist.nets.insert(name);
        if (kind == "input") {
          netlist.inputs.insert(name);
        } else if (kind == "output") {
          netlist.outputs.insert(name);
        }
      }
      continue;
    }
    if (stmt.rfind("assign", 0) == 0) {
      const std::size_t equals = stmt.find('=');
      if (equals == std::string::npos) {
        return Invalid("replacement assign without '=': " + stmt);
      }
      const std::string lhs = Trim(stmt.substr(6, equals - 6));
      const std::string rhs = Trim(stmt.substr(equals + 1));
      if (!IsPlainIdentifier(lhs) || !IsPlainIdentifier(rhs)) {
        return Unsupported(
            "replacement assign is not a plain wire between two nets "
            "(a constant or an inversion the mapper should have "
            "mapped to a cell): " +
            stmt);
      }
      netlist.assigns[lhs] = rhs;
      netlist.nets.insert(lhs);
      netlist.nets.insert(rhs);
      continue;
    }
    // A gate instantiation: CELL name ( .PIN(net), ... ), in the
    // form ABC's write_verilog emits (the instance name is glued
    // to the opening parenthesis: "AND2_X1 g0(.A(b), ...)").
    const std::vector<std::string> words = word_list();
    if (words.size() < 2 || !masters.count(words[0])) {
      return Unsupported(
          "replacement netlist statement is not an instance of a "
          "known master: " +
          stmt);
    }
    ReplacementInstance instance;
    instance.cell = words[0];
    {
      const std::size_t after_cell = stmt.find(words[0]) + words[0].size();
      const std::size_t open = stmt.find('(', after_cell);
      if (open == std::string::npos) {
        return Invalid("replacement instance has no pin list: " + stmt);
      }
      instance.name = Trim(stmt.substr(after_cell, open - after_cell));
      if (!IsPlainIdentifier(instance.name)) {
        return Unsupported("replacement instance name " + instance.name +
                           " is not a plain identifier");
      }
    }
    // Named connections: scan for .PIN(net) pairs.
    for (std::size_t i = 0; i < stmt.size();) {
      const std::size_t dot = stmt.find('.', i);
      if (dot == std::string::npos) {
        break;
      }
      const std::size_t open = stmt.find('(', dot);
      const std::size_t close =
          open == std::string::npos ? std::string::npos
                                    : stmt.find(')', open);
      if (close == std::string::npos) {
        return Invalid("replacement instance has a malformed pin "
                       "connection: " +
                       stmt);
      }
      const std::string pin = Trim(stmt.substr(dot + 1, open - dot - 1));
      const std::string net = Trim(stmt.substr(open + 1, close - open - 1));
      if (!IsPlainIdentifier(net)) {
        return Unsupported("replacement instance " + instance.name +
                           " connects pin " + pin + " to " + net +
                           ", which is not a plain net name");
      }
      instance.pin_net[pin] = net;
      netlist.nets.insert(net);
      i = close + 1;
    }
    if (instance.pin_net.empty()) {
      return Invalid("replacement instance " + instance.name +
                     " has no pin connections");
    }
    netlist.instances.push_back(std::move(instance));
  }
  return netlist;
}

// Restores database from the checkpoint file by replacement, the
// same discipline the ECO transaction engine uses.
Status RestoreFrom(db::OdbDatabase& database, const std::string& checkpoint) {
  db::OdbDatabase restored;
  const Status read = restored.ReadDb(checkpoint);
  if (!read.ok()) {
    return Status::Error(
        read.code(),
        "resynthesis rollback failed: the checkpoint could not be "
        "read back (" +
            read.message() +
            "); the design is in an unknown state and must be "
            "reloaded");
  }
  database = std::move(restored);
  return Status::Ok();
}

const char* VerdictWord(ProofVerdict verdict) {
  switch (verdict) {
    case ProofVerdict::kEquivalent:
      return "equivalent";
    case ProofVerdict::kNotEquivalent:
      return "NOT EQUIVALENT";
    case ProofVerdict::kUnknown:
      return "unknown (the proof did not conclude)";
  }
  return "unknown";
}

}  // namespace

Result<ResynthesisOutcome> SpliceReplacementCone(
    db::OdbDatabase& database, const ResynthesisRequest& request,
    const std::string& replacement_verilog_path) {
  if (request.target_net.empty()) {
    return Invalid("a resynthesis needs a target net");
  }
  if (request.liberty_path.empty()) {
    return Invalid("a resynthesis needs a Liberty library");
  }
  if (request.scratch_dir.empty()) {
    return Invalid("a resynthesis needs a scratch directory");
  }
  if (!database.has_top_block()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "a resynthesis needs a design with a top block");
  }
  const std::set<std::string> sequential(request.sequential_masters.begin(),
                                         request.sequential_masters.end());

  const auto cells = ParseLiberty(request.liberty_path);
  if (!cells.ok()) {
    return cells.status();
  }
  const std::string genlib_path = request.scratch_dir + "/resynth.genlib";
  const auto mappable = WriteGenlib(cells.value(), genlib_path);
  if (!mappable.ok()) {
    return mappable.status();
  }

  std::set<std::string> masters;
  for (const std::string& name : database.MasterNames()) {
    masters.insert(name);
  }
  DesignView view = BuildView(database, masters);
  const auto cone = ExtractCone(view, request, mappable.value(), sequential);
  if (!cone.ok()) {
    return cone.status();
  }

  const auto model_before = CecModel(view, sequential);
  if (!model_before.ok()) {
    return model_before.status();
  }
  const std::string model_before_path =
      request.scratch_dir + "/cec_model_before.v";
  Status status = WriteTextFile(model_before_path, model_before.value());
  if (!status.ok()) {
    return status;
  }

  const auto replacement =
      ParseReplacement(replacement_verilog_path, masters);
  if (!replacement.ok()) {
    return replacement.status();
  }
  const ReplacementNetlist& repl = replacement.value();

  // Validate the replacement against the cone contract before any
  // edit: same outputs; inputs drawn from the cone's boundary;
  // every replacement master known to the library's genlib (its
  // instances must be creatable and provable).
  if (repl.outputs !=
      std::set<std::string>(cone.value().output_nets.begin(),
                            cone.value().output_nets.end())) {
    return Invalid(
        "the replacement cone's outputs do not match the extracted "
        "cone's outputs; a splice must drive exactly the nets the "
        "cone drove");
  }
  const std::set<std::string> cone_inputs(cone.value().input_nets.begin(),
                                          cone.value().input_nets.end());
  const std::set<std::string> cone_outputs(repl.outputs);
  for (const std::string& input : repl.inputs) {
    if (!cone_inputs.count(input)) {
      return Invalid("the replacement cone reads net " + input +
                     ", which is not a boundary input of the "
                     "extracted cone");
    }
  }
  for (const ReplacementInstance& instance : repl.instances) {
    if (!mappable.value().count(instance.cell)) {
      return Unsupported("replacement instance " + instance.name +
                         " has cell " + instance.cell +
                         ", which is not a mappable library cell");
    }
  }
  // Alias resolution through assigns: res(net) follows assign
  // edges to a terminal (a gate-driven internal net, a boundary
  // input, or a cone output driven by a gate under an alias).
  std::map<std::string, int> gate_driven;  // net -> instance index
  for (std::size_t i = 0; i < repl.instances.size(); ++i) {
    for (const auto& pin : repl.instances[i].pin_net) {
      const std::vector<db::OdbMasterPin>& pins =
          view.master_pins.at(repl.instances[i].cell);
      for (const db::OdbMasterPin& master_pin : pins) {
        if (master_pin.name == pin.first && master_pin.is_output &&
            !master_pin.is_input) {
          gate_driven[pin.second] = static_cast<int>(i);
        }
      }
    }
  }
  const auto resolve = [&repl](const std::string& net) {
    std::string current = net;
    std::set<std::string> seen;
    while (true) {
      const auto alias = repl.assigns.find(current);
      if (alias == repl.assigns.end()) {
        return current;
      }
      if (!seen.insert(current).second) {
        return std::string();  // an assign cycle
      }
      current = alias->second;
    }
  };
  // Classify every replacement net and give each surviving
  // internal net a fresh design name. A cone output whose terminal
  // is a boundary input is a wire collapse, spliced by moving its
  // loads; an output whose terminal is a gate net takes that gate
  // net's name over.
  std::map<std::string, std::string> final_name;  // repl net -> design net
  std::map<std::string, std::string> wire_collapse;  // output -> boundary net
  for (const std::string& net : repl.nets) {
    const std::string terminal = resolve(net);
    if (terminal.empty()) {
      return Invalid("the replacement netlist has an assign cycle "
                     "at net " +
                     net);
    }
    if (cone_inputs.count(terminal)) {
      if (cone_outputs.count(net)) {
        wire_collapse[net] = terminal;
      }
      final_name[net] = terminal;
      continue;
    }
    if (cone_outputs.count(terminal)) {
      final_name[net] = terminal;
      continue;
    }
    if (gate_driven.count(terminal)) {
      // A fresh internal net, unless this net is itself a cone
      // output (handled above by the terminal test when aliased)
      // or the terminal already has a name.
      const auto named = final_name.find(terminal);
      if (named != final_name.end()) {
        final_name[net] = named->second;
        continue;
      }
      if (cone_outputs.count(net)) {
        final_name[net] = net;
        final_name[terminal] = net;
        continue;
      }
      std::string fresh = "resynth_n_" + terminal;
      int suffix = 0;
      while (database.HasNet(fresh) ||
             std::find_if(final_name.begin(), final_name.end(),
                          [&fresh](const auto& entry) {
                            return entry.second == fresh;
                          }) != final_name.end()) {
        fresh = "resynth_n_" + terminal + "_" + std::to_string(++suffix);
      }
      final_name[net] = fresh;
      if (net == terminal) {
        continue;
      }
      final_name[terminal] = fresh;
      continue;
    }
    return Invalid("replacement net " + net +
                   " is driven by nothing in the replacement and is "
                   "not a cone boundary net");
  }
  // Wire-collapse outputs must have instance loads only: a port
  // load cannot move nets through the facade.
  for (const auto& collapse : wire_collapse) {
    for (const db::OdbNetConnection& conn :
         SignalConnections(view, collapse.first)) {
      if (!conn.is_driver && conn.is_port) {
        return Unsupported(
            "the remap reduces cone output " + collapse.first +
            " to a wire from boundary net " + collapse.second +
            ", but the output drives a top-level port, whose "
            "connection cannot move");
      }
    }
  }

  // Checkpoint, then edit.
  const std::string checkpoint_path =
      request.scratch_dir + "/resynth_checkpoint.odb";
  status = database.WriteDb(checkpoint_path);
  if (!status.ok()) {
    return Status::Error(status.code(),
                         "resynthesis checkpoint failed: " +
                             status.message());
  }

  ResynthesisOutcome outcome;
  outcome.cone_instances = cone.value().instances;
  const auto fail_and_restore = [&](const Status& failure) -> Status {
    const Status restored = RestoreFrom(database, checkpoint_path);
    if (!restored.ok()) {
      return restored;
    }
    return failure;
  };

  // The splice itself: delete the cone's instances, delete the
  // cone-internal nets (loadless once the instances are gone),
  // move the loads of any wire-collapsed output onto its boundary
  // net and delete that net, then create the replacement's nets
  // and instances at the first cone instance's location.
  const db::OdbInstanceInfo& first_cone =
      *view.instance_by_name.at(cone.value().instances.front());
  for (const std::string& name : cone.value().instances) {
    status = database.DeleteInstance(name);
    if (!status.ok()) return fail_and_restore(status);
  }
  for (const std::string& net : cone.value().internal_nets) {
    status = database.DeleteNet(net);
    if (!status.ok()) return fail_and_restore(status);
  }
  for (const auto& collapse : wire_collapse) {
    for (const db::OdbNetConnection& conn :
         SignalConnections(view, collapse.first)) {
      if (conn.is_driver) {
        continue;
      }
      status = database.DisconnectInstancePin(conn.instance, conn.pin);
      if (!status.ok()) return fail_and_restore(status);
      status =
          database.ConnectInstancePin(conn.instance, conn.pin, collapse.second);
      if (!status.ok()) return fail_and_restore(status);
    }
    status = database.DeleteNet(collapse.first);
    if (!status.ok()) return fail_and_restore(status);
  }
  {
    std::set<std::string> created_nets;
    for (const auto& entry : final_name) {
      const std::string& design_net = entry.second;
      if (cone_inputs.count(design_net) || cone_outputs.count(design_net) ||
          created_nets.count(design_net)) {
        continue;
      }
      status = database.CreateNet(design_net);
      if (!status.ok()) return fail_and_restore(status);
      created_nets.insert(design_net);
    }
  }
  for (std::size_t i = 0; i < repl.instances.size(); ++i) {
    const ReplacementInstance& instance = repl.instances[i];
    std::string name = "resynth_" + std::to_string(i) + "_" + instance.name;
    int suffix = 0;
    while (database.HasInstance(name)) {
      name = "resynth_" + std::to_string(i) + "_" + instance.name + "_" +
             std::to_string(++suffix);
    }
    status = database.CreateInstance(name, instance.cell, first_cone.x,
                                     first_cone.y);
    if (!status.ok()) return fail_and_restore(status);
    outcome.created_instances.push_back(name);
    for (const auto& pin : instance.pin_net) {
      const std::string& design_net = final_name.at(pin.second);
      status = database.ConnectInstancePin(name, pin.first, design_net);
      if (!status.ok()) return fail_and_restore(status);
    }
  }

  // The mandatory proof: the full design's combinational model,
  // before against after, with ABC's cec.
  DesignView after_view = BuildView(database, masters);
  const auto model_after = CecModel(after_view, sequential);
  if (!model_after.ok()) {
    const Status restored = RestoreFrom(database, checkpoint_path);
    if (!restored.ok()) {
      return restored;
    }
    return model_after.status();
  }
  const std::string model_after_path =
      request.scratch_dir + "/cec_model_after.v";
  status = WriteTextFile(model_after_path, model_after.value());
  if (!status.ok()) {
    return fail_and_restore(status);
  }
  synth::AbcEngine engine;
  const auto verdict =
      engine.ProveEquivalent(model_before_path, model_after_path,
                             genlib_path);
  if (!verdict.ok()) {
    const Status restored = RestoreFrom(database, checkpoint_path);
    if (!restored.ok()) {
      return restored;
    }
    return verdict.status();
  }
  switch (verdict.value()) {
    case synth::EquivalenceVerdict::kEquivalent:
      outcome.proof = ProofVerdict::kEquivalent;
      break;
    case synth::EquivalenceVerdict::kNotEquivalent:
      outcome.proof = ProofVerdict::kNotEquivalent;
      break;
    case synth::EquivalenceVerdict::kUnknown:
      outcome.proof = ProofVerdict::kUnknown;
      break;
  }
  if (outcome.proof != ProofVerdict::kEquivalent) {
    const Status restored = RestoreFrom(database, checkpoint_path);
    if (!restored.ok()) {
      return restored;
    }
    outcome.applied = false;
    outcome.created_instances.clear();
    outcome.reason =
        "the equivalence proof is " +
        std::string(VerdictWord(outcome.proof)) +
        "; a logic change may stand only on a passed proof, so the "
        "splice was rolled back and the design is unchanged";
    return outcome;
  }
  outcome.applied = true;
  outcome.reason =
      "the remapped cone was spliced in and ABC cec proved the "
      "full design equivalent before and after";
  return outcome;
}

Result<ResynthesisOutcome> ResynthesizeCone(
    db::OdbDatabase& database, const ResynthesisRequest& request) {
  if (request.target_net.empty() || request.liberty_path.empty() ||
      request.scratch_dir.empty()) {
    return Invalid(
        "a resynthesis needs a target net, a Liberty library and a "
        "scratch directory");
  }
  if (!database.has_top_block()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "a resynthesis needs a design with a top block");
  }
  const std::set<std::string> sequential(request.sequential_masters.begin(),
                                         request.sequential_masters.end());
  const auto cells = ParseLiberty(request.liberty_path);
  if (!cells.ok()) {
    return cells.status();
  }
  const std::string genlib_path = request.scratch_dir + "/resynth.genlib";
  const auto mappable = WriteGenlib(cells.value(), genlib_path);
  if (!mappable.ok()) {
    return mappable.status();
  }
  std::set<std::string> masters;
  for (const std::string& name : database.MasterNames()) {
    masters.insert(name);
  }
  DesignView view = BuildView(database, masters);
  const auto cone = ExtractCone(view, request, mappable.value(), sequential);
  if (!cone.ok()) {
    return cone.status();
  }
  const auto cone_verilog = ConeVerilog(view, cone.value());
  if (!cone_verilog.ok()) {
    return cone_verilog.status();
  }
  const std::string cone_path = request.scratch_dir + "/resynth_cone.v";
  Status status = WriteTextFile(cone_path, cone_verilog.value());
  if (!status.ok()) {
    return status;
  }
  const std::string mapped_path = request.scratch_dir + "/resynth_mapped.v";
  {
    // The mapping engine must be destroyed before the splice:
    // the ABC frame is a singleton behind the facade, so a second
    // live engine would be inert and its proof call would fail.
    synth::AbcEngine engine;
    status = engine.MapForDelay(cone_path, genlib_path, mapped_path);
    if (!status.ok()) {
      return status;
    }
  }
  return SpliceReplacementCone(database, request, mapped_path);
}

}  // namespace eos::eco
