// gui: the GDS canvas widget (internal, Qt only).
//
// Built only when Qt6 is found at configure time. The widget
// is thin over the GdsModel: it renders the flattened
// top-structure geometry of the recorded export, one polygon
// per boundary and one polyline per path, each in its
// layer's color, with the finish counts, the export record,
// the per-layer census, and a layer selector beside the
// canvas. Like the rest of this module, the widget uses
// plain callbacks, never Qt signals, so no Q_OBJECT here and
// no moc step is needed.
//
// Scene coordinates are file coordinates with the Y axis
// flipped (a file point (x, y) draws at (x, -y)), so the
// file's north stays at the top of the canvas. A GDS file
// carries no die rectangle, so the canvas fits to the
// model's geometry bounding box instead of an outline. A
// layer keeps its color across refreshes: the color is the
// hash of the layer's display name over the palette (the
// technology name, or "gds<number>" for a layer the query
// could not name).
//
// The layer selector is view-only widget state, held by the
// diagram like the hierarchy selection: it touches no model
// and no session record. "All layers" draws every boundary
// and path; selecting one layer filters the drawn geometry
// to that layer.
#ifndef EOS_UI_GUI_GDS_DIAGRAM_H_
#define EOS_UI_GUI_GDS_DIAGRAM_H_

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

class GdsDiagram : public QWidget {
 public:
  explicit GdsDiagram(SessionView& view, QWidget* parent = nullptr);

  // Rebuilds the layer selector, the counts column, and the
  // scene from the models. Called after any session change,
  // via MainWindow::Refresh.
  void Refresh();

  // Test seam: the scene this widget renders, for unit tests
  // to inspect the drawn items. Read-only; the widget owns it.
  const QGraphicsScene* Scene() const { return scene_; }

  // Test seam: the counts text beside the canvas.
  std::string CountsText() const;

  // Test seam: the selected layer's display name, or empty
  // for "All layers".
  std::string SelectedLayer() const;

  // Test seam: selects a layer by display name (or "All
  // layers"), as the combo does; a name the model does not
  // carry selects "All layers".
  void SelectLayer(const std::string& layer);

 protected:
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;
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
  // The selected layer's display name, or empty for "All
  // layers". Kept beside the combo so a Refresh that
  // rebuilds the combo items keeps the selection when the
  // layer is still there.
  std::string selected_layer_;
};

}  // namespace eos::ui

#endif  // EOS_UI_GUI_GDS_DIAGRAM_H_
