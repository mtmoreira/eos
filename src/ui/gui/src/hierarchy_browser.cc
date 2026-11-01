// gui: the hierarchy browser widget (Qt only).
#include "hierarchy_browser.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <string>
#include <vector>

#include "eos/ui/gui.h"
#include "eos/ui/hierarchy.h"

namespace eos::ui {

namespace {

// Tree columns: the instance name (last path component) and the
// module it instantiates. The full path rides in the item's user
// data, since that is what selection and expansion restore key
// on, not the display text.
constexpr int kPathRole = 0x0100;  // Qt::UserRole.

std::string InstanceName(const std::string& path) {
  const std::size_t slash = path.rfind('/');
  if (slash == std::string::npos) return path;
  return path.substr(slash + 1);
}

}  // namespace

HierarchyBrowser::HierarchyBrowser(SessionView& view, QWidget* parent)
    : QWidget(parent), view_(view) {
  auto* layout = new QVBoxLayout(this);
  tree_ = new QTreeWidget(this);
  tree_->setColumnCount(2);
  tree_->setHeaderLabels({"Instance", "Module"});
  layout->addWidget(tree_);
  connect(tree_, &QTreeWidget::itemClicked,
          [this](QTreeWidgetItem* item, int) {
            if (item == nullptr) return;
            if (view_.SelectHierarchyPath(
                    item->data(0, kPathRole).toString().toStdString()) &&
                on_select_) {
              on_select_();
            }
          });
}

void HierarchyBrowser::PopulateItem(QTreeWidgetItem* parent,
                                    const HierarchyNode& node) {
  // A null parent means a top-level item, which the item
  // constructor only creates when handed the tree itself.
  auto* item = parent == nullptr ? new QTreeWidgetItem(tree_)
                                 : new QTreeWidgetItem(parent);
  item->setText(0, QString::fromStdString(InstanceName(node.path)));
  item->setText(1, QString::fromStdString(node.module));
  item->setData(0, kPathRole, QString::fromStdString(node.path));
  for (const HierarchyNode& child : node.children) {
    PopulateItem(item, child);
  }
}

void HierarchyBrowser::CollectExpanded(QTreeWidgetItem* item,
                                       std::vector<std::string>& out) {
  if (item->isExpanded()) {
    out.push_back(item->data(0, kPathRole).toString().toStdString());
  }
  for (int i = 0; i < item->childCount(); ++i) {
    CollectExpanded(item->child(i), out);
  }
}

void HierarchyBrowser::RestoreExpanded(
    QTreeWidgetItem* item, const std::vector<std::string>& expanded) {
  const std::string path =
      item->data(0, kPathRole).toString().toStdString();
  for (const std::string& kept : expanded) {
    if (kept == path) {
      item->setExpanded(true);
      break;
    }
  }
  for (int i = 0; i < item->childCount(); ++i) {
    RestoreExpanded(item->child(i), expanded);
  }
}

QTreeWidgetItem* HierarchyBrowser::FindItem(QTreeWidgetItem* parent,
                                            const std::string& path) {
  if (parent->data(0, kPathRole).toString().toStdString() == path) {
    return parent;
  }
  for (int i = 0; i < parent->childCount(); ++i) {
    if (QTreeWidgetItem* found = FindItem(parent->child(i), path)) {
      return found;
    }
  }
  return nullptr;
}

void HierarchyBrowser::Refresh() {
  std::vector<std::string> expanded;
  for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
    CollectExpanded(tree_->topLevelItem(i), expanded);
  }
  tree_->clear();
  const HierarchyNode* root = view_.Hierarchy().Root();
  if (root != nullptr) {
    PopulateItem(nullptr, *root);
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
      RestoreExpanded(tree_->topLevelItem(i), expanded);
    }
    const std::string& selected = view_.SelectedHierarchyPath();
    if (!selected.empty()) {
      for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        if (QTreeWidgetItem* item = FindItem(tree_->topLevelItem(i), selected)) {
          tree_->setCurrentItem(item);
          break;
        }
      }
    }
  }
}

}  // namespace eos::ui
