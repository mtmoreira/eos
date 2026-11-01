// gui: the Elaboration main window (internal, Qt only).
//
// Built only when Qt6 is found at configure time. Widgets talk to
// the session through SessionView and use plain callbacks, never
// Qt signals: no Q_OBJECT here, so no moc step is needed.
#ifndef EOS_UI_GUI_MAIN_WINDOW_H_
#define EOS_UI_GUI_MAIN_WINDOW_H_

#include <QMainWindow>

#include <string>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace eos::ui {

class SessionView;

class MainWindow : public QMainWindow {
 public:
  explicit MainWindow(SessionView& view, QWidget* parent = nullptr);

  // Re-reads step state, center text, and the echo log into the
  // widgets. Called after any session change.
  void Refresh();

 private:
  void RefreshSteps();
  void RefreshCenter();
  void RefreshConsole();
  std::string CenterText() const;

  SessionView& view_;
  // 0: Read, 1: Elaborate, 2: Generic synth.
  int selected_step_ = 0;
  QPushButton* step_buttons_[3];
  QLabel* center_ = nullptr;
  QPlainTextEdit* console_ = nullptr;
};

}  // namespace eos::ui

#endif  // EOS_UI_GUI_MAIN_WINDOW_H_
