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

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
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

// The symbol the schematic widget draws for a gate type.
// kGeneric is the fallback rectangle for any type with no
// standard shape (adders, flip-flops, and anything else).
enum class GateSymbol {
  kAnd,
  kNand,
  kOr,
  kNor,
  kXor,
  kXnor,
  kNot,
  kBuf,
  kGeneric,
};

// Selects the symbol for a raw cell type such as $and or $not.
// The match strips a leading '$' and ignores case, so $AND and
// $and select the same symbol; $inv selects kNot like $not.
// Any unrecognized type selects kGeneric.
GateSymbol SymbolForType(const std::string& type);

// The label the schematic widget shows for a raw cell type:
// the type without its leading '$', uppercased, so $and shows
// as AND and $not as NOT. The '$' internal prefix never
// leaks into a label.
std::string DisplayType(const std::string& type);

// The label the schematic widget shows for a gate instance.
// Plain identifiers show as-is. Yosys auto-generated names of
// the form $type$file:line$counter show only the counter, as
// #counter, and auto names without a counter show nothing, so
// the widget shows the type alone: no file path, temp path, or
// source location ever appears in a label. A non-auto name
// that still contains a path shows only its basename without
// a trailing :line suffix, and a temp-file basename shows
// nothing. The model keeps the raw instance as the gate key;
// this is display only, and net endpoints still match on the
// raw name.
std::string DisplayInstance(const std::string& instance);

// One endpoint of a schematic net on a gate: the instance and
// the port it touches.
struct SchematicNetEndpoint {
  std::string gate;
  std::string port;
};

// One net of a leaf module: a wire, its endpoints on gates, and
// the parent ports it touches. Structural only: no delay, slew,
// capacitance, or slack is carried, because no Liberty timing
// exists at this stage. Width is the wire's bit width, carried
// from the engine (1 when the payload omits it), so the widget
// can label a bus with its width.
struct SchematicNet {
  std::string name;
  std::vector<SchematicNetEndpoint> endpoints;
  std::vector<std::string> terminals;
  int width = 1;
};

// The label the schematic widget shows for a net: the name
// alone for a single-bit net, and "name [width]" for a bus, so
// a multi-bit connection carries its width at a glance.
std::string NetLabel(const SchematicNet& net);

