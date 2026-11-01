// gui: the block diagram widget (internal, Qt only).
//
// Built only when Qt6 is found at configure time. The widget is
// thin over the DiagramModel: it renders the level SessionView
// reports, and reports clicks to SessionView, and keeps no
// design state of its own. Like the rest of this module, the
// widget uses plain callbacks, never Qt signals, so no Q_OBJECT
// here and no moc step is needed.
#ifndef EOS_UI_GUI_BLOCK_DIAGRAM_H_
#define EOS_UI_GUI_BLOCK_DIAGRAM_H_

#include <QWidget>

#include <functional>
#include <string>

class QGraphicsScene;
class QGraphicsView;
class QHBoxLayout;

namespace eos::ui {

class SessionView;

class BlockDiagram : public QWidget {
 public:
  explicit BlockDiagram(SessionView& view, QWidget* parent = nullptr);

  // Rebuilds the breadcrumb and the scene from the model.
  // Called after any session change, via MainWindow::Refresh.
  void Refresh();

  // Called after a diagram click changes the selection or the
  // level, so the main window can refresh. Unset by default:
  // without it the diagram only updates the model.
  void SetActionCallback(std::function<void()> callback) {
    on_action_ = std::move(callback);
  }

 protected:
  // Refit on resize, so the diagram fills the view after the
  // window is shown or resized, not just when its content
  // changes. Without this, the fit computed before show (when
  // the view has no real size yet) sticks, and the diagram
  // renders tiny in the middle of a large window.
  void resizeEvent(QResizeEvent* event) override;

  // Refit on show as well: a widget resized before show gets
  // no resize event at show time (its size does not change),
  // so resize alone does not cover the first paint.
  void showEvent(QShowEvent* event) override;

  // Watch the view itself: the layout gives the view its real
  // size during show, after showEvent, without resizing this
  // widget, so only the view's own resize covers the first
  // paint in every case. Plain override, no signals, no moc.
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void RebuildBreadcrumb();
  void RebuildScene();
  void FitView();
  void OnBlockClicked(const std::string& instance);
  void OnBlockDoubleClicked(const std::string& instance);
  void OnCrumbClicked(const std::string& path);

  SessionView& view_;
  QWidget* crumbs_ = nullptr;
  QHBoxLayout* crumbs_layout_ = nullptr;
  QGraphicsScene* scene_ = nullptr;
  QGraphicsView* view_widget_ = nullptr;
  std::function<void()> on_action_;
};

}  // namespace eos::ui

#endif  // EOS_UI_GUI_BLOCK_DIAGRAM_H_
