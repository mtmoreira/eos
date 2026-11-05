// gui: the gate-level schematic widget (Qt only).
#include "schematic_diagram.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainterPath>
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
// Inversion bubble radius: NAND, NOR, XNOR, and NOT draw a
// bubble of this radius past the output edge, and output
// anchors shift by twice it so wires touch the bubble.
constexpr qreal kBubbleRadius = 6;
// OR-family geometry: the input-side curve sits kOrInset
// inside the bounding rect and bulges kOrBulge toward the
// output at mid-height. The XOR extra curve is the same curve
// shifted kOrInset left, so it sits on the bounding edge.
constexpr qreal kOrInset = 16;
constexpr qreal kOrBulge = 14;

struct Anchor {
  QPointF point;
  bool left_side;
  bool is_output;
};

// True for the symbols that carry an inversion bubble.
bool IsInverted(GateSymbol symbol) {
  return symbol == GateSymbol::kNand || symbol == GateSymbol::kNor ||
         symbol == GateSymbol::kXnor || symbol == GateSymbol::kNot;
}

// X of the OR-family input-side curve at height y within rect:
// the quadratic Bezier from (base, top) to (base, bottom)
// with control (base + 2*bulge, middle). Base is left + inset
// for OR/NOR and left for the XOR/XNOR extra curve.
qreal LeftCurveX(qreal base, const QRectF& rect, qreal y) {
  const qreal t = (y - rect.top()) / rect.height();
  return base + 4 * kOrBulge * (1 - t) * t;
}