class SchematicModel {
 public:
  // Parses a synth_schematic payload: a "module=<module>" line,
  // then "parent port=<name> direction=<dir>" lines, "gate
  // instance=<name> type=<type> ports=<p:dir,...>" lines, and
  // "net name=<wire> gates=<inst:port,...>
  // terminals=<port,...> [width=<n>]" lines. The width field is
  // optional: a net line without it reads as a single-bit net.
  // Returns false (schematic unchanged) on any malformed line,
  // unknown key, missing module line, duplicate parent port,
  // gate, or net, duplicate port within a gate, endpoint on an
  // unknown gate or port, terminal on an unknown parent port,
  // an unknown direction, a net with neither endpoints nor
  // terminals, a width that is not a positive integer, or an
  // empty name or type. A successful load replaces the previous
  // schematic.
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

// Each gate's logic level: the longest path from a primary
// input or a sequential output. A gate whose inputs are all
// driven by parent input terminals, or are undriven, is level
// 0; a gate fed by another gate is one past the highest level
// of its drivers. Sequential cells (flip-flops, latches,
// memories) are level 0 themselves: their outputs are sources
// for the combinational logic they feed, whatever drives their
// data inputs. Keys are raw gate instances; every gate in the
// model has an entry.
std::map<std::string, int> SchematicLevels(const SchematicModel& model);

// The transitive fanin cone of a gate: every gate that drives
// it, directly or through other gates, not including the gate
// itself. The transitive fanout cone is the mirror: every gate
// it drives, directly or through other gates. Both are empty
// for an unknown instance.
std::set<std::string> FaninCone(const SchematicModel& model,
                                const std::string& instance);
std::set<std::string> FanoutCone(const SchematicModel& model,
                                 const std::string& instance);

// Layout geometry, shared by the model layout below and the
// widget, so both agree on positions. All in scene units: gates
// are kSchematicGateHeight tall with kSchematicGateGap between
// rows, columns start at kSchematicGateX with
// kSchematicColumnGap between them, input terminals sit at
// kSchematicInputX, and a lone anchor gets a stub of
// kSchematicStubLength.
constexpr double kSchematicGateTop = 40;
constexpr double kSchematicGateHeight = 64;
constexpr double kSchematicGateGap = 48;
constexpr double kSchematicColumnGap = 120;
constexpr double kSchematicGateX = 300;
constexpr double kSchematicMinGateWidth = 180;
constexpr double kSchematicInputX = 40;
constexpr double kSchematicTerminalSize = 16;
constexpr double kSchematicStubLength = 28;

// One point of a routed net, in scene units.
struct SchematicPoint {
  double x;
  double y;
  bool operator<(const SchematicPoint& other) const {
    if (x != other.x) return x < other.x;
    return y < other.y;
  }
  bool operator==(const SchematicPoint& other) const {
    return x == other.x && y == other.y;
  }
};

// One port anchor of a placed gate: the point on the gate's
// bounding edge where a net attaches. Input and inout ports sit
// on the left edge, output ports on the right; the widget shifts
// the drawn endpoint to the symbol curve or inversion bubble,
// but the route ends at this bounding-edge point.
struct SchematicPortAnchor {
  std::string name;
  double x;
  double y;
  bool left_side;
  bool is_output;
};

// One placed gate: its level (column) and row, its bounding
// rect, and its port anchors.
struct SchematicGatePlacement {
  std::string instance;
  int level;
  int row;
  double x;
  double y;
  double width;
  double height;
  std::vector<SchematicPortAnchor> ports;
};

// One placed parent terminal: the point on the arrowhead where
// a net attaches: the tip for an input, the base center for an
// output. Input terminals face right, output terminals face
// left; the widget draws the arrowhead outward from the point,
// so the tip is at the point for an input and the base is at
// the point for an output.
struct SchematicTerminalPlacement {
  std::string name;
  bool input_side;
  double x;
  double y;
};

// One routed path's endpoints: the driving anchor and the load
// anchor it serves. A gate endpoint names the gate instance
// and port; a terminal endpoint names the parent port. The
// single-anchor stub has the same anchor on both sides, so a
// stub's driver and load name the same gate or terminal.
struct SchematicNetPathEnds {
  std::string driver_gate;
  std::string driver_terminal;
  std::string load_gate;
  std::string load_terminal;
};

// One routed net: a polyline per load, each from the driving
// anchor to one load anchor, plus a dot at every true branch
// point, where three or more wire ends meet. All segments are
// orthogonal: horizontal and vertical only, never diagonal.
// A net with a single anchor routes as one stub path. Each
// path has one entry in path_ends, in the same order, naming
// the anchors it starts and ends at, so the widget can color
// each path by the cone it belongs to instead of guessing
// from coordinates.
struct SchematicNetRoute {
  std::string name;
  std::string label;
  std::vector<std::vector<SchematicPoint>> paths;
  std::vector<SchematicNetPathEnds> path_ends;
  std::vector<SchematicPoint> branch_dots;
};

// A level-based layout of a schematic: gates in columns by
// logic level (inputs left, outputs right), rows within a
// column ordered to keep connected gates aligned, parent
// terminals on the outer sides, and every net routed
// orthogonally. Output_x is the left edge of the output
// terminal arrowheads; input terminals sit at kSchematicInputX.
struct SchematicLayout {
  std::vector<SchematicGatePlacement> gates;
  std::vector<SchematicTerminalPlacement> terminals;
  std::vector<SchematicNetRoute> nets;
  double output_x;
};

// Computes the level-based layout for a loaded model: levels
// from SchematicLevels, rows within a level by barycenter
// ordering against the neighbouring levels (a few sweeps, so
// connected gates line up and avoidable crossings drop out),
// columns by level, terminals stacked in declaration order on
// their sides, and nets routed orthogonally with branch dots.
// Gate_width is the widget-measured gate width, shared by
// every column. The result is deterministic for a given model
// and width: same model, same layout.
SchematicLayout ComputeSchematicLayout(const SchematicModel& model,
                                       double gate_width);

// The net paths in the transitive fanin and fanout cones of a
// gate: for each net, the indices of the paths whose wire
// carries a cone signal. A path is in the cone when both its
// endpoints are cone endpoints: gate to gate between two cone
// gates, a primary input feeding a fanin gate, or a cone gate
// driving a primary output. A path from a cone gate to a
// sibling load, or from a primary input to a fanout gate, is
// not in the cone. Empty for an unknown instance.
std::set<std::pair<std::string, std::size_t>> ConeNetPaths(
    const SchematicModel& model, const SchematicLayout& layout,
    const std::string& instance);

}  // namespace eos::ui

#endif  // EOS_UI_SCHEMATIC_H_
