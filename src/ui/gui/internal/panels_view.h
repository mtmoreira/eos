// gui: the right panels widget (internal, Qt only).
//
// Built only when Qt6 is found at configure time. The widget is
// thin over the PanelsModel, the IssuesModel, the schematic
// model, and the dispatch message log: it renders six tabs
// (Issues, Sources, Results, Opt log, Messages, Schematic) and
// keeps no panel state of its own, except the issues table's
// content signature (so a refresh that changes nothing keeps
// the table, and the row selection, as it is). Like the rest
// of this module, the widget uses plain callbacks, never Qt
// signals, so no Q_OBJECT here and no moc step is needed.
#ifndef EOS_UI_GUI_PANELS_VIEW_H_
#define EOS_UI_GUI_PANELS_VIEW_H_

#include <QTabWidget>

#include <functional>
#include <string>

class QLabel;
class QPlainTextEdit;
class QTableWidget;

namespace eos::ui {

class SchematicDiagram;
class SessionView;

class PanelsView : public QTabWidget {
 public:
  explicit PanelsView(SessionView& view, QWidget* parent = nullptr);

  // Re-renders all six tabs from the models and the message
  // log. Called after any session change, via
  // MainWindow::Refresh.
  void Refresh();

  // Shows one tab by index: 0 Issues, 1 Sources, 2 Results,
  // 3 Opt log, 4 Messages, 5 Schematic. The stepper maps its
  // steps onto Issues, Sources, and Opt log, so clicking a
  // step shows that step's outputs. Out-of-range indexes are
  // ignored.
  void ShowTab(int index);

  // Shows the Schematic tab. The block diagram's
  // click-to-expand calls this when a click descends to a
  // leaf level.
  void ShowSchematicTab();

  // Locates an issue: selects its instance path in the
  // hierarchy, descends the diagram to it, refreshes the
  // schematic for the leaf level, highlights the offending
  // gate when the level is a leaf, shows the Schematic tab,
  // and runs the action callback (MainWindow::Refresh) so the
  // window shows the new state. A path the hierarchy does
  // not hold does nothing.
  void LocateIssue(const std::string& path, const std::string& target);

  // Called after an issue click or a schematic breadcrumb
  // click changes the selection or the level, so the main
  // window can refresh. Unset by default: without it the
  // panels only update their models.
  void SetActionCallback(std::function<void()> callback) {
    on_action_ = std::move(callback);
  }

 private:
  void RefreshIssues();
  void RefreshMessages();

  SessionView& view_;
  QLabel* issues_header_ = nullptr;
  QTableWidget* issues_table_ = nullptr;
  // The issues content the table currently shows, so a
  // refresh that changes nothing skips the rebuild and keeps
  // the row selection.
  std::string issues_signature_;
  QPlainTextEdit* sources_ = nullptr;
  QPlainTextEdit* results_ = nullptr;
  QPlainTextEdit* optlog_ = nullptr;
  QPlainTextEdit* messages_ = nullptr;
  SchematicDiagram* schematic_ = nullptr;
  std::function<void()> on_action_;
};

}  // namespace eos::ui

#endif  // EOS_UI_GUI_PANELS_VIEW_H_
