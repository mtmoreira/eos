// panels: the GUI's right panels model (L6 interface, Qt-free).
//
// The model the right panels widget reads: source files from the
// Read step, elaborated totals from the Elaborate step, the
// generic record from the Generic synth step, and the live
// design's cell-type breakdown from a synth_stats payload. The
// model is built only from the live session, never from mock
// data. Live stats parsing is all-or-nothing like
// HierarchyModel: a malformed payload leaves the previous stats
// untouched and reports false. Messages live on SessionView,
// not here: they are dispatch history, not a session record.
#ifndef EOS_UI_PANELS_H_
#define EOS_UI_PANELS_H_

#include <string>
#include <vector>

namespace eos::ui {

// One cell type in the live design's stats, with its count.
// Sorted by type name, as the synth_stats payload renders them.
struct PanelCellType {
  std::string type;
  int count = 0;
};

// The live design's netlist stats, from a synth_stats payload.
// Present only when the session holds an elaborated design and
// the query succeeded; cleared otherwise, so a stale breakdown
// never lingers beside newer step outputs.
class LiveStatsModel {
 public:
  // Parses a synth_stats payload: a "module=<module>" line, a
  // "cells=<n> wires=<n> depth=<n> sequential=<n>" line, then
  // one "type <name> <count>" line per cell type, in
  // name-sorted order. Returns false (stats unchanged) on any
  // malformed line, unknown key, missing module or stats line,
  // duplicate or out-of-order type, negative or non-numeric
  // count, or an empty name. A design with no cells renders no
  // type lines, which loads fine. A successful load replaces
  // the previous stats.
  bool LoadPayload(const std::string& payload);
  void Clear();

  bool Empty() const { return module_.empty(); }
  const std::string& Module() const { return module_; }
  int Cells() const { return cells_; }
  int Wires() const { return wires_; }
  int Depth() const { return depth_; }
  int Sequential() const { return sequential_; }
  const std::vector<PanelCellType>& Types() const { return types_; }

 private:
  std::string module_;
  int cells_ = 0;
  int wires_ = 0;
  int depth_ = 0;
  int sequential_ = 0;
  std::vector<PanelCellType> types_;
};

// The mapped synthesis record, from a synth_summary payload.
// Present only when the session holds a mapped design and the
// query succeeded; cleared otherwise, so a stale mapped record
// never lingers beside newer step outputs.
class MappedSynthModel {
 public:
  // Parses a synth_summary payload: exactly three lines, fixed
  // key order per line:
  //   mapped target=<t> liberty=<p> verilog=<p> cells=<n>
  //     types=<Type:count,...>|none judged=true|false
  //     worst_slack_ns=<num>|none tns_ns=<num>|none
  //   opt run=true|false winner=<name>|none iterations=<n>
  //     verilog=<p>|none cells=<n> types=<...>|none
  //     worst_slack_ns=<num>|none tns_ns=<num>|none
  //   equiv run=true|false verdict=<v>|none first=<p>|none
  //     second=<p>|none
  // Returns false (model unchanged) on any malformed line, wrong
  // line count, unknown key, out-of-order key, negative or
  // non-numeric count, unsorted or duplicate type, non-numeric
  // slack, or an inconsistent placeholder: judged=false requires
  // both slack fields none; opt run=false requires every opt
  // field at its placeholder; equiv run=true requires a verdict
  // in {equivalent, not_equivalent, unknown}. A successful load
  // replaces the previous record.
  bool LoadPayload(const std::string& payload);
  void Clear();

  bool Empty() const { return target_.empty(); }
  const std::string& Target() const { return target_; }
  const std::string& Liberty() const { return liberty_; }
  const std::string& Verilog() const { return verilog_; }
  int Cells() const { return cells_; }
  const std::vector<PanelCellType>& Types() const { return types_; }
  bool Judged() const { return judged_; }
  double WorstSlackNs() const { return worst_slack_ns_; }
  double TnsNs() const { return tns_ns_; }
  bool OptRun() const { return opt_run_; }
  const std::string& OptWinner() const { return opt_winner_; }
  int OptIterations() const { return opt_iterations_; }
  const std::string& OptVerilog() const { return opt_verilog_; }
  int OptCells() const { return opt_cells_; }
  const std::vector<PanelCellType>& OptTypes() const {
    return opt_types_;
  }
  double OptWorstSlackNs() const { return opt_worst_slack_ns_; }
  double OptTnsNs() const { return opt_tns_ns_; }
  bool EquivRun() const { return equiv_run_; }
  const std::string& EquivVerdict() const { return equiv_verdict_; }
  const std::string& EquivFirst() const { return equiv_first_; }
  const std::string& EquivSecond() const { return equiv_second_; }

 private:
  std::string target_;
  std::string liberty_;
  std::string verilog_;
  int cells_ = 0;
  std::vector<PanelCellType> types_;
  bool judged_ = false;
  double worst_slack_ns_ = 0.0;
  double tns_ns_ = 0.0;
  bool opt_run_ = false;
  std::string opt_winner_;
  int opt_iterations_ = 0;
  std::string opt_verilog_;
  int opt_cells_ = 0;
  std::vector<PanelCellType> opt_types_;
  double opt_worst_slack_ns_ = 0.0;
  double opt_tns_ns_ = 0.0;
  bool equiv_run_ = false;
  std::string equiv_verdict_;
  std::string equiv_first_;
  std::string equiv_second_;
};

// The right panels' snapshot, filled by
// SessionView::RefreshPanels from the session record plus the
// live stats query. Sources name exactly one shape: a design
// from read_verilog names one file; a design from
// read_filelist names the list plus its resolved entries in
// list order. The generic record is present only once generic
// synthesis ran; the saved counts are before minus after, so a
// script that grows the design reports a negative saving
// honestly instead of clamping it away.
struct PanelsModel {
  bool parsed = false;
  std::string top;
  std::string verilog_path;
  std::string filelist_path;
  std::vector<std::string> filelist_files;

  bool elaborated = false;
  int elaborated_cells = 0;
  int elaborated_wires = 0;
  int elaborated_depth = 0;

  bool generic_run = false;
  std::string generic_script;
  int generic_before_and = 0;
  int generic_before_levels = 0;
  int generic_after_and = 0;
  int generic_after_levels = 0;
  std::string generic_verilog;

  LiveStatsModel live_stats;

  // The mapped synthesis record, from a synth_summary query.
  // Present only once synth_map ran and the query succeeded.
  MappedSynthModel mapped_synth;

  int SavedAnd() const { return generic_before_and - generic_after_and; }
  int SavedLevels() const {
    return generic_before_levels - generic_after_levels;
  }

  void Clear();
};

}  // namespace eos::ui

#endif  // EOS_UI_PANELS_H_
