// gui: the Elaboration main window (internal, Qt only).
//
// Built only when Qt6 is found at configure time. Widgets talk to
// the session through SessionView and use plain callbacks, never
// Qt signals: no Q_OBJECT here, so no moc step is needed.
#ifndef EOS_UI_GUI_MAIN_WINDOW_H_
#define EOS_UI_GUI_MAIN_WINDOW_H_

#include <QMainWindow>

#include <cstddef>
#include <string>

#include "eos/ui/console_entry.h"
#include "eos/ui/gui.h"

class QCheckBox;
class QCloseEvent;
class QCompleter;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStringListModel;
class QSplitter;

namespace eos::ui {

class CenterView;
class HierarchyBrowser;
class PanelsView;

class MainWindow : public QMainWindow {
 public:
  explicit MainWindow(SessionView& view, QWidget* parent = nullptr);

  // Re-reads step state, hierarchy, diagram, schematic, panels,
  // issues, and the log into the widgets. Called after any
  // session change. The center shows the block diagram for a
  // non-leaf level and the schematic for a leaf level.
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

  // Console entry keys: up and down cycle the entry's command
  // history, Tab completes the registry command name (a popup
  // lists multiple matches). Return always submits the entry
  // text, even while the completion popup is open: Tab is the
  // completion key, and letting the completer swallow Return
  // left submissions silently unrun. While the completion popup
  // is open, its other keys navigate and accept, and this filter
  // stays out of the way.
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void RefreshSteps();
  void RefreshConsole();
  // Appends the log entries RefreshLog drained since the last
  // render, in severity colors, skipping severities whose
  // filter toggle is off. A filter change re-renders the whole
  // log from the start.
  void RefreshLogWindow();
  void AppendLogLine(const SessionView::LogEntry& entry);
  bool LogSeverityEnabled(const std::string& severity) const;
  void FindNextLogMatch();

  // Submits the console entry text: records it in the entry's
  // history, dispatches it through the session, clears the
  // entry, and refreshes the window, so each effect is visible
  // live. The one submit path for both the Return key and the
  // completion-popup case.
  void SubmitEntry();

  SessionView& view_;
  QPushButton* step_buttons_[3];
  QLabel* checkpoint_label_ = nullptr;
  QLabel* floorplan_label_ = nullptr;
  HierarchyBrowser* browser_ = nullptr;
  CenterView* center_ = nullptr;
  PanelsView* panels_ = nullptr;
  QPlainTextEdit* console_ = nullptr;
  QLineEdit* entry_ = nullptr;
  QCompleter* entry_completer_ = nullptr;
  QStringListModel* entry_completion_model_ = nullptr;
  ConsoleHistory entry_history_;
  QPlainTextEdit* log_ = nullptr;
  QLineEdit* log_search_ = nullptr;
  QCheckBox* log_errors_ = nullptr;
  QCheckBox* log_warnings_ = nullptr;
  QCheckBox* log_infos_ = nullptr;
  // Log entries already rendered, so a refresh only appends
  // the new ones. A filter change resets this to 0 and clears
  // the view, so the whole log re-renders under the new
  // filters.
  std::size_t log_shown_ = 0;
  bool log_filters_dirty_ = false;
  bool stdin_closed_ = false;
};

}  // namespace eos::ui

#endif  // EOS_UI_GUI_MAIN_WINDOW_H_
