// gui: the Elaboration main window (internal, Qt only).
//
// Built only when Qt6 is found at configure time. Widgets talk to
// the session through SessionView and use plain callbacks, never
// Qt signals: no Q_OBJECT here, so no moc step is needed.
#ifndef EOS_UI_GUI_MAIN_WINDOW_H_
#define EOS_UI_GUI_MAIN_WINDOW_H_

#include <QMainWindow>

#include <string>

class QCloseEvent;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSplitter;

namespace eos::ui {

class BlockDiagram;
class HierarchyBrowser;
class PanelsView;
class SchematicDiagram;
class SessionView;

class MainWindow : public QMainWindow {
 public:
  explicit MainWindow(SessionView& view, QWidget* parent = nullptr);

  // Re-reads step state, hierarchy, diagram, schematic, panels,
  // and the echo log into the widgets. Called after any session
  // change. At a leaf level the schematic replaces the diagram
  // in the center; otherwise the diagram shows.
  void Refresh();

  // Shows a hidden window again (U6): the terminal reopen
  // command calls this on the GUI thread, so a window closed
  // earlier reappears on the same session.
  void ShowAgain();

  // Records that the terminal stdin reached EOF (U6). A window
  // hidden with no terminal left cannot be reopened, so in that
  // state the application quits instead of lingering headless.
  void NoteStdinClosed();

 protected:
  // Closing the window hides it (U6); it never kills the
  // session. The one exception is a window closed after the
  // terminal stdin closed: with no way back in, the close is
  // accepted and the application quits.
  void closeEvent(QCloseEvent* event) override;

 private:
  void RefreshSteps();
  void RefreshConsole();

  SessionView& view_;
  QPushButton* step_buttons_[3];
  QLabel* checkpoint_label_ = nullptr;
  HierarchyBrowser* browser_ = nullptr;
  BlockDiagram* diagram_ = nullptr;
  SchematicDiagram* schematic_ = nullptr;
  PanelsView* panels_ = nullptr;
  QPlainTextEdit* console_ = nullptr;
  QLineEdit* entry_ = nullptr;
  bool stdin_closed_ = false;
};

}  // namespace eos::ui

#endif  // EOS_UI_GUI_MAIN_WINDOW_H_
