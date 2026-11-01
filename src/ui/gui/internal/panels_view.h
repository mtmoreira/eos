// gui: the right panels widget (internal, Qt only).
//
// Built only when Qt6 is found at configure time. The widget is
// thin over the PanelsModel and the dispatch message log: it
// renders four read-only tabs (Sources, Results, Opt log,
// Messages) and keeps no panel state of its own. Like the rest
// of this module, the widget uses plain callbacks, never Qt
// signals, so no Q_OBJECT here and no moc step is needed.
#ifndef EOS_UI_GUI_PANELS_VIEW_H_
#define EOS_UI_GUI_PANELS_VIEW_H_

#include <QTabWidget>

class QPlainTextEdit;

namespace eos::ui {

class SessionView;

class PanelsView : public QTabWidget {
 public:
  explicit PanelsView(SessionView& view, QWidget* parent = nullptr);

  // Re-renders all four tabs from the model and the message
  // log. Called after any session change, via
  // MainWindow::Refresh.
  void Refresh();

  // Shows one tab by index: 0 Sources, 1 Results, 2 Opt log,
  // 3 Messages. The stepper maps its steps onto the first
  // three, so clicking a step shows that step's outputs.
  // Out-of-range indexes are ignored.
  void ShowTab(int index);

 private:
  SessionView& view_;
  QPlainTextEdit* sources_ = nullptr;
  QPlainTextEdit* results_ = nullptr;
  QPlainTextEdit* optlog_ = nullptr;
  QPlainTextEdit* messages_ = nullptr;
};

}  // namespace eos::ui

#endif  // EOS_UI_GUI_PANELS_VIEW_H_
