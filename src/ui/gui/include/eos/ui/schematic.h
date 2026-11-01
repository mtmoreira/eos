// schematic: the GUI's gate-level schematic model (L6 interface,
// Qt-free).
//
// The model the schematic widget reads: one leaf module's parent
// ports, primitive gates, and nets. Whether the center shows
// the schematic or the block diagram is SessionView's leaf test
// (DiagramLevelIsLeaf), not this model's: the model only holds
// one module's schematic. The model is built only from a
// synth_schematic payload, never from mock data, and parsing is
// all-or-nothing like HierarchyModel and DiagramModel: a
// malformed payload leaves the previous schematic untouched and
// reports false.
#ifndef EOS_UI_SCHEMATIC_H_
#define EOS_UI_SCHEMATIC_H_

#include <string>
#include <vector>

namespace eos::ui {

// One port of a schematic gate or of its parent module, with
// its direction: "in", "out", or "inout". Gate ports use "in"
// and "out" only, because primitive cells have input and output
// ports only; a parent port flagged both input and output, or
// neither, reads as "inout", exactly as in DiagramModel.
struct SchematicPort {
  std::string name;
  std::string direction;
};

// One gate of a leaf module: a primitive cell, with its type
// (such as $and or $add) and its ports sorted by name.
struct SchematicGate {
  std::string instance;
  std::string type;
  std::vector<SchematicPort> ports;
};

// One endpoint of a schematic net on a gate: the instance and
// the port it touches.
struct SchematicNetEndpoint {
  std::string gate;
  std::string port;
};

// One net of a leaf module: a wire, its endpoints on gates, and
// the parent ports it touches. Structural only: no delay, slew,
// capacitance, or slack is carried, because no Liberty timing
// exists at this stage.
struct SchematicNet {
  std::string name;
  std::vector<SchematicNetEndpoint> endpoints;
  std::vector<std::string> terminals;
};

class SchematicModel {
 public:
  // Parses a synth_schematic payload: a "module=<module>" line,
  // then "parent port=<name> direction=<dir>" lines, "gate
  // instance=<name> type=<type> ports=<p:dir,...>" lines, and
  // "net name=<wire> gates=<inst:port,...>
  // terminals=<port,...>" lines. Returns false (schematic
  // unchanged) on any malformed line, unknown key, missing
  // module line, duplicate parent port, gate, or net, duplicate
  // port within a gate, endpoint on an unknown gate or port,
  // terminal on an unknown parent port, an unknown direction, a
  // net with neither endpoints nor terminals, or an empty name
  // or type. A successful load replaces the previous schematic.
  bool LoadPayload(const std::string& payload);
  void Clear();

  bool Empty() const { return module_.empty(); }
  const std::string& Module() const { return module_; }
  const std::vector<SchematicPort>& ParentPorts() const {
    return parent_ports_;
  }
  const std::vector<SchematicGate>& Gates() const { return gates_; }
  const std::vector<SchematicNet>& Nets() const { return nets_; }
  const SchematicGate* FindGate(const std::string& instance) const;

 private:
  std::string module_;
  std::vector<SchematicPort> parent_ports_;
  std::vector<SchematicGate> gates_;
  std::vector<SchematicNet> nets_;
};

}  // namespace eos::ui

#endif  // EOS_UI_SCHEMATIC_H_
