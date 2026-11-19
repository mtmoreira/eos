// gui: the Elaboration main window (Qt only).
#include "main_window.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QCompleter>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStringListModel>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWidget>

#include <optional>
#include <string>
#include <vector>

#include "eos/ui/gui.h"
#include "block_diagram.h"
#include "center_view.h"
#include "hierarchy_browser.h"
#include "panels_view.h"

namespace eos::ui {

namespace {

// The right panels' tab indices, in PanelsView's construction
// order (panels_view.cc), which the tabText test pins: 0 Issues,
// 1 Sources, 2 Results, 3 Timing, 4 Clocks, 5 Opt log,
// 6 Messages, 7 Signoff. Every ShowTab argument in this file
// uses these constants, so inserting a tab updates one list
// here instead of leaving stale literals behind (full-flow
// GUI unit F11).
const int kIssuesTab = 0;
const int kSourcesTab = 1;
const int kResultsTab = 2;
const int kTimingTab = 3;
const int kClocksTab = 4;
const int kOptLogTab = 5;
const int kMessagesTab = 6;
const int kSignoffTab = 7;

// The stepper's eleven steps in flow order (full-flow GUI
// unit F11): the three original steps, then one per stage
// with a step contract, spelled as the stage bar spells them,
// with GDS export from its contract's comment.
const char* kStepNames[11] = {"Read",    "Elaborate", "Generic synth",
                              "Synthesis", "Floorplan", "Place",
                              "CTS",     "Route",     "Extract",
                              "Finish",  "GDS export"};
// The tab each of the first three stepper steps shows: Read
// shows Sources, Elaborate shows Issues (the default landing
// view after Elaborate), Generic synth shows Opt log. The
// later steps replicate their stage buttons' clicks instead
// (see the stepper construction below).
const int kStepTabs[3] = {kSourcesTab, kIssuesTab, kOptLogTab};
const char* kLaterStages[10] = {"Synthesis", "Floorplan", "Place",
                                "CTS",       "Route",    "Extract",
                                "Finish",    "STA",      "ECO",
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
  // Floorplan is a button, not a label: it stays disabled until a
  // floorplan is loaded, and its text carries the error count
  // while any elaboration error exists (backend finding 1).
  // Synthesis is a button too: it stays disabled until
  // elaboration is done, and shows the Results tab's synthesis
  // section. Place is a button too: it stays disabled until a
  // placement is loaded, and shows the placement canvas.
  // CTS is a button too: it stays disabled until a CTS run is
  // recorded, and shows the Clocks tab. Route is a button too:
  // it stays disabled until a route run is recorded, and shows
  // the routing canvas. Signoff is a button
  // too: it stays disabled until signoff verdicts are loaded,
  // and shows the Signoff tab.
  // Elaboration, Floorplan, and Place are flat buttons, not labels: the
  // user switches the center between the hierarchy views and
  // the floorplan or placement canvas there. The other later stages stay
  // greyed labels until their units land.
  auto* stages = new QWidget(root);
  auto* stages_layout = new QHBoxLayout(stages);
  elaboration_button_ = new QPushButton("Elaboration", stages);
  elaboration_button_->setFlat(true);
  elaboration_button_->setStyleSheet("font-weight: bold;");
  stages_layout->addWidget(elaboration_button_);
  connect(elaboration_button_, &QPushButton::clicked, [this]() {
    view_.ShowFloorplan(false);
    view_.ShowPlacement(false);
    view_.ShowRoute(false);
    view_.ShowGds(false);
    Refresh();
  });
  for (const char* stage : kLaterStages) {
    if (std::string(stage) == "Synthesis") {
      synthesis_button_ = new QPushButton(stage, stages);
      synthesis_button_->setFlat(true);
      synthesis_button_->setEnabled(false);
      stages_layout->addWidget(synthesis_button_);
      connect(synthesis_button_, &QPushButton::clicked, [this]() {
        // PanelsView is created below; clicks only happen after
        // construction, so it is set by then.
        panels_->ShowTab(kResultsTab);
        Refresh();
      });
      continue;
    }
    if (std::string(stage) == "Floorplan") {
      floorplan_button_ = new QPushButton(stage, stages);
      floorplan_button_->setFlat(true);
      floorplan_button_->setEnabled(false);
      stages_layout->addWidget(floorplan_button_);
      connect(floorplan_button_, &QPushButton::clicked, [this]() {
        view_.ShowPlacement(false);
        view_.ShowRoute(false);
        view_.ShowGds(false);
        view_.ShowFloorplan(true);
        Refresh();
      });
      continue;
    }
    if (std::string(stage) == "Place") {
      placement_button_ = new QPushButton(stage, stages);
      placement_button_->setFlat(true);
      placement_button_->setEnabled(false);
      stages_layout->addWidget(placement_button_);
      connect(placement_button_, &QPushButton::clicked, [this]() {
        view_.ShowFloorplan(false);
        view_.ShowRoute(false);
        view_.ShowGds(false);
        view_.ShowPlacement(true);
        Refresh();
      });
      continue;
    }
    if (std::string(stage) == "CTS") {
      cts_button_ = new QPushButton(stage, stages);
      cts_button_->setFlat(true);
      cts_button_->setEnabled(false);
      stages_layout->addWidget(cts_button_);
      connect(cts_button_, &QPushButton::clicked, [this]() {
        // PanelsView is created below; clicks only happen after
        // construction, so it is set by then.
        view_.ShowRoute(false);
        view_.ShowGds(false);
        panels_->ShowTab(kClocksTab);
        Refresh();
      });
      continue;
    }
    if (std::string(stage) == "Route") {
      route_button_ = new QPushButton(stage, stages);
      route_button_->setFlat(true);
      route_button_->setEnabled(false);
      stages_layout->addWidget(route_button_);
      connect(route_button_, &QPushButton::clicked, [this]() {
        view_.ShowFloorplan(false);
        view_.ShowPlacement(false);
        view_.ShowGds(false);
        view_.ShowRoute(true);
        Refresh();
      });
      continue;
    }
    if (std::string(stage) == "Extract") {
      extract_button_ = new QPushButton(stage, stages);
      extract_button_->setFlat(true);
      extract_button_->setEnabled(false);
      stages_layout->addWidget(extract_button_);
      connect(extract_button_, &QPushButton::clicked, [this]() {
        // PanelsView is created below; clicks only happen after
        // construction, so it is set by then.
        view_.ShowRoute(false);
        view_.ShowGds(false);
        panels_->ShowTab(kTimingTab);
        Refresh();
      });
      continue;
    }
    if (std::string(stage) == "Finish") {
      finish_button_ = new QPushButton(stage, stages);
      finish_button_->setFlat(true);
      finish_button_->setEnabled(false);
      stages_layout->addWidget(finish_button_);
      connect(finish_button_, &QPushButton::clicked, [this]() {
        view_.ShowFloorplan(false);
        view_.ShowPlacement(false);
        view_.ShowRoute(false);
        view_.ShowGds(true);
        Refresh();
      });
      continue;
    }
    if (std::string(stage) == "Signoff") {
      signoff_button_ = new QPushButton(stage, stages);
      signoff_button_->setFlat(true);
      signoff_button_->setEnabled(false);
      stages_layout->addWidget(signoff_button_);
      connect(signoff_button_, &QPushButton::clicked, [this]() {
        // PanelsView is created below; clicks only happen after
        // construction, so it is set by then.
        panels_->ShowTab(kSignoffTab);
        Refresh();
      });
      continue;
    }
    auto* label = new QLabel(stage, stages);
    label->setEnabled(false);
    stages_layout->addWidget(label);
  }
  stages_layout->addStretch();
  layout->addWidget(stages);

  // Stepper: flow progress, state as text from the step
  // contracts. The first three steps show their outputs in
  // the right panels (Read shows Sources, Elaborate shows
  // Issues, Generic synth shows the Opt log); every later
  // step's click replicates its stage button's click exactly
  // (same tab or canvas, same center-flag clearing), and
  // refreshes the window. Step buttons are always enabled:
  // state is carried by the text, not by gating.
  auto* stepper = new QWidget(root);
  auto* stepper_layout = new QHBoxLayout(stepper);
  for (int i = 0; i < 11; ++i) {
    step_buttons_[i] = new QPushButton(kStepNames[i], stepper);
    stepper_layout->addWidget(step_buttons_[i]);
    connect(step_buttons_[i], &QPushButton::clicked, [this, i]() {
      // PanelsView is created below; clicks only happen after
      // construction, so it is set by then.
      switch (i) {
        case 0:
        case 1:
        case 2:
          panels_->ShowTab(kStepTabs[i]);
          break;
        case 3:  // Synthesis
          panels_->ShowTab(kResultsTab);
          break;
        case 4:  // Floorplan
          view_.ShowPlacement(false);
          view_.ShowRoute(false);
          view_.ShowGds(false);
          view_.ShowFloorplan(true);
          break;
        case 5:  // Place
          view_.ShowFloorplan(false);
          view_.ShowRoute(false);
          view_.ShowGds(false);
          view_.ShowPlacement(true);
          break;
        case 6:  // CTS
          view_.ShowRoute(false);
          view_.ShowGds(false);
          panels_->ShowTab(kClocksTab);
          break;
        case 7:  // Route
          view_.ShowFloorplan(false);
          view_.ShowPlacement(false);
          view_.ShowGds(false);
          view_.ShowRoute(true);
          break;
        case 8:  // Extract
          view_.ShowRoute(false);
          view_.ShowGds(false);
          panels_->ShowTab(kTimingTab);
          break;
        case 9:   // Finish
        case 10:  // GDS export
          view_.ShowFloorplan(false);
          view_.ShowPlacement(false);
          view_.ShowRoute(false);
          view_.ShowGds(true);
          break;
      }
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

  // Center row: hierarchy browser left, center view center,
  // right panels right. The browser is U2; the center is the
  // block diagram for a non-leaf level and the gate-level
  // schematic for a leaf level, and consumes the browser's
  // selection. Clicking a block descends into it, and when the
  // new level is a leaf the center switches to the schematic.
  auto* middle = new QSplitter(root);
  browser_ = new HierarchyBrowser(view_, middle);
  browser_->SetSelectionCallback([this]() {
    // Hierarchy navigation shows the hierarchy views: it
    // clears the floorplan, placement, and route stages, so a click in
    // the tree never leaves the user on a floorplan,
    // placement, or routing canvas for a different level.
    view_.ShowFloorplan(false);
    view_.ShowPlacement(false);
    view_.ShowRoute(false);
    view_.ShowGds(false);
    view_.SetDiagramLevel(view_.SelectedHierarchyPath());
    Refresh();
  });
  middle->addWidget(browser_);
  center_ = new CenterView(view_, middle);
  center_->SetActionCallback([this]() { Refresh(); });
  middle->addWidget(center_);
  // Right panels: issues, sources, results, timing,
  // optimization log, and messages, rendered from the live
  // session. An issue click highlights the offending gate in the center
  // schematic; a schematic breadcrumb click refreshes the
  // window through the action callback.
  panels_ = new PanelsView(view_, middle);
  panels_->SetActionCallback([this]() { Refresh(); });
  panels_->SetHighlightGateCallback(
      [this](const std::string& target) { center_->HighlightGate(target); });
  middle->addWidget(panels_);
  layout->addWidget(middle, 1);

  // Bottom row: the console (transcript plus entry) on the
  // left, and the single log window on the right, showing all
  // engine output beside the CLI instead of scattered across
  // tabs. The console entry takes one registry command per
  // Return, dispatches it through the session, and refreshes
  // the window, so each effect is visible live; a pasted
  // submission holding several commands runs each in turn, and
  // the transcript shows every command with its outcome. The
  // entry takes registry commands only, not Tcl; the terminal
  // REPL keeps full Tcl. Up and down cycle the entry's own
  // command history; Tab completes the registry command name,
  // with a popup when several names match.
  auto* bottom = new QSplitter(root);
  bottom->setMinimumHeight(180);
  auto* console_side = new QWidget(bottom);
  auto* console_layout = new QVBoxLayout(console_side);
  console_ = new QPlainTextEdit(console_side);
  console_->setReadOnly(true);
  console_layout->addWidget(console_, 1);
  entry_ = new QLineEdit(console_side);
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
  console_layout->addWidget(entry_);
  bottom->addWidget(console_side);
  auto* log_side = new QWidget(bottom);
  auto* log_layout = new QVBoxLayout(log_side);
  auto* log_header = new QWidget(log_side);
  auto* log_header_layout = new QHBoxLayout(log_header);
  log_header_layout->addWidget(new QLabel("Log", log_header));
  log_search_ = new QLineEdit(log_header);
  log_search_->setPlaceholderText("Search log");
  log_header_layout->addWidget(log_search_, 1);
  connect(log_search_, &QLineEdit::returnPressed, [this]() {
    FindNextLogMatch();
  });
  log_errors_ = new QCheckBox("Errors", log_header);
  log_errors_->setChecked(true);
  log_warnings_ = new QCheckBox("Warnings", log_header);
  log_warnings_->setChecked(true);
  log_infos_ = new QCheckBox("Info", log_header);
  log_infos_->setChecked(true);
  auto mark_filters_dirty = [this]() { log_filters_dirty_ = true; };
  connect(log_errors_, &QCheckBox::toggled, mark_filters_dirty);
  connect(log_warnings_, &QCheckBox::toggled, mark_filters_dirty);
  connect(log_infos_, &QCheckBox::toggled, mark_filters_dirty);
  log_header_layout->addWidget(log_errors_);
  log_header_layout->addWidget(log_warnings_);
  log_header_layout->addWidget(log_infos_);
  log_layout->addWidget(log_header);
  log_ = new QPlainTextEdit(log_side);
  log_->setReadOnly(true);
  log_layout->addWidget(log_, 1);
  bottom->addWidget(log_side);
  layout->addWidget(bottom);

  setCentralWidget(root);
  setWindowTitle("EOS Elaboration");
  Refresh();
}

void MainWindow::ShowAgain() {
  show();
  raise();
  activateWindow();
}

// Test seams (see main_window.h): defined here, where the full
// PanelsView and QPushButton types are visible.
int MainWindow::CurrentTab() const { return panels_->currentIndex(); }

bool MainWindow::SynthesisEnabled() const {
  return synthesis_button_ != nullptr && synthesis_button_->isEnabled();
}

bool MainWindow::CtsEnabled() const {
  return cts_button_ != nullptr && cts_button_->isEnabled();
}

bool MainWindow::RouteEnabled() const {
  return route_button_ != nullptr && route_button_->isEnabled();
}

bool MainWindow::ExtractEnabled() const {
  return extract_button_ != nullptr && extract_button_->isEnabled();
}

bool MainWindow::FinishEnabled() const {
  return finish_button_ != nullptr && finish_button_->isEnabled();
}

bool MainWindow::SignoffEnabled() const {
  return signoff_button_ != nullptr && signoff_button_->isEnabled();
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
  const auto* key = static_cast<QKeyEvent*>(event);
  // Return always submits the entry, even while the completion
  // popup is open: the completer would otherwise accept the
  // highlighted completion and swallow the key, leaving the
  // submission silently unrun. Tab is the completion key, and
  // the popup is hidden first so it never lingers over the
  // refreshed transcript.
  if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
    if (entry_completer_->popup()->isVisible()) {
      entry_completer_->popup()->hide();
    }
    SubmitEntry();
    return true;
  }
  // While the completion popup is open, its own keys navigate
  // and accept; this filter stays out of the way.
  if (entry_completer_->popup()->isVisible()) {
    return QMainWindow::eventFilter(watched, event);
  }
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
  // A just-run command's results take the user to their
  // surface, once: the destination is consumed here, so a
  // later refresh never yanks the user back to a tab they
  // already left. The tab and center flags are set before the
  // refreshes below render them.
  switch (view_.TakeNavigation()) {
    case SessionView::ResultDestination::kNone:
      break;
    case SessionView::ResultDestination::kIssues:
      panels_->ShowTab(kIssuesTab);
      break;
    case SessionView::ResultDestination::kSources:
      panels_->ShowTab(kSourcesTab);
      break;
    case SessionView::ResultDestination::kResults:
    case SessionView::ResultDestination::kSynthesis:
      panels_->ShowTab(kResultsTab);
      break;
    case SessionView::ResultDestination::kTiming:
      panels_->ShowTab(kTimingTab);
      break;
    case SessionView::ResultDestination::kClocks:
      panels_->ShowTab(kClocksTab);
      break;
    case SessionView::ResultDestination::kOptLog:
      panels_->ShowTab(kOptLogTab);
      break;
    case SessionView::ResultDestination::kSignoff:
      panels_->ShowTab(kSignoffTab);
      break;
    case SessionView::ResultDestination::kCenter:
      view_.ShowFloorplan(false);
      view_.ShowPlacement(false);
      break;
    case SessionView::ResultDestination::kGds:
      view_.ShowFloorplan(false);
      view_.ShowPlacement(false);
      view_.ShowRoute(false);
      break;
  }
  view_.RefreshHierarchy();
  view_.RefreshDiagram();
  view_.RefreshSchematic();
  view_.RefreshPanels();
  view_.RefreshIssues();
  view_.RefreshTiming();
  view_.RefreshFloorplan();
  view_.RefreshPlacement();
  view_.RefreshClockNetwork();
  view_.RefreshCts();
  view_.RefreshRoute();
  view_.RefreshExtract();
  view_.RefreshFinish();
  view_.RefreshGds();
  view_.RefreshSignoff();
  view_.RefreshLog();
  browser_->Refresh();
  center_->Refresh();
  panels_->Refresh();
  RefreshSteps();
  RefreshConsole();
  RefreshLogWindow();
}

void MainWindow::RefreshSteps() {
  const StepStatus states[11] = {
      view_.ReadStep(),      view_.ElaborateStep(), view_.GenericStep(),
      view_.SynthesisStep(), view_.FloorplanStep(), view_.PlacementStep(),
      view_.CtsStep(),       view_.RouteStep(),     view_.ExtractStep(),
      view_.FinishStep(),    view_.GdsStep()};
  for (int i = 0; i < 11; ++i) {
    step_buttons_[i]->setText(
        QString::fromStdString(StepText(kStepNames[i], states[i])));
  }
  const std::string& checkpoint = view_.CheckpointName();
  checkpoint_label_->setText(QString::fromStdString(
      checkpoint.empty() ? "Checkpoint: none"
                         : "Checkpoint: " + checkpoint));
  // The stage buttons show which center view is active: bold
  // for the one showing, normal for the others. Floorplan stays
  // disabled until a floorplan is loaded, and its label carries
  // the error count while any elaboration error exists.
  // Synthesis stays disabled until elaboration is done, and
  // stays enabled once it is: the flag comes from the live
  // session record, so a later refresh never disables it while
  // the design is still elaborated. Place stays disabled until
  // a placement is loaded.
  if (synthesis_button_ != nullptr) {
    synthesis_button_->setEnabled(view_.ElaborateStep() ==
                                 StepStatus::kDone);
  }
  if (elaboration_button_ != nullptr) {
    elaboration_button_->setStyleSheet(
        view_.ShowingFloorplan() || view_.ShowingPlacement()
            ? ""
            : "font-weight: bold;");
  }
  if (floorplan_button_ != nullptr) {
    const int errors = view_.Issues().ErrorCount();
    floorplan_button_->setText(QString::fromStdString(
        errors > 0 ? "Floorplan (" + std::to_string(errors) + " errors)"
                   : "Floorplan"));
    floorplan_button_->setEnabled(view_.Floorplan().Loaded());
    floorplan_button_->setStyleSheet(
        view_.ShowingFloorplan() ? "font-weight: bold;" : "");
  }
  if (placement_button_ != nullptr) {
    placement_button_->setEnabled(view_.Placement().Loaded());
    placement_button_->setStyleSheet(
        view_.ShowingPlacement() ? "font-weight: bold;" : "");
  }
  // CTS stays disabled until a CTS run is recorded, and stays
  // enabled once it is: the flag comes from the live model,
  // so a later refresh never disables it while the record is
  // still loaded.
  if (cts_button_ != nullptr) {
    cts_button_->setEnabled(view_.Cts().Loaded());
  }
  // Route stays disabled until a route run is recorded, and
  // stays enabled once it is: the flag comes from the live
  // model, so a later refresh never disables it while the
  // record is still loaded. It is bold while the routing
  // canvas is showing, following the placement button's
  // pattern.
  if (route_button_ != nullptr) {
    route_button_->setEnabled(view_.Route().Loaded());
    route_button_->setStyleSheet(
        view_.ShowingRoute() ? "font-weight: bold;" : "");
  }
  // Extract stays disabled until an extract run is recorded,
  // and stays enabled once it is: the flag comes from the
  // live model, so a later refresh never disables it while
  // the record is still loaded.
  if (extract_button_ != nullptr) {
    extract_button_->setEnabled(view_.Extract().Loaded());
  }
  // Finish stays disabled until a finish run is recorded,
  // and stays enabled once it is: the flag comes from the
  // live model, so a later refresh never disables it while
  // the record is still loaded. It is bold while the GDS
  // canvas is showing, following the route button's pattern.
  if (finish_button_ != nullptr) {
    finish_button_->setEnabled(view_.Finish().Loaded());
    finish_button_->setStyleSheet(
        view_.ShowingGds() ? "font-weight: bold;" : "");
  }
  // Signoff stays disabled until verdicts are loaded, and stays
  // enabled once they are: the flag comes from the live model,
  // so a later refresh never disables it while the verdicts are
  // still loaded.
  if (signoff_button_ != nullptr) {
    signoff_button_->setEnabled(view_.Signoff().Loaded());
  }
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

void MainWindow::SubmitEntry() {
  const std::string text = entry_->text().toStdString();
  entry_history_.Add(text);
  view_.DispatchEntry(text);
  entry_->clear();
  Refresh();
}

void MainWindow::RefreshLogWindow() {
  const std::vector<SessionView::LogEntry>& log = view_.Log();
  if (log_filters_dirty_) {
    log_filters_dirty_ = false;
    log_shown_ = 0;
    log_->clear();
  }
  while (log_shown_ < log.size()) {
    const SessionView::LogEntry& entry = log[log_shown_++];
    if (!LogSeverityEnabled(entry.severity)) continue;
    AppendLogLine(entry);
  }
}

void MainWindow::AppendLogLine(const SessionView::LogEntry& entry) {
  QTextCursor cursor(log_->document());
  cursor.movePosition(QTextCursor::End);
  QTextCharFormat format;
  if (entry.severity == "error") {
    format.setForeground(Qt::red);
  } else if (entry.severity == "warning") {
    format.setForeground(QColor(176, 112, 0));
  }
  cursor.setCharFormat(format);
  cursor.insertText(QString::fromStdString("[" + entry.timestamp + "] " +
                                           entry.text + "\n"));
  log_->moveCursor(QTextCursor::End);
}

bool MainWindow::LogSeverityEnabled(const std::string& severity) const {
  if (severity == "error") return log_errors_->isChecked();
  if (severity == "warning") return log_warnings_->isChecked();
  return log_infos_->isChecked();
}

void MainWindow::FindNextLogMatch() {
  const QString needle = log_search_->text();
  if (needle.isEmpty()) return;
  // Search forward from the cursor; wrap to the top when the
  // match is past the end, so Enter keeps cycling through
  // matches.
  if (log_->find(needle)) return;
  QTextCursor cursor(log_->document());
  cursor.movePosition(QTextCursor::Start);
  log_->setTextCursor(cursor);
  log_->find(needle);
}

}  // namespace eos::ui
