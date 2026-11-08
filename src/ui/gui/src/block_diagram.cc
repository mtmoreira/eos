// gui: the block diagram widget (Qt only).
#include "block_diagram.h"

#include <QBrush>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
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
#include <set>
#include <utility>

#include "eos/ui/diagram.h"
#include "eos/ui/gui.h"

namespace eos::ui {

namespace {

// Layout: blocks in one center column, input terminals left,
// output terminals right. All positions derive from the model
// order, so the same level always renders the same way.
constexpr qreal kBlockTop = 40;
constexpr qreal kBlockHeight = 64;
constexpr qreal kBlockGap = 48;
constexpr qreal kBlockX = 300;
constexpr qreal kMinBlockWidth = 180;
constexpr qreal kInputX = 40;
constexpr qreal kOutputX = 640;
constexpr qreal kTerminalSize = 16;
constexpr qreal kStubLength = 28;

// A clickable block rectangle. Plain callbacks, no signals.
class BlockItem : public QGraphicsRectItem {
 public:
  BlockItem(const QRectF& rect, std::string instance,
            std::function<void(const std::string&)> clicked,
            std::function<void(const std::string&)> double_clicked)
      : QGraphicsRectItem(rect),
        instance_(std::move(instance)),
        clicked_(std::move(clicked)),
        double_clicked_(std::move(double_clicked)) {
    setAcceptedMouseButtons(Qt::LeftButton);
  }

 protected:
  void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
    if (clicked_) clicked_(instance_);
    event->accept();
  }
  void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override {
    if (double_clicked_) double_clicked_(instance_);
    event->accept();
  }

 private:
  std::string instance_;
  std::function<void(const std::string&)> clicked_;
  std::function<void(const std::string&)> double_clicked_;
};

struct Anchor {
  QPointF point;
  bool left_side;
  bool is_output;
  // True for a parent terminal anchor: the point is on the
  // terminal arrowhead, and a lone anchor's stub leaves from
  // the arrowhead's outer point. False for a block-edge anchor,
  // whose stub starts at the edge point itself.
  bool is_terminal;
};

std::string LastComponent(const std::string& path) {
  const std::size_t slash = path.rfind('/');
  if (slash == std::string::npos) return path;
  return path.substr(slash + 1);
}

// Draws one parent terminal as an arrowhead pointing in the
// signal direction (right, on both sides): the tip meets the
// wire on the input side and points outward on the output
// side. White contour, no fill, like the blocks. Returns the
// point where a net attaches: the tip for an input, the base
// center for an output.
QPointF DrawTerminalArrow(QGraphicsScene* scene, qreal x, qreal y,
                          bool input_side) {
  QPainterPath path;
  path.moveTo(x + kTerminalSize, y + kTerminalSize / 2);
  path.lineTo(x, y);
  path.lineTo(x, y + kTerminalSize);
  path.closeSubpath();
  scene->addPath(path, QPen(Qt::white, 1), Qt::NoBrush);
  return input_side ? QPointF(x + kTerminalSize, y + kTerminalSize / 2)
                    : QPointF(x, y + kTerminalSize / 2);
}

}  // namespace

BlockDiagram::BlockDiagram(SessionView& view, QWidget* parent)
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

void BlockDiagram::Refresh() {
  RebuildBreadcrumb();
  RebuildScene();
}

void BlockDiagram::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  FitView();
}

void BlockDiagram::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  FitView();
}

bool BlockDiagram::eventFilter(QObject* watched, QEvent* event) {
  if (watched == view_widget_ && event->type() == QEvent::Resize) {
    FitView();
  }
  return QWidget::eventFilter(watched, event);
}

void BlockDiagram::FitView() {
  // No fit for an empty diagram: the placeholder is one text
  // item, and fitting it would scale that text to fill the
  // view. RebuildScene sets the scene rect for real diagrams.
  if (view_.Diagram().Empty()) return;
  view_widget_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
}

