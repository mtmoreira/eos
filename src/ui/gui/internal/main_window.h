// gui: the Elaboration main window (internal, Qt only).
//
// Built only when Qt6 is found at configure time. Widgets talk to
// the session through SessionView and use plain callbacks, never
// Qt signals: no Q_OBJECT here, so no moc step is needed.
#ifndef EOS_UI_GUI_MAIN_WINDOW_H_
#define EOS_UI_GUI_MAIN_WINDOW_H_

#include <QMainWindow>

#include <string>

class QPlainTextEdit;
class QPushButton;
class QSplitter;

namespace eos::ui {

class BlockDiagram;
class HierarchyBrowser;
class SchematicDiagram;
class SessionView;

class MainWindow : public QMainWindow {
 public:
  explicit MainWindow(SessionView& view, QWidget* parent = nullptr);

  // Re-reads step state, hierarchy, diagram, schematic, and
  // the echo log into the widgets. Called after any session
  // change. At a leaf level the schematic replaces the diagram
  // in the center; otherwise the diagram shows.
  void Refresh();

 private:
  void RefreshSteps();
  void RefreshConsole();

  SessionView& view_;
  QPushButton* step_buttons_[3];
  HierarchyBrowser* browser_ = nullptr;
  BlockDiagram* diagram_ = nullptr;
  SchematicDiagram* schematic_ = nullptr;
  QPlainTextEdit* console_ = nullptr;
};

}  // namespace eos::ui

#endif  // EOS_UI_GUI_MAIN_WINDOW_H_
