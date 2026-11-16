// gui: the routing canvas widget (internal, Qt only).
//
// Built only when Qt6 is found at configure time. The widget is
// thin over the RouteModel: it renders the die outline, the
// core outline, one faint rectangle per placed instance (from
// the already-loaded PlacementModel), one polyline per routed
// wire segment in its layer's color, and one small square per
// via in its upper layer's color, with the payload counts, the
// per-layer congestion table, and a layer selector beside the
// canvas. Like the rest of this module, the widget uses plain
// callbacks, never Qt signals, so no Q_OBJECT here and no moc
// step is needed.
//
// Scene coordinates are database units with the Y axis flipped
// (a database point (x, y) draws at (x, -y), a database
// rectangle (lx, ly, ux, uy) draws as
// (lx, -uy, ux-lx, uy-ly)), so the database's north stays at
// the top of the canvas. The canvas follows the outline style
// of the schematic, the floorplan, and the placement: white
// contours for the die and core, on the theme background, with
// the die outline in a heavier pen. Wire polylines are drawn
// at their recorded width, in their layer's color (the master
// palette's hash rule, keyed on the layer name, so a layer
// keeps its color across refreshes). Via squares are sized
// from the die height, so they read at any zoom.
//
// The layer selector is view-only widget state, held by the
// diagram like the hierarchy selection: it touches no model
// and no session record. "All layers" draws every segment
// and via; selecting one layer filters the drawn polylines
// and vias to that layer (the instance rectangles and the
// outlines stay).
#ifndef EOS_UI_GUI_ROUTE_DIAGRAM_H_
#define EOS_UI_GUI_ROUTE_DIAGRAM_H_

#include <QWidget>

#include <string>

class QComboBox;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QResizeEvent;
class QShowEvent;

namespace eos::ui {

class SessionView;

class RouteDiagram : public QWidget {
 public:
  explicit RouteDiagram(SessionView& view, QWidget* parent = nullptr);

  // Rebuilds the layer selector, the counts column, and the
  // scene from the model. Called after any session change, via
  // MainWindow::Refresh.
  void Refresh();

  // Test seam: the scene this widget renders, for unit tests to
  // inspect the drawn items. Read-only; the widget owns it.
  const QGraphicsScene* Scene() const { return scene_; }

  // Test seam: the counts text beside the canvas.
  std::string CountsText() const;

  // Test seam: the selected layer name, or empty for "All
  // layers".
  std::string SelectedLayer() const;

  // Test seam: selects a layer by name (or "All layers"), as
  // the combo does; a name the model does not carry selects
  // "All layers".
  void SelectLayer(const std::string& layer);

 protected:
  // Refit on resize, so the route fills the view after the
  // window is shown or resized, not just when its content
  // changes. Without this, the fit computed before show (when
  // the view has no real size yet) sticks, and the route
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
  void RebuildSelector();
  void FitView();

  SessionView& view_;
  QComboBox* layer_selector_ = nullptr;
  QLabel* counts_ = nullptr;
  QGraphicsScene* scene_ = nullptr;
  QGraphicsView* view_widget_ = nullptr;
  // The selected layer name, or empty for "All layers". Kept
  // beside the combo so a Refresh that rebuilds the combo
  // items keeps the selection when the layer is still there.
  std::string selected_layer_;
};

}  // namespace eos::ui

#endif  // EOS_UI_GUI_ROUTE_DIAGRAM_H_
