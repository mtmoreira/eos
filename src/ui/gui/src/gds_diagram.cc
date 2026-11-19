// gui: the GDS canvas widget (Qt only).
#include "gds_diagram.h"

#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPen>
#include <QPolygonF>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <string>

#include "eos/ui/finish.h"
#include "eos/ui/gds.h"
#include "eos/ui/gui.h"

namespace eos::ui {

namespace {

// The display name of a geometry shape's layer: the
// technology name, or "gds<number>" when the query could
// not name the layer.
std::string DisplayName(const std::string& layer_name, int gds_layer) {
  return layer_name.empty() ? "gds" + std::to_string(gds_layer)
                            : layer_name;
}

QPointF ToScenePoint(int x, int y) { return QPointF(x, -y); }

// The layer palette: distinct colors that read on the dark
// theme background. A layer keeps its color across
// refreshes: the color is the hash of the layer's display
// name over this palette.
const QColor kLayerPalette[] = {
    Qt::cyan,    Qt::yellow,   Qt::green,    Qt::magenta,
    Qt::red,     Qt::white,    QColor(255, 165, 0),
    QColor(100, 149, 237),
};

QColor LayerColor(const std::string& layer) {
  const std::size_t palette_size =
      sizeof(kLayerPalette) / sizeof(kLayerPalette[0]);
  return kLayerPalette[std::hash<std::string>{}(layer) % palette_size];
}

// The finish counts line of the counts column, or the
// not-run-yet line when no finish run is recorded.
std::string FinishText(const FinishModel& model) {
  if (!model.Loaded()) {
    return "No finish run yet: eos::finish first";
  }
  return "Finish: fillers=" + std::to_string(model.Fillers()) +
         " grid_shapes=" + std::to_string(model.GridShapes()) +
         " metal_fills=" + std::to_string(model.MetalFills()) +
         " pdn=" + (model.Pdn() ? "true" : "false");
}

}  // namespace

GdsDiagram::GdsDiagram(SessionView& view, QWidget* parent)
    : QWidget(parent), view_(view) {
  auto* layout = new QHBoxLayout(this);
  scene_ = new QGraphicsScene(this);
  view_widget_ = new QGraphicsView(scene_, this);
  view_widget_->setRenderHint(QPainter::Antialiasing);
  view_widget_->installEventFilter(this);
  layout->addWidget(view_widget_, 1);
  auto* side = new QWidget(this);
  auto* side_layout = new QVBoxLayout(side);
  layer_selector_ = new QComboBox(side);
  layer_selector_->addItem("All layers");
  side_layout->addWidget(layer_selector_);
  counts_ = new QLabel(side);
  counts_->setWordWrap(true);
  side_layout->addWidget(counts_);
  side_layout->addStretch();
  layout->addWidget(side);
  connect(layer_selector_,
          QOverload<int>::of(&QComboBox::currentIndexChanged), [this]() {
            const QString selected = layer_selector_->currentText();
            selected_layer_ =
                selected == "All layers" ? "" : selected.toStdString();
            RebuildScene();
          });
}

void GdsDiagram::Refresh() {
  const GdsModel& model = view_.Gds();
  std::string text = FinishText(view_.Finish());
  if (!model.Loaded()) {
    text += "\nNo GDS exported yet: eos::write_gds first";
    counts_->setText(QString::fromStdString(text));
    RebuildSelector();
    RebuildScene();
    return;
  }
  text += "\nGDS: " + model.Path();
  text += "\nSize: " + std::to_string(model.Size()) + " bytes";
  text += "\nLibrary: " + model.Library();
  text += "\nStructures: " + std::to_string(model.Structures()) +
          " (instances " + std::to_string(model.Instances()) + ")";
  text += "\nDBU per micron: " +
          QString::number(model.DbuPerMicron(), 'g', 10).toStdString();
  text += "\nTop SREFs: " + std::to_string(model.Srefs().size());
  text += "\nLayers:";
  for (const GdsLayerInfo& layer : model.LayerInfos()) {
    text += "\n" + DisplayName(layer.name, layer.gds_layer) +
            ": boundaries " + std::to_string(layer.boundaries) +
            " paths " + std::to_string(layer.paths);
  }
  text += "\nGeometry: " + std::to_string(model.Boundaries().size()) +
          " boundaries, " + std::to_string(model.Paths().size()) +
          " paths";
  counts_->setText(QString::fromStdString(text));
  RebuildSelector();
  RebuildScene();
}

std::string GdsDiagram::CountsText() const {
  return counts_->text().toStdString();
}

std::string GdsDiagram::SelectedLayer() const {
  return selected_layer_;
}

void GdsDiagram::SelectLayer(const std::string& layer) {
  if (layer.empty()) {
    selected_layer_.clear();
    layer_selector_->setCurrentIndex(0);
    return;
  }
  for (int i = 0; i < layer_selector_->count(); ++i) {
    if (layer_selector_->itemText(i).toStdString() == layer) {
      selected_layer_ = layer;
      layer_selector_->setCurrentIndex(i);
      return;
    }
  }
  selected_layer_.clear();
  layer_selector_->setCurrentIndex(0);
}

void GdsDiagram::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  FitView();
}

