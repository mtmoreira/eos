// gui: the Elaboration main window (Qt only).
#include "main_window.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

#include "eos/ui/gui.h"

namespace eos::ui {

namespace {

const char* kStepNames[3] = {"Read", "Elaborate", "Generic synth"};
const char* kLaterStages[9] = {"Synthesis", "Floorplan", "Place", "CTS",
                                "Route",     "Extract",   "STA", "ECO",
                                "Signoff"};

std::string StepText(const char* name, StepStatus status) {
  return std::string(name) +
         (status == StepStatus::kDone ? ": Done" : ": Pending");
}

}  // namespace

MainWindow::MainWindow(SessionView& view, QWidget* parent)
    : QMainWindow(parent), view_(view) {
  auto* root = new QWidget(this);
  auto* layout = new QVBoxLayout(root);

  // Stage bar: Elaboration active, later stages greyed placeholders.
  auto* stages = new QWidget(root);
  auto* stages_layout = new QHBoxLayout(stages);
  auto* active = new QLabel("Elaboration", stages);
  active->setStyleSheet("font-weight: bold;");
  stages_layout->addWidget(active);
  for (const char* stage : kLaterStages) {
    auto* label = new QLabel(stage, stages);
    label->setEnabled(false);
    stages_layout->addWidget(label);
  }
  stages_layout->addStretch();
  layout->addWidget(stages);

  // Stepper: clicking a step shows that step's outputs in the center.
  auto* stepper = new QWidget(root);
  auto* stepper_layout = new QHBoxLayout(stepper);
  for (int i = 0; i < 3; ++i) {
    step_buttons_[i] = new QPushButton(kStepNames[i], stepper);
    stepper_layout->addWidget(step_buttons_[i]);
    connect(step_buttons_[i], &QPushButton::clicked, [this, i]() {
      selected_step_ = i;
      RefreshCenter();
    });
  }
  stepper_layout->addStretch();
  layout->addWidget(stepper);

  // Center: step outputs in U1; hierarchy views arrive in U2-U4.
  center_ = new QLabel(root);
  center_->setWordWrap(true);
  layout->addWidget(center_, 1);

  // Console echo: GUI-issued commands, read only in U1. Command
  // entry arrives in U6.
  console_ = new QPlainTextEdit(root);
  console_->setReadOnly(true);
  layout->addWidget(console_);

  setCentralWidget(root);
  setWindowTitle("EOS Elaboration");
  Refresh();
}

void MainWindow::Refresh() {
  RefreshSteps();
  RefreshCenter();
  RefreshConsole();
}

void MainWindow::RefreshSteps() {
  const StepStatus states[3] = {view_.ReadStep(), view_.ElaborateStep(),
                                view_.GenericStep()};
  for (int i = 0; i < 3; ++i) {
    step_buttons_[i]->setText(
        QString::fromStdString(StepText(kStepNames[i], states[i])));
  }
}

std::string MainWindow::CenterText() const {
  const CommandContext& ctx = view_.context();
  if (selected_step_ == 0) {
    if (!ctx.synth.parsed) return "No design loaded.";
    if (!ctx.synth.filelist_path.empty()) {
      return "File list: " + ctx.synth.filelist_path +
             "\nTop: " + ctx.synth.top_cell;
    }
    return "File: " + ctx.synth.verilog_path +
           "\nTop: " + ctx.synth.top_cell;
  }
  if (selected_step_ == 1) {
    if (!ctx.synth.elaborated) {
      return ctx.synth.parsed ? "Parsed, not elaborated."
                              : "No design loaded.";
    }
    return "Cells: " + std::to_string(ctx.synth.elaborated_cells) +
           "\nWires: " + std::to_string(ctx.synth.elaborated_wires) +
           "\nDepth: " + std::to_string(ctx.synth.elaborated_depth);
  }
  if (!ctx.synth.generic_optimized) {
    return ctx.synth.elaborated ? "Elaborated, generic synthesis not run."
                                : "No design loaded.";
  }
  return "Script: " + ctx.synth.generic_script +
         "\nBefore: " + std::to_string(ctx.synth.generic_before_and) +
         " AND, " + std::to_string(ctx.synth.generic_before_levels) +
         " levels\nAfter: " + std::to_string(ctx.synth.generic_after_and) +
         " AND, " + std::to_string(ctx.synth.generic_after_levels) +
         " levels\nCells: " + std::to_string(ctx.synth.elaborated_cells) +
         "\nWires: " + std::to_string(ctx.synth.elaborated_wires) +
         "\nDepth: " + std::to_string(ctx.synth.elaborated_depth);
}

void MainWindow::RefreshCenter() {
  center_->setText(QString::fromStdString(CenterText()));
}

void MainWindow::RefreshConsole() {
  const std::vector<std::string>& log = view_.EchoLog();
  if (log.empty()) {
    console_->setPlainText("(no commands yet)");
    return;
  }
  std::string text;
  for (const std::string& line : log) {
    text += line;
    text += '\n';
  }
  console_->setPlainText(QString::fromStdString(text));
}

}  // namespace eos::ui
