// gui: the gate-level schematic widget (Qt only).
#include "schematic_diagram.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPen>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <utility>

#include "eos/ui/diagram.h"
#include "eos/ui/gui.h"
#include "eos/ui/schematic.h"

namespace eos::ui {

namespace {

// Layout: gates in one center column, input terminals left,
// output terminals right. All positions derive from the model
// order, so the same level always renders the same way. The
// geometry matches the block diagram, so ascending from a
// schematic to its parent diagram keeps the same frame.
constexpr qreal kGateTop = 40;
constexpr qreal kGateHeight = 64;
constexpr qreal kGateGap = 48;
constexpr qreal kGateX = 300;
constexpr qreal kMinGateWidth = 180;
constexpr qreal kInputX = 40;
constexpr qreal kOutputX = 640;
constexpr qreal kTerminalSize = 16;
constexpr qreal kStubLength = 28;

struct Anchor {
  QPointF point;
  bool left_side;
  bool is_output;
};

std::string LastComponent(const std::string& path) {
  const std::size_t slash = path.rfind('/');
  if (slash == std::string::npos) return path;
  return path.substr(slash + 1);
}

}  // namespace

SchematicDiagram::SchematicDiagram(SessionView& view, QWidget* parent)
    : QWidget(parent), view_(view) {
  auto* layout = new QVBoxLayout(this);
  crumbs_ = new QWidget(this);
  crumbs_layout_ = new QHBoxLayout(crumbs_);
  crumbs_layout_->addStretch();
  layout->addWidget(crumbs_);
  scene_ = new QGraphicsScene(this);
  view_widget_ = new QGraphicsView(scene_, this);
  view_widget_->setRenderHint(QPainter::Antialiasing);
  view_widget_->installEventFilter(this);
  layout->addWidget(view_widget_, 1);
}

void SchematicDiagram::Refresh() {
  RebuildBreadcrumb();
  RebuildScene();
}

void SchematicDiagram::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  FitView();
}

void SchematicDiagram::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  FitView();
}

bool SchematicDiagram::eventFilter(QObject* watched, QEvent* event) {
  if (watched == view_widget_ && event->type() == QEvent::Resize) {
    FitView();
  }
  return QWidget::eventFilter(watched, event);
}

void SchematicDiagram::FitView() {
  // No fit for an empty schematic: the placeholder is one text
  // item, and fitting it would scale that text to fill the
  // view. RebuildScene sets the scene rect for real schematics.
  if (view_.Schematic().Empty()) return;
  view_widget_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
}

void SchematicDiagram::RebuildBreadcrumb() {
  QLayoutItem* item = nullptr;
  while ((item = crumbs_layout_->takeAt(0)) != nullptr) {
    delete item->widget();
    delete item;
  }
  const std::vector<std::string> crumbs =
      BreadcrumbPaths(view_.DiagramLevel());
  for (std::size_t i = 0; i < crumbs.size(); ++i) {
    if (i > 0) {
      crumbs_layout_->addWidget(new QLabel("/", crumbs_));
    }
    if (i + 1 < crumbs.size()) {
      auto* button = new QPushButton(
          QString::fromStdString(LastComponent(crumbs[i])), crumbs_);
      const std::string path = crumbs[i];
      connect(button, &QPushButton::clicked,
              [this, path]() { OnCrumbClicked(path); });
      crumbs_layout_->addWidget(button);
    } else {
      auto* label = new QLabel(
          QString::fromStdString(LastComponent(crumbs[i])), crumbs_);
      label->setStyleSheet("font-weight: bold;");
      crumbs_layout_->addWidget(label);
    }
  }
  crumbs_layout_->addStretch();
}

