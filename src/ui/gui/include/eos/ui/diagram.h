// diagram: the GUI's block diagram model (L6 interface, Qt-free).
//
// The model the block diagram widget reads: one hierarchy level's
// parent ports, blocks, and nets, plus the breadcrumb helper for
// navigation. The model is built only from a synth_blocks
// payload, never from mock data, and parsing is all-or-nothing
// like HierarchyModel: a malformed payload leaves the previous
// diagram untouched and reports false.
#ifndef EOS_UI_DIAGRAM_H_
#define EOS_UI_DIAGRAM_H_

#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace eos::ui {

// One port of a block diagram, with its direction: "in", "out",
// or "inout".
struct DiagramPort {
  std::string name;
  std::string direction;
};

// One block of a level: a submodule instance, with its ports in
// declaration order.
struct DiagramBlock {
  std::string instance;
  std::string module;
  std::vector<DiagramPort> ports;
};

// One endpoint of a diagram net on a block: the instance and the
// port it touches.
struct DiagramNetEndpoint {
  std::string block;
  std::string port;
};

// One net of a level: a wire, its endpoints on blocks, and the
// parent ports it touches.
struct DiagramNet {
  std::string name;
  std::vector<DiagramNetEndpoint> endpoints;
  std::vector<std::string> terminals;
};

class DiagramModel {
 public:
  // Parses a synth_blocks payload: a "module=<module>" line,
  // then "parent port=<name> direction=<dir>" lines, "block
  // instance=<name> module=<module> ports=<p:dir,...>" lines,
  // and "net name=<wire> blocks=<inst:port,...>
  // terminals=<port,...>" lines. Returns false (diagram
  // unchanged) on any malformed line, unknown key, missing
  // module line, duplicate parent port, block, or net,
  // duplicate port within a block, endpoint on an unknown block
  // or port, terminal on an unknown parent port, an unknown
  // direction, a net with neither endpoints nor terminals, or
  // an empty name. A successful load replaces the previous
  // diagram.
  bool LoadPayload(const std::string& payload);
  void Clear();

  bool Empty() const { return module_.empty(); }
  const std::string& Module() const { return module_; }
  const std::vector<DiagramPort>& ParentPorts() const {
    return parent_ports_;
  }
  const std::vector<DiagramBlock>& Blocks() const { return blocks_; }
  const std::vector<DiagramNet>& Nets() const { return nets_; }
  const DiagramBlock* FindBlock(const std::string& instance) const;

 private:
  std::string module_;
  std::vector<DiagramPort> parent_ports_;
  std::vector<DiagramBlock> blocks_;
  std::vector<DiagramNet> nets_;
};

// The transitive fanin cone of a block: every block that drives
// the block's input ports, and their drivers in turn. A parent
// input terminal stops the walk: it is a source, not a block.
// Empty for an unknown instance.
std::set<std::string> FaninCone(const DiagramModel& model,
                                const std::string& instance);

// The transitive fanout cone of a block: every block loaded by
// the block's output ports, and their loads in turn. A parent
// output terminal stops the walk. Empty for an unknown instance.
std::set<std::string> FanoutCone(const DiagramModel& model,
                                 const std::string& instance);

// The center anchor of a diagram net: the index into the
// endpoints-then-terminals anchor order of the driving output
// endpoint when one exists, else the first anchor. The widget
// draws its star from this anchor, and ConeNetSpokes keys its
// spokes by the far anchor's index in the same order.
std::size_t DiagramNetCenterIndex(const DiagramModel& model,
                                  const DiagramNet& net);

// The net spokes in the transitive fanin and fanout cones of a
// block: (net name, anchor index) pairs for the star segments
// the widget draws, one per spoke from the net's center anchor
// to another anchor, plus index 0 for a lone anchor's stub.
// A spoke is in the cone when both its ends carry a cone
// signal. A spoke between two blocks is in the cone when both
// blocks are cone blocks; a spoke between a block and a parent
// terminal is in the cone when the block is on the terminal's
// side, fanin for an input terminal, fanout for an output
// terminal. A spoke between two parent terminals is never in
// the cone. A lone anchor's stub is in the cone when its block
// is a cone block. Empty for an unknown instance.
std::set<std::pair<std::string, std::size_t>> ConeNetSpokes(
    const DiagramModel& model, const std::string& instance);

// Breadcrumb paths for a hierarchy path: each prefix from the
// root down to the path itself. "top/u_mid/u_add" gives "top",
// "top/u_mid", "top/u_mid/u_add". An empty path gives no crumbs.
// A path with an empty interior or leading component (such as
// "top//x" or "/top") also gives no crumbs; a trailing slash is
// ignored ("top/" crumbs to "top").
std::vector<std::string> BreadcrumbPaths(const std::string& path);

}  // namespace eos::ui

#endif  // EOS_UI_DIAGRAM_H_
