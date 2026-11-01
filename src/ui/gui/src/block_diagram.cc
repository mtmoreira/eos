// gui: the block diagram widget (Qt only).
#include "block_diagram.h"

#include <QBrush>
#include <QColor>
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
};

std::string LastComponent(const std::string& path) {
  const std::size_t slash = path.rfind('/');
  if (slash == std::string::npos) return path;
  return path.substr(slash + 1);
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
    scene_->addSimpleText(QString::fromStdString(text));
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
    item->setPen(QPen(highlighted ? Qt::blue : Qt::black,
                      highlighted ? 3 : 1));
    item->setBrush(highlighted ? QBrush(QColor(220, 232, 255))
                               : QBrush(Qt::white));
    item->setToolTip(QString::fromStdString(block.instance + " (" +
                                                  block.module + ")"));
    scene_->addItem(item);
    auto* instance_text = scene_->addSimpleText(
        QString::fromStdString(block.instance));
    instance_text->setFont(instance_font);
    instance_text->setPos(kBlockX + 10, y + 8);
    auto* module_text = scene_->addSimpleText(
        QString::fromStdString(block.module));
    module_text->setFont(module_font);
    module_text->setPos(kBlockX + 10, y + 34);
  }

  // Terminals: input ports left, output and inout ports right,
  // each list in declaration order.
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
    auto* square = scene_->addRect(
        QRectF(x, y, kTerminalSize, kTerminalSize));
    square->setPen(QPen(Qt::black, 1));
    square->setBrush(QBrush(Qt::white));
    auto* label = scene_->addSimpleText(QString::fromStdString(port.name));
    if (input_side) {
      label->setPos(x + kTerminalSize + 8, y);
    } else {
      const qreal width = module_metrics.horizontalAdvance(
          QString::fromStdString(port.name));
      label->setPos(x - 8 - width, y);
    }
    terminal_anchors[port.name] = Anchor{
        QPointF(x + kTerminalSize / 2, y + kTerminalSize / 2),
        input_side, port.direction == "out"};
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
    return Anchor{QPointF(x, y), left_side, is_output};
  };

  // Nets: a star from the driving output endpoint when one
  // exists, else from the first anchor; a lone anchor gets a
  // short stub outward. Labels sit on the first segment.
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
      const QPointF end(anchor.point.x() +
                            (anchor.left_side ? -kStubLength : kStubLength),
                        anchor.point.y());
      scene_->addLine(QLineF(anchor.point, end));
      auto* label = scene_->addSimpleText(
          QString::fromStdString(net.name));
      label->setPos(end.x() + (anchor.left_side ? -4 : 4) -
                        (anchor.left_side
                             ? module_metrics.horizontalAdvance(
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

void BlockDiagram::OnBlockClicked(const std::string& instance) {
  const std::string& level = view_.DiagramLevel();
  if (level.empty()) return;
  if (view_.SelectHierarchyPath(level + "/" + instance) && on_action_) {
    on_action_();
  }
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
