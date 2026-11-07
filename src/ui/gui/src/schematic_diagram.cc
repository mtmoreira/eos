// gui: the gate-level schematic widget (Qt only).
#include "schematic_diagram.h"

#include <QBrush>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include "eos/ui/diagram.h"
#include "eos/ui/gui.h"
#include "eos/ui/schematic.h"

namespace eos::ui {

namespace {

// Layout geometry comes from the model (schematic.h), so the
// widget and the layout agree on positions. Only shape-only
// constants live here.
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

// Draws one gate symbol inside rect. The rect is the gate's
// bounding box: anchors and labels use it, and every shape
// stays inside it except the inversion bubble, which sits past
// the output edge. The schematic sits on a dark background, so
// every shape is a white contour with no fill: a solid white
// fill would be a white box on black. A dimmed gate (outside
// the selected cone) outlines in grey; a selected gate
// outlines in blue. control_y is the scene y of the
// flip-flop clock or latch enable anchor, or -1 when the gate
// has none: the control triangle straddles the left edge
// there, apex pointing into the box.
void DrawGateShape(QGraphicsScene* scene, const QRectF& rect,
                   GateSymbol symbol, bool dimmed, bool selected,
                   double control_y = -1) {
  const qreal left = rect.left();
  const qreal right = rect.right();
  const qreal top = rect.top();
  const qreal bottom = rect.bottom();
  const qreal middle = top + rect.height() / 2;
  const QPen pen(selected ? Qt::blue : (dimmed ? Qt::gray : Qt::white),
                 selected ? 2 : 1);
  auto add_shape = [&](const QPainterPath& path) {
    auto* item = scene->addPath(path, pen, Qt::NoBrush);
    Q_UNUSED(item);
  };
  switch (symbol) {
    case GateSymbol::kGeneric: {
      auto* item = scene->addRect(rect);
      item->setPen(pen);
      item->setBrush(Qt::NoBrush);
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
    case GateSymbol::kMux: {
      // Trapezoid, wide side at the inputs: the left edge is
      // the full height, the right edge is a short centered
      // segment, so the output anchor at mid-height still
      // touches the shape and no anchor adjustment is needed.
      constexpr qreal kMuxHalfOutput = 12;
      QPainterPath path;
      path.moveTo(left, top);
      path.lineTo(right, middle - kMuxHalfOutput);
      path.lineTo(right, middle + kMuxHalfOutput);
      path.lineTo(left, bottom);
      path.closeSubpath();
      add_shape(path);
      break;
    }
    case GateSymbol::kDff:
    case GateSymbol::kLatch: {
      // The IEEE rectangular symbol: an outline rectangle,
      // with a small triangle on the control edge. The
      // triangle straddles the left edge at the clock (or
      // enable) anchor, apex pointing into the box.
      auto* item = scene->addRect(rect);
      item->setPen(pen);
      item->setBrush(Qt::NoBrush);
      if (control_y >= 0) {
        constexpr qreal kControlHalf = 6;
        constexpr qreal kControlDepth = 10;
        QPainterPath triangle;
        triangle.moveTo(left, control_y - kControlHalf);
        triangle.lineTo(left + kControlDepth, control_y);
        triangle.lineTo(left, control_y + kControlHalf);
        triangle.closeSubpath();
        add_shape(triangle);
      }
      break;
    }
  }
  if (IsInverted(symbol)) {
    auto* bubble = scene->addEllipse(
        QRectF(right, middle - kBubbleRadius, 2 * kBubbleRadius,
               2 * kBubbleRadius));
    bubble->setPen(pen);
    bubble->setBrush(Qt::NoBrush);
  }
}

// Draws one parent terminal as an arrowhead pointing in the
// signal direction (right, on both sides): the tip meets the
// wire on the input side and points outward on the output
// side. White contour, no fill, like the gates. The anchor is
// the point on the arrowhead where the net attaches: the tip
// for an input, the base center for an output. The color
// follows the cone: white with no selection, blue when the
// terminal is in the cone, grey when a selection is active
// and it is not.
void DrawTerminalArrow(QGraphicsScene* scene, const QPointF& anchor,
                       bool input_side, const QColor& color) {
  const qreal size = kSchematicTerminalSize;
  const qreal half = size / 2;
  QPainterPath path;
  if (input_side) {
    path.moveTo(anchor.x(), anchor.y());
    path.lineTo(anchor.x() - size, anchor.y() - half);
    path.lineTo(anchor.x() - size, anchor.y() + half);
  } else {
    path.moveTo(anchor.x(), anchor.y() - half);
    path.lineTo(anchor.x(), anchor.y() + half);
    path.lineTo(anchor.x() + size, anchor.y());
  }
  path.closeSubpath();
  scene->addPath(path, QPen(color, 1), Qt::NoBrush);
}

std::string LastComponent(const std::string& path) {
  const std::size_t slash = path.rfind('/');
  if (slash == std::string::npos) return path;
  return path.substr(slash + 1);
}

// True when the point lies on the axis-aligned segment from a
// to b. Branch dots sit on route segments, so this decides
// whether a dot belongs to a cone path.
bool PointOnSegment(const QPointF& point, const SchematicPoint& a,
                    const SchematicPoint& b) {
  const double x = point.x();
  const double y = point.y();
  if (a.x == b.x) {
    return x == a.x && y >= std::min(a.y, b.y) &&
           y <= std::max(a.y, b.y);
  }
  return y == a.y && x >= std::min(a.x, b.x) &&
         x <= std::max(a.x, b.x);
}

}  // namespace

SchematicDiagram::SchematicDiagram(SessionView& view, QWidget* parent)
    : QWidget(parent), view_(view) {
  auto* layout = new QVBoxLayout(this);
  // Netlist source: which netlist this schematic draws, so the
  // elaborated and generic views can never be mistaken for
  // each other. The session owns the fact (synth_generic
  // replaces the live design), and the module names the leaf.
  source_label_ = new QLabel(this);
  layout->addWidget(source_label_);
  crumbs_ = new QWidget(this);
  crumbs_layout_ = new QHBoxLayout(crumbs_);
  crumbs_layout_->addStretch();
  layout->addWidget(crumbs_);
  scene_ = new QGraphicsScene(this);
  view_widget_ = new QGraphicsView(scene_, this);
  view_widget_->setRenderHint(QPainter::Antialiasing);
  view_widget_->installEventFilter(this);
  // Mouse presses land on the viewport, not the view, so the
  // click filter lives there; the resize filter stays on the
  // view, which is what gets resize events.
  view_widget_->viewport()->installEventFilter(this);
  layout->addWidget(view_widget_, 1);
}

void SchematicDiagram::Refresh() {
  std::string source;
  if (!view_.context().synth.elaborated) {
    source = "No design loaded.";
  } else {
    source = view_.context().synth.generic_optimized ? "Generic netlist"
                                                      : "Elaborated netlist";
    const std::string& module = view_.Schematic().Module();
    if (!module.empty()) source += ": " + module;
  }
  source_label_->setText(QString::fromStdString(source));
  RebuildBreadcrumb();
  RebuildScene();
}

void SchematicDiagram::HighlightGate(const std::string& instance) {
  if (view_.Schematic().FindGate(instance) == nullptr) return;
  // The selection belongs to the current module: claim it
  // first, so the rebuild below does not clear the selection
  // as a stale gate from the previous module.
  selected_module_ = view_.Schematic().Module();
  if (instance != selected_) {
    selected_ = instance;
    RebuildScene();
  }
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
  if (watched == view_widget_->viewport() &&
      event->type() == QEvent::MouseButtonPress) {
    const auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() == Qt::LeftButton) {
      // Clicking a gate selects it and highlights its fanin
      // and fanout cones; clicking empty space clears the
      // highlight. The scene rebuilds only when the selection
      // changes.
      const QPointF scene_point = view_widget_->mapToScene(mouse->pos());
      std::string hit;
      for (const auto& entry : gate_rects_) {
        if (entry.second.contains(scene_point)) {
          hit = entry.first;
          break;
        }
      }
      if (hit != selected_) {
        selected_ = hit;
        RebuildScene();
      }
    }
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
    scene_->addSimpleText(QString::fromStdString(text))
        ->setBrush(QBrush(Qt::white));
    scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-20, -20, 20, 20));
    return;
  }

  // A selection belongs to one schematic: a new module clears
  // it, so a stale gate is never highlighted.
  if (schematic.Module() != selected_module_) {
    selected_.clear();
    selected_module_ = schematic.Module();
  }

  // Gate width fits the longest display label, so names never
  // overflow. Display labels only: the raw instance can hold
  // a synth temp path, which must never size or label a box.
  QFont instance_font;
  instance_font.setBold(true);
  QFontMetrics instance_metrics(instance_font);
  QFont type_font;
  QFontMetrics type_metrics(type_font);
  qreal gate_width = kSchematicMinGateWidth;
  for (const SchematicGate& gate : schematic.Gates()) {
    const qreal instance_w = instance_metrics.horizontalAdvance(
        QString::fromStdString(DisplayInstance(gate.instance)));
    const qreal type_w = type_metrics.horizontalAdvance(
        QString::fromStdString(DisplayType(gate.type)));
    gate_width = std::max(gate_width, std::max(instance_w, type_w) + 20);
  }

  const SchematicLayout layout =
      ComputeSchematicLayout(schematic, gate_width);

  // The cone highlight: the selected gate plus its transitive
  // fanin and fanout cones. Empty when nothing is selected,
  // and then every gate and net draws normally.
  std::set<std::string> cone;
  if (!selected_.empty()) {
    cone.insert(selected_);
    for (const std::string& gate : FaninCone(schematic, selected_)) {
      cone.insert(gate);
    }
    for (const std::string& gate : FanoutCone(schematic, selected_)) {
      cone.insert(gate);
    }
  }
  const bool highlighting = !selected_.empty();

  // The cone paths: the net paths whose wire carries a cone
  // signal. Per path, not per net: a shared net lights only
  // its cone branches, so a sibling load on the same net stays
  // grey.
  const std::set<std::pair<std::string, std::size_t>> cone_paths =
      highlighting
          ? ConeNetPaths(schematic, layout, selected_)
          : std::set<std::pair<std::string, std::size_t>>();
  // Terminals in the cone: a parent port the cone touches, as
  // the driver of a cone path or its load.
  std::set<std::string> cone_terminals;
  std::map<std::string, const SchematicNetRoute*> route_of;
  for (const SchematicNetRoute& route : layout.nets) {
    route_of[route.name] = &route;
  }
  for (const auto& entry : cone_paths) {
    const auto found = route_of.find(entry.first);
    if (found == route_of.end()) continue;
    if (entry.second >= found->second->path_ends.size()) continue;
    const SchematicNetPathEnds& ends =
        found->second->path_ends[entry.second];
    if (!ends.driver_terminal.empty()) {
      cone_terminals.insert(ends.driver_terminal);
    }
    if (!ends.load_terminal.empty()) {
      cone_terminals.insert(ends.load_terminal);
    }
  }

  // Shape-aware endpoints: input wires touch the OR-family
  // curves, not the bounding edge, and output wires touch the
  // inversion bubble, not the shape edge. Routes end at the
  // bounding edge; each path end moves to the drawn shape.
  std::map<std::pair<double, double>, QPointF> shape_anchors;
  for (const SchematicGatePlacement& placement : layout.gates) {
    const SchematicGate* gate = schematic.FindGate(placement.instance);
    if (gate == nullptr) continue;
    const GateSymbol symbol = SymbolForType(gate->type);
    const QRectF rect(placement.x, placement.y, placement.width,
                      placement.height);
    for (const SchematicPortAnchor& anchor : placement.ports) {
      QPointF point(anchor.x, anchor.y);
      if (anchor.left_side) {
        if (symbol == GateSymbol::kOr ||
            symbol == GateSymbol::kNor) {
          point.setX(
              LeftCurveX(placement.x + kOrInset, rect, anchor.y));
        } else if (symbol == GateSymbol::kXor ||
                   symbol == GateSymbol::kXnor) {
          point.setX(LeftCurveX(placement.x, rect, anchor.y));
        }
      } else if (IsInverted(symbol)) {
        point.setX(placement.x + placement.width + 2 * kBubbleRadius);
      }
      shape_anchors[{anchor.x, anchor.y}] = point;
    }
  }

  // Gate symbols, one per layout placement: columns by logic
  // level, inputs left, outputs right. Labels are display
  // labels, centered in the bounding rect; a gate with no
  // display instance shows its type alone.
  gate_rects_.clear();
  for (const SchematicGatePlacement& placement : layout.gates) {
    const SchematicGate* gate = schematic.FindGate(placement.instance);
    if (gate == nullptr) continue;
    const QRectF rect(placement.x, placement.y, placement.width,
                      placement.height);
    gate_rects_[placement.instance] = rect;
    const bool in_cone = cone.count(placement.instance) != 0;
    const GateSymbol symbol = SymbolForType(gate->type);
    // The control anchor for the clock/enable triangle:
    // CLK for flip-flops, EN or G for latches. The $sr
    // latch has no single control input, so it draws no
    // triangle.
    double control_y = -1;
    if (symbol == GateSymbol::kDff || symbol == GateSymbol::kLatch) {
      const std::string wanted =
          symbol == GateSymbol::kDff ? "CLK" : "EN";
      for (const SchematicPortAnchor& anchor : placement.ports) {
        if (anchor.left_side &&
            (anchor.name == wanted ||
             (symbol == GateSymbol::kLatch && anchor.name == "G"))) {
          control_y = anchor.y;
          break;
        }
      }
    }
    DrawGateShape(scene_, rect, symbol, highlighting && !in_cone,
                  placement.instance == selected_, control_y);
    const std::string instance_label = DisplayInstance(gate->instance);
    const std::string type_label = DisplayType(gate->type);
    const std::string equation = LogicEquation(gate->type);
    const QString tooltip =
        instance_label.empty()
            ? QString::fromStdString(
                  equation.empty()
                      ? type_label
                      : type_label + ": " + equation)
            : QString::fromStdString(
                  instance_label + " (" + type_label + ")" +
                  (equation.empty() ? "" : ": " + equation));
    // The tooltip belongs to the shape, but shapes are several
    // items; attach it to a transparent rect over the bounds
    // so hovering anywhere on the gate shows it.
    auto* hover = scene_->addRect(rect);
    hover->setPen(Qt::NoPen);
    hover->setBrush(Qt::NoBrush);
    hover->setToolTip(tooltip);
    auto center_x = [&](const QFontMetrics& metrics,
                        const std::string& text) {
      return placement.x + (placement.width -
                            metrics.horizontalAdvance(
                                QString::fromStdString(text))) /
                               2;
    };
    if (!instance_label.empty()) {
      auto* instance_text = scene_->addSimpleText(
          QString::fromStdString(instance_label));
      instance_text->setFont(instance_font);
      instance_text->setBrush(QBrush(Qt::white));
      instance_text->setPos(center_x(instance_metrics, instance_label),
                            placement.y + 8);
      auto* type_text = scene_->addSimpleText(
          QString::fromStdString(type_label));
      type_text->setFont(type_font);
      type_text->setBrush(QBrush(Qt::white));
      type_text->setPos(center_x(type_metrics, type_label),
                        placement.y + 34);
    } else {
      auto* type_text = scene_->addSimpleText(
          QString::fromStdString(type_label));
      type_text->setFont(type_font);
      type_text->setBrush(QBrush(Qt::white));
      type_text->setPos(center_x(type_metrics, type_label),
                        placement.y + 22);
    }
    // The third line: the operator glyph for arithmetic,
    // comparison, and shift boxes, or the logic equation
    // for a box with no standard symbol. One or the other,
    // never both: OperatorGlyph and LogicEquation are
    // disjoint by construction. It sits below the type
    // label, still inside the 64-high gate rect.
    const std::string glyph = OperatorGlyph(gate->type);
    if (!glyph.empty()) {
      QFont glyph_font;
      glyph_font.setBold(true);
      // 8pt like the equation line: the third line starts
      // at +48 in a 64-high rect, so the default font's
      // 17px line height overflows the rect by a pixel.
      glyph_font.setPointSize(8);
      QFontMetrics glyph_metrics(glyph_font);
      auto* glyph_text =
          scene_->addSimpleText(QString::fromStdString(glyph));
      glyph_text->setFont(glyph_font);
      glyph_text->setBrush(QBrush(Qt::white));
      glyph_text->setPos(center_x(glyph_metrics, glyph), placement.y + 48);
    } else if (!equation.empty()) {
      QFont equation_font;
      equation_font.setPointSize(8);
      QFontMetrics equation_metrics(equation_font);
      auto* equation_text = scene_->addSimpleText(
          QString::fromStdString(equation));
      equation_text->setFont(equation_font);
      equation_text->setBrush(QBrush(Qt::white));
      equation_text->setPos(center_x(equation_metrics, equation),
                            placement.y + 48);
    }
  }

  // Terminals: input ports left, output and inout ports right,
  // stacked in declaration order on their side. Each terminal
  // draws as an arrowhead pointing in the signal direction,
  // with the tip at the wire on the input side and pointing
  // outward on the output side. The layout gives the anchor
  // point where a net attaches: the tip for an input, the base
  // center for an output.
  for (const SchematicTerminalPlacement& terminal : layout.terminals) {
    const QColor color =
        !highlighting
            ? Qt::white
            : (cone_terminals.count(terminal.name) != 0 ? Qt::blue
                                                        : Qt::gray);
    DrawTerminalArrow(scene_, QPointF(terminal.x, terminal.y),
                      terminal.input_side, color);
    const qreal x = terminal.input_side
                        ? terminal.x - kSchematicTerminalSize
                        : terminal.x;
    const qreal y = terminal.y - kSchematicTerminalSize / 2;
    auto* label =
        scene_->addSimpleText(QString::fromStdString(terminal.name));
    label->setBrush(QBrush(Qt::white));
    if (terminal.input_side) {
      label->setPos(x + kSchematicTerminalSize + 8, y);
    } else {
      const qreal width = type_metrics.horizontalAdvance(
          QString::fromStdString(terminal.name));
      label->setPos(x - 8 - width, y);
    }
  }

  // Nets: the layout's orthogonal polylines, one per load,
  // with a dot at every true branch point. Labels carry the
  // net name and, for a bus, its width. A path in the selected
  // cone draws blue; with a selection active, the rest draw
  // grey. Two passes when highlighting, grey first: a segment
  // shared by a cone path and a sibling path ends blue, and a
  // segment only a sibling path uses stays grey. Structural
  // only: no timing annotation is drawn.
  for (const SchematicNetRoute& route : layout.nets) {
    auto path_pen = [&](std::size_t index) {
      if (!highlighting) return QPen(Qt::black, 1);
      return cone_paths.count({route.name, index}) != 0
                 ? QPen(Qt::blue, 2)
                 : QPen(Qt::lightGray, 1);
    };
    auto adjust = [&](const QPointF& point) {
      const auto found = shape_anchors.find(
          {point.x(), point.y()});
      return found == shape_anchors.end() ? point : found->second;
    };
    auto draw_path = [&](const std::vector<SchematicPoint>& path,
                         const QPen& pen) {
      for (std::size_t i = 1; i < path.size(); ++i) {
        QPointF a(path[i - 1].x, path[i - 1].y);
        QPointF b(path[i].x, path[i].y);
        if (i == 1) a = adjust(a);
        if (i + 1 == path.size()) b = adjust(b);
        scene_->addLine(QLineF(a, b), pen);
      }
    };
    for (int pass = 0; pass < (highlighting ? 2 : 1); ++pass) {
      for (std::size_t i = 0; i < route.paths.size(); ++i) {
        const bool in_cone =
            cone_paths.count({route.name, i}) != 0;
        if (highlighting && in_cone == (pass == 0)) continue;
        draw_path(route.paths[i], path_pen(i));
      }
    }
    for (const SchematicPoint& dot : route.branch_dots) {
      // A dot is in the cone when a cone path runs through it:
      // the branch point where a cone branch splits from its
      // siblings lights with the cone.
      bool dot_in_cone = false;
      if (highlighting) {
        for (const auto& entry : cone_paths) {
          if (entry.first != route.name) continue;
          if (entry.second >= route.paths.size()) continue;
          const std::vector<SchematicPoint>& path =
              route.paths[entry.second];
          for (std::size_t i = 1; i < path.size(); ++i) {
            if (PointOnSegment(QPointF(dot.x, dot.y), path[i - 1],
                               path[i])) {
              dot_in_cone = true;
              break;
            }
          }
          if (dot_in_cone) break;
        }
      }
      const QPen pen =
          !highlighting
              ? QPen(Qt::black, 1)
              : (dot_in_cone ? QPen(Qt::blue, 2)
                             : QPen(Qt::lightGray, 1));
      auto* item = scene_->addEllipse(
          QRectF(dot.x - 3, dot.y - 3, 6, 6), pen, QBrush(pen.color()));
      Q_UNUSED(item);
    }
    if (!route.paths.empty() && route.paths[0].size() >= 2) {
      const std::vector<SchematicPoint>& first = route.paths[0];
      const QPointF mid((first[0].x + first[1].x) / 2,
                        (first[0].y + first[1].y) / 2);
      auto* label = scene_->addSimpleText(
          QString::fromStdString(route.label));
      label->setBrush(QBrush(Qt::white));
      label->setPos(mid.x() + 4, mid.y() - 18);
    }
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
