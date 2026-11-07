// CenterView: the central panel of the main window.
// See center_view.h for the layout contract.

#include "center_view.h"

#include <QVBoxLayout>

namespace eos::ui {

CenterView::CenterView(SessionView& view, QWidget* parent)
    : QWidget(parent), view_(view) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  stack_ = new QStackedWidget(this);
  diagram_ = new BlockDiagram(view_, stack_);
  schematic_ = new SchematicDiagram(view_, stack_);
  stack_->addWidget(diagram_);
  stack_->addWidget(schematic_);
  layout->addWidget(stack_);
  diagram_->SetActionCallback([this] {
    if (on_action_) on_action_();
  });
  schematic_->SetActionCallback([this] {
    if (on_action_) on_action_();
  });
  Refresh();
}

void CenterView::Refresh() {
  showing_schematic_ = view_.DiagramLevelIsLeaf();
  diagram_->Refresh();
  schematic_->Refresh();
  stack_->setCurrentWidget(showing_schematic_
                               ? static_cast<QWidget*>(schematic_)
                               : static_cast<QWidget*>(diagram_));
}

void CenterView::HighlightGate(const std::string& instance) {
  if (!showing_schematic_) return;
  schematic_->HighlightGate(instance);
}

void CenterView::SetActionCallback(std::function<void()> callback) {
  on_action_ = std::move(callback);
}

}  // namespace eos::ui
