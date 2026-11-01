// hierarchy: the GUI's hierarchy model (L6 interface, Qt-free).
//
// The model the hierarchy browser widget reads: one tree plus the
// current selection. The tree is built only from a
// synth_hierarchy payload, never from mock data, and parsing is
// all-or-nothing: a malformed payload leaves the previous tree
// untouched and reports false, so the browser never shows a
// half-parsed tree. The widget owns expand and collapse display;
// the model only records which nodes have children, which is
// what makes a node expandable at all.
#ifndef EOS_UI_HIERARCHY_H_
#define EOS_UI_HIERARCHY_H_

#include <optional>
#include <string>
#include <vector>

namespace eos::ui {

// One node of the elaborated design's hierarchy, as the browser
// shows it: an instance path, the module it instantiates, its
// ports in declaration order, and its submodule instances. The
// root's path is the top module name; a child's path is its
// parent's path plus "/" plus the instance name. A node with no
// children is a leaf in the browser: it has no expand affordance,
// and (in U3) it is where the block diagram gives way to the
// schematic.
struct HierarchyNode {
  std::string path;
  std::string module;
  std::vector<std::string> ports;
  std::vector<HierarchyNode> children;
};

class HierarchyModel {
 public:
  // Parses a synth_hierarchy payload: a "top=<module>" line, then
  // one "node path=<path> module=<module> ports=<p1,p2,...>" line
  // per node, pre-order. Returns false (tree and selection
  // unchanged) on any malformed line, unknown key, missing top
  // line, duplicate path, node whose parent is absent, or a path
  // with an empty component. A successful load clears a
  // selection the new tree no longer contains, so U3 never
  // consumes a stale path.
  bool LoadPayload(const std::string& payload);
  void Clear();

  bool Empty() const { return !root_.has_value(); }
  const HierarchyNode* Root() const {
    return root_.has_value() ? &root_.value() : nullptr;
  }
  const HierarchyNode* Find(const std::string& path) const;

  // Selection: only a path present in the tree can be selected.
  // Select returns false (selection unchanged) for an unknown
  // path.
  bool Select(const std::string& path);
  void ClearSelection();
  const std::string& SelectedPath() const { return selected_; }
  bool HasSelection() const { return !selected_.empty(); }

 private:
  std::optional<HierarchyNode> root_;
  std::string selected_;
};

}  // namespace eos::ui

#endif  // EOS_UI_HIERARCHY_H_
