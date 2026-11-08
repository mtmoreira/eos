// gui: the floorplan canvas widget (internal, Qt only).
//
// Built only when Qt6 is found at configure time. The widget is
// thin over the FloorplanModel: it renders the die outline, the
// core outline, one rectangle per row, and one marker per placed
// IO pin, with the payload counts beside the canvas. Like the
// rest of this module, the widget uses plain callbacks, never
// Qt signals, so no Q_OBJECT here and no moc step is needed.
//
// Scene coordinates are database units with the Y axis flipped
// (a database rectangle (lx, ly, ux, uy) draws as
// (lx, -uy, ux-lx, uy-ly)), so the database's north stays at
// the top of the canvas. The canvas follows the outline style
// of the schematic: white contours with no fill, white text,
// on the theme background. The die outlines in a heavier pen
// than the core, and the rows in grey, so the die reads as the
// frame, the core as the placement area, and the rows as the
// fill. A pin with no placed geometry has no marker: the
// counts column counts it instead.
#ifndef EOS_UI_GUI_FLOORPLAN_DIAGRAM_H_
#define EOS_UI_GUI_FLOORPLAN_DIAGRAM_H_

#include <QWidget>

#include <string>

class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QResizeEvent;
class QShowEvent;

namespace eos::ui {

class SessionView;

class FloorplanDiagram : public QWidget {
 public:
  explicit FloorplanDiagram(SessionView& view, QWidget* parent = nullptr);

  // Rebuilds the counts column and the scene from the model.
  // Called after any session change, via MainWindow::Refresh.
  void Refresh();

  // Test seam: the scene this widget renders, for unit tests to
  // inspect the drawn items. Read-only; the widget owns it.
  const QGraphicsScene* Scene() const { return scene_; }

  // Test seam: the counts text beside the canvas.
  std::string CountsText() const;

 protected:
  // Refit on resize, so the floorplan fills the view after the
  // window is shown or resized, not just when its content
  // changes. Without this, the fit computed before show (when
  // the view has no real size yet) sticks, and the floorplan
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
  void RebuildScene();
  void FitView();

  SessionView& view_;
  QLabel* counts_ = nullptr;
  QGraphicsScene* scene_ = nullptr;
  QGraphicsView* view_widget_ = nullptr;
};

}  // namespace eos::ui

#endif  // EOS_UI_GUI_FLOORPLAN_DIAGRAM_H_