void BlockDiagram::RebuildBreadcrumb() {
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

void BlockDiagram::RebuildScene() {
  scene_->clear();
  const DiagramModel& diagram = view_.Diagram();
  if (diagram.Empty()) {
    std::string text = "No diagram for this level.";
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

  // Block width fits the longest label, so names never overflow.
  QFont instance_font;
  instance_font.setBold(true);
  QFontMetrics instance_metrics(instance_font);
  QFont module_font;
  QFontMetrics module_metrics(module_font);
  qreal block_width = kMinBlockWidth;
  for (const DiagramBlock& block : diagram.Blocks()) {
    const qreal instance_w = instance_metrics.horizontalAdvance(
        QString::fromStdString(block.instance));
    const qreal module_w = module_metrics.horizontalAdvance(
        QString::fromStdString(block.module));
    block_width = std::max(block_width, std::max(instance_w, module_w) + 20);
  }

  // Block rectangles, one row each, in model order.
  std::map<std::string, QRectF> block_rects;
  const std::string& level = view_.DiagramLevel();
  const std::string& selected = view_.SelectedHierarchyPath();
  // The selected block, by instance: the hierarchy selection
  // names a full path, while blocks carry instance names.
  std::string selected_instance;
  for (const DiagramBlock& block : diagram.Blocks()) {
    const std::string block_path =
        level.empty() ? block.instance : level + "/" + block.instance;
    if (block_path == selected) {
      selected_instance = block.instance;
      break;
    }
  }
  // Tracing a selected block lights only its fanin and fanout
  // cone, like a gate click in the schematic; everything else
  // dims. With no block selected every net draws plain.
  const bool highlighting = !selected_instance.empty();
  std::set<std::string> cone;
  std::set<std::pair<std::string, std::size_t>> cone_spokes;
  if (highlighting) {
    cone = FaninCone(diagram, selected_instance);
    const std::set<std::string> fanout =
        FanoutCone(diagram, selected_instance);
    cone.insert(fanout.begin(), fanout.end());
    cone.insert(selected_instance);
    cone_spokes = ConeNetSpokes(diagram, selected_instance);
  }
  for (std::size_t i = 0; i < diagram.Blocks().size(); ++i) {
    const DiagramBlock& block = diagram.Blocks()[i];
    const qreal y = kBlockTop + static_cast<qreal>(i) *
                                      (kBlockHeight + kBlockGap);
    const QRectF rect(kBlockX, y, block_width, kBlockHeight);
    block_rects[block.instance] = rect;
    const std::string block_path =
        level.empty() ? block.instance : level + "/" + block.instance;
    const bool highlighted = block_path == selected;
    auto* item = new BlockItem(
        rect, block.instance,
        [this](const std::string& instance) { OnBlockClicked(instance); },
        [this](const std::string& instance) {
          OnBlockDoubleClicked(instance);
        });
    // Blocks are white contours with no fill: the diagram sits
    // on a dark background, so a solid white fill would be a
    // white box on black. A highlighted block outlines in blue.
    // While a block is traced, blocks outside its cone outline
    // in grey, like off-cone gates in the schematic.
    const bool in_cone = cone.count(block.instance) != 0;
    item->setPen(QPen(highlighted
                          ? Qt::blue
                          : (highlighting && !in_cone ? Qt::gray
                                                      : Qt::white),
                      highlighted ? 3 : 1));
    item->setBrush(Qt::NoBrush);
    item->setToolTip(QString::fromStdString(block.instance + " (" +
                                                  block.module + ")"));
    scene_->addItem(item);
    auto* instance_text = scene_->addSimpleText(
        QString::fromStdString(block.instance));
    instance_text->setFont(instance_font);
    instance_text->setBrush(QBrush(Qt::white));
    instance_text->setPos(kBlockX + 10, y + 8);
    auto* module_text = scene_->addSimpleText(
        QString::fromStdString(block.module));
    module_text->setFont(module_font);
    module_text->setBrush(QBrush(Qt::white));
    module_text->setPos(kBlockX + 10, y + 34);
  }

  // Terminals: input ports left, output and inout ports right,
  // each list in declaration order. Each terminal draws as an
  // arrowhead pointing in the signal direction; a net attaches
  // at the tip for an input and at the base center for an
  // output, so the wire never crosses the arrowhead.
  std::map<std::string, Anchor> terminal_anchors;
  std::size_t input_row = 0;
  std::size_t output_row = 0;
  for (const DiagramPort& port : diagram.ParentPorts()) {
    const bool input_side =
        port.direction == "in" || port.direction == "inout";
    const std::size_t row = input_side ? input_row++ : output_row++;
    const qreal y = kBlockTop + static_cast<qreal>(row) *
                                    (kBlockHeight + kBlockGap);
    const qreal x = input_side ? kInputX : kOutputX;
    const QPointF attach =
        DrawTerminalArrow(scene_, x, y, input_side);
    auto* label = scene_->addSimpleText(QString::fromStdString(port.name));
    label->setBrush(QBrush(Qt::white));
    if (input_side) {
      label->setPos(x + kTerminalSize + 8, y);
    } else {
      const qreal width = module_metrics.horizontalAdvance(
          QString::fromStdString(port.name));
      label->setPos(x - 8 - width, y);
    }
    terminal_anchors[port.name] = Anchor{
        attach, input_side, port.direction == "out", true};
  }

  // Port anchors along block edges: input and inout ports on the
  // left edge, output ports on the right, spread by port index
  // among the ports on the same side.
  auto block_anchor = [&](const std::string& instance,
                          const std::string& port_name) -> Anchor {
    const DiagramBlock* block = diagram.FindBlock(instance);
    const QRectF rect = block_rects[instance];
    bool left_side = true;
    bool is_output = false;
    for (const DiagramPort& port : block->ports) {
      if (port.name == port_name) {
        left_side = port.direction == "in" || port.direction == "inout";
        is_output = port.direction == "out";
      }
    }
    std::vector<const DiagramPort*> side_ports;
    for (const DiagramPort& port : block->ports) {
      const bool input_side =
          port.direction == "in" || port.direction == "inout";
      if (input_side == left_side) side_ports.push_back(&port);
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
    return Anchor{QPointF(x, y), left_side, is_output, false};
  };

  // Nets: a star from the driving output endpoint when one
  // exists, else from the first anchor; a lone anchor gets a
  // short stub outward. Labels sit on the first segment. A
  // spoke in the selected cone draws blue; with a selection
  // active, the rest draw grey, like cone paths in the
  // schematic.
  auto net_pen = [&](const std::string& name, std::size_t spoke) {
    if (!highlighting) return QPen(Qt::black, 1);
    return cone_spokes.count({name, spoke}) != 0
               ? QPen(Qt::blue, 2)
               : QPen(Qt::lightGray, 1);
  };
  for (const DiagramNet& net : diagram.Nets()) {
    std::vector<Anchor> anchors;
    for (const DiagramNetEndpoint& endpoint : net.endpoints) {
      anchors.push_back(block_anchor(endpoint.block, endpoint.port));
    }
    for (const std::string& terminal : net.terminals) {
      const auto found = terminal_anchors.find(terminal);
      if (found != terminal_anchors.end()) anchors.push_back(found->second);
    }
    if (anchors.empty()) continue;
    if (anchors.size() == 1) {
      const Anchor& anchor = anchors[0];
      // The stub leaves from the outer point of the terminal
      // symbol (the base for an input, the tip for an output),
      // not the net attach point, so it never crosses the
      // arrowhead. A block-edge anchor stubs from its own
      // point: shifting it would leave a gap at the block.
      const QPointF outer =
          anchor.is_terminal
              ? QPointF(anchor.point.x() +
                            (anchor.left_side ? -kTerminalSize
                                             : kTerminalSize),
                        anchor.point.y())
              : anchor.point;
      const QPointF end(outer.x() +
                            (anchor.left_side ? -kStubLength : kStubLength),
                        outer.y());
      scene_->addLine(QLineF(outer, end), net_pen(net.name, 0));
      auto* label = scene_->addSimpleText(
          QString::fromStdString(net.name));
      label->setBrush(QBrush(Qt::white));
      label->setPos(end.x() + (anchor.left_side ? -4 : 4) -
                        (anchor.left_side
                             ? module_metrics.horizontalAdvance(
                                   QString::fromStdString(net.name))
                             : 0),
                    end.y() - 18);
      continue;
    }
    // The star center is the driving output endpoint when one
    // exists, else the first anchor: the same rule the model
    // uses to key cone spokes, so the indices line up.
    const std::size_t center = DiagramNetCenterIndex(diagram, net);
    for (std::size_t i = 0; i < anchors.size(); ++i) {
      if (i == center) continue;
      scene_->addLine(QLineF(anchors[center].point, anchors[i].point),
                      net_pen(net.name, i));
    }
    std::size_t other = center == 0 ? 1 : 0;
    const QPointF mid((anchors[center].point.x() + anchors[other].point.x()) / 2,
                      (anchors[center].point.y() + anchors[other].point.y()) / 2);
    auto* label = scene_->addSimpleText(QString::fromStdString(net.name));
    label->setBrush(QBrush(Qt::white));
    label->setPos(mid.x() + 4, mid.y() - 18);
  }

  scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-20, -20, 20, 20));
  FitView();
}

void BlockDiagram::OnBlockClicked(const std::string& instance) {
  const std::string& level = view_.DiagramLevel();
  if (level.empty()) return;
  const std::string path = level + "/" + instance;
  // A single click selects the block and traces its nets, like
  // a gate click in the schematic: it never descends. Only a
  // double-click descends into the block. A block the hierarchy
  // does not hold (an unresolved or black-box cell) selects
  // nothing and traces nothing. The center view switches to
  // the schematic when the new level is a leaf, on the refresh
  // the action callback triggers.
  if (!view_.SelectHierarchyPath(path)) return;
  if (on_action_) on_action_();
}

void BlockDiagram::OnBlockDoubleClicked(const std::string& instance) {
  const std::string& level = view_.DiagramLevel();
  if (level.empty()) return;
  const std::string path = level + "/" + instance;
  if (view_.SelectHierarchyPath(path)) {
    view_.SetDiagramLevel(path);
    if (on_action_) on_action_();
  }
}

void BlockDiagram::OnCrumbClicked(const std::string& path) {
  if (view_.SelectHierarchyPath(path)) {
    view_.SetDiagramLevel(path);
    if (on_action_) on_action_();
  }
}

}  // namespace eos::ui
