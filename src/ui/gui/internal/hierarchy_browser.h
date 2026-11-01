// gui: the hierarchy browser widget (internal, Qt only).
//
// Built only when Qt6 is found at configure time. The widget is
// thin over the HierarchyModel: it displays the tree and reports
// clicks to SessionView, and keeps no hierarchy state of its
// own. Expand and collapse are native QTreeWidget behavior; the
// model only records which nodes have children, which is what
// makes a node expandable at all. Like the rest of this module,
// the widget uses plain callbacks, never Qt signals, so no
// Q_OBJECT here and no moc step is needed.
#ifndef EOS_UI_GUI_HIERARCHY_BROWSER_H_
#define EOS_UI_GUI_HIERARCHY_BROWSER_H_

#include <QWidget>

#include <string>
#include <vector>

class QTreeWidget;
class QTreeWidgetItem;

namespace eos::ui {

class HierarchyNode;
class SessionView;

class HierarchyBrowser : public QWidget {
 public:
  explicit HierarchyBrowser(SessionView& view, QWidget* parent = nullptr);

  // Rebuilds the tree from the model, preserving expanded paths
  // and restoring the model's selection. Called after any
  // session change, via MainWindow::Refresh.
  void Refresh();

 private:
  void PopulateItem(QTreeWidgetItem* parent, const HierarchyNode& node);
  void CollectExpanded(QTreeWidgetItem* item, std::vector<std::string>& out);
  void RestoreExpanded(QTreeWidgetItem* item,
                       const std::vector<std::string>& expanded);
  QTreeWidgetItem* FindItem(QTreeWidgetItem* parent,
                            const std::string& path);

  SessionView& view_;
  QTreeWidget* tree_ = nullptr;
};

}  // namespace eos::ui

#endif  // EOS_UI_GUI_HIERARCHY_BROWSER_H_