// Draws one gate symbol inside rect, filled white with a black
// outline. The rect is the gate's bounding box: anchors and
// labels use it, and every shape stays inside it except the
// inversion bubble, which sits past the output edge.
void DrawGateShape(QGraphicsScene* scene, const QRectF& rect,
                   GateSymbol symbol) {
  const qreal left = rect.left();
  const qreal right = rect.right();
  const qreal top = rect.top();
  const qreal bottom = rect.bottom();
  const qreal middle = top + rect.height() / 2;
  const QPen pen(Qt::black, 1);
  const QBrush brush(Qt::white);
  auto add_shape = [&](const QPainterPath& path) {
    auto* item = scene->addPath(path, pen, brush);
    Q_UNUSED(item);
  };
  switch (symbol) {
    case GateSymbol::kGeneric: {
      auto* item = scene->addRect(rect);
      item->setPen(pen);
      item->setBrush(brush);
      break;
    }
    case GateSymbol::kAnd:
    case GateSymbol::kNand: {
      QPainterPath path;
      path.moveTo(left, top);
      path.lineTo(right - rect.height() / 2, top);
      path.arcTo(right - rect.height(), top, rect.height(),
                 rect.height(), 90, -180);
      path.lineTo(left, bottom);
      path.closeSubpath();
      add_shape(path);
      break;
    }
    case GateSymbol::kOr:
    case GateSymbol::kNor:
    case GateSymbol::kXor:
    case GateSymbol::kXnor: {
      QPainterPath path;
      path.moveTo(left + kOrInset, top);
      path.quadTo(left + rect.width() * 0.55, top, right, middle);
      path.quadTo(left + rect.width() * 0.55, bottom,
                  left + kOrInset, bottom);
      path.quadTo(left + kOrInset + 2 * kOrBulge, middle,
                  left + kOrInset, top);
      add_shape(path);
      if (symbol == GateSymbol::kXor ||
          symbol == GateSymbol::kXnor) {
        QPainterPath extra;
        extra.moveTo(left, top);
        extra.quadTo(left + 2 * kOrBulge, middle, left, bottom);
        auto* item = scene->addPath(extra, pen);
        Q_UNUSED(item);
      }
      break;
    }
    case GateSymbol::kNot:
    case GateSymbol::kBuf: {
      QPainterPath path;
      path.moveTo(left, top);
      path.lineTo(left, bottom);
      path.lineTo(right, middle);
      path.closeSubpath();
      add_shape(path);
      break;
    }
  }
  if (IsInverted(symbol)) {
    auto* bubble = scene->addEllipse(
        QRectF(right, middle - kBubbleRadius, 2 * kBubbleRadius,
               2 * kBubbleRadius));
    bubble->setPen(pen);
    bubble->setBrush(brush);
  }
}

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

  // Gate width fits the longest display label, so names never
  // overflow. Display labels only: the raw instance can hold
  // a synth temp path, which must never size or label a box.
  QFont instance_font;
  instance_font.setBold(true);
  QFontMetrics instance_metrics(instance_font);
  QFont type_font;
  QFontMetrics type_metrics(type_font);
  qreal gate_width = kMinGateWidth;
  for (const SchematicGate& gate : schematic.Gates()) {
    const qreal instance_w = instance_metrics.horizontalAdvance(
        QString::fromStdString(DisplayInstance(gate.instance)));
    const qreal type_w = type_metrics.horizontalAdvance(
        QString::fromStdString(DisplayType(gate.type)));
    gate_width = std::max(gate_width, std::max(instance_w, type_w) + 20);
  }

  // Gate symbols, one row each, in model order. Gates are
  // not clickable: the schematic is the bottom of the stack.
  // Labels are display labels, centered in the bounding rect;
  // a gate with no display instance shows its type alone.
  std::map<std::string, QRectF> gate_rects;
  for (std::size_t i = 0; i < schematic.Gates().size(); ++i) {
    const SchematicGate& gate = schematic.Gates()[i];
    const qreal y = kGateTop + static_cast<qreal>(i) *
                                    (kGateHeight + kGateGap);
    const QRectF rect(kGateX, y, gate_width, kGateHeight);
    gate_rects[gate.instance] = rect;
    DrawGateShape(scene_, rect, SymbolForType(gate.type));
    const std::string instance_label = DisplayInstance(gate.instance);
    const std::string type_label = DisplayType(gate.type);
    const QString tooltip =
        instance_label.empty()
            ? QString::fromStdString(type_label)
            : QString::fromStdString(instance_label + " (" +
                                     type_label + ")");
    // The tooltip belongs to the shape, but shapes are several
    // items; attach it to a transparent rect over the bounds
    // so hovering anywhere on the gate shows it.
    auto* hover = scene_->addRect(rect);
    hover->setPen(Qt::NoPen);
    hover->setBrush(Qt::NoBrush);
    hover->setToolTip(tooltip);
    auto center_x = [&](const QFontMetrics& metrics,
                        const std::string& text) {
      return kGateX + (gate_width -
                       metrics.horizontalAdvance(
                           QString::fromStdString(text))) /
                          2;
    };
    if (!instance_label.empty()) {
      auto* instance_text = scene_->addSimpleText(
          QString::fromStdString(instance_label));
      instance_text->setFont(instance_font);
      instance_text->setPos(center_x(instance_metrics, instance_label),
                            y + 8);
      auto* type_text = scene_->addSimpleText(
          QString::fromStdString(type_label));
      type_text->setFont(type_font);
      type_text->setPos(center_x(type_metrics, type_label), y + 34);
    } else {
      auto* type_text = scene_->addSimpleText(
          QString::fromStdString(type_label));
      type_text->setFont(type_font);
      type_text->setPos(center_x(type_metrics, type_label), y + 22);
    }
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
    qreal x = left_side ? rect.left() : rect.right();
    // Shape-aware endpoints: input wires touch the OR-family
    // curves, not the bounding edge, and output wires touch
    // the inversion bubble, not the shape edge.
    if (gate != nullptr) {
      const GateSymbol symbol = SymbolForType(gate->type);
      if (left_side) {
        if (symbol == GateSymbol::kOr ||
            symbol == GateSymbol::kNor) {
          x = LeftCurveX(rect.left() + kOrInset, rect, y);
        } else if (symbol == GateSymbol::kXor ||
                   symbol == GateSymbol::kXnor) {
          x = LeftCurveX(rect.left(), rect, y);
        }
      } else if (IsInverted(symbol)) {
        x = rect.right() + 2 * kBubbleRadius;
      }
    }
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
