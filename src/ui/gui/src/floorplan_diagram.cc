// gui: the floorplan canvas widget (Qt only).
#include "floorplan_diagram.h"

#include <QBrush>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPen>
#include <QResizeEvent>
#include <QShowEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <string>

#include "eos/ui/floorplan.h"
#include "eos/ui/gui.h"

namespace eos::ui {

namespace {

// The scene rectangle of a database rectangle: the Y axis is
// flipped so the database's north stays at the top of the
// canvas (see the header).
QRectF ToScene(const FloorplanRect& rect) {
  return QRectF(rect[0], -rect[3], rect[2] - rect[0], rect[3] - rect[1]);
}

std::string RectText(const FloorplanRect& rect) {
  return std::to_string(rect[0]) + "," + std::to_string(rect[1]) + "," +
         std::to_string(rect[2]) + "," + std::to_string(rect[3]);
}

}  // namespace

FloorplanDiagram::FloorplanDiagram(SessionView& view, QWidget* parent)
    : QWidget(parent), view_(view) {
  auto* layout = new QHBoxLayout(this);
  scene_ = new QGraphicsScene(this);
  view_widget_ = new QGraphicsView(scene_, this);
  view_widget_->setRenderHint(QPainter::Antialiasing);
  view_widget_->installEventFilter(this);
  layout->addWidget(view_widget_, 1);
  auto* counts_side = new QWidget(this);
  auto* counts_layout = new QVBoxLayout(counts_side);
  counts_ = new QLabel(counts_side);
  counts_->setWordWrap(true);
  counts_layout->addWidget(counts_);
  counts_layout->addStretch();
  layout->addWidget(counts_side);
}

void FloorplanDiagram::Refresh() {
  const FloorplanModel& model = view_.Floorplan();
  if (!model.Loaded()) {
    counts_->setText(
        "(no floorplan yet: run eos::floorplan first)");
  } else {
    counts_->setText(QString::fromStdString(
        "Die: " + RectText(model.DieArea()) + "\n" +
        "Core: " + RectText(model.CoreArea()) + "\n" +
        "Rows: " + std::to_string(model.Rows().size()) + "\n" +
        "Track grids: " + std::to_string(model.TrackGridCount()) +
        "\n" + "IO placed: " + std::to_string(model.IoPlaced()) +
        " / " + std::to_string(model.IoTotal())));
  }
  RebuildScene();
}

std::string FloorplanDiagram::CountsText() const {
  return counts_->text().toStdString();
}

void FloorplanDiagram::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  FitView();
}

void FloorplanDiagram::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  FitView();
}

bool FloorplanDiagram::eventFilter(QObject* watched, QEvent* event) {
  if (watched == view_widget_ && event->type() == QEvent::Resize) {
    FitView();
  }
  return QWidget::eventFilter(watched, event);
}

void FloorplanDiagram::FitView() {
  // No fit for an empty floorplan: the placeholder is one text
  // item, and fitting it would scale that text to fill the
  // view. RebuildScene sets the scene rect for real
  // floorplans.
  if (!view_.Floorplan().Loaded()) return;
  view_widget_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
}

void FloorplanDiagram::RebuildScene() {
  scene_->clear();
  const FloorplanModel& model = view_.Floorplan();
  if (!model.Loaded()) {
    auto* placeholder = scene_->addSimpleText(
        "(no floorplan yet: run eos::floorplan first)");
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
  // Rows: grey outlines, so the die and core stay dominant.
  for (const FloorplanRect& row : model.Rows()) {
    auto* row_item = scene_->addRect(ToScene(row));
    row_item->setPen(QPen(Qt::gray, 1));
    row_item->setBrush(Qt::NoBrush);
  }
  // Pin labels scale with the die, so a label reads the same
  // on a 5 micron demo die and a production die: one
  // thirtieth of the die height.
  const int die_height = model.DieArea()[3] - model.DieArea()[1];
  QFont label_font;
  label_font.setPixelSize(std::max(1, die_height / 30));
  const QFontMetrics label_metrics(label_font);
  for (const FloorplanPin& pin : model.Pins()) {
    if (!pin.placed) continue;
    const QRectF rect = ToScene(pin.rect);
    // A placed pin with no geometry has no marker: the counts
    // column counts it instead.
    if (rect.width() <= 0 || rect.height() <= 0) continue;
    auto* pin_item = scene_->addRect(rect);
    pin_item->setPen(QPen(Qt::white, 1));
    pin_item->setBrush(Qt::NoBrush);
    auto* hover = scene_->addRect(rect);
    hover->setPen(Qt::NoPen);
    hover->setBrush(Qt::NoBrush);
    hover->setToolTip(QString::fromStdString(
        pin.name + " (" + pin.direction + "), placed"));
    // The label sits just inside the die from the nearest
    // edge, so it never covers the die outline.
    const qreal left_gap = rect.left() - die.left();
    const qreal right_gap = die.right() - rect.right();
    const qreal top_gap = rect.top() - die.top();
    const qreal bottom_gap = die.bottom() - rect.bottom();
    const int text_width =
        label_metrics.horizontalAdvance(QString::fromStdString(pin.name));
    const int text_height = label_metrics.height();
    auto* label = scene_->addSimpleText(QString::fromStdString(pin.name));
    label->setFont(label_font);
    label->setBrush(QBrush(Qt::white));
    const qreal gap = std::max(1, die_height / 100);
    if (left_gap <= right_gap && left_gap <= top_gap &&
        left_gap <= bottom_gap) {
      label->setPos(rect.right() + gap,
                    rect.center().y() - text_height / 2);
    } else if (right_gap <= top_gap && right_gap <= bottom_gap) {
      label->setPos(rect.left() - gap - text_width,
                    rect.center().y() - text_height / 2);
    } else if (top_gap <= bottom_gap) {
      label->setPos(rect.center().x() - text_width / 2,
                    rect.bottom() + gap);
    } else {
      label->setPos(rect.center().x() - text_width / 2,
                    rect.top() - gap - text_height);
    }
  }
  scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-20, -20, 20, 20));
  FitView();
}

}  // namespace eos::ui
