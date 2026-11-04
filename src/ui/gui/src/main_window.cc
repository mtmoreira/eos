// gui: the Elaboration main window (Qt only).
#include "main_window.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QCloseEvent>
#include <QCompleter>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStringListModel>
#include <QVBoxLayout>
#include <QWidget>

#include <optional>
#include <string>
#include <vector>

#include "eos/ui/gui.h"
#include "block_diagram.h"
#include "hierarchy_browser.h"
#include "panels_view.h"
#include "schematic_diagram.h"

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

  // Stepper: status from the session record. Clicking a step
  // shows that step's outputs in the right panels (Read shows
  // Sources, Elaborate shows Results, Generic synth shows the
  // Opt log) and refreshes the window; the center is the block
  // diagram (U3) or the schematic at a leaf (U4).
  auto* stepper = new QWidget(root);
  auto* stepper_layout = new QHBoxLayout(stepper);
  for (int i = 0; i < 3; ++i) {
    step_buttons_[i] = new QPushButton(kStepNames[i], stepper);
    stepper_layout->addWidget(step_buttons_[i]);
    connect(step_buttons_[i], &QPushButton::clicked, [this, i]() {
      // PanelsView is created below; clicks only happen after
      // construction, so it is set by then.
      panels_->ShowTab(i);
      Refresh();
    });
  }
  stepper_layout->addStretch();
  // Checkpoint indicator (U6): the current checkpoint path
  // only, no compare UI. It sits at the right of the stepper
  // row and refreshes with the step states.
  checkpoint_label_ = new QLabel("Checkpoint: none", stepper);
  stepper_layout->addWidget(checkpoint_label_);
  layout->addWidget(stepper);

  // Center row: hierarchy browser left, block diagram or
  // schematic right. The browser is U2; the diagram is U3 and
  // consumes the browser's selection, and the schematic (U4)
  // replaces the diagram at leaf levels, as the bottom of the
  // same navigation stack.
  auto* middle = new QSplitter(root);
  browser_ = new HierarchyBrowser(view_, middle);
  browser_->SetSelectionCallback([this]() {
    view_.SetDiagramLevel(view_.SelectedHierarchyPath());
    Refresh();
  });
  middle->addWidget(browser_);
  diagram_ = new BlockDiagram(view_, middle);
  diagram_->SetActionCallback([this]() { Refresh(); });
  middle->addWidget(diagram_);
  schematic_ = new SchematicDiagram(view_, middle);
  schematic_->SetActionCallback([this]() { Refresh(); });
  middle->addWidget(schematic_);
  // Right panels (U5): sources, results, optimization log,
  // and messages, rendered from the live session.
  panels_ = new PanelsView(view_, middle);
  middle->addWidget(panels_);
  layout->addWidget(middle, 1);

  // Console echo: GUI-issued commands, read only in U1. Command
  // entry arrives in U6: the line below the transcript takes one
  // registry command per Return, dispatches it through the
  // session, and refreshes the window, so each effect is visible
  // live. The entry takes registry commands only, not Tcl; the
  // terminal REPL keeps full Tcl. Up and down cycle the entry's
  // own command history; Tab completes the registry command
  // name, with a popup when several names match.
  console_ = new QPlainTextEdit(root);
  console_->setReadOnly(true);
  layout->addWidget(console_);
  entry_ = new QLineEdit(root);
  entry_->setPlaceholderText("eos::elaborate");
  entry_completer_ = new QCompleter(entry_);
  entry_completion_model_ = new QStringListModel(entry_completer_);
  entry_completer_->setModel(entry_completion_model_);
  entry_completer_->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
  entry_completer_->setWidget(entry_);
  connect(entry_completer_,
          QOverload<const QString&>::of(&QCompleter::activated),
          [this](const QString& text) { entry_->setText(text); });
  entry_->installEventFilter(this);
  connect(entry_, &QLineEdit::textChanged, [this]() {
    // A typed character invalidates the list Tab computed, so
    // the popup never offers stale candidates.
    if (entry_completer_->popup()->isVisible()) {
      entry_completer_->popup()->hide();
    }
  });
  connect(entry_, &QLineEdit::returnPressed, [this]() {
    const std::string text = entry_->text().toStdString();
    entry_history_.Add(text);
    view_.DispatchEntry(text);
    entry_->clear();
    Refresh();
  });
  layout->addWidget(entry_);

  setCentralWidget(root);
  setWindowTitle("EOS Elaboration");
  Refresh();
}

void MainWindow::ShowAgain() {
  show();
  raise();
  activateWindow();
}

void MainWindow::NoteStdinClosed() {
  stdin_closed_ = true;
  if (!isVisible()) QCoreApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (stdin_closed_) {
    event->accept();
    QCoreApplication::quit();
    return;
  }
  hide();
  event->ignore();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched != entry_ || event->type() != QEvent::KeyPress) {
    return QMainWindow::eventFilter(watched, event);
  }
  // While the completion popup is open, its own keys navigate
  // and accept; this filter stays out of the way.
  if (entry_completer_->popup()->isVisible()) {
    return QMainWindow::eventFilter(watched, event);
  }
  const auto* key = static_cast<QKeyEvent*>(event);
  if (key->key() == Qt::Key_Up) {
    std::optional<std::string> older =
        entry_history_.Up(entry_->text().toStdString());
    if (older.has_value()) entry_->setText(QString::fromStdString(*older));
    return true;
  }
  if (key->key() == Qt::Key_Down) {
    std::optional<std::string> newer = entry_history_.Down();
    if (newer.has_value()) entry_->setText(QString::fromStdString(*newer));
    return true;
  }
  if (key->key() == Qt::Key_Tab) {
    const std::vector<std::string> candidates = CompleteConsoleCommand(
        view_.CommandNames(), entry_->text().toStdString());
    if (candidates.empty()) return true;
    if (candidates.size() == 1) {
      entry_->setText(QString::fromStdString(candidates[0]));
      return true;
    }
    QStringList list;
    for (const std::string& candidate : candidates) {
      list << QString::fromStdString(candidate);
    }
    entry_completion_model_->setStringList(list);
    entry_completer_->complete();
    return true;
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::Refresh() {
  view_.RefreshHierarchy();
  view_.RefreshDiagram();
  view_.RefreshSchematic();
  view_.RefreshPanels();
  browser_->Refresh();
  diagram_->Refresh();
  schematic_->Refresh();
  panels_->Refresh();
  // The center shows exactly one of the diagram and the
  // schematic: the schematic at a leaf level, the diagram
  // elsewhere. The hidden widget keeps no state, so switching
  // levels never shows a stale view.
  const bool leaf = view_.DiagramLevelIsLeaf();
  diagram_->setVisible(!leaf);
  schematic_->setVisible(leaf);
  RefreshSteps();
  RefreshConsole();
}

void MainWindow::RefreshSteps() {
  const StepStatus states[3] = {view_.ReadStep(), view_.ElaborateStep(),
                                view_.GenericStep()};
  for (int i = 0; i < 3; ++i) {
    step_buttons_[i]->setText(
        QString::fromStdString(StepText(kStepNames[i], states[i])));
  }
  const std::string& checkpoint = view_.CheckpointName();
  checkpoint_label_->setText(QString::fromStdString(
      checkpoint.empty() ? "Checkpoint: none"
                         : "Checkpoint: " + checkpoint));
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
