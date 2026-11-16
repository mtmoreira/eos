// gui: the routing canvas widget (Qt only).
#include "route_diagram.h"

#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
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

#include "eos/ui/gui.h"
#include "eos/ui/placement.h"
#include "eos/ui/route.h"

namespace eos::ui {

namespace {

// The scene rectangle of a database rectangle: the Y axis is
// flipped so the database's north stays at the top of the
// canvas (see the header).
QRectF ToScene(const RouteRect& rect) {
  return QRectF(rect[0], -rect[3], rect[2] - rect[0], rect[3] - rect[1]);
}

QPointF ToScenePoint(int x, int y) {
  return QPointF(x, -y);
}

std::string RectText(const RouteRect& rect) {
  return std::to_string(rect[0]) + "," + std::to_string(rect[1]) + "," +
         std::to_string(rect[2]) + "," + std::to_string(rect[3]);
}

// The layer palette: distinct colors that read on the dark
// theme background. A layer keeps its color across refreshes:
// the color is the hash of the layer name over this palette.
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

}  // namespace

RouteDiagram::RouteDiagram(SessionView& view, QWidget* parent)
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

void RouteDiagram::Refresh() {
  const RouteModel& model = view_.Route();
  if (!model.Loaded()) {
    counts_->setText("(no routing yet: run eos::route first)");
    RebuildSelector();
    RebuildScene();
    return;
  }
  std::string text = "Die: " + RectText(model.DieArea()) + "\n" +
                     "Core: " + RectText(model.CoreArea()) + "\n" +
                     "Routed: " + std::to_string(model.Routed()) + " / " +
                     std::to_string(model.Total()) + "\n" +
                     "Guides: " + std::to_string(model.Guides()) + "\n" +
                     "Overflow: " + std::to_string(model.Overflow()) +
                     "\n" +
                     "Violations: " + std::to_string(model.Violations()) +
                     (model.StoppedAtBound() ? " (stopped at bound)"
                                             : " (completed)") +
                     "\n" +
                     "Detailed iterations: " +
                     std::to_string(model.DetailedIterations()) + " / " +
                     std::to_string(model.DetailedBound()) + "\n" +
                     "Liberty corner: " + model.LibertyCorner();
  if (model.Slacks().empty()) {
    if (!model.StaNote().empty()) {
      text += "\n" + model.StaNote();
    } else {
      text += "\nPost-route slack: none measured";
    }
  } else {
    for (const RouteSlack& slack : model.Slacks()) {
      text += "\n" + slack.scene + " worst slack: " +
              QString::number(slack.worst_slack_ns, 'g', 10).toStdString() +
              " ns";
    }
  }
  text += "\nVias: " + std::to_string(model.Vias().size());
  text += "\nLayers:";
  for (const RouteLayer& layer : model.Layers()) {
    text += "\n" + layer.name + ": capacity " +
            std::to_string(layer.capacity) + " usage " +
            std::to_string(layer.usage) + " overflow " +
            std::to_string(layer.overflow);
    if (layer.overflow > 0) text += " OVERFLOW";
  }
  if (!model.UnwiredNets().empty()) {
    text += "\nUnwired nets:";
    for (const std::string& net : model.UnwiredNets()) {
      text += "\n" + net;
    }
  }
  counts_->setText(QString::fromStdString(text));
  RebuildSelector();
  RebuildScene();
}

std::string RouteDiagram::CountsText() const {
  return counts_->text().toStdString();
}

std::string RouteDiagram::SelectedLayer() const {
  return selected_layer_;
}

void RouteDiagram::SelectLayer(const std::string& layer) {
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

void RouteDiagram::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  FitView();
}

void RouteDiagram::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  FitView();
}

bool RouteDiagram::eventFilter(QObject* watched, QEvent* event) {
  if (watched == view_widget_ && event->type() == QEvent::Resize) {
    FitView();
  }
  return QWidget::eventFilter(watched, event);
}

void RouteDiagram::FitView() {
  // No fit for an empty route: the placeholder is one text
  // item, and fitting it would scale that text to fill the
  // view. RebuildScene sets the scene rect for real routes.
  if (!view_.Route().Loaded()) return;
  view_widget_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
}

