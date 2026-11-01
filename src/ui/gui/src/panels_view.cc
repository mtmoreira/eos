// gui: the right panels widget (Qt only).
#include "panels_view.h"

#include <QPlainTextEdit>
#include <QString>

#include <string>
#include <vector>

#include "eos/ui/gui.h"
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

QPlainTextEdit* AddTab(QTabWidget* tabs, const char* name) {
  auto* edit = new QPlainTextEdit(tabs);
  edit->setReadOnly(true);
  tabs->addTab(edit, name);
  return edit;
}

}  // namespace

PanelsView::PanelsView(SessionView& view, QWidget* parent)
    : QTabWidget(parent), view_(view) {
  sources_ = AddTab(this, "Sources");
  results_ = AddTab(this, "Results");
  optlog_ = AddTab(this, "Opt log");
  messages_ = AddTab(this, "Messages");
  Refresh();
}

void PanelsView::Refresh() {
  const PanelsModel& panels = view_.Panels();
  sources_->setPlainText(QString::fromStdString(RenderSources(panels)));
  results_->setPlainText(QString::fromStdString(RenderResults(panels)));
  optlog_->setPlainText(QString::fromStdString(RenderOptLog(panels)));
  messages_->setPlainText(
      QString::fromStdString(RenderMessages(view_.Messages())));
}

void PanelsView::ShowTab(int index) {
  if (index < 0 || index >= count()) return;
  setCurrentIndex(index);
}

}  // namespace eos::ui