void SchematicDiagram::RebuildScene() {
  scene_->clear();
  const SchematicModel& schematic = view_.Schematic();
  if (schematic.Empty()) {
    std::string text = "No schematic for this level.";
    if (!view_.context().synth.elaborated) {
      text = "No design loaded.";
    } else if (view_.Hierarchy().Empty()) {
      text = "No hierarchy.";
    }
    scene_->addSimpleText(QString::fromStdString(text));
    scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-20, -20, 20, 20));
    return;
  }

  // Gate width fits the longest label, so names never overflow.
  QFont instance_font;
  instance_font.setBold(true);
  QFontMetrics instance_metrics(instance_font);
  QFont type_font;
  QFontMetrics type_metrics(type_font);
  qreal gate_width = kMinGateWidth;
  for (const SchematicGate& gate : schematic.Gates()) {
    const qreal instance_w = instance_metrics.horizontalAdvance(
        QString::fromStdString(gate.instance));
    const qreal type_w = type_metrics.horizontalAdvance(
        QString::fromStdString(gate.type));
    gate_width = std::max(gate_width, std::max(instance_w, type_w) + 20);
  }

  // Gate rectangles, one row each, in model order. Gates are
  // not clickable: the schematic is the bottom of the stack.
  std::map<std::string, QRectF> gate_rects;
  for (std::size_t i = 0; i < schematic.Gates().size(); ++i) {
    const SchematicGate& gate = schematic.Gates()[i];
    const qreal y = kGateTop + static_cast<qreal>(i) *
                                    (kGateHeight + kGateGap);
    const QRectF rect(kGateX, y, gate_width, kGateHeight);
    gate_rects[gate.instance] = rect;
    auto* item = scene_->addRect(rect);
    item->setPen(QPen(Qt::black, 1));
    item->setBrush(QBrush(Qt::white));
    item->setToolTip(QString::fromStdString(gate.instance + " (" +
                                                  gate.type + ")"));
    auto* instance_text = scene_->addSimpleText(
        QString::fromStdString(gate.instance));
    instance_text->setFont(instance_font);
    instance_text->setPos(kGateX + 10, y + 8);
    auto* type_text = scene_->addSimpleText(
        QString::fromStdString(gate.type));
    type_text->setFont(type_font);
    type_text->setPos(kGateX + 10, y + 34);
  }

  // Terminals: input ports left, output and inout ports right,
  // each list in declaration order.
  std::map<std::string, Anchor> terminal_anchors;
  std::size_t input_row = 0;
  std::size_t output_row = 0;
  for (const SchematicPort& port : schematic.ParentPorts()) {
    const bool input_side =
        port.direction == "in" || port.direction == "inout";
    const std::size_t row = input_side ? input_row++ : output_row++;
    const qreal y = kGateTop + static_cast<qreal>(row) *
                                    (kGateHeight + kGateGap);
    const qreal x = input_side ? kInputX : kOutputX;
    auto* square = scene_->addRect(
        QRectF(x, y, kTerminalSize, kTerminalSize));
    square->setPen(QPen(Qt::black, 1));
    square->setBrush(QBrush(Qt::white));
    auto* label = scene_->addSimpleText(QString::fromStdString(port.name));
    if (input_side) {
      label->setPos(x + kTerminalSize + 8, y);
    } else {
      const qreal width = type_metrics.horizontalAdvance(
          QString::fromStdString(port.name));
      label->setPos(x - 8 - width, y);
    }
    terminal_anchors[port.name] = Anchor{
        QPointF(x + kTerminalSize / 2, y + kTerminalSize / 2),
        input_side, port.direction == "out"};
  }

  // Port anchors along gate edges: input and inout ports on the
  // left edge, output ports on the right, spread by port index
  // among the ports on the same side.
  auto gate_anchor = [&](const std::string& instance,
                         const std::string& port_name) -> Anchor {
    const SchematicGate* gate = schematic.FindGate(instance);
    const QRectF rect = gate_rects[instance];
    bool left_side = true;
    bool is_output = false;
    if (gate != nullptr) {
      for (const SchematicPort& port : gate->ports) {
        if (port.name == port_name) {
          left_side = port.direction == "in" ||
                      port.direction == "inout";
          is_output = port.direction == "out";
        }
      }
    }
    std::vector<const SchematicPort*> side_ports;
    if (gate != nullptr) {
      for (const SchematicPort& port : gate->ports) {
        const bool input_side =
            port.direction == "in" || port.direction == "inout";
        if (input_side == left_side) side_ports.push_back(&port);
      }
    }
    std::size_t index = 0;
    for (std::size_t i = 0; i < side_ports.size(); ++i) {
      if (side_ports[i]->name == port_name) {
        index = i;
        break;
      }
    }
    const qreal n = static_cast<qreal>(side_ports.size());
    const qreal y = rect.top() +
                    (static_cast<qreal>(index) + 1) * rect.height() /
                        (n + 1);
    const qreal x = left_side ? rect.left() : rect.right();
    return Anchor{QPointF(x, y), left_side, is_output};
  };

  // Nets: a star from the driving output endpoint when one
  // exists, else from the first anchor; a lone anchor gets a
  // short stub outward. Labels sit on the first segment.
  // Structural only: no timing annotation is drawn.
  for (const SchematicNet& net : schematic.Nets()) {
    std::vector<Anchor> anchors;
    for (const SchematicNetEndpoint& endpoint : net.endpoints) {
      anchors.push_back(gate_anchor(endpoint.gate, endpoint.port));
    }
    for (const std::string& terminal : net.terminals) {
      const auto found = terminal_anchors.find(terminal);
      if (found != terminal_anchors.end()) anchors.push_back(found->second);
    }
    if (anchors.empty()) continue;
    if (anchors.size() == 1) {
      const Anchor& anchor = anchors[0];
      const QPointF end(anchor.point.x() +
                            (anchor.left_side ? -kStubLength : kStubLength),
                        anchor.point.y());
      scene_->addLine(QLineF(anchor.point, end));
      auto* label = scene_->addSimpleText(
          QString::fromStdString(net.name));
      label->setPos(end.x() + (anchor.left_side ? -4 : 4) -
                        (anchor.left_side
                             ? type_metrics.horizontalAdvance(
                                   QString::fromStdString(net.name))
                             : 0),
                    end.y() - 18);
      continue;
    }
    std::size_t center = 0;
    for (std::size_t i = 0; i < anchors.size(); ++i) {
      if (anchors[i].is_output) {
        center = i;
        break;
      }
    }
    for (std::size_t i = 0; i < anchors.size(); ++i) {
      if (i == center) continue;
      scene_->addLine(QLineF(anchors[center].point, anchors[i].point));
    }
    std::size_t other = center == 0 ? 1 : 0;
    const QPointF mid((anchors[center].point.x() + anchors[other].point.x()) / 2,
                      (anchors[center].point.y() + anchors[other].point.y()) / 2);
    auto* label = scene_->addSimpleText(QString::fromStdString(net.name));
    label->setPos(mid.x() + 4, mid.y() - 18);
  }

  scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-20, -20, 20, 20));
  FitView();
}

void SchematicDiagram::OnCrumbClicked(const std::string& path) {
  if (view_.SelectHierarchyPath(path)) {
    view_.SetDiagramLevel(path);
    if (on_action_) on_action_();
  }
}

}  // namespace eos::ui
