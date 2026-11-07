// gui: the right panels widget (Qt only).
#include "panels_view.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QString>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <string>
#include <vector>

#include "eos/ui/gui.h"
#include "eos/ui/issues.h"
#include "eos/ui/panels.h"

namespace eos::ui {

namespace {

std::string RenderSources(const PanelsModel& panels) {
  if (!panels.parsed) return "(no design read yet)";
  std::string out = "Top: " + panels.top + "\n";
  if (!panels.filelist_path.empty()) {
    out += "File list: " + panels.filelist_path + "\n";
    for (const std::string& file : panels.filelist_files) {
      out += "  " + file + "\n";
    }
  } else {
    out += "Verilog: " + panels.verilog_path + "\n";
  }
  return out;
}

std::string RenderResults(const PanelsModel& panels) {
  std::string out;
  if (panels.elaborated) {
    out += "Elaborated: cells=" +
           std::to_string(panels.elaborated_cells) +
           " wires=" + std::to_string(panels.elaborated_wires) +
           " depth=" + std::to_string(panels.elaborated_depth) + "\n";
  } else {
    out += "Elaborated: not run yet\n";
  }
  if (panels.generic_run) {
    out += "Generic: script=" + panels.generic_script +
           " before_and=" + std::to_string(panels.generic_before_and) +
           " before_levels=" +
           std::to_string(panels.generic_before_levels) +
           " after_and=" + std::to_string(panels.generic_after_and) +
           " after_levels=" + std::to_string(panels.generic_after_levels) +
           " saved_and=" + std::to_string(panels.SavedAnd()) +
           " saved_levels=" + std::to_string(panels.SavedLevels()) + "\n";
    out += "Generic netlist: " + panels.generic_verilog + "\n";
  } else {
    out += "Generic: not run yet\n";
  }
  if (!panels.live_stats.Empty()) {
    out += "Live cell types (" +
           std::string(panels.generic_run ? "generic netlist"
                                          : "elaborated design") +
           "): module=" + panels.live_stats.Module() +
           " cells=" + std::to_string(panels.live_stats.Cells()) +
           " wires=" + std::to_string(panels.live_stats.Wires()) +
           " depth=" + std::to_string(panels.live_stats.Depth()) +
           " sequential=" + std::to_string(panels.live_stats.Sequential()) +
           "\n";
    for (const PanelCellType& type : panels.live_stats.Types()) {
      out += "  " + type.type + ": " + std::to_string(type.count) + "\n";
    }
  } else if (panels.elaborated) {
    out += "Live cell types: unavailable\n";
  }
  return out;
}

std::string RenderOptLog(const PanelsModel& panels) {
  if (!panels.generic_run) return "(generic synthesis not run yet)";
  std::string out = "Script: " + panels.generic_script + "\n";
  out += "Before: " + std::to_string(panels.generic_before_and) +
         " ANDs, " + std::to_string(panels.generic_before_levels) +
         " levels\n";
  out += "After: " + std::to_string(panels.generic_after_and) +
         " ANDs, " + std::to_string(panels.generic_after_levels) +
         " levels\n";
  out += "Saved: " + std::to_string(panels.SavedAnd()) + " ANDs, " +
         std::to_string(panels.SavedLevels()) + " levels\n";
  out += "Verilog: " + panels.generic_verilog + "\n";
  return out;
}

std::string RenderMessages(const std::vector<std::string>& messages) {
  if (messages.empty()) return "(no messages)";
  std::string out;
  for (const std::string& message : messages) {
    out += message;
    out += '\n';
  }
  return out;
}

// The issues tab label: "Issues" when there is nothing to
// count, else the nonzero counts in severity order, so the
// counts are always visible on the tab itself.
std::string IssuesTabText(const IssuesModel& issues) {
  const int errors = issues.ErrorCount();
  const int warnings = issues.WarningCount();
  const int infos = issues.InfoCount();
  if (errors == 0 && warnings == 0 && infos == 0) return "Issues";
  std::string text = "Issues (";
  bool first = true;
  auto add = [&](int count, const char* word) {
    if (count == 0) return;
    if (!first) text += ", ";
    first = false;
    text += std::to_string(count);
    text += " ";
    text += word;
    if (count != 1) text += "s";
  };
  add(errors, "error");
  add(warnings, "warning");
  add(infos, "info");
  text += ")";
  return text;
}

// The issues header: the counts plus what was checked, so the
// empty state states what was checked. When no design is
// elaborated the model is not loaded, and the header says so.
std::string IssuesHeaderText(const IssuesModel& issues) {
  if (!issues.Loaded()) {
    return "No elaborated design yet. Run eos::elaborate to check "
           "the design.";
  }
  const int errors = issues.ErrorCount();
  const int warnings = issues.WarningCount();
  const int infos = issues.InfoCount();
  std::string text;
  if (errors == 0 && warnings == 0 && infos == 0) {
    text = "No issues. ";
  } else {
    text = std::to_string(errors) + " error" +
           (errors == 1 ? "" : "s") + ", " + std::to_string(warnings) +
           " warning" + (warnings == 1 ? "" : "s") + ", " +
           std::to_string(infos) + " info. ";
  }
  text += "Checked: " + issues.Checked() + " (" + issues.Netlist() +
          " netlist).";
  return text;
}

// The content signature of the issues model: the checked list,
// the netlist, and every issue's fields. A refresh whose
// signature matches the table's skips the rebuild, so the row
// selection survives.
std::string IssuesSignature(const IssuesModel& issues) {
  std::string signature = issues.Checked();
  signature += "|";
  signature += issues.Netlist();
  for (const GuiIssue& issue : issues.Issues()) {
    signature += "|";
    signature += issue.severity;
    signature += ",";
    signature += issue.code;
    signature += ",";
    signature += issue.module;
    signature += ",";
    signature += issue.path;
    signature += ",";
    signature += issue.target;
    signature += ",";
    signature += issue.file;
    signature += ",";
    signature += std::to_string(issue.line);
    signature += ",";
    signature += issue.message;
  }
  return signature;
}

std::string IssueSourceText(const GuiIssue& issue) {
  if (issue.file.empty()) return "(no location)";
  if (issue.line == 0) return issue.file;
  return issue.file + ":" + std::to_string(issue.line);
}

QPlainTextEdit* AddTab(QTabWidget* tabs, const char* name) {
  auto* edit = new QPlainTextEdit(tabs);
  edit->setReadOnly(true);
  tabs->addTab(edit, name);
  return edit;
}

}  // namespace

PanelsView::PanelsView(SessionView& view, QWidget* parent)
    : QTabWidget(parent), view_(view) {
  // Issues tab: a header stating the counts and what was
  // checked, over a table with one row per issue. Clicking a
  // row locates the issue: the instance is selected in the
  // hierarchy and the offending gate is highlighted in the
  // schematic.
  auto* issues_tab = new QWidget(this);
  auto* issues_layout = new QVBoxLayout(issues_tab);
  issues_header_ = new QLabel(issues_tab);
  issues_header_->setWordWrap(true);
  issues_layout->addWidget(issues_header_);
  issues_table_ = new QTableWidget(issues_tab);
  issues_table_->setColumnCount(6);
  issues_table_->setHorizontalHeaderLabels(
      {"Severity", "Code", "Message", "Source", "Instance path",
       "Module"});
  issues_table_->setEditTriggers(QTableWidget::NoEditTriggers);
  issues_table_->setSelectionBehavior(QTableWidget::SelectRows);
  issues_table_->setSelectionMode(QTableWidget::SingleSelection);
  issues_layout->addWidget(issues_table_, 1);
  addTab(issues_tab, "Issues");
  connect(issues_table_, &QTableWidget::cellClicked,
          [this](int row, int) {
            const std::vector<GuiIssue>& issues = view_.Issues().Issues();
            if (row < 0 ||
                row >= static_cast<int>(issues.size())) {
              return;
            }
            LocateIssue(issues[row].path, issues[row].target);
          });
  sources_ = AddTab(this, "Sources");
  results_ = AddTab(this, "Results");
  optlog_ = AddTab(this, "Opt log");
  // Messages tab: dispatch history only, never design
  // problems (those live in Issues). The header says so.
  auto* messages_tab = new QWidget(this);
  auto* messages_layout = new QVBoxLayout(messages_tab);
  auto* messages_header = new QLabel(
      "Command dispatch history. Design problems live in Issues.",
      messages_tab);
  messages_header->setWordWrap(true);
  messages_layout->addWidget(messages_header);
  messages_ = new QPlainTextEdit(messages_tab);
  messages_->setReadOnly(true);
  messages_layout->addWidget(messages_, 1);
  addTab(messages_tab, "Messages");
  // No Schematic tab here: the schematic lives in the center
  // panel when the diagram level is a leaf. An issue click
  // highlights the offending gate there through the highlight
  // callback set by the main window.
  Refresh();
}

void PanelsView::Refresh() {
  RefreshIssues();
  const PanelsModel& panels = view_.Panels();
  sources_->setPlainText(QString::fromStdString(RenderSources(panels)));
  results_->setPlainText(QString::fromStdString(RenderResults(panels)));
  optlog_->setPlainText(QString::fromStdString(RenderOptLog(panels)));
  RefreshMessages();
}

void PanelsView::RefreshIssues() {
  const IssuesModel& issues = view_.Issues();
  issues_header_->setText(
      QString::fromStdString(IssuesHeaderText(issues)));
  setTabText(0, QString::fromStdString(IssuesTabText(issues)));
  const std::string signature = IssuesSignature(issues);
  if (signature == issues_signature_) return;
  issues_signature_ = signature;
  issues_table_->setRowCount(
      static_cast<int>(issues.Issues().size()));
  int row = 0;
  for (const GuiIssue& issue : issues.Issues()) {
    issues_table_->setItem(
        row, 0, new QTableWidgetItem(QString::fromStdString(
                     issue.severity)));
    issues_table_->setItem(
        row, 1, new QTableWidgetItem(QString::fromStdString(issue.code)));
    issues_table_->setItem(
        row, 2,
        new QTableWidgetItem(QString::fromStdString(issue.message)));
    issues_table_->setItem(
        row, 3,
        new QTableWidgetItem(
            QString::fromStdString(IssueSourceText(issue))));
    issues_table_->setItem(
        row, 4,
        new QTableWidgetItem(QString::fromStdString(
            IssuesModel::DisplayPath(issue))));
    issues_table_->setItem(
        row, 5,
        new QTableWidgetItem(QString::fromStdString(issue.module)));
    ++row;
  }
}

void PanelsView::RefreshMessages() {
  messages_->setPlainText(
      QString::fromStdString(RenderMessages(view_.Messages())));
}

void PanelsView::ShowTab(int index) {
  if (index < 0 || index >= count()) return;
  setCurrentIndex(index);
}

void PanelsView::LocateIssue(const std::string& path,
                             const std::string& target) {
  if (!view_.SelectHierarchyPath(path)) return;
  if (!view_.SetDiagramLevel(path)) return;
  view_.RefreshSchematic();
  if (view_.DiagramLevelIsLeaf() && on_highlight_gate_) {
    on_highlight_gate_(target);
  }
  if (on_action_) on_action_();
}

}  // namespace eos::ui