void RouteDiagram::RebuildSelector() {
  const QSignalBlocker block(layer_selector_);
  const std::string kept = selected_layer_;
  layer_selector_->clear();
  layer_selector_->addItem("All layers");
  const RouteModel& model = view_.Route();
  if (model.Loaded()) {
    for (const RouteLayer& layer : model.Layers()) {
      layer_selector_->addItem(QString::fromStdString(layer.name));
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

void RouteDiagram::RebuildScene() {
  scene_->clear();
  const RouteModel& model = view_.Route();
  if (!model.Loaded()) {
    auto* placeholder = scene_->addSimpleText(
        "(no routing yet: run eos::route first)");
    placeholder->setBrush(QBrush(Qt::white));
    scene_->setSceneRect(placeholder->boundingRect());
    return;
  }
  const QRectF die = ToScene(model.DieArea());
  const QRectF core = ToScene(model.CoreArea());
  // Die first, heavier: the frame of the canvas.
  auto* die_item = scene_->addRect(die);
  die_item->setPen(QPen(Qt::white, 2));
  die_item->setBrush(Qt::NoBrush);
  auto* core_item = scene_->addRect(core);
  core_item->setPen(QPen(Qt::white, 1));
  core_item->setBrush(Qt::NoBrush);
  // One faint rectangle per placed instance, from the
  // already-loaded placement model: the route payload does
  // not duplicate instance geometry. Drawn nothing when the
  // placement model is empty.
  const PlacementModel& placement = view_.Placement();
  if (placement.Loaded()) {
    const QColor instance_color(160, 160, 160);
    for (const PlacementInstance& instance : placement.Instances()) {
      const QRectF rect = ToScene(instance.rect);
      if (rect.width() <= 0 || rect.height() <= 0) continue;
      auto* item = scene_->addRect(rect);
      item->setPen(QPen(instance_color, 1));
      item->setBrush(Qt::NoBrush);
      item->setToolTip(QString::fromStdString(
          instance.name + " (" + instance.master + ")"));
    }
  }
  // One polyline per wire segment, in its layer's color, at
  // its recorded width; one small square per via, in its
  // upper layer's color. The selector filters both to the
  // chosen layer (the outlines and instance rectangles stay).
  const bool all_layers = selected_layer_.empty();
  for (const RouteSegment& segment : model.Segments()) {
    if (!all_layers && segment.layer != selected_layer_) continue;
    QPolygonF polyline;
    for (const auto& point : segment.points) {
      polyline << ToScenePoint(point.first, point.second);
    }
    if (polyline.size() < 2) continue;
    auto* item = scene_->addPolygon(polyline);
    item->setPen(QPen(LayerColor(segment.layer), segment.width));
    item->setBrush(Qt::NoBrush);
    item->setToolTip(QString::fromStdString(segment.net + " (" +
                                            segment.layer + ")"));
  }
  // Via squares are sized from the die height, so they read
  // at any zoom: one two-hundredth of the die height, at
  // least 1 database unit.
  const int die_height = model.DieArea()[3] - model.DieArea()[1];
  const qreal via_size = std::max<qreal>(1, die_height / 200.0);
  for (const RouteViaPoint& via : model.Vias()) {
    if (!all_layers && via.lower_layer != selected_layer_ &&
        via.upper_layer != selected_layer_) {
      continue;
    }
    const QPointF at = ToScenePoint(via.x, via.y);
    auto* item = scene_->addRect(
        QRectF(at.x() - via_size / 2, at.y() - via_size / 2, via_size,
               via_size));
    const QColor color = LayerColor(via.upper_layer);
    item->setPen(QPen(color, 1));
    QColor fill = color;
    fill.setAlpha(128);
    item->setBrush(QBrush(fill));
    item->setToolTip(QString::fromStdString(
        via.net + " (" + via.lower_layer + "-" + via.upper_layer + ")"));
  }
  scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-20, -20, 20, 20));
  FitView();
}

}  // namespace eos::ui
