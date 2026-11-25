// gui: the right panels widget (Qt only).
#include "panels_view.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QString>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "eos/ui/gui.h"
#include "eos/ui/issues.h"
#include "eos/ui/panels.h"
#include "eos/ui/signoff.h"
#include "eos/ui/timing.h"

namespace eos::ui {

namespace {

// Formats a slack the way the registry payloads do, so the panel
// shows exactly the number the query reported.
std::string FormatSlack(double value) {
  std::ostringstream out;
  out << std::setprecision(10) << value;
  return out.str();
}

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
  // Mapped synthesis (full-flow GUI unit F1): the synth_map
  // record, the loop record, and the equivalence verdict, from
  // the synth_summary query. Each section names its own "not
  // run yet" state instead of hiding, so the panel never looks
  // like a mapped design when the stage has not run.
  const MappedSynthModel& mapped = panels.mapped_synth;
  if (!mapped.Empty()) {
    out += "Mapped: target=" + mapped.Target() +
           " cells=" + std::to_string(mapped.Cells());
    if (mapped.Judged()) {
      out += " worst slack=" + FormatSlack(mapped.WorstSlackNs()) +
             " ns, TNS=" + FormatSlack(mapped.TnsNs()) + " ns";
    } else {
      out += " worst slack=n/a, TNS=n/a";
    }
    out += "\n";
    for (const PanelCellType& type : mapped.Types()) {
      out += "  " + type.type + ": " + std::to_string(type.count) + "\n";
    }
    if (mapped.OptRun()) {
      out += "Loop: winner=" + mapped.OptWinner() +
             " iterations=" +
             std::to_string(mapped.OptIterations()) +
             " cells=" + std::to_string(mapped.OptCells()) +
             " worst slack=" + FormatSlack(mapped.OptWorstSlackNs()) +
             " ns, TNS=" + FormatSlack(mapped.OptTnsNs()) + " ns\n";
      for (const PanelCellType& type : mapped.OptTypes()) {
        out += "  " + type.type + ": " + std::to_string(type.count) +
               "\n";
      }
    } else {
      out += "Loop: not run yet\n";
    }
    if (mapped.EquivRun()) {
      out += "Equivalence: " + mapped.EquivVerdict() + "\n";
    } else {
      out += "Equivalence: not run yet\n";
    }
  } else if (panels.elaborated) {
    out += "Mapped: not run yet\n";
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

std::string RenderTiming(const TimingModel& timing) {
  if (!timing.Loaded()) {
    return "(no timing results yet: run eos::run_timing first)";
  }
  std::string out;
  for (const TimingScene& scene : timing.Scenes()) {
    out += "Scene " + scene.scene + " (" + scene.mode + "/" +
           scene.corner + "/" + scene.parasitics + "): worst slack " +
           scene.worst_slack_ns + " ns, TNS " +
           scene.total_negative_slack_ns + " ns, worst hold slack " +
           scene.worst_hold_slack_ns + " ns, hold TNS " +
           scene.total_negative_hold_slack_ns + " ns\n";
    out += "  max_transition: worst " + scene.max_transition_ns +
           " ns, " +
           std::to_string(scene.max_transition_violations) +
           " violations; max_capacitance: worst " +
           scene.max_capacitance_pf + " pF, " +
           std::to_string(scene.max_capacitance_violations) +
           " violations; max_fanout: worst " + scene.max_fanout +
           ", " + std::to_string(scene.max_fanout_violations) +
           " violations\n";
  }
  if (!timing.Clocks().empty() || !timing.GeneratedClocks().empty() ||
      !timing.BindClocks().empty()) {
    out += "Clocks:\n";
    for (const TimingClock& clock : timing.Clocks()) {
      const std::string period =
          clock.period == "-" ? "-" : clock.period + " ns";
      const std::string waveform =
          (clock.rise == "-" || clock.fall == "-")
              ? "-"
              : "{" + clock.rise + " " + clock.fall + "}";
      out += "  " + clock.name + ": period " + period +
             ", waveform " + waveform + ", attrs " + clock.attrs +
             ", skew " + clock.skew + ", sources " + clock.sources +
             "\n";
    }
    for (const TimingGeneratedClock& gen : timing.GeneratedClocks()) {
      out += "  " + gen.name + " (generated): master " +
             gen.master_clock + ", master source " + gen.master_source +
             ", modification " + gen.modification + ", sources " +
             gen.sources + "\n";
    }
    for (const TimingBindClock& bind : timing.BindClocks()) {
      out += "  " + bind.name + " (construction record, no period): " +
             bind.target + "\n";
    }
  } else {
    out += "Clocks: none defined\n";
  }
  out += "Worst violators:\n";
  for (const TimingViolator& row : timing.Violators()) {
    out += "  " + row.scene + " " + row.klass + ": violator " +
           row.violator + ", slack " + row.slack + ", cost " +
           row.cost + ", weight 1, violations " +
           std::to_string(row.violations);
    if (!row.has_limit) out += " (no limit defined)";
    out += "\n";
  }
  bool any_paths = false;
  for (const TimingPath& path : timing.Paths()) {
    if (!any_paths) {
      out += "Worst paths:\n";
      any_paths = true;
    }
    out += "  " + path.scene + " " + path.check + ": " +
           path.endpoint + " from " + path.startpoint + ", slack " +
           path.slack + " ns, arrival " + path.arrival +
           " ns, required " + path.required + " ns\n";
  }
  if (!any_paths) out += "Worst paths: none\n";
  out += "Coverage:\n";
  for (const TimingCoverageTotal& total : timing.CoverageTotals()) {
    out += "  " + total.scene + ": " +
           std::to_string(total.checks) + " checks, " +
           std::to_string(total.tested) + " tested, " +
           std::to_string(total.untested) + " untested\n";
    for (const TimingCoverageType& type : timing.CoverageTypes()) {
      if (type.scene != total.scene) continue;
      out += "    " + type.type + ": " +
             std::to_string(type.tested) + " tested, " +
             std::to_string(type.untested) + " untested (" +
             type.percent + "%)\n";
    }
  }
  out += "QoR:\n";
  for (const TimingQor& qor : timing.QorScenes()) {
    out += "  " + qor.scene + ": cells " +
           std::to_string(qor.cells) + ", area " + qor.area +
           ", compile CPU " + qor.compile_cpu_s + " s\n";
    out += "    setup: union TNS " + qor.union_tns + " ns (" +
           std::to_string(qor.union_endpoints) +
           " endpoints), every-group TNS " + qor.every_group_tns +
           " ns (" + std::to_string(qor.every_group_endpoints) +
           " endpoints)\n";
    out += "    hold: union TNS " + qor.hold_union_tns + " ns (" +
           std::to_string(qor.hold_union_endpoints) +
           " endpoints), every-group TNS " +
           qor.hold_every_group_tns + " ns (" +
           std::to_string(qor.hold_every_group_endpoints) +
           " endpoints)\n";
    for (const TimingQorGroup& group : timing.QorGroups()) {
      if (group.scene != qor.scene) continue;
      out += "    group " + group.group + " (" + group.check +
             "): WNS " + group.wns + " ns, TNS " + group.tns +
             " ns, " + std::to_string(group.violating_paths) +
             " violating paths\n";
    }
    for (const TimingQorCell& cell : timing.QorCells()) {
      if (cell.scene != qor.scene) continue;
      out += "    cell " + cell.cell + ": " +
             std::to_string(cell.count) + ", area " + cell.area + "\n";
    }
    for (const TimingQorDrc& drc : timing.QorDrcs()) {
      if (drc.scene != qor.scene) continue;
      out += "    " + drc.drc + ": " +
             std::to_string(drc.violations) + " violations, worst slack " +
             drc.worst_slack + "\n";
    }
  }
  return out;
}

// The Clocks tab: the clock network at the stage the session
// holds. One block per clock (the clock line, the generated
// derivation, the tree lines indented by depth, the interclock
// line), then the notes. Latency is "-" at every stage in this
// unit: it exists only after CTS, from post-CTS STA, and the
// note line says so (clock-network design sections 3.3, 3.4).
std::string RenderClockNetwork(const ClockNetworkModel& model) {
  if (!model.Loaded()) {
    return "(no clock network yet: no clocks are defined in this "
           "session. Define one with create_clock or read an SDC "
           "file with read_sdc.)";
  }
  std::string notes;
  for (const std::string& note : model.notes()) {
    if (!notes.empty()) notes += "\n";
    notes += "Note: " + note;
  }
  if (model.clocks().empty()) {
    std::string out = "Clocks: none defined.";
    if (!notes.empty()) out += "\n" + notes;
    return out;
  }
  const auto unbrace = [](const std::string& value) {
    if (value.size() >= 2 && value.front() == '{' &&
        value.back() == '}') {
      return value.substr(1, value.size() - 2);
    }
    return value;
  };
  const auto find_clock = [&model](const std::string& name) {
    for (const ClockNetworkClock& clock : model.clocks()) {
      if (clock.name == name) return &clock;
    }
    return static_cast<const ClockNetworkClock*>(nullptr);
  };
  std::string stage_line = "Stage: " + model.stage();
  if (model.stage() == "constraints") {
    stage_line = "Stage: constraints only (no design walked yet)";
  } else if (model.stage() == "elaborated") {
    stage_line = "Stage: elaborated design";
  } else if (model.stage() == "mapped") {
    stage_line = "Stage: mapped design";
  }
  std::string out = stage_line + "\n";
  bool first_block = true;
  for (const ClockNetworkClock& clock : model.clocks()) {
    out += first_block ? "\n" : "";
    first_block = false;
    const std::string period =
        clock.period == "-" ? "-" : clock.period + " ns";
    const std::string waveform =
        (clock.rise == "-" || clock.fall == "-")
            ? "-"
            : "{" + clock.rise + " " + clock.fall + "}";
    const std::string skew =
        clock.skew == "none" ? "none" : clock.skew + " ns";
    out += clock.name + (clock.generated ? " (generated)" : "") +
           ": period " + period + ", waveform " + waveform +
           ", sources " + clock.sources + ", skew " + skew +
           ", sinks " + clock.sinks + ", latency -\n";
    if (clock.generated && clock.master != "-") {
      out += "  generated from " + clock.master;
      if (clock.modification != "-") {
        out += " by " + unbrace(clock.modification);
      }
      const ClockNetworkClock* master = find_clock(clock.master);
      out += ", master source " +
             (master != nullptr ? master->sources : std::string("{-}")) +
             "\n";
    }
    if (clock.tree != "none") {
      for (const ClockNetworkNode& node : clock.nodes) {
        const std::string indent(2 * (node.depth + 1), ' ');
        if (node.kind == "source") {
          out += indent + "source: " +
                 (clock.generated ? node.name : "port " + node.name) +
                 "\n";
        } else if (node.kind == "leaf" || node.kind == "divider") {
          // Node names are "<path> (<module>)": the leaf line
          // shows the local instance name and the module.
          std::string path = node.name;
          std::string module;
          const std::size_t open = node.name.rfind(" (");
          if (open != std::string::npos &&
              node.name.back() == ')') {
            path = node.name.substr(0, open);
            module = node.name.substr(open + 2, node.name.size() - open - 3);
          }
          const std::size_t slash = path.rfind('/');
          const std::string local =
              slash == std::string::npos ? path : path.substr(slash + 1);
          out += indent + local + " (" + module + "), " + node.count +
                 (node.count == "1" ? " sink" : " sinks");
          if (node.generates != "-") {
            out += ", generates " + node.generates;
          }
          out += "\n";
        } else if (node.kind == "buffer") {
          out += indent + node.name + " pin " + node.pin + ", buffer\n";
        } else {  // sink
          out += indent + node.name + " pin " + node.pin;
          if (node.generates != "-") {
            out += ", generates " + node.generates;
          }
          out += "\n";
        }
      }
    }
    if (clock.generated && clock.master != "-") {
      // The interclock line prints when this clock's source
      // node is a sink of its master (the master's tree flags
      // it with generates=): the shared count comes from the
      // query's shared lines, 0 when there is no line.
      const ClockNetworkClock* master = find_clock(clock.master);
      bool source_is_sink = false;
      if (master != nullptr) {
        for (const ClockNetworkNode& node : master->nodes) {
          if (node.generates == clock.name) source_is_sink = true;
        }
      }
      if (source_is_sink) {
        long shared = 0;
        for (const ClockNetworkShared& line : model.shared()) {
          if ((line.clock == clock.master && line.other == clock.name) ||
              (line.clock == clock.name && line.other == clock.master)) {
            shared = static_cast<long>(line.count);
          }
        }
        std::string source_text = unbrace(clock.sources);
        for (const ClockNetworkNode& node : clock.nodes) {
          if (node.kind == "source") source_text = node.name;
        }
        out += "  " + clock.name + " is generated from " + source_text +
               ", a sink of " + clock.master +
               ". The two trees share " + std::to_string(shared) +
               " sinks.\n";
      }
    }
  }
  if (!notes.empty()) out += "\n" + notes + "\n";
  return out;
}

// The Clocks tab's CTS section: the session's recorded cts
// runs, appended under the clock trees when the CTS model is
// loaded, and nothing when it is not (the tab's stage line
// and notes already state the pre-CTS truth). The section is
// keyed by net name and the tree blocks above it by clock
// name; on the clocked design the two differ for the
// generated clock (net div_q, clock clk_div), so the section
// stands on its own rather than joining the blocks (F5
// design section 4.3). Counts are the record's: sinks counts
// every load pin on the net, and when some of those loads
// are not clock pins the clock-pin count is named too. A run
// whose STA leg could not analyze shows its unavailable
// statement verbatim instead of a slack line.
std::string RenderCts(const CtsModel& model) {
  if (!model.Loaded()) return "";
  std::string out = "\nCTS (this session):\n";
  for (const CtsNet& net : model.Nets()) {
    out += "  net " + net.name + ": roots " +
           std::to_string(net.roots) + ", buffers " +
           std::to_string(net.buffers) + ", sinks " +
           std::to_string(net.sinks);
    if (net.clock_sinks != net.sinks) {
      out += " (" + std::to_string(net.clock_sinks) + " clock pins)";
    }
    out += ", subnets " + std::to_string(net.subnets) +
           ", Liberty corner " + net.liberty_corner + "\n";
    for (const CtsSlack& slack : net.slacks) {
      out += "    post-CTS worst slack (" + slack.scene +
             "): " + FormatSlack(slack.worst_slack_ns) + " ns\n";
    }
    if (!net.sta_note.empty()) {
      out += "    " + net.sta_note + "\n";
    }
  }
  return out;
}

// The Timing tab's Extraction section (full-flow GUI unit
// F7): the recorded extract run under the per-scene timing
// lines it pairs, in its own labeled block so the section
// stands on its own rather than joining the blocks (the F5
// shape). Counts are the record's. A run whose STA leg could
// not analyze shows its unavailable statement verbatim
// instead of a slack line. The comparison lines pair the
// estimated and extracted worst slacks per (mode, corner);
// when no pair has run yet the section says so, so the panel
// never implies a comparison that has not run. The word is
// "extracted", never a signoff word: the mock model's values
// are invented fixture values.
std::string RenderExtract(const ExtractModel& model) {
  if (!model.Loaded()) return "";
  std::string out = "\nExtraction (post-route):\n";
  out += "  nets " + std::to_string(model.Nets()) + ", RSEGs " +
         std::to_string(model.Rsegs()) + ", capacitance nodes " +
         std::to_string(model.CapNodes()) +
         ", coupling segments " + std::to_string(model.CcSegs()) +
         "\n";
  out += "  SPEF: " + model.SpefPath() + "\n";
  out += "  model: " + model.ModelPath() + "\n";
  for (const ExtractSlack& slack : model.Slacks()) {
    out += "  post-route estimated worst slack (" + slack.scene +
           "): " + FormatSlack(slack.worst_slack_ns) + " ns\n";
  }
  if (!model.StaNote().empty()) {
    out += "  " + model.StaNote() + "\n";
  }
  if (model.Compares().empty()) {
    out += "  extracted comparison: not run yet (create the "
           "extracted scenario, set_spef, and run_timing)\n";
  }
  for (const ExtractCompare& compare : model.Compares()) {
    out += "  " + compare.mode + "/" + compare.corner +
           ": estimated " +
           FormatSlack(compare.estimated_worst_slack_ns) +
           " ns (scene " + compare.estimate_scene +
           ") vs extracted " +
           FormatSlack(compare.extracted_worst_slack_ns) +
           " ns (scene " + compare.extracted_scene + "); hold " +
           FormatSlack(compare.estimated_worst_hold_slack_ns) +
           " ns vs " +
           FormatSlack(compare.extracted_worst_hold_slack_ns) +
           " ns\n";
  }
  return out;
}

// The Signoff tab's Finish section (full-flow GUI unit
// F8): the recorded finish run and, once an export exists,
// the recorded export, appended after the signoff verdicts
// in their own labeled block. F8 renders no verdict of its
// own (D14): the section reports the two records, nothing
// more. Each half renders its not-run-yet line while its
// record is empty, so the section never implies a run that
// has not happened.
// Formats one ECO scene metric the way the tab's other slack
// lines format theirs, with the payload's own word for a
// metric the analysis did not produce.
std::string FormatEcoMetric(const std::optional<double>& value) {
  if (!value.has_value()) return "none";
  return FormatSlack(*value);
}

std::string RenderEco(const EcoModel& model) {
  if (!model.Loaded()) return "";
  std::string out = "\nECO (timing closure):\n";
  out += std::string("  ") + (model.Closed() ? "closed" : "not closed") +
         ", iterations " + std::to_string(model.Iterations()) + " of " +
         std::to_string(model.MaxIterations()) + ", candidates tried " +
         std::to_string(model.CandidatesTried()) + ", rejected " +
         std::to_string(model.CandidatesRejected()) + ", skipped stale " +
         std::to_string(model.CandidatesSkippedStale()) +
         ", skipped memory " +
         std::to_string(model.CandidatesSkippedMemory()) + "\n";
  for (const EcoSceneLine& scene : model.Scenes()) {
    out += "  " + scene.name + ": worst slack " +
           FormatEcoMetric(scene.initial_worst_slack_ns) + " ns -> " +
           FormatEcoMetric(scene.worst_slack_ns) + " ns; worst hold slack " +
           FormatEcoMetric(scene.initial_worst_hold_slack_ns) + " ns -> " +
           FormatEcoMetric(scene.worst_hold_slack_ns) + " ns\n";
  }
  if (model.Steps().empty()) {
    out += "  no edits were accepted\n";
  } else {
    out += "  accepted edits:\n";
    for (const std::string& step : model.Steps()) {
      out += "    " + step + "\n";
    }
  }
  out += "  stop reason: " + model.StopReason() + "\n";
  return out;
}

std::string RenderFinish(const FinishModel& finish,
                         const GdsModel& gds) {
  std::string out = "\nFinish:\n";
  if (!finish.Loaded()) {
    out += "  (no finish run yet: run eos::finish first)\n";
  } else {
    out += "  fillers " + std::to_string(finish.Fillers()) +
           ", grid shapes " + std::to_string(finish.GridShapes()) +
           ", metal fills " + std::to_string(finish.MetalFills()) +
           ", pdn " + (finish.Pdn() ? "true" : "false") + "\n";
  }
  if (!gds.Loaded()) {
    out += "  (no GDS exported yet: run eos::write_gds first)\n";
  } else {
    out += "  GDS: " + gds.Path() + " (" +
           std::to_string(gds.Size()) + " bytes), " +
           std::to_string(gds.Structures()) + " structures, " +
           std::to_string(gds.Instances()) + " instances, library " +
           gds.Library() + "\n";
  }
  return out;
}

std::string IssueKindText(const std::string& kind) {
  if (kind == "extraction_artifact") return "extraction artifact";
  if (kind == "resolved_artifact") {
    return "resolved as an extraction artifact";
  }
  return kind;
}

std::string RenderSignoff(const SignoffModel& signoff) {
  if (!signoff.Loaded()) {
    return "(no signoff verdicts yet: run eos::signoff_summary first)";
  }
  std::string out;
  if (signoff.Stale()) {
    out += "STALE: the design changed since these verdicts were "
           "recorded. Re-run the checks and refresh.\n";
  }
  out += "DRC: ";
  out += signoff.DrcPass() ? "PASS" : "FAIL";
  out += ", " + std::to_string(signoff.DrcViolations()) +
         " violations in " + std::to_string(signoff.DrcCategories()) +
         " categories (";
  out += signoff.DrcSource() == "intree" ? "live-database"
                                         : signoff.PvReport();
  out += ")\n";
  for (const SignoffDrcFamily& family : signoff.DrcFamilies()) {
    out += "  " + family.family + ": " +
           std::to_string(family.categories) + " categories, " +
           std::to_string(family.violations) + " violations\n";
  }
  for (const SignoffDrcViolation& violation :
       signoff.DrcViolationList()) {
    out += "  " + violation.check + " " + violation.layer + " " +
           violation.net + " " + violation.detail + " bbox " +
           std::to_string(violation.x1) + "," +
           std::to_string(violation.y1) + "," +
           std::to_string(violation.x2) + "," +
           std::to_string(violation.y2) + "\n";
  }
  if (signoff.LvsSource() == "intree") {
    out += "LVS (connectivity, in-tree): ";
    out += signoff.LvsPass() ? "PASS" : "FAIL";
    out += ", " + std::to_string(signoff.LvsMismatches()) +
           " mismatches, " +
           std::to_string(signoff.LvsNetsCompared()) +
           " nets compared, " +
           std::to_string(signoff.LvsPinsCompared()) +
           " pins compared (live-database)\n";
    for (const char* kind : {"open", "short", "floating"}) {
      out += "  " + std::string(kind) + ": " +
             std::to_string(signoff.LvsKindCount(kind)) +
             " mismatches\n";
    }
    for (const SignoffLvsMismatch& mismatch :
         signoff.LvsMismatchList()) {
      out += "  " + mismatch.kind + " net=" + mismatch.net;
      if (mismatch.kind == "open") {
        out += " pin=" + mismatch.pin;
      } else if (mismatch.kind == "short") {
        out += " other_net=" + mismatch.other_net +
               " layer=" + mismatch.layer;
      } else {
        out += " layer=" + mismatch.layer;
      }
      out += " bbox " + std::to_string(mismatch.x1) + "," +
             std::to_string(mismatch.y1) + "," +
             std::to_string(mismatch.x2) + "," +
             std::to_string(mismatch.y2) + "\n";
    }
  } else {
    out += "LVS: ";
    out += signoff.LvsPass() ? "PASS" : "NOT CLEAN";
    out += " (" + signoff.PvReport() + ", " + signoff.LvsReport() + ")\n";
    for (const SignoffLvsPath& path : signoff.LvsPaths()) {
      out += "  " + path.path + ": " +
             std::to_string(path.classes_equivalent) + " of " +
             std::to_string(path.classes_compared) +
             " classes equivalent";
      if (path.has_fingers) {
        out += ", " + std::to_string(path.fingers_extracted) +
               " fingers vs " + std::to_string(path.fingers_reference);
      }
      out += path.pass ? ": PASS\n" : ": NOT CLEAN\n";
    }
    for (const SignoffLvsIssue& issue : signoff.LvsIssues()) {
      out += "  " + issue.issue + " (" + issue.path + "): " +
             IssueKindText(issue.kind);
      if (issue.cell != "none") out += ", " + issue.cell;
      out += "\n";
    }
    if (signoff.HasResidual()) {
      const SignoffLvsResidual& residual = signoff.Residual();
      out += "  Residual (magic): " + residual.cell + ", " +
             std::to_string(residual.transistors) + " transistors in " +
             std::to_string(residual.instances) + " instances, cause " +
             residual.cause + "\n";
    }
  }
  out += "Antenna: ";
  out += signoff.AntennaPass() ? "PASS" : "FAIL";
  out += ", " + std::to_string(signoff.AntennaNetViolations()) +
         " net violations, " +
         std::to_string(signoff.AntennaPinViolations()) +
         " pin violations (" + signoff.PvReport() + ")\n";
  out += "Fingerprint: " + signoff.Fingerprint() + "\n";
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
  timing_ = AddTab(this, "Timing");
  clocks_ = AddTab(this, "Clocks");
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
  signoff_ = AddTab(this, "Signoff");
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
  timing_->setPlainText(QString::fromStdString(
      RenderTiming(view_.Timing()) +
      RenderExtract(view_.Extract()) +
      RenderEco(view_.Eco())));
  clocks_->setPlainText(QString::fromStdString(
      RenderClockNetwork(view_.ClockNetwork()) +
      RenderCts(view_.Cts())));
  optlog_->setPlainText(QString::fromStdString(RenderOptLog(panels)));
  signoff_->setPlainText(QString::fromStdString(
      RenderSignoff(view_.Signoff()) +
      RenderFinish(view_.Finish(), view_.Gds())));
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
  // Locating an issue navigates the hierarchy: it clears the
  // floorplan and route stages, so the center shows the
  // schematic the issue belongs to.
  view_.ShowFloorplan(false);
  view_.ShowRoute(false);
  view_.ShowGds(false);
  view_.RefreshSchematic();
  if (view_.DiagramLevelIsLeaf() && on_highlight_gate_) {
    on_highlight_gate_(target);
  }
  if (on_action_) on_action_();
}

}  // namespace eos::ui