void GdsDiagram::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  FitView();
}

bool GdsDiagram::eventFilter(QObject* watched, QEvent* event) {
  if (watched == view_widget_ && event->type() == QEvent::Resize) {
    FitView();
  }
  return QWidget::eventFilter(watched, event);
}

void GdsDiagram::FitView() {
  // No fit for an empty export: the placeholder is one text
  // item, and fitting it would scale that text to fill the
  // view. RebuildScene sets the scene rect for real exports.
  if (!view_.Gds().Loaded()) return;
  view_widget_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
}

void GdsDiagram::RebuildSelector() {
  const QSignalBlocker block(layer_selector_);
  const std::string kept = selected_layer_;
  layer_selector_->clear();
  layer_selector_->addItem("All layers");
  const GdsModel& model = view_.Gds();
  if (model.Loaded()) {
    for (const GdsLayerInfo& layer : model.LayerInfos()) {
      layer_selector_->addItem(QString::fromStdString(
          DisplayName(layer.name, layer.gds_layer)));
    }
  }
  selected_layer_.clear();
  layer_selector_->setCurrentIndex(0);
  if (!kept.empty()) {
    for (int i = 0; i < layer_selector_->count(); ++i) {
      if (layer_selector_->itemText(i).toStdString() == kept) {
        selected_layer_ = kept;
        layer_selector_->setCurrentIndex(i);
        break;
      }
    }
  }
}

void GdsDiagram::RebuildScene() {
  scene_->clear();
  const GdsModel& model = view_.Gds();
  if (!model.Loaded()) {
    auto* placeholder = scene_->addSimpleText(
        "No GDS exported yet: eos::write_gds first");
    placeholder->setBrush(QBrush(Qt::white));
    scene_->setSceneRect(placeholder->boundingRect());
    return;
  }
  const bool all_layers = selected_layer_.empty();
  for (const GdsBoundaryShape& shape : model.Boundaries()) {
    const std::string display =
        DisplayName(shape.layer_name, shape.gds_layer);
    if (!all_layers && display != selected_layer_) continue;
    QPolygonF polygon;
    for (const auto& point : shape.points) {
      polygon << ToScenePoint(point[0], point[1]);
    }
    if (polygon.size() < 3) continue;
    auto* item = scene_->addPolygon(polygon);
    const QColor color = LayerColor(display);
    item->setPen(QPen(color, 1));
    QColor fill = color;
    fill.setAlpha(96);
    item->setBrush(QBrush(fill));
    item->setToolTip(QString::fromStdString(shape.structure + " (" +
                                            display + ")"));
  }
  for (const GdsPathShape& shape : model.Paths()) {
    const std::string display =
        DisplayName(shape.layer_name, shape.gds_layer);
    if (!all_layers && display != selected_layer_) continue;
    QPolygonF polyline;
    for (const auto& point : shape.points) {
      polyline << ToScenePoint(point[0], point[1]);
    }
    if (polyline.size() < 2) continue;
    auto* item = scene_->addPolygon(polyline);
    item->setPen(QPen(LayerColor(display),
                      shape.has_width ? std::max(1, shape.width) : 1));
    item->setBrush(Qt::NoBrush);
    item->setToolTip(QString::fromStdString(shape.structure + " (" +
                                            display + ")"));
  }
  if (model.HasBoundingBox()) {
    const std::array<int, 4> box = model.BoundingBox();
    scene_->setSceneRect(
        QRectF(box[0], -box[3], box[2] - box[0], box[3] - box[1])
            .adjusted(-20, -20, 20, 20));
  } else {
    scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(
        -20, -20, 20, 20));
  }
  FitView();
}

}  // namespace eos::ui
