// gui: the placement canvas widget (Qt only).
#include "placement_diagram.h"

#include <QBrush>
#include <QColor>
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
#include <functional>
#include <map>
#include <string>

#include "eos/ui/gui.h"
#include "eos/ui/placement.h"

namespace eos::ui {

namespace {

// The scene rectangle of a database rectangle: the Y axis is
// flipped so the database's north stays at the top of the
// canvas (see the header).
QRectF ToScene(const PlacementRect& rect) {
  return QRectF(rect[0], -rect[3], rect[2] - rect[0], rect[3] - rect[1]);
}

std::string RectText(const PlacementRect& rect) {
  return std::to_string(rect[0]) + "," + std::to_string(rect[1]) + "," +
         std::to_string(rect[2]) + "," + std::to_string(rect[3]);
}

// The master palette: distinct colors that read on the dark
// theme background. A master keeps its color across refreshes:
// the color is the hash of the master name over this palette.
const QColor kMasterPalette[] = {
    Qt::cyan,    Qt::yellow,   Qt::green,    Qt::magenta,
    Qt::red,     Qt::white,    QColor(255, 165, 0),
    QColor(100, 149, 237),
};

QColor MasterColor(const std::string& master) {
  const std::size_t palette_size =
      sizeof(kMasterPalette) / sizeof(kMasterPalette[0]);
  return kMasterPalette[std::hash<std::string>{}(master) % palette_size];
}

}  // namespace

PlacementDiagram::PlacementDiagram(SessionView& view, QWidget* parent)
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

void PlacementDiagram::Refresh() {
  const PlacementModel& model = view_.Placement();
  if (!model.Loaded()) {
    counts_->setText("(no placement yet: run eos::place first)");
  } else {
    std::string text = "Die: " + RectText(model.DieArea()) + "\n" +
                       "Core: " + RectText(model.CoreArea()) + "\n" +
                       "Placed: " + std::to_string(model.Placed()) +
                       " / " + std::to_string(model.Total()) + "\n" +
                       "HPWL: " + std::to_string(model.Hpwl()) + "\n" +
                       "Overlaps: " + std::to_string(model.Overlaps());
    if (model.Slacks().empty()) {
      text += "\nPost-place slack: none measured";
    } else {
      for (const PlacementSlack& slack : model.Slacks()) {
        text += "\n" + slack.scene + " worst slack: " +
                QString::number(slack.worst_slack_ns, 'g', 10)
                    .toStdString() +
                " ns";
      }
    }
    // The master legend, sorted by master name so the column
    // reads the same every refresh.
    std::map<std::string, std::size_t> by_master;
    for (const PlacementInstance& instance : model.Instances()) {
      ++by_master[instance.master];
    }
    text += "\nMasters:";
    for (const auto& entry : by_master) {
      text += "\n" + entry.first + ": " +
              std::to_string(entry.second);
    }
    counts_->setText(QString::fromStdString(text));
  }
  RebuildScene();
}

std::string PlacementDiagram::CountsText() const {
  return counts_->text().toStdString();
}

void PlacementDiagram::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  FitView();
}

void PlacementDiagram::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  FitView();
}

bool PlacementDiagram::eventFilter(QObject* watched, QEvent* event) {
  if (watched == view_widget_ && event->type() == QEvent::Resize) {
    FitView();
  }
  return QWidget::eventFilter(watched, event);
}

void PlacementDiagram::FitView() {
  // No fit for an empty placement: the placeholder is one text
  // item, and fitting it would scale that text to fill the
  // view. RebuildScene sets the scene rect for real
  // placements.
  if (!view_.Placement().Loaded()) return;
  view_widget_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
}

void PlacementDiagram::RebuildScene() {
  scene_->clear();
  const PlacementModel& model = view_.Placement();
  if (!model.Loaded()) {
    auto* placeholder = scene_->addSimpleText(
        "(no placement yet: run eos::place first)");
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
  // Instance labels scale with the die, so a label reads the
  // same on a 5 micron demo die and a production die: one
  // thirtieth of the die height.
  const int die_height = model.DieArea()[3] - model.DieArea()[1];
  QFont label_font;
  label_font.setPixelSize(std::max(1, die_height / 30));
  const QFontMetrics label_metrics(label_font);
  for (const PlacementInstance& instance : model.Instances()) {
    const QRectF rect = ToScene(instance.rect);
    if (rect.width() <= 0 || rect.height() <= 0) continue;
    const QColor color = MasterColor(instance.master);
    auto* item = scene_->addRect(rect);
    item->setPen(QPen(color, 1));
    QColor fill = color;
    fill.setAlpha(64);
    item->setBrush(QBrush(fill));
    auto* hover = scene_->addRect(rect);
    hover->setPen(Qt::NoPen);
    hover->setBrush(Qt::NoBrush);
    hover->setToolTip(QString::fromStdString(
        instance.name + " (" + instance.master + ")"));
    // The label sits just inside the die from the nearest
    // edge, so it never covers the die outline.
    const qreal left_gap = rect.left() - die.left();
    const qreal right_gap = die.right() - rect.right();
    const qreal top_gap = rect.top() - die.top();
    const qreal bottom_gap = die.bottom() - rect.bottom();
    const int text_width = label_metrics.horizontalAdvance(
        QString::fromStdString(instance.name));
    const int text_height = label_metrics.height();
    auto* label =
        scene_->addSimpleText(QString::fromStdString(instance.name));
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
