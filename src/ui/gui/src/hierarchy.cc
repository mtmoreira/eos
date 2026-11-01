// hierarchy: the GUI's hierarchy model (L6 interface, Qt-free).
#include "eos/ui/hierarchy.h"

#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace eos::ui {

namespace {

// Maximum nesting depth a payload may claim: real hierarchies
// nest a handful of levels, so 1024 only bounds pathological
// input, never a legal design.
constexpr int kMaxPayloadDepth = 1024;

// One parsed node line, before tree linking.
struct RawNode {
  std::string path;
  std::string module;
  std::vector<std::string> ports;
};

bool HasEmptyComponent(const std::string& path) {
  if (path.empty() || path.front() == '/' || path.back() == '/') {
    return true;
  }
  return path.find("//") != std::string::npos;
}

std::string ParentPath(const std::string& path) {
  const std::size_t slash = path.rfind('/');
  if (slash == std::string::npos) return "";
  return path.substr(0, slash);
}

int PathDepth(const std::string& path) {
  int depth = 1;
  for (char c : path) {
    if (c == '/') ++depth;
  }
  return depth;
}

// Splits one "node ..." line into its fields. Every field is
// required exactly once, and no other key is allowed.
bool ParseNodeLine(const std::string& line, RawNode& node) {
  if (line.rfind("node ", 0) != 0) return false;
  std::string path;
  std::string module;
  std::string ports_value;
  bool path_seen = false;
  bool module_seen = false;
  bool ports_seen = false;
  std::istringstream in(line.substr(5));
  std::string token;
  while (in >> token) {
    const std::size_t equal = token.find('=');
    if (equal == std::string::npos) return false;
    const std::string key = token.substr(0, equal);
    const std::string value = token.substr(equal + 1);
    if (key == "path") {
      if (path_seen || value.empty()) return false;
      path_seen = true;
      path = value;
    } else if (key == "module") {
      if (module_seen || value.empty()) return false;
      module_seen = true;
      module = value;
    } else if (key == "ports") {
      if (ports_seen) return false;
      ports_seen = true;
      ports_value = value;
    } else {
      return false;
    }
  }
  if (!path_seen || !module_seen || !ports_seen) return false;
  if (HasEmptyComponent(path)) return false;
  node.path = path;
  node.module = module;
  if (!ports_value.empty()) {
    std::istringstream ports_in(ports_value);
    std::string port;
    while (std::getline(ports_in, port, ',')) {
      if (port.empty()) return false;
      node.ports.push_back(port);
    }
  }
  return true;
}

HierarchyNode BuildSubtree(
    const std::string& path,
    const std::map<std::string, const RawNode*>& by_path,
    const std::map<std::string, std::vector<const RawNode*>>& by_parent,
    int depth) {
  HierarchyNode node;
  const RawNode* raw = by_path.at(path);
  node.path = raw->path;
  node.module = raw->module;
  node.ports = raw->ports;
  if (depth <= 0) return node;
  const auto children = by_parent.find(path);
  if (children == by_parent.end()) return node;
  for (const RawNode* child_raw : children->second) {
    node.children.push_back(
        BuildSubtree(child_raw->path, by_path, by_parent, depth - 1));
  }
  return node;
}

}  // namespace

bool HierarchyModel::LoadPayload(const std::string& payload) {
  std::istringstream in(payload);
  std::string line;
  std::string top;
  bool top_seen = false;
  std::vector<RawNode> raw_nodes;
  std::set<std::string> seen_paths;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    if (!top_seen) {
      if (line.rfind("top=", 0) != 0) return false;
      top = line.substr(4);
      if (top.empty()) return false;
      top_seen = true;
      continue;
    }
    RawNode node;
    if (!ParseNodeLine(line, node)) return false;
    if (PathDepth(node.path) > kMaxPayloadDepth) return false;
    if (!seen_paths.insert(node.path).second) return false;
    raw_nodes.push_back(std::move(node));
  }
  if (!top_seen || raw_nodes.empty()) return false;
  if (raw_nodes[0].path != top) return false;
  std::map<std::string, const RawNode*> by_path;
  std::map<std::string, std::vector<const RawNode*>> by_parent;
  for (const RawNode& raw : raw_nodes) {
    by_path[raw.path] = &raw;
  }
  for (const RawNode& raw : raw_nodes) {
    if (raw.path == top) continue;
    const std::string parent = ParentPath(raw.path);
    if (by_path.find(parent) == by_path.end()) return false;
    by_parent[parent].push_back(&raw);
  }
  HierarchyNode root =
      BuildSubtree(top, by_path, by_parent, kMaxPayloadDepth);
  root_ = std::move(root);
  if (!selected_.empty() && Find(selected_) == nullptr) {
    selected_.clear();
  }
  return true;
}

void HierarchyModel::Clear() {
  root_.reset();
  selected_.clear();
}

const HierarchyNode* HierarchyModel::Find(const std::string& path) const {
  if (!root_.has_value() || path.empty()) return nullptr;
  std::vector<std::string> parts;
  std::istringstream in(path);
  std::string part;
  while (std::getline(in, part, '/')) {
    if (part.empty()) return nullptr;
    parts.push_back(part);
  }
  const HierarchyNode* node = &root_.value();
  std::string prefix = parts[0];
  if (node->path != prefix) return nullptr;
  for (std::size_t i = 1; i < parts.size(); ++i) {
    prefix += "/" + parts[i];
    const HierarchyNode* next = nullptr;
    for (const HierarchyNode& child : node->children) {
      if (child.path == prefix) {
        next = &child;
        break;
      }
    }
    if (next == nullptr) return nullptr;
    node = next;
  }
  return node;
}

bool HierarchyModel::Select(const std::string& path) {
  if (Find(path) == nullptr) return false;
  selected_ = path;
  return true;
}

void HierarchyModel::ClearSelection() { selected_.clear(); }

}  // namespace eos::ui
