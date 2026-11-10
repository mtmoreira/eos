// Unit tests: ui/registry and the core command set.
#include "eos/ui/registry.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "eos/db/odb.h"

namespace eos::ui {
namespace {

struct Shell {
  CommandRegistry registry;
  CommandContext ctx;
  Shell() { RegisterCoreCommands(registry); }
  CommandResult Run(const std::string& name,
                    std::vector<std::string> args = {}) {
    return registry.Dispatch(ctx, name, args);
  }
};

TEST(Registry, RegisterAndDispatchCustomCommand) {
  CommandRegistry registry;
  const foundation::Status added = registry.Register(
      "ping", "ping",
      [](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Ok("pong=1");
      });
  ASSERT_TRUE(added.ok());
  const foundation::Status dup = registry.Register(
      "ping", "ping",
      [](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Ok("pong=2");
      });
  EXPECT_FALSE(dup.ok());
  CommandContext ctx;
  const CommandResult result = registry.Dispatch(ctx, "ping", {});
  ASSERT_TRUE(result.ok);
  EXPECT_EQ(result.payload, "pong=1");
}

TEST(Registry, UnknownCommandIsAnInvalidCommandDiagnostic) {
  Shell shell;
  const CommandResult result = shell.Run("nope");
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidCommand);
  EXPECT_EQ(std::string(DiagName(result.diag)), "invalid_command");
}

TEST(Registry, CoreSetIsComplete) {
  Shell shell;
  // The count is a contract: adding a command means updating it.
  // S1 adds elaborate and synth_generic; S2 adds synth_map; S3
  // adds synth_opt; S4 adds read_filelist; S7 adds
  // write_verilog, check_equivalence, and synth_report; P1 adds
  // floorplan; U3 adds synth_blocks; U4 adds synth_schematic;
  // U5 adds synth_stats; PR4 adds repair_timing and set_wire_rc;
  // P2 adds place; P3 adds cts; P4 adds route; P6 adds
  // extract_parasitics; P7 adds finish and write_gds; Immediate
  // unit 3 renames create_clock to bind_clock (same slot) and
  // adds create_clock (SDC), create_generated_clock, and the
  // nine M27 commands; Immediate unit 4 adds check_timing;
  // gui_show adds gui_show; Immediate unit 5 adds report_clock.
  // The panel rework adds synth_issues; Immediate unit 6 adds
  // report_constraint; Immediate unit 7 adds
  // report_analysis_coverage; Immediate unit 8 adds write_sdc;
  // Immediate unit 9 adds report_qor, report_bottleneck, and
  // set_bottleneck_capture (three commands: the two reports plus
  // the capture setting the bottleneck report requires before
  // the first timing update).
  // F0 adds write_def, read_lef, and read_def. Immediate unit
  // 10 adds the five check-specific reports:
  // report_global_timing, report_global_slack, report_min_period,
  // report_min_pulse_width and report_clock_gating_check. F2 adds
  // timing_summary. F1 adds synth_summary.
  // Immediate unit 11 adds the twenty collection commands: the
  // C1 vocabulary (get_cells, get_nets, get_pins, get_ports,
  // get_clocks, all_inputs, all_outputs, all_registers,
  // all_clocks), the C2 filter and attribute commands
  // (filter_collection, sort_collection, get_attribute,
  // set_attribute), and the C3 algebra (add_to_collection,
  // append_to_collection, remove_from_collection,
  // compare_collections, index_collection, query_objects,
  // foreach_in_collection). get_objects is the generic alias and
  // exists already, so it adds no pin. F3 adds floorplan_summary.
  // F4 adds place_summary. F13 adds signoff_summary.
  EXPECT_EQ(shell.registry.Names().size(), 114u);
  EXPECT_NE(shell.registry.Find("finish"), nullptr);
  EXPECT_NE(shell.registry.Find("write_gds"), nullptr);
  EXPECT_NE(shell.registry.Find("extract_parasitics"), nullptr);
  EXPECT_NE(shell.registry.Find("bind_clock"), nullptr);
  EXPECT_NE(shell.registry.Find("create_clock"), nullptr);
  EXPECT_NE(shell.registry.Find("create_generated_clock"), nullptr);
  EXPECT_NE(shell.registry.Find("gui_show"), nullptr);
  EXPECT_NE(shell.registry.Find("check_timing"), nullptr);
  EXPECT_NE(shell.registry.Find("report_clock"), nullptr);
  EXPECT_NE(shell.registry.Find("synth_issues"), nullptr);
  EXPECT_NE(shell.registry.Find("report_constraint"), nullptr);
  EXPECT_NE(shell.registry.Find("report_analysis_coverage"), nullptr);
  EXPECT_NE(shell.registry.Find("report_qor"), nullptr);
  EXPECT_NE(shell.registry.Find("report_bottleneck"), nullptr);
  EXPECT_NE(shell.registry.Find("set_bottleneck_capture"), nullptr);
  EXPECT_NE(shell.registry.Find("report_global_timing"), nullptr);
  EXPECT_NE(shell.registry.Find("report_global_slack"), nullptr);
  EXPECT_NE(shell.registry.Find("report_min_period"), nullptr);
  EXPECT_NE(shell.registry.Find("report_min_pulse_width"), nullptr);
  EXPECT_NE(shell.registry.Find("report_clock_gating_check"), nullptr);
  EXPECT_NE(shell.registry.Find("get_cells"), nullptr);
  EXPECT_NE(shell.registry.Find("get_nets"), nullptr);
  EXPECT_NE(shell.registry.Find("get_pins"), nullptr);
  EXPECT_NE(shell.registry.Find("get_ports"), nullptr);
  EXPECT_NE(shell.registry.Find("get_clocks"), nullptr);
  EXPECT_NE(shell.registry.Find("all_inputs"), nullptr);
  EXPECT_NE(shell.registry.Find("all_outputs"), nullptr);
  EXPECT_NE(shell.registry.Find("all_registers"), nullptr);
  EXPECT_NE(shell.registry.Find("all_clocks"), nullptr);
  EXPECT_NE(shell.registry.Find("filter_collection"), nullptr);
  EXPECT_NE(shell.registry.Find("sort_collection"), nullptr);
  EXPECT_NE(shell.registry.Find("get_attribute"), nullptr);
  EXPECT_NE(shell.registry.Find("set_attribute"), nullptr);
  EXPECT_NE(shell.registry.Find("add_to_collection"), nullptr);
  EXPECT_NE(shell.registry.Find("append_to_collection"), nullptr);
  EXPECT_NE(shell.registry.Find("remove_from_collection"), nullptr);
  EXPECT_NE(shell.registry.Find("compare_collections"), nullptr);
  EXPECT_NE(shell.registry.Find("index_collection"), nullptr);
  EXPECT_NE(shell.registry.Find("query_objects"), nullptr);
  EXPECT_NE(shell.registry.Find("foreach_in_collection"), nullptr);
  EXPECT_NE(shell.registry.Find("floorplan"), nullptr);
  EXPECT_NE(shell.registry.Find("synth_stats"), nullptr);
  EXPECT_NE(shell.registry.Find("synth_summary"), nullptr);
  EXPECT_NE(shell.registry.Find("read_filelist"), nullptr);
  EXPECT_NE(shell.registry.Find("write_verilog"), nullptr);
  EXPECT_NE(shell.registry.Find("check_equivalence"), nullptr);
  EXPECT_NE(shell.registry.Find("synth_report"), nullptr);
  EXPECT_NE(shell.registry.Find("synth_opt"), nullptr);
  EXPECT_NE(shell.registry.Find("create_design"), nullptr);
  EXPECT_NE(shell.registry.Find("restore"), nullptr);
  EXPECT_NE(shell.registry.Find("report_timing"), nullptr);
  EXPECT_NE(shell.registry.Find("synth_map"), nullptr);
  EXPECT_NE(shell.registry.Find("help"), nullptr);
  EXPECT_NE(shell.registry.Find("gui_show"), nullptr);
}

TEST(CoreCommands, DesignLifecycleAndUnclosedDesignDiagnostic) {
  Shell shell;
  const CommandResult no_design = shell.Run("create_net", {"n1"});
  ASSERT_FALSE(no_design.ok);
  EXPECT_EQ(no_design.diag, Diag::kUnclosedDesign);

  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  const CommandResult again = shell.Run("create_design", {"other"});
  ASSERT_FALSE(again.ok);
  EXPECT_EQ(again.diag, Diag::kInvalidConfiguration);

  const CommandResult current = shell.Run("current_design");
  ASSERT_TRUE(current.ok);
  EXPECT_NE(current.payload.find("design=top"), std::string::npos);
  EXPECT_NE(current.payload.find("revision=0"), std::string::npos);
  ASSERT_TRUE(shell.Run("close_design").ok);
  EXPECT_EQ(shell.Run("current_design").diag, Diag::kUnclosedDesign);
}

TEST(CoreCommands, BuildADesignThroughCommands) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_cell", {"dff"}).ok);
  ASSERT_TRUE(shell.Run("create_cell", {"pad", "-macro"}).ok);
  ASSERT_TRUE(shell.Run("create_instance", {"u1", "dff"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"clk_net"}).ok);
  const CommandResult pin = shell.Run("connect", {"u1", "CK", "clk_net"});
  ASSERT_TRUE(pin.ok);
  EXPECT_NE(pin.payload.find("name=u1/CK"), std::string::npos);

  const CommandResult bad_cell = shell.Run("create_instance", {"u2", "nope"});
  ASSERT_FALSE(bad_cell.ok);
  EXPECT_EQ(bad_cell.diag, Diag::kInvalidConfiguration);
  const CommandResult bad_dir = shell.Run("create_port", {"x", "sideways"});
  EXPECT_FALSE(bad_dir.ok);
}

TEST(CoreCommands, UnsupportedOptionsAreRejectedNeverIgnored) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  const CommandResult result = shell.Run("create_cell", {"c1", "-bogus"});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(result.message.find("unsupported option"),
            std::string::npos);
  // And the rejected command created nothing.
  EXPECT_TRUE(shell.ctx.project.design
                  ->Find(foundation::ObjectKind::kCell, "c1")
                  .is_null());
}

TEST(CoreCommands, ConstraintsReportTheirBindingHonestly) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  const CommandResult bound = shell.Run("bind_clock", {"c1", "clk"});
  ASSERT_TRUE(bound.ok);
  EXPECT_NE(bound.payload.find("status=bound"), std::string::npos);

  const CommandResult unresolved =
      shell.Run("bind_clock", {"c2", "missing_clk"});
  ASSERT_TRUE(unresolved.ok);  // the command succeeded...
  EXPECT_NE(unresolved.payload.find("status=unresolved"),
            std::string::npos);  // ...and says so, loudly.

  const CommandResult coverage = shell.Run("constraint_coverage");
  ASSERT_TRUE(coverage.ok);
  EXPECT_NE(coverage.payload.find("total=2"), std::string::npos);
  EXPECT_NE(coverage.payload.find("complete=0"), std::string::npos);

  ASSERT_TRUE(shell.Run("create_port", {"missing_clk", "in"}).ok);
  const CommandResult rebound = shell.Run("rebind_constraints");
  ASSERT_TRUE(rebound.ok);
  EXPECT_NE(rebound.payload.find("bound=2"), std::string::npos);
  EXPECT_NE(rebound.payload.find("complete=1"), std::string::npos);
}

TEST(CoreCommands, CollectionsAreLiveHandlesWithGlobMatching) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"n1"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"n2"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"clk_net"}).ok);
  const CommandResult got = shell.Run("get_objects", {"net", "n*"});
  ASSERT_TRUE(got.ok);
  EXPECT_NE(got.payload.find("collection=eos_coll_1"), std::string::npos);
  EXPECT_NE(got.payload.find("count=2"), std::string::npos);
  const CommandResult size = shell.Run("sizeof_collection", {"eos_coll_1"});
  ASSERT_TRUE(size.ok);
  EXPECT_EQ(size.payload, "count=2");
  const CommandResult list = shell.Run("list_collection", {"eos_coll_1"});
  ASSERT_TRUE(list.ok);
  EXPECT_EQ(list.payload, "n1\nn2");
  const CommandResult unknown = shell.Run("sizeof_collection", {"nope"});
  ASSERT_FALSE(unknown.ok);
}

TEST(CoreCommands, TransactionsAndReportsThroughCommands) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  const CommandResult report =
      shell.Run("register_report", {"timing", "pass"});
  ASSERT_TRUE(report.ok);
  EXPECT_NE(report.payload.find("artifact=1"), std::string::npos);

  ASSERT_TRUE(shell.Run("begin_transaction", {"place"}).ok);
  const CommandResult twice = shell.Run("begin_transaction", {"again"});
  EXPECT_FALSE(twice.ok);
  const CommandResult committed = shell.Run("commit_transaction", {"pass"});
  ASSERT_TRUE(committed.ok);
  EXPECT_EQ(committed.payload, "revision=1");

  const CommandResult status = shell.Run("report_status", {"1"});
  ASSERT_TRUE(status.ok);
  EXPECT_NE(status.payload.find("valid=1"), std::string::npos);
  EXPECT_NE(status.payload.find("current=0"), std::string::npos);

  ASSERT_TRUE(shell.Run("rollback", {"0"}).ok);
  EXPECT_EQ(shell.Run("current_revision").payload, "revision=0");
  const CommandResult restored = shell.Run("report_status", {"1"});
  EXPECT_NE(restored.payload.find("current=1"), std::string::npos);
}

TEST(CoreCommands, CheckpointAndRestoreRoundTripTheWholeProject) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(shell.Run("bind_clock", {"c1", "clk"}).ok);
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(shell.Run("begin_transaction", {"place"}).ok);
  ASSERT_TRUE(shell.Run("commit_transaction", {"pass"}).ok);
  ASSERT_TRUE(shell.Run("register_report", {"timing", "pass"}).ok);
  const CommandResult before = shell.Run("current_design");
  ASSERT_TRUE(before.ok);

  const std::string path = "registry_test_project.eosproj";
  std::remove(path.c_str());
  const CommandResult saved = shell.Run("checkpoint", {path});
  ASSERT_TRUE(saved.ok);
  EXPECT_EQ(saved.payload, "checkpoint=" + path);

  // Restore into a fresh context reproduces the project: design,
  // scenarios, constraints and the ledger revision alike.
  Shell fresh;
  const CommandResult restored = fresh.Run("restore", {path});
  ASSERT_TRUE(restored.ok);
  EXPECT_NE(restored.payload.find("design=top"), std::string::npos);
  EXPECT_NE(restored.payload.find("revision=1"), std::string::npos);
  EXPECT_NE(restored.payload.find("unresolved=0"), std::string::npos);
  // The report registered before the checkpoint is project state:
  // its binding restores with the project and the gate passes it in
  // the fresh context (Phase 2 unit 2.0).
  EXPECT_NE(restored.payload.find("bindings_restored=1"), std::string::npos);
  EXPECT_NE(restored.payload.find("bindings_dropped=0"), std::string::npos);
  const CommandResult gate = fresh.Run("export_check", {"timing"});
  ASSERT_TRUE(gate.ok);
  EXPECT_NE(gate.payload.find("export=pass"), std::string::npos);
  const CommandResult after = fresh.Run("current_design");
  ASSERT_TRUE(after.ok);
  EXPECT_EQ(after.payload, before.payload);
  EXPECT_NE(fresh.Run("list_scenarios").payload.find("func_tt"),
            std::string::npos);
  EXPECT_NE(fresh.Run("list_constraints").payload.find("status=bound"),
            std::string::npos);
  const CommandResult report = fresh.Run("report_status", {"1"});
  ASSERT_TRUE(report.ok);
  EXPECT_NE(report.payload.find("current=1"), std::string::npos);

  // A corrupt project file is refused and leaves the live project
  // exactly as it was.
  Shell live;
  ASSERT_TRUE(live.Run("create_design", {"live"}).ok);
  {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string corrupt = buffer.str();
    const std::size_t mid = corrupt.size() / 2;
    corrupt[mid] = corrupt[mid] == 'a' ? 'b' : 'a';
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << corrupt;
  }
  const CommandResult refused = live.Run("restore", {path});
  EXPECT_FALSE(refused.ok);
  const CommandResult still = live.Run("current_design");
  ASSERT_TRUE(still.ok);
  EXPECT_NE(still.payload.find("design=live"), std::string::npos);
  std::remove(path.c_str());
}

TEST(CoreCommands, ExportCheckPassesAndNamesEveryBlockerKind) {
  // The passing form: a current, proven report and complete
  // coverage carry the verified_pass diagnostic.
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(shell.Run("bind_clock", {"c1", "clk"}).ok);
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(shell.Run("register_report", {"timing", "pass"}).ok);
  const CommandResult pass = shell.Run("export_check", {"timing"});
  ASSERT_TRUE(pass.ok);
  EXPECT_EQ(pass.diag, Diag::kVerifiedPass);
  EXPECT_EQ(std::string(DiagName(pass.diag)), "verified_pass");
  EXPECT_NE(pass.payload.find("export=pass"), std::string::npos);

  // Missing: proof_unknown, and the blocker names the report.
  const CommandResult missing = shell.Run("export_check", {"nope"});
  ASSERT_FALSE(missing.ok);
  EXPECT_EQ(missing.diag, Diag::kProofUnknown);
  EXPECT_NE(missing.payload.find("blocker=missing"), std::string::npos);
  EXPECT_NE(missing.payload.find("report=nope"), std::string::npos);

  // Stale: a commit that does not invalidate ages the report out.
  ASSERT_TRUE(shell.Run("begin_transaction", {"place"}).ok);
  ASSERT_TRUE(shell.Run("commit_transaction", {"pass"}).ok);
  const CommandResult stale = shell.Run("export_check", {"timing"});
  ASSERT_FALSE(stale.ok);
  EXPECT_EQ(stale.diag, Diag::kProofUnknown);
  EXPECT_NE(stale.payload.find("blocker=stale"), std::string::npos);

  // Coverage: an unresolved constraint blocks even a fresh report.
  Shell uncovered;
  ASSERT_TRUE(uncovered.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(uncovered.Run("bind_clock", {"c1", "missing_clk"}).ok);
  ASSERT_TRUE(uncovered.Run("register_report", {"timing", "pass"}).ok);
  const CommandResult blocked = uncovered.Run("export_check", {"timing"});
  ASSERT_FALSE(blocked.ok);
  EXPECT_NE(blocked.payload.find("blocker=coverage_incomplete"),
            std::string::npos);

  // Proof: a report registered without a pass blocks on its proof.
  Shell unproven;
  ASSERT_TRUE(unproven.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(unproven.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(unproven.Run("bind_clock", {"c1", "clk"}).ok);
  ASSERT_TRUE(unproven.Run("register_report", {"timing"}).ok);
  const CommandResult proof = unproven.Run("export_check", {"timing"});
  ASSERT_FALSE(proof.ok);
  EXPECT_NE(proof.payload.find("blocker=proof_not_pass"),
            std::string::npos);

  // export_check with no reports named is a usage error, and with
  // no design open it is the unclosed-design diagnostic.
  Shell empty;
  EXPECT_EQ(empty.Run("export_check").diag, Diag::kInvalidConfiguration);
  EXPECT_EQ(empty.Run("export_check", {"timing"}).diag,
            Diag::kUnclosedDesign);
}

TEST(CoreCommands, ScenariosAndListCommands) {
  Shell shell;
  const CommandResult made =
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"});
  ASSERT_TRUE(made.ok);
  const CommandResult dup =
      shell.Run("create_scenario", {"other", "func", "tt", "estimate"});
  ASSERT_FALSE(dup.ok);
  const CommandResult list = shell.Run("list_scenarios");
  EXPECT_NE(list.payload.find("func_tt"), std::string::npos);
  const CommandResult commands = shell.Run("list_commands");
  ASSERT_TRUE(commands.ok);
  EXPECT_NE(commands.payload.find("create_design"), std::string::npos);
  EXPECT_NE(commands.payload.find("checkpoint"), std::string::npos);
}

namespace {

// The unit 2.2 STA fixture, reused for the timing commands: the
// reg-to-reg delay is 0.6 ns by construction, so a 10 ns clock
// gives worst slack 9.4 ns (the pinned upstream value).
const char kTimingLiberty[] = R"LIB(library (eos_fixture) {
  delay_model : "table_lookup";
  time_unit : "1ns";
  voltage_unit : "1v";
  capacitive_load_unit (1,pF);
  current_unit : "1mA";
  pulling_resistance_unit : "1kohm";
  leakage_power_unit : "1pW";
  input_threshold_pct_rise : 50;
  input_threshold_pct_fall : 50;
  output_threshold_pct_rise : 50;
  output_threshold_pct_fall : 50;
  slew_lower_threshold_pct_rise : 30;
  slew_lower_threshold_pct_fall : 30;
  slew_upper_threshold_pct_rise : 70;
  slew_upper_threshold_pct_fall : 70;
  slew_derate_from_library : 1.0;
  nom_process : 1.0;
  nom_temperature : 25.0;
  nom_voltage : 1.0;
  cell (DFF) {
    area : 10.0;
    ff (IQ, IQN) {
      next_state : "D";
      clocked_on : "CK";
    }
    pin (D) {
      direction : input;
      capacitance : 0.001;
      timing () {
        related_pin : "CK";
        timing_type : setup_rising;
        rise_constraint (scalar) { values ("0.1"); }
        fall_constraint (scalar) { values ("0.1"); }
      }
      timing () {
        related_pin : "CK";
        timing_type : hold_rising;
        rise_constraint (scalar) { values ("0.05"); }
        fall_constraint (scalar) { values ("0.05"); }
      }
    }
    pin (CK) {
      direction : input;
      capacitance : 0.001;
      clock : true;
    }
    pin (Q) {
      direction : output;
      function : "IQ";
      timing () {
        related_pin : "CK";
        timing_type : rising_edge;
        cell_rise (scalar) { values ("0.2"); }
        cell_fall (scalar) { values ("0.2"); }
        rise_transition (scalar) { values ("0.1"); }
        fall_transition (scalar) { values ("0.1"); }
      }
    }
  }
  cell (BUF) {
    area : 2.0;
    pin (A) {
      direction : input;
      capacitance : 0.001;
    }
    pin (Y) {
      direction : output;
      function : "A";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.3"); }
        cell_fall (scalar) { values ("0.3"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
}
)LIB";

const char kTimingVerilog[] = R"V(module top (clk, d, q);
  input clk, d;
  output q;
  wire n1, n2;
  DFF r1 (.D(d), .CK(clk), .Q(n1));
  BUF u1 (.A(n1), .Y(n2));
  DFF r2 (.D(n2), .CK(clk), .Q(q));
endmodule
)V";

const char kTimingSdc[] =
    "create_clock -name clk -period 10 [get_ports clk]\n";

// The two-path fixture library for get_timing_paths: the base
// fixture cells plus a MUX whose A input is the slow arc
// (0.4 ns) and whose B input is the fast one (0.1 ns).
const char kTimingLibertyTwoPath[] = R"LIB(library (eos_fixture) {
  delay_model : "table_lookup";
  time_unit : "1ns";
  voltage_unit : "1v";
  capacitive_load_unit (1,pF);
  current_unit : "1mA";
  pulling_resistance_unit : "1kohm";
  leakage_power_unit : "1pW";
  input_threshold_pct_rise : 50;
  input_threshold_pct_fall : 50;
  output_threshold_pct_rise : 50;
  output_threshold_pct_fall : 50;
  slew_lower_threshold_pct_rise : 30;
  slew_lower_threshold_pct_fall : 30;
  slew_upper_threshold_pct_rise : 70;
  slew_upper_threshold_pct_fall : 70;
  slew_derate_from_library : 1.0;
  nom_process : 1.0;
  nom_temperature : 25.0;
  nom_voltage : 1.0;
  cell (DFF) {
    area : 10.0;
    ff (IQ, IQN) {
      next_state : "D";
      clocked_on : "CK";
    }
    pin (D) {
      direction : input;
      capacitance : 0.001;
      timing () {
        related_pin : "CK";
        timing_type : setup_rising;
        rise_constraint (scalar) { values ("0.1"); }
        fall_constraint (scalar) { values ("0.1"); }
      }
      timing () {
        related_pin : "CK";
        timing_type : hold_rising;
        rise_constraint (scalar) { values ("0.05"); }
        fall_constraint (scalar) { values ("0.05"); }
      }
    }
    pin (CK) {
      direction : input;
      capacitance : 0.001;
      clock : true;
    }
    pin (Q) {
      direction : output;
      function : "IQ";
      timing () {
        related_pin : "CK";
        timing_type : rising_edge;
        cell_rise (scalar) { values ("0.2"); }
        cell_fall (scalar) { values ("0.2"); }
        rise_transition (scalar) { values ("0.1"); }
        fall_transition (scalar) { values ("0.1"); }
      }
    }
  }
  cell (BUF) {
    area : 2.0;
    pin (A) {
      direction : input;
      capacitance : 0.001;
    }
    pin (Y) {
      direction : output;
      function : "A";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.3"); }
        cell_fall (scalar) { values ("0.3"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
  cell (MUX) {
    area : 3.0;
    pin (A) {
      direction : input;
      capacitance : 0.001;
    }
    pin (B) {
      direction : input;
      capacitance : 0.001;
    }
    pin (S) {
      direction : input;
      capacitance : 0.001;
    }
    pin (Y) {
      direction : output;
      function : "(A*S)+(B*!S)";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.4"); }
        cell_fall (scalar) { values ("0.4"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
      timing () {
        related_pin : "B";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.1"); }
        cell_fall (scalar) { values ("0.1"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
}
)LIB";

// The two-path fixture netlist: two flops drive r2/D through a
// mux, so two distinct paths reach one endpoint. The slow path
// (via r1, u1, m1/A) arrives at 0.2 + 0.3 + 0.4 = 0.9 ns and
// slacks 9.0 ns; the fast path (via r3, u2, m1/B) arrives at
// 0.2 + 0.3 + 0.1 = 0.6 ns and slacks 9.3 ns.
const char kTimingVerilogTwoPath[] = R"V(module top (clk, d, d2, sel, q);
  input clk, d, d2, sel;
  output q;
  wire n1, n2, n3, n4, n5;
  DFF r1 (.D(d), .CK(clk), .Q(n1));
  BUF u1 (.A(n1), .Y(n2));
  DFF r3 (.D(d2), .CK(clk), .Q(n3));
  BUF u2 (.A(n3), .Y(n4));
  MUX m1 (.A(n2), .B(n4), .S(sel), .Y(n5));
  DFF r2 (.D(n5), .CK(clk), .Q(q));
endmodule
)V";

// The placement-query Liberty (full-flow GUI unit F4): the
// single cell the floorplan fixture uses, BUFX2, with a scalar
// 0.3 ns arc, so a timed place run on that fixture measures a
// real post-place slack. The area matches the LEF macro size
// (0.46 by 2.72 microns) used for the rectangle assertions.
const char kPlaceLiberty[] = R"LIB(library (eos_place) {
  delay_model : "table_lookup";
  time_unit : "1ns";
  voltage_unit : "1v";
  capacitive_load_unit (1,pF);
  current_unit : "1mA";
  pulling_resistance_unit : "1kohm";
  leakage_power_unit : "1pW";
  input_threshold_pct_rise : 50;
  input_threshold_pct_fall : 50;
  output_threshold_pct_rise : 50;
  output_threshold_pct_fall : 50;
  slew_lower_threshold_pct_rise : 30;
  slew_lower_threshold_pct_fall : 30;
  slew_upper_threshold_pct_rise : 70;
  slew_upper_threshold_pct_fall : 70;
  slew_derate_from_library : 1.0;
  nom_process : 1.0;
  nom_temperature : 25.0;
  nom_voltage : 1.0;
  cell (BUFX2) {
    area : 2.72;
    pin (A) {
      direction : input;
      capacitance : 0.001;
    }
    pin (Y) {
      direction : output;
      function : "A";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.3"); }
        cell_fall (scalar) { values ("0.3"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
}
)LIB";

// The placement-query SDC (full-flow GUI unit F4): a virtual
// clock plus input and output delays on the fixture's ports,
// so STA on the combinational two-buffer fixture has a
// constrained path to measure. The fixture has no clock port,
// so a create_clock on a port would fail at analysis time.
const char kPlaceSdc[] =
    "create_clock -name vclk -period 10\n"
    "set_input_delay 1.0 -clock vclk [get_ports in1]\n"
    "set_output_delay 1.0 -clock vclk [get_ports out1]\n";

// The electrical variant of the timing fixture (unit F5):
// the buffer output u1/Y violates all three electrical
// limits by construction; see the note above
// kLibertyElectrical in the STA facade tests for the
// expected values.
const char kTimingLibertyElectrical[] = R"LIB(library (eos_fixture_elec) {
  delay_model : "table_lookup";
  time_unit : "1ns";
  voltage_unit : "1v";
  capacitive_load_unit (1,pF);
  current_unit : "1mA";
  pulling_resistance_unit : "1kohm";
  leakage_power_unit : "1pW";
  input_threshold_pct_rise : 50;
  input_threshold_pct_fall : 50;
  output_threshold_pct_rise : 50;
  output_threshold_pct_fall : 50;
  slew_lower_threshold_pct_rise : 30;
  slew_lower_threshold_pct_fall : 30;
  slew_upper_threshold_pct_rise : 70;
  slew_upper_threshold_pct_fall : 70;
  slew_derate_from_library : 1.0;
  nom_process : 1.0;
  nom_temperature : 25.0;
  nom_voltage : 1.0;

  cell (DFF) {
    area : 10.0;
    ff (IQ, IQN) {
      next_state : "D";
      clocked_on : "CK";
    }
    pin (D) {
      direction : input;
      capacitance : 0.01;
      fanout_load : 2.0;
      timing () {
        related_pin : "CK";
        timing_type : setup_rising;
        rise_constraint (scalar) { values ("0.1"); }
        fall_constraint (scalar) { values ("0.1"); }
      }
      timing () {
        related_pin : "CK";
        timing_type : hold_rising;
        rise_constraint (scalar) { values ("0.05"); }
        fall_constraint (scalar) { values ("0.05"); }
      }
    }
    pin (CK) {
      direction : input;
      capacitance : 0.001;
      clock : true;
    }
    pin (Q) {
      direction : output;
      function : "IQ";
      timing () {
        related_pin : "CK";
        timing_type : rising_edge;
        cell_rise (scalar) { values ("0.2"); }
        cell_fall (scalar) { values ("0.2"); }
        rise_transition (scalar) { values ("0.1"); }
        fall_transition (scalar) { values ("0.1"); }
      }
    }
  }

  cell (BUF) {
    area : 2.0;
    pin (A) {
      direction : input;
      capacitance : 0.001;
    }
    pin (Y) {
      direction : output;
      function : "A";
      max_transition : 0.04;
      max_capacitance : 0.001;
      max_fanout : 1.0;
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.3"); }
        cell_fall (scalar) { values ("0.3"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
}
)LIB";

const char kTimingSdcTight[] =
    "create_clock -name clk -period 0.5 [get_ports clk]\n";

const char kTimingSdcMinPulseWidth[] =
    "create_clock -name clk -period 10 [get_ports clk]\n"
    "set_min_pulse_width 1.0 [get_pins r1/CK]\n"
    "set_min_pulse_width 1.0 [get_pins r2/CK]\n";

// The gated-clock fixture liberty: the timing fixture cells plus
// an AND2, so the second flop's clock can be gated by an AND
// whose enable is a primary input (untested gating checks) or a
// flop output (tested gating checks).
const char kTimingLibertyGated[] = R"LIB(library (eos_fixture_gated) {
  delay_model : "table_lookup";
  time_unit : "1ns";
  voltage_unit : "1v";
  capacitive_load_unit (1,pF);
  current_unit : "1mA";
  pulling_resistance_unit : "1kohm";
  leakage_power_unit : "1pW";
  input_threshold_pct_rise : 50;
  input_threshold_pct_fall : 50;
  output_threshold_pct_rise : 50;
  output_threshold_pct_fall : 50;
  slew_lower_threshold_pct_rise : 30;
  slew_lower_threshold_pct_fall : 30;
  slew_upper_threshold_pct_rise : 70;
  slew_upper_threshold_pct_fall : 70;
  slew_derate_from_library : 1.0;
  nom_process : 1.0;
  nom_temperature : 25.0;
  nom_voltage : 1.0;
  cell (DFF) {
    area : 10.0;
    ff (IQ, IQN) {
      next_state : "D";
      clocked_on : "CK";
    }
    pin (D) {
      direction : input;
      capacitance : 0.001;
      timing () {
        related_pin : "CK";
        timing_type : setup_rising;
        rise_constraint (scalar) { values ("0.1"); }
        fall_constraint (scalar) { values ("0.1"); }
      }
      timing () {
        related_pin : "CK";
        timing_type : hold_rising;
        rise_constraint (scalar) { values ("0.05"); }
        fall_constraint (scalar) { values ("0.05"); }
      }
    }
    pin (CK) {
      direction : input;
      capacitance : 0.001;
      clock : true;
    }
    pin (Q) {
      direction : output;
      function : "IQ";
      timing () {
        related_pin : "CK";
        timing_type : rising_edge;
        cell_rise (scalar) { values ("0.2"); }
        cell_fall (scalar) { values ("0.2"); }
        rise_transition (scalar) { values ("0.1"); }
        fall_transition (scalar) { values ("0.1"); }
      }
    }
  }
  cell (BUF) {
    area : 2.0;
    pin (A) {
      direction : input;
      capacitance : 0.001;
    }
    pin (Y) {
      direction : output;
      function : "A";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.3"); }
        cell_fall (scalar) { values ("0.3"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
  cell (AND2) {
    area : 4.0;
    pin (A) {
      direction : input;
      capacitance : 0.001;
    }
    pin (B) {
      direction : input;
      capacitance : 0.001;
    }
    pin (Y) {
      direction : output;
      function : "A & B";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.3"); }
        cell_fall (scalar) { values ("0.3"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
      timing () {
        related_pin : "B";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.3"); }
        cell_fall (scalar) { values ("0.3"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
}
)LIB";

// The gated-clock fixture: the second flop's clock is gated by
// an AND2 whose enable is the unclocked primary input e, so the
// gating checks live on the enable pin of the gate itself
// (g1/B) and are untested with no clocked startpoint.
const char kTimingVerilogGated[] = R"V(module top (clk, e, d, q);
  input clk, e, d;
  output q;
  wire n1, n2, gclk;

  AND2 g1 (.A(clk), .B(e), .Y(gclk));
  DFF r1 (.D(d), .CK(clk), .Q(n1));
  BUF u1 (.A(n1), .Y(n2));
  DFF r2 (.D(n2), .CK(gclk), .Q(q));
endmodule
)V";

// The tested gated-clock fixture: the enable is driven by a
// flop clocked by clk, so a clocked path reaches the enable
// and the coverage marks the gating checks tested. Note the
// engine produces no gated-clock path end for this session,
// so the gating report lists the checks with unmeasured
// slacks (see ReportClockGatingCheck.TestedEnableReportsNoMeasuredSlack);
// the 9.8/0.2 ns values a hand calculation gives are NOT
// asserted, because the engine never measures them.
const char kTimingVerilogGatedTested[] = R"V(module top (clk, d, q);
  input clk, d;
  output q;
  wire n1, n2, gclk, e;

  DFF r0 (.D(d), .CK(clk), .Q(e));
  AND2 g1 (.A(clk), .B(e), .Y(gclk));
  DFF r1 (.D(d), .CK(clk), .Q(n1));
  BUF u1 (.A(n1), .Y(n2));
  DFF r2 (.D(n2), .CK(gclk), .Q(q));
endmodule
)V";

const char kTimingLef[] = R"LEF(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.005 ;
SITE core
  CLASS CORE ;
  SIZE 0.46 BY 2.72 ;
END core
LAYER metal1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.19 ;
  WIDTH 0.065 ;
END metal1
MACRO BUFX2
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.35 -0.05 0.45 0.05 ;
    END
  END Y
END BUFX2
END LIBRARY
)LEF";

// Route command fixture: kTimingLef plus a second, vertical
// routing layer. kTimingLef itself carries only metal1, so a
// route test built on it can never report layer_2; the shared
// fixture stays untouched and only the route test uses this one.
const char kRouteLef[] = R"LEF(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.005 ;
SITE core
  CLASS CORE ;
  SIZE 0.46 BY 2.72 ;
END core
LAYER metal1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.19 ;
  WIDTH 0.06 ;
END metal1
LAYER via1
  TYPE CUT ;
  WIDTH 0.06 ;
END via1
LAYER metal2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 0.19 ;
  WIDTH 0.06 ;
END metal2
VIA VIA12
  LAYER metal1 ;
    RECT -0.05 -0.05 0.05 0.05 ;
  LAYER via1 ;
    RECT -0.03 -0.03 0.03 0.03 ;
  LAYER metal2 ;
    RECT -0.05 -0.05 0.05 0.05 ;
END VIA12
MACRO BUFX2
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT 0.05 0.05 0.25 0.25 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT 0.26 0.05 0.46 0.25 ;
    END
  END Y
END BUFX2
END LIBRARY
)LEF";

// P7: the route fixture plus filler masters (LEF CLASS CORE
// SPACER), so eos::finish has filler cells to place. The DEF stays
// kFloorplanDef: plain finish needs no supply nets, and the -pdn
// grid path is covered by the pd.finish unit tests.
const char kFinishLef[] = R"LEF(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.005 ;
SITE core
  CLASS CORE ;
  SIZE 0.46 BY 2.72 ;
END core
LAYER metal1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.19 ;
  WIDTH 0.06 ;
END metal1
LAYER via1
  TYPE CUT ;
  WIDTH 0.06 ;
END via1
LAYER metal2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 0.19 ;
  WIDTH 0.06 ;
END metal2
VIA VIA12
  LAYER metal1 ;
    RECT -0.05 -0.05 0.05 0.05 ;
  LAYER via1 ;
    RECT -0.03 -0.03 0.03 0.03 ;
  LAYER metal2 ;
    RECT -0.05 -0.05 0.05 0.05 ;
END VIA12
MACRO BUFX2
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT 0.05 0.05 0.25 0.25 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT 0.26 0.05 0.46 0.25 ;
    END
  END Y
END BUFX2
MACRO FILL1
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
END FILL1
MACRO FILL2
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 0.92 BY 2.72 ;
  SITE core ;
END FILL2
END LIBRARY
)LEF";

const char kTimingDef[] = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 2 ;
- u1 BUFX2 + PLACED ( 1000 1000 ) N ;
- u2 BUFX2 + PLACED ( 2000 1000 ) N ;
END COMPONENTS
PINS 2 ;
- in1 + NET n_in + DIRECTION INPUT + USE SIGNAL + PLACED ( 0 5000 ) N ;
- out1 + NET n_out + DIRECTION OUTPUT + USE SIGNAL + PLACED ( 10000 5000 ) N ;
END PINS
NETS 3 ;
- n_in ( PIN in1 ) ( u1 A ) ;
- n_mid ( u1 Y ) ( u2 A ) ;
- n_out ( u2 Y ) ( PIN out1 ) ;
END NETS
END DESIGN
)DEF";

// The closure fixture (Phase 2 unit 2.5): the sizing chain with
// two drive variants. By construction the weak variant's delay
// (0.8 ns) makes the setup slack -0.1 ns at the 1 ns clock and
// the strong variant's (0.2 ns) makes it +0.5 ns, so one
// close_timing iteration must close it. The variant names follow
// the trailing-drive-number convention the command derives
// families from.
const char kClosureLef[] = R"LEF(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.005 ;
SITE core
  CLASS CORE ;
  SIZE 0.46 BY 2.72 ;
END core
LAYER metal1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.19 ;
  WIDTH 0.065 ;
END metal1
MACRO DFF
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
  PIN D
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END D
  PIN CK
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END CK
  PIN Q
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END Q
END DFF
MACRO BUF_X1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END Y
END BUF_X1
MACRO BUF_X2
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END Y
END BUF_X2
END LIBRARY
)LEF";

const char kClosureDef[] = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 3 ;
- r1 DFF + PLACED ( 1000 1000 ) N ;
- u1 BUF_X1 + PLACED ( 2000 1000 ) N ;
- r2 DFF + PLACED ( 3000 1000 ) N ;
END COMPONENTS
PINS 3 ;
- clk + NET clk + DIRECTION INPUT + USE SIGNAL + PLACED ( 0 5000 ) N ;
- d + NET d + DIRECTION INPUT + USE SIGNAL + PLACED ( 0 6000 ) N ;
- q + NET q + DIRECTION OUTPUT + USE SIGNAL + PLACED ( 10000 5000 ) N ;
END PINS
NETS 5 ;
- clk ( PIN clk ) ( r1 CK ) ( r2 CK ) ;
- d ( PIN d ) ( r1 D ) ;
- n1 ( r1 Q ) ( u1 A ) ;
- n2 ( u1 Y ) ( r2 D ) ;
- q ( r2 Q ) ( PIN q ) ;
END NETS
END DESIGN
)DEF";

const char kClosureLiberty[] = R"LIB(library (eos_closure_fixture) {
  delay_model : "table_lookup";
  time_unit : "1ns";
  voltage_unit : "1v";
  capacitive_load_unit (1,pF);
  current_unit : "1mA";
  pulling_resistance_unit : "1kohm";
  leakage_power_unit : "1pW";
  input_threshold_pct_rise : 50;
  input_threshold_pct_fall : 50;
  output_threshold_pct_rise : 50;
  output_threshold_pct_fall : 50;
  slew_lower_threshold_pct_rise : 30;
  slew_lower_threshold_pct_fall : 30;
  slew_upper_threshold_pct_rise : 70;
  slew_upper_threshold_pct_fall : 70;
  slew_derate_from_library : 1.0;
  nom_process : 1.0;
  nom_temperature : 25.0;
  nom_voltage : 1.0;

  cell (DFF) {
    area : 10.0;
    ff (IQ, IQN) {
      next_state : "D";
      clocked_on : "CK";
    }
    pin (D) {
      direction : input;
      capacitance : 0.001;
      timing () {
        related_pin : "CK";
        timing_type : setup_rising;
        rise_constraint (scalar) { values ("0.1"); }
        fall_constraint (scalar) { values ("0.1"); }
      }
      timing () {
        related_pin : "CK";
        timing_type : hold_rising;
        rise_constraint (scalar) { values ("0.05"); }
        fall_constraint (scalar) { values ("0.05"); }
      }
    }
    pin (CK) {
      direction : input;
      capacitance : 0.001;
      clock : true;
    }
    pin (Q) {
      direction : output;
      function : "IQ";
      timing () {
        related_pin : "CK";
        timing_type : rising_edge;
        cell_rise (scalar) { values ("0.2"); }
        cell_fall (scalar) { values ("0.2"); }
        rise_transition (scalar) { values ("0.1"); }
        fall_transition (scalar) { values ("0.1"); }
      }
    }
  }
  cell (BUF_X1) {
    area : 2.0;
    pin (A) {
      direction : input;
      capacitance : 0.001;
    }
    pin (Y) {
      direction : output;
      function : "A";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.8"); }
        cell_fall (scalar) { values ("0.8"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
  cell (BUF_X2) {
    area : 2.0;
    pin (A) {
      direction : input;
      capacitance : 0.001;
    }
    pin (Y) {
      direction : output;
      function : "A";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.2"); }
        cell_fall (scalar) { values ("0.2"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
}
)LIB";

const char kClosureSdc[] =
    "create_clock -name clk -period 1 [get_ports clk]\n";

class TimingTempDir {
 public:
  TimingTempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("eos_registry_timing_test_" + std::to_string(counter_++))) {
    std::filesystem::create_directories(path_);
  }
  ~TimingTempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  std::string Write(const std::string& name,
                    const std::string& text) const {
    const std::filesystem::path file = path_ / name;
    std::ofstream out(file);
    out << text;
    out.close();
    return file.string();
  }
  std::string Path(const std::string& name) const {
    return (path_ / name).string();
  }

 private:
  static inline int counter_ = 0;
  std::filesystem::path path_;
};

}  // namespace

TEST(CoreCommands, TimingRunRegistersPerSceneReports) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kTimingLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("top.v", kTimingVerilog), "top"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kTimingSdc)}).ok);
  const CommandResult run = shell.Run("run_timing");
  ASSERT_TRUE(run.ok) << run.message;
  EXPECT_NE(run.payload.find("source=verilog scenes=1"), std::string::npos);
  EXPECT_NE(run.payload.find("scene=func_tt"), std::string::npos);
  // The payload carries the measured values at float precision
  // (9.399999... for the fixture's 9.4 ns), so parse and compare
  // numerically rather than matching a rounded string.
  const auto payload_number = [&run](const std::string& key) {
    const std::size_t at = run.payload.find(key + "=");
    EXPECT_NE(at, std::string::npos) << "missing " << key;
    if (at == std::string::npos) return 0.0;
    return std::stod(run.payload.substr(at + key.size() + 1));
  };
  EXPECT_NEAR(payload_number("worst_slack_ns"), 9.4, 1e-6);
  EXPECT_NEAR(payload_number("worst_hold_slack_ns"), 0.45, 1e-6);
  EXPECT_NE(run.payload.find("report=timing_func_tt"), std::string::npos);
  EXPECT_NE(shell.ctx.reports.FindByName(shell.ctx.project,
                                         "timing_func_tt"),
            nullptr);
  ASSERT_EQ(shell.ctx.timing.last_results.size(), 1u);
  const std::size_t first_artifact_at = run.payload.find("artifact=");
  ASSERT_NE(first_artifact_at, std::string::npos);
  const std::uint64_t first_artifact =
      std::stoull(run.payload.substr(first_artifact_at + 9));
  // A second run overwrites the same report names (repeatable
  // reports, Immediate unit 1): it succeeds with a new artifact id,
  // and exactly one live binding remains per name.
  const CommandResult again = shell.Run("run_timing");
  ASSERT_TRUE(again.ok) << again.message;
  EXPECT_NE(again.payload.find("report=timing_func_tt"), std::string::npos);
  const std::size_t second_artifact_at = again.payload.find("artifact=");
  ASSERT_NE(second_artifact_at, std::string::npos);
  EXPECT_NE(std::stoull(again.payload.substr(second_artifact_at + 9)),
            first_artifact);
  EXPECT_EQ(shell.ctx.reports.bindings(shell.ctx.project).size(), 1u);
  ASSERT_EQ(shell.ctx.timing.last_results.size(), 1u);
}

TEST(CoreCommands, RepeatReportTimingOverwritesItsBinding) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kTimingLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("top.v", kTimingVerilog), "top"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kTimingSdc)}).ok);
  const CommandResult first = shell.Run("report_timing", {});
  ASSERT_TRUE(first.ok) << first.message;
  const std::size_t first_at = first.payload.find("artifact=");
  ASSERT_NE(first_at, std::string::npos);
  const std::uint64_t first_artifact =
      std::stoull(first.payload.substr(first_at + 9));
  const CommandResult second = shell.Run("report_timing", {});
  ASSERT_TRUE(second.ok) << second.message;
  const std::size_t second_at = second.payload.find("artifact=");
  ASSERT_NE(second_at, std::string::npos);
  EXPECT_NE(std::stoull(second.payload.substr(second_at + 9)),
            first_artifact);
  EXPECT_EQ(shell.ctx.reports.bindings(shell.ctx.project).size(), 1u);
  EXPECT_NE(shell.ctx.reports.FindByName(shell.ctx.project,
                                         "timing_report_func_tt"),
            nullptr);
}

TEST(CoreCommands, TimingRepeatAfterRestoreOverwritesRestoredBinding) {
  TimingTempDir dir;
  Shell shell;
  // Checkpoint needs a project design open; the timing session
  // design from read_verilog is a separate store, so open one first.
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  const std::string lib = dir.Write("cells.lib", kTimingLiberty);
  const std::string verilog = dir.Write("top.v", kTimingVerilog);
  const std::string sdc = dir.Write("top.sdc", kTimingSdc);
  ASSERT_TRUE(shell.Run("read_liberty", {"tt", lib}).ok);
  ASSERT_TRUE(shell.Run("read_verilog", {verilog, "top"}).ok);
  ASSERT_TRUE(shell.Run("read_sdc", {sdc}).ok);
  const CommandResult first = shell.Run("run_timing");
  ASSERT_TRUE(first.ok) << first.message;
  const std::size_t first_at = first.payload.find("artifact=");
  ASSERT_NE(first_at, std::string::npos);
  const std::uint64_t first_artifact =
      std::stoull(first.payload.substr(first_at + 9));
  const std::string path = dir.Path("imm1_project.eosproj");
  const CommandResult saved = shell.Run("checkpoint", {path});
  ASSERT_TRUE(saved.ok) << saved.message;

  Shell fresh;
  const CommandResult restored = fresh.Run("restore", {path});
  ASSERT_TRUE(restored.ok) << restored.message;
  EXPECT_NE(restored.payload.find("bindings_restored=1"),
            std::string::npos);
  // Project state restores; the timing session does not, so
  // re-establish it, then repeat the run: the restored binding is
  // overwritten, not duplicated.
  ASSERT_TRUE(fresh.Run("read_liberty", {"tt", lib}).ok);
  ASSERT_TRUE(fresh.Run("read_verilog", {verilog, "top"}).ok);
  ASSERT_TRUE(fresh.Run("read_sdc", {sdc}).ok);
  const CommandResult again = fresh.Run("run_timing");
  ASSERT_TRUE(again.ok) << again.message;
  const std::size_t again_at = again.payload.find("artifact=");
  ASSERT_NE(again_at, std::string::npos);
  EXPECT_NE(std::stoull(again.payload.substr(again_at + 9)), first_artifact);
  EXPECT_EQ(fresh.ctx.reports.bindings(fresh.ctx.project).size(), 1u);
}

TEST(CoreCommands, TimingRunOverwritesACollidingExplicitBinding) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kTimingLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("top.v", kTimingVerilog), "top"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kTimingSdc)}).ok);
  // A binding from another source under run_timing's name does not
  // block the run: the run overwrites it, it is not duplicated.
  ASSERT_TRUE(shell.Run("register_report", {"timing_func_tt", "pass"}).ok);
  EXPECT_EQ(shell.ctx.reports.bindings(shell.ctx.project).size(), 1u);
  const CommandResult run = shell.Run("run_timing");
  ASSERT_TRUE(run.ok) << run.message;
  EXPECT_EQ(shell.ctx.reports.bindings(shell.ctx.project).size(), 1u);
  const auto* binding = shell.ctx.reports.FindByName(shell.ctx.project,
                                                     "timing_func_tt");
  ASSERT_NE(binding, nullptr);
  const std::size_t at = run.payload.find("artifact=");
  ASSERT_NE(at, std::string::npos);
  EXPECT_EQ(binding->artifact_id, std::stoull(run.payload.substr(at + 9)));
}

TEST(CoreCommands, TimingRunDiagnostics) {
  Shell shell;
  // No scenarios yet: a configuration failure, before any source
  // question.
  const CommandResult no_scenarios = shell.Run("run_timing");
  ASSERT_FALSE(no_scenarios.ok);
  EXPECT_EQ(no_scenarios.diag, Diag::kInvalidConfiguration);
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  // A scenario but no design source: the unclosed-design
  // diagnostic.
  const CommandResult no_source = shell.Run("run_timing");
  ASSERT_FALSE(no_source.ok);
  EXPECT_EQ(no_source.diag, Diag::kUnclosedDesign);
  // A source but no Liberty for the scene's corner names the
  // corner.
  TimingTempDir dir;
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("top.v", kTimingVerilog), "top"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kTimingSdc)}).ok);
  const CommandResult no_liberty = shell.Run("run_timing");
  ASSERT_FALSE(no_liberty.ok);
  EXPECT_EQ(no_liberty.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(no_liberty.message.find("tt"), std::string::npos);
}

// The S1 synthesis fixtures: the registered adder from the
// Yosys facade tests, and a combinational design with duplicate
// and doubly-inverted logic, the form a generic script improves.
// After elaboration (hierarchy, process lowering, FSM and memory
// identification, no optimization) the combinational design is
// five cells at depth 4 at the pinned Yosys 0.70: its proc pass
// folds the double inversion, so the OR sits on the duplicated
// product and the output chain is two ANDs deep from it.
const char kSynthOptDemo[] = R"V(module optdemo (a, b, c, d, y);
  input a, b, c, d;
  output y;
  wire n1, n2, n3, n4;
  assign n1 = a & b;
  assign n2 = a & b;
  assign n3 = ~~n1;
  assign n4 = n2 | n3;
  assign y = (n4 & c) & d;
endmodule
)V";

const char kSynthDemo[] = R"V(module demo (clk, rst, a, b, y);
  input clk, rst;
  input [3:0] a, b;
  output reg [3:0] y;
  reg [3:0] sum;
  always @(posedge clk) begin
    if (rst) sum <= 4'd0;
    else sum <= a + b;
  end
  always @* y = sum;
endmodule
)V";

TEST(CoreCommands, ReadVerilogParsesAndElaborateReportsStats) {
  TimingTempDir dir;
  Shell shell;
  const CommandResult read = shell.Run(
      "read_verilog", {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"});
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_NE(read.payload.find("modules=optdemo"), std::string::npos);
  EXPECT_TRUE(shell.ctx.synth.parsed);
  EXPECT_TRUE(shell.ctx.timing.verilog_path.find("optdemo.v") !=
              std::string::npos);
  const CommandResult elaborated = shell.Run("elaborate");
  ASSERT_TRUE(elaborated.ok) << elaborated.message;
  EXPECT_NE(elaborated.payload.find("module=optdemo"), std::string::npos);
  EXPECT_NE(elaborated.payload.find("cells=5"), std::string::npos);
  EXPECT_NE(elaborated.payload.find("depth=4"), std::string::npos);
  EXPECT_EQ(shell.ctx.synth.elaborated_cells, 5);
  EXPECT_EQ(shell.ctx.synth.elaborated_depth, 4);
}

// The hierarchy fixture for synth_hierarchy: three levels, top
// instantiates mid and ctrl, mid instantiates adder. Expected
// payload lines are fixture construction facts, in pre-order
// with children sorted by path.
const char kHierarchyVerilog[] = R"V(module adder (a, b, y);
  input [3:0] a, b;
  output [3:0] y;
  assign y = a + b;
endmodule
module ctrl (sel, op);
  input sel;
  output op;
  assign op = sel;
endmodule
module mid (a, b, y);
  input [3:0] a, b;
  output [3:0] y;
  adder u_add (.a(a), .b(b), .y(y));
endmodule
module top (a, b, sel, y);
  input [3:0] a, b;
  input sel;
  output [3:0] y;
  wire [3:0] m;
  wire op;
  mid u_mid (.a(a), .b(b), .y(m));
  ctrl u_ctrl (.sel(sel), .op(op));
  assign y = op ? m : a;
endmodule
)V";

TEST(CoreCommands, SynthHierarchyFailsFastWhenNotElaborated) {
  Shell shell;
  const CommandResult fresh = shell.Run("synth_hierarchy");
  EXPECT_FALSE(fresh.ok);
  EXPECT_EQ(fresh.diag, Diag::kInvalidConfiguration);
  TimingTempDir dir;
  Shell parsed;
  ASSERT_TRUE(parsed
                  .Run("read_verilog",
                       {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"})
                  .ok);
  const CommandResult unelaborated = parsed.Run("synth_hierarchy");
  EXPECT_FALSE(unelaborated.ok);
  EXPECT_EQ(unelaborated.diag, Diag::kInvalidConfiguration);
}

TEST(CoreCommands, SynthHierarchyFlatReportsRootOnly) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell
                  .Run("read_verilog",
                       {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"})
                  .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult hierarchy = shell.Run("synth_hierarchy");
  ASSERT_TRUE(hierarchy.ok) << hierarchy.message;
  EXPECT_EQ(hierarchy.payload,
            "top=optdemo\n"
            "node path=optdemo module=optdemo ports=a,b,c,d,y\n");
}

TEST(CoreCommands, SynthHierarchyMultiModuleReportsNestedTree) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell
                  .Run("read_verilog",
                       {dir.Write("top.v", kHierarchyVerilog), "top"})
                  .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult hierarchy = shell.Run("synth_hierarchy");
  ASSERT_TRUE(hierarchy.ok) << hierarchy.message;
  EXPECT_EQ(hierarchy.payload,
            "top=top\n"
            "node path=top module=top ports=a,b,sel,y\n"
            "node path=top/u_ctrl module=ctrl ports=sel,op\n"
            "node path=top/u_mid module=mid ports=a,b,y\n"
            "node path=top/u_mid/u_add module=adder ports=a,b,y\n");
}

TEST(CoreCommands, SynthBlocksFailsFastWhenNotElaborated) {
  Shell shell;
  const CommandResult fresh = shell.Run("synth_blocks", {"top"});
  EXPECT_FALSE(fresh.ok);
  EXPECT_EQ(fresh.diag, Diag::kInvalidConfiguration);
  TimingTempDir dir;
  Shell parsed;
  ASSERT_TRUE(parsed
                  .Run("read_verilog",
                       {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"})
                  .ok);
  const CommandResult unelaborated = parsed.Run("synth_blocks", {"optdemo"});
  EXPECT_FALSE(unelaborated.ok);
  EXPECT_EQ(unelaborated.diag, Diag::kInvalidConfiguration);
}

TEST(CoreCommands, SynthBlocksRejectsBadArguments) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell
                  .Run("read_verilog",
                       {dir.Write("top.v", kHierarchyVerilog), "top"})
                  .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  EXPECT_FALSE(shell.Run("synth_blocks").ok);
  EXPECT_FALSE(shell.Run("synth_blocks", {"top", "extra"}).ok);
  EXPECT_FALSE(shell.Run("synth_blocks", {"nope"}).ok);
  EXPECT_FALSE(shell.Run("synth_blocks", {"bad name"}).ok);
}

TEST(CoreCommands, SynthBlocksTopReportsBlocksAndNets) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell
                  .Run("read_verilog",
                       {dir.Write("top.v", kHierarchyVerilog), "top"})
                  .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult blocks = shell.Run("synth_blocks", {"top"});
  ASSERT_TRUE(blocks.ok) << blocks.message;
  EXPECT_EQ(blocks.payload,
            "module=top\n"
            "parent port=a direction=in\n"
            "parent port=b direction=in\n"
            "parent port=sel direction=in\n"
            "parent port=y direction=out\n"
            "block instance=u_ctrl module=ctrl ports=sel:in,op:out\n"
            "block instance=u_mid module=mid ports=a:in,b:in,y:out\n"
            "net name=a blocks=u_mid:a terminals=a\n"
            "net name=b blocks=u_mid:b terminals=b\n"
            "net name=m blocks=u_mid:y terminals=\n"
            "net name=op blocks=u_ctrl:op terminals=\n"
            "net name=sel blocks=u_ctrl:sel terminals=sel\n"
            "net name=y blocks= terminals=y\n");
}

TEST(CoreCommands, SynthBlocksLeafReportsParentsOnly) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell
                  .Run("read_verilog",
                       {dir.Write("top.v", kHierarchyVerilog), "top"})
                  .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult blocks = shell.Run("synth_blocks", {"adder"});
  ASSERT_TRUE(blocks.ok) << blocks.message;
  EXPECT_EQ(blocks.payload,
            "module=adder\n"
            "parent port=a direction=in\n"
            "parent port=b direction=in\n"
            "parent port=y direction=out\n"
            "net name=a blocks= terminals=a\n"
            "net name=b blocks= terminals=b\n"
            "net name=y blocks= terminals=y\n");
}

TEST(CoreCommands, SynthSchematicFailsFastWhenNotElaborated) {
  Shell shell;
  const CommandResult fresh = shell.Run("synth_schematic", {"top"});
  EXPECT_FALSE(fresh.ok);
  EXPECT_EQ(fresh.diag, Diag::kInvalidConfiguration);
  TimingTempDir dir;
  Shell parsed;
  ASSERT_TRUE(parsed
                  .Run("read_verilog",
                       {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"})
                  .ok);
  const CommandResult unelaborated =
      parsed.Run("synth_schematic", {"optdemo"});
  EXPECT_FALSE(unelaborated.ok);
  EXPECT_EQ(unelaborated.diag, Diag::kInvalidConfiguration);
}

TEST(CoreCommands, SynthSchematicRejectsBadArguments) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell
                  .Run("read_verilog",
                       {dir.Write("top.v", kHierarchyVerilog), "top"})
                  .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  EXPECT_FALSE(shell.Run("synth_schematic").ok);
  EXPECT_FALSE(shell.Run("synth_schematic", {"top", "extra"}).ok);
  EXPECT_FALSE(shell.Run("synth_schematic", {"nope"}).ok);
  EXPECT_FALSE(shell.Run("synth_schematic", {"bad name"}).ok);
}

TEST(CoreCommands, ReadVerilogUnknownTopRecordsNothing) {
  TimingTempDir dir;
  Shell shell;
  const CommandResult read = shell.Run(
      "read_verilog", {dir.Write("optdemo.v", kSynthOptDemo), "nope"});
  EXPECT_FALSE(read.ok);
  EXPECT_EQ(read.diag, Diag::kInvalidConfiguration);
  EXPECT_TRUE(shell.ctx.timing.verilog_path.empty());
  EXPECT_FALSE(shell.ctx.synth.parsed);
}

TEST(CoreCommands, ElaborateRequiresAParsedDesign) {
  Shell shell;
  const CommandResult elaborated = shell.Run("elaborate");
  EXPECT_FALSE(elaborated.ok);
  EXPECT_EQ(elaborated.diag, Diag::kInvalidConfiguration);
  const CommandResult generic = shell.Run("synth_generic");
  EXPECT_FALSE(generic.ok);
  EXPECT_EQ(generic.diag, Diag::kInvalidConfiguration);
}

TEST(CoreCommands, SynthGenericImprovesTheDemo) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"})
                    .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult generic = shell.Run("synth_generic");
  ASSERT_TRUE(generic.ok) << generic.message;
  EXPECT_NE(generic.payload.find("script=default"), std::string::npos);
  // The generic script, judged against the unoptimized
  // elaborated netlist: five cells at depth 4 become three cells
  // at depth 2, the duplicated product folded away and the
  // remaining four-input AND balanced into a two-level tree.
  // The ABC script itself must not worsen either of its own
  // numbers.
  EXPECT_NE(generic.payload.find("elaborated_cells=5"), std::string::npos);
  EXPECT_NE(generic.payload.find("elaborated_depth=4"), std::string::npos);
  EXPECT_NE(generic.payload.find("cells=3"), std::string::npos);
  EXPECT_NE(generic.payload.find("depth=2"), std::string::npos);
  EXPECT_TRUE(shell.ctx.synth.generic_optimized);
  EXPECT_LE(shell.ctx.synth.generic_after_and,
            shell.ctx.synth.generic_before_and);
  EXPECT_LE(shell.ctx.synth.generic_after_levels,
            shell.ctx.synth.generic_before_levels);
  std::ifstream in(shell.ctx.synth.generic_verilog);
  EXPECT_TRUE(in.good());
}

TEST(CoreCommands, SynthGenericRejectsBadOptions) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"})
                    .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  EXPECT_FALSE(shell.Run("synth_generic", {"-bogus", "1"}).ok);
  EXPECT_FALSE(shell.Run("synth_generic", {"-script"}).ok);
  EXPECT_FALSE(
      shell.Run("synth_generic", {"-script", "default", "-script", "balance"})
          .ok);
  EXPECT_FALSE(shell.Run("synth_generic", {"-script", "sideways"}).ok);
  // Every rejection above happened before any engine ran, so the
  // elaborated state is untouched and a valid run still works.
  EXPECT_TRUE(shell.ctx.synth.elaborated);
  ASSERT_TRUE(shell.Run("synth_generic", {"-script", "balance"}).ok);
}

TEST(CoreCommands, SynthGenericOnTheRegisteredAdder) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(
      shell.Run("read_verilog", {dir.Write("demo.v", kSynthDemo), "demo"})
          .ok);
  const CommandResult elaborated = shell.Run("elaborate");
  ASSERT_TRUE(elaborated.ok) << elaborated.message;
  EXPECT_NE(elaborated.payload.find("cells=3"), std::string::npos);
  EXPECT_NE(elaborated.payload.find("depth=2"), std::string::npos);
  const CommandResult generic = shell.Run("synth_generic");
  ASSERT_TRUE(generic.ok) << generic.message;
  EXPECT_TRUE(shell.ctx.synth.generic_optimized);
  // The adder is sequential (a 4-bit register), so it takes
  // the `abc -dff` path, and its before/after numbers are the
  // live design's cell counts and logic depths, not ABC
  // print_stats. The depth is reported, not asserted to
  // shrink: ABC's area-oriented default script may deepen the
  // network while shrinking it.
  EXPECT_LE(shell.ctx.synth.generic_after_and,
            shell.ctx.synth.generic_before_and);
}

// The sequential synth_generic fixtures (unit F12): the same
// 4-bit $adff counter the Yosys facade tests use, plus a
// sequential design with an unresolved reference for the
// pre-gate. Each test binary carries its own copies, as the S1
// fixtures do.
const char kSeqCounter[] = R"V(module cnt (clk, rst_n, en, q);
  input clk, rst_n, en;
  output reg [3:0] q;
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) q <= 4'd0;
    else if (en) q <= q + 4'd1;
  end
endmodule
)V";

const char kSeqUnresolved[] = R"V(module badseq (clk, rst_n, en, q);
  input clk, rst_n, en;
  output reg [3:0] q;
  missing_cell u_missing (.clk(clk));
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) q <= 4'd0;
    else if (en) q <= q + 4'd1;
  end
endmodule
)V";

// Repo root, derived from this file's own path, so the test
// finds demo/hier no matter which directory it runs from.
std::filesystem::path RepoRoot() {
  std::filesystem::path dir =
      std::filesystem::absolute(__FILE__).parent_path();
  // tests/unit/ui/registry -> repo root is four levels up.
  for (int i = 0; i < 4; ++i) dir = dir.parent_path();
  return dir;
}

TEST(CoreCommands, SynthGenericOnSequentialCounter) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("cnt.v", kSeqCounter), "cnt"})
                    .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult generic = shell.Run("synth_generic");
  ASSERT_TRUE(generic.ok) << generic.message;
  EXPECT_NE(generic.payload.find("script=default"), std::string::npos);
  EXPECT_TRUE(shell.ctx.synth.generic_optimized);
  // The -dff path reads its before/after numbers from the live
  // design's cell counts, not ABC print_stats: the incrementer
  // logic shrinks while the four flops stay. The depth is
  // reported, not asserted to shrink: ABC's area-oriented
  // default script may deepen the network while shrinking it
  // (here 5 levels before, 6 after).
  EXPECT_LT(shell.ctx.synth.generic_after_and,
            shell.ctx.synth.generic_before_and);
  std::ifstream in(shell.ctx.synth.generic_verilog);
  ASSERT_TRUE(in.good());
  std::stringstream verilog;
  verilog << in.rdbuf();
  EXPECT_NE(verilog.str().find("rst_n"), std::string::npos);
  EXPECT_NE(verilog.str().find("clk"), std::string::npos);
  EXPECT_NE(verilog.str().find("negedge rst_n"), std::string::npos);
}

TEST(CoreCommands, SynthGenericOnHierTop) {
  Shell shell;
  const std::filesystem::path list =
      RepoRoot() / "demo" / "hier" / "design.f";
  ASSERT_TRUE(
      shell.Run("read_filelist", {list.string(), "hier_top"}).ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult generic = shell.Run("synth_generic");
  ASSERT_TRUE(generic.ok) << generic.message;
  EXPECT_TRUE(shell.ctx.synth.generic_optimized);
  std::ifstream in(shell.ctx.synth.generic_verilog);
  ASSERT_TRUE(in.good());
  std::stringstream verilog;
  verilog << in.rdbuf();
  EXPECT_NE(verilog.str().find("rst_n"), std::string::npos);
  EXPECT_NE(verilog.str().find("clk"), std::string::npos);
}

TEST(CoreCommands, SynthGenericBrokenVariantStillFails) {
  Shell shell;
  const std::filesystem::path broken =
      RepoRoot() / "demo" / "hier" / "broken" / "unresolved_top.v";
  ASSERT_TRUE(shell.Run("read_verilog",
                        {broken.string(), "unresolved_top"})
                    .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult generic = shell.Run("synth_generic");
  EXPECT_FALSE(generic.ok);
  // The message names the failing step: ABC reads the BLIF
  // bridge with its unresolved .subckt reference, and the
  // structural-hashing step is where the missing model fails.
  // AbcEngine reports the step and the command, not ABC's own
  // error text. ABC's exact error ("Cannot find the model for
  // subcircuit does_not_exist_anywhere") is printed to the
  // console output during the run, as the demo/hier README
  // documents.
  EXPECT_NE(generic.message.find("structural hashing"),
            std::string::npos);
  // The failure happens after aigmap mutates the design, so the
  // synth state is cleared and the recovery is read_verilog
  // followed by elaborate.
  EXPECT_FALSE(shell.ctx.synth.elaborated);
}

TEST(CoreCommands, SynthGenericSequentialWithUnresolvedReferenceFailsAtPreGate) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("badseq.v", kSeqUnresolved), "badseq"})
                    .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult generic = shell.Run("synth_generic");
  EXPECT_FALSE(generic.ok);
  EXPECT_NE(generic.message.find("missing_cell"), std::string::npos);
  // The pre-gate runs before any mutation, so the elaborated
  // state is untouched: the design is still the elaborated one.
  EXPECT_TRUE(shell.ctx.synth.elaborated);
}

// The S4 file-list fixtures: the same leaf and top modules as
// the Yosys facade file-list tests (each test binary carries
// its own copies, as the S1 fixtures do), plus the single-file
// form of the same design: the sub module, then the top module
// with the header wire inlined and the WIDE branch taken
// unconditionally. Elaborating the list (with -I and -D) and
// the single file must report byte-identical stats.
const char kFilelistSub[] = R"V(module sub(a, b, y);
  input a, b;
  output y;
  assign y = a & b;
endmodule
)V";

const char kFilelistTop[] = R"V(module filetop(a, b, c, d, y);
  input a, b, c, d;
  output y;
`include "defs.vh"
  sub u_sub(.a(a), .b(b), .y(s));
`ifdef WIDE
  wire t;
  assign t = s & c;
  assign y = t & d;
`else
  assign y = s;
`endif
endmodule
)V";

const char kFilelistDefs[] = "wire s;\n";

const char kFilelistSingle[] = R"V(module sub(a, b, y);
  input a, b;
  output y;
  assign y = a & b;
endmodule
module filetop(a, b, c, d, y);
  input a, b, c, d;
  output y;
  wire s;
  sub u_sub(.a(a), .b(b), .y(s));
  wire t;
  assign t = s & c;
  assign y = t & d;
endmodule
)V";

TEST(CoreCommands, ReadFilelistReproducesSingleFileElaboration) {
  TimingTempDir dir;
  std::filesystem::create_directories(dir.Path("inc"));
  const std::string sub = dir.Write("sub.v", kFilelistSub);
  (void)sub;
  dir.Write("filetop.v", kFilelistTop);
  dir.Write("inc/defs.vh", kFilelistDefs);
  const std::string list = dir.Write(
      "design.f",
      "# a comment line\n// another comment\n\nsub.v\nfiletop.v\n");
  Shell filelist_shell;
  const CommandResult read = filelist_shell.Run(
      "read_filelist",
      {list, "filetop", "-I", dir.Path("inc"), "-D", "WIDE"});
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_NE(read.payload.find("files=2"), std::string::npos);
  EXPECT_NE(read.payload.find("top=filetop"), std::string::npos);
  EXPECT_NE(read.payload.find("entries=sub.v,filetop.v"),
            std::string::npos);
  EXPECT_TRUE(filelist_shell.ctx.synth.parsed);
  EXPECT_EQ(filelist_shell.ctx.synth.filelist_path, list);
  ASSERT_EQ(filelist_shell.ctx.synth.filelist_files.size(), 2u);
  const CommandResult elaborated = filelist_shell.Run("elaborate");
  ASSERT_TRUE(elaborated.ok) << elaborated.message;
  Shell single_shell;
  ASSERT_TRUE(single_shell.Run(
      "read_verilog",
      {dir.Write("single.v", kFilelistSingle), "filetop"}).ok);
  const CommandResult single_elaborated = single_shell.Run("elaborate");
  ASSERT_TRUE(single_elaborated.ok) << single_elaborated.message;
  // Exact reproduction: same cells, wires, depth, and cell
  // types, not just the same top name.
  EXPECT_EQ(elaborated.payload, single_elaborated.payload);
}

TEST(CoreCommands, ReadFilelistAcceptsAttachedAndRepeatedFlags) {
  TimingTempDir dir;
  std::filesystem::create_directories(dir.Path("inc"));
  dir.Write("sub.v", kFilelistSub);
  dir.Write("filetop.v", kFilelistTop);
  dir.Write("inc/defs.vh", kFilelistDefs);
  const std::string list =
      dir.Write("design.f", "sub.v\nfiletop.v\n");
  Shell shell;
  // Attached forms, a repeated -I (an unused directory first,
  // the real one second), and a repeated -D.
  const CommandResult read = shell.Run(
      "read_filelist",
      {list, "filetop", "-I" + dir.Path("no_such_dir"),
       "-I" + dir.Path("inc"), "-DWIDE", "-DOTHER=1"});
  ASSERT_TRUE(read.ok) << read.message;
  const CommandResult elaborated = shell.Run("elaborate");
  ASSERT_TRUE(elaborated.ok) << elaborated.message;
  EXPECT_NE(elaborated.payload.find("cells=3"), std::string::npos);
}

TEST(CoreCommands, ReadFilelistMissingEntryNamesListAndEntry) {
  TimingTempDir dir;
  dir.Write("sub.v", kFilelistSub);
  const std::string list =
      dir.Write("design.f", "sub.v\nmissing.v\n");
  Shell shell;
  // A good design first: a pre-engine failure must leave it
  // alone, because the live Yosys design is untouched.
  ASSERT_TRUE(shell.Run(
      "read_verilog",
      {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"}).ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult read =
      shell.Run("read_filelist", {list, "filetop"});
  EXPECT_FALSE(read.ok);
  EXPECT_EQ(read.diag, Diag::kIoError);
  EXPECT_NE(read.message.find(list), std::string::npos);
  EXPECT_NE(read.message.find("missing.v"), std::string::npos);
  EXPECT_TRUE(shell.ctx.synth.elaborated);
}

TEST(CoreCommands, ReadFilelistRejectsBadOptionsAndInputs) {
  TimingTempDir dir;
  std::filesystem::create_directories(dir.Path("inc"));
  dir.Write("sub.v", kFilelistSub);
  dir.Write("filetop.v", kFilelistTop);
  dir.Write("inc/defs.vh", kFilelistDefs);
  const std::string list =
      dir.Write("design.f", "sub.v\nfiletop.v\n");
  const std::string empty_list =
      dir.Write("empty.f", "# only a comment\n\n");
  Shell shell;
  ASSERT_TRUE(shell.Run(
      "read_verilog",
      {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"}).ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  // Every rejection here happens before any engine runs, so
  // the elaborated state is untouched afterwards.
  EXPECT_FALSE(
      shell.Run("read_filelist", {list, "filetop", "-bogus", "1"}).ok);
  EXPECT_FALSE(
      shell.Run("read_filelist", {list, "filetop", "-I"}).ok);
  EXPECT_FALSE(
      shell.Run("read_filelist", {list, "filetop", "-D"}).ok);
  EXPECT_FALSE(
      shell.Run("read_filelist", {list, "filetop", "-I", ""}).ok);
  EXPECT_FALSE(
      shell.Run("read_filelist",
                {list, "filetop", "-D", "1BAD"}).ok);
  EXPECT_FALSE(shell.Run("read_filelist", {list}).ok);
  const CommandResult empty =
      shell.Run("read_filelist", {empty_list, "filetop"});
  EXPECT_FALSE(empty.ok);
  EXPECT_NE(empty.message.find(empty_list), std::string::npos);
  const CommandResult unreadable = shell.Run(
      "read_filelist", {dir.Path("no_such.f"), "filetop"});
  EXPECT_FALSE(unreadable.ok);
  EXPECT_EQ(unreadable.diag, Diag::kIoError);
  EXPECT_TRUE(shell.ctx.synth.elaborated);
  // An unknown top parses the list, so the live design is no
  // longer the old one: the session is cleared and nothing is
  // recorded, and a valid read still works afterwards. The
  // case passes -I, so the parse itself succeeds and only the
  // top check fails: without -I the list's `include would
  // abort the process (upstream log_error), which is not a
  // testable failure here.
  const CommandResult unknown_top = shell.Run(
      "read_filelist",
      {list, "nope", "-I", dir.Path("inc"), "-D", "WIDE"});
  EXPECT_FALSE(unknown_top.ok);
  EXPECT_NE(unknown_top.message.find("nope"), std::string::npos);
  EXPECT_FALSE(shell.ctx.synth.parsed);
  ASSERT_TRUE(shell.Run(
      "read_filelist",
      {list, "filetop", "-I", dir.Path("inc"), "-D", "WIDE"}).ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
}

// The S2 mapping fixtures: a Liberty with the cells the
// optdemo maps to, plus a sequential cell the converter must
// skip, and the combinational SDC that judges the mapped
// netlist: a virtual 10 ns clock with zero input and output
// delay, so the mapped path delay is the arrival and the slack
// is 10 ns minus that delay.
const char kSynthMapLiberty[] = R"LIB(library (eos_map_fixture) {
  delay_model : "table_lookup";
  time_unit : "1ns";
  voltage_unit : "1v";
  capacitive_load_unit (1,pF);
  current_unit : "1mA";
  pulling_resistance_unit : "1kohm";
  leakage_power_unit : "1pW";
  input_threshold_pct_rise : 50;
  input_threshold_pct_fall : 50;
  output_threshold_pct_rise : 50;
  output_threshold_pct_fall : 50;
  slew_lower_threshold_pct_rise : 30;
  slew_lower_threshold_pct_fall : 30;
  slew_upper_threshold_pct_rise : 70;
  slew_upper_threshold_pct_fall : 70;
  slew_derate_from_library : 1.0;
  nom_process : 1.0;
  nom_temperature : 25.0;
  nom_voltage : 1.0;
  cell (AND2) {
    area : 2.0;
    pin (A) { direction : input; capacitance : 0.001; }
    pin (B) { direction : input; capacitance : 0.001; }
    pin (Y) {
      direction : output;
      function : "A*B";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.6"); }
        cell_fall (scalar) { values ("0.6"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
  cell (OR2) {
    area : 2.0;
    pin (A) { direction : input; capacitance : 0.001; }
    pin (B) { direction : input; capacitance : 0.001; }
    pin (Y) {
      direction : output;
      function : "A+B";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.6"); }
        cell_fall (scalar) { values ("0.6"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
  cell (INV) {
    area : 1.0;
    pin (A) { direction : input; capacitance : 0.001; }
    pin (Y) {
      direction : output;
      function : "!A";
      timing () {
        related_pin : "A";
        timing_sense : negative_unate;
        cell_rise (scalar) { values ("0.5"); }
        cell_fall (scalar) { values ("0.5"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
  cell (BUF) {
    area : 1.0;
    pin (A) { direction : input; capacitance : 0.001; }
    pin (Y) {
      direction : output;
      function : "A";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.4"); }
        cell_fall (scalar) { values ("0.4"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
  cell (DFF) {
    area : 10.0;
    ff (IQ, IQN) {
      next_state : "D";
      clocked_on : "CK";
    }
    pin (D) {
      direction : input;
      capacitance : 0.001;
      timing () {
        related_pin : "CK";
        timing_type : setup_rising;
        rise_constraint (scalar) { values ("0.1"); }
        fall_constraint (scalar) { values ("0.1"); }
      }
    }
    pin (CK) {
      direction : input;
      capacitance : 0.001;
      clock : true;
    }
    pin (Q) {
      direction : output;
      function : "IQ";
      timing () {
        related_pin : "CK";
        timing_type : rising_edge;
        cell_rise (scalar) { values ("0.2"); }
        cell_fall (scalar) { values ("0.2"); }
        rise_transition (scalar) { values ("0.1"); }
        fall_transition (scalar) { values ("0.1"); }
      }
    }
  }
}
)LIB";

const char kSynthMapSdc[] =
    "create_clock -name vclk -period 10\n"
    "set_input_delay -clock vclk 0 [all_inputs]\n"
    "set_output_delay -clock vclk 0 [all_outputs]\n";

TEST(CoreCommands, SynthMapDelayJudgedByStaAndRunTimingReproducesIt) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kSynthMapLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kSynthMapSdc)})
                    .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  ASSERT_TRUE(shell.Run("synth_generic").ok);
  const CommandResult mapped = shell.Run("synth_map");
  ASSERT_TRUE(mapped.ok) << mapped.message;
  EXPECT_NE(mapped.payload.find("target=delay"), std::string::npos);
  EXPECT_NE(mapped.payload.find("genlib_cells=AND2,BUF,INV,OR2"),
            std::string::npos);
  EXPECT_NE(mapped.payload.find("cells=3"), std::string::npos);
  EXPECT_NE(mapped.payload.find("types=AND2:3"), std::string::npos);
  EXPECT_NE(mapped.payload.find("scene=func_tt"), std::string::npos);
  EXPECT_NE(mapped.payload.find("electrical_violations=0"),
            std::string::npos);
  EXPECT_TRUE(shell.ctx.synth.mapped);
  EXPECT_EQ(shell.ctx.synth.mapped_target, "delay");
  EXPECT_TRUE(shell.ctx.synth.mapped_timing_judged);
  // Immediate unit 9: the successful mapping records its CPU
  // time for report_qor's compile CPU.
  EXPECT_GE(shell.ctx.synth.mapped_cpu_s, 0.0);
  EXPECT_EQ(shell.ctx.timing.verilog_path,
            shell.ctx.synth.mapped_verilog);
  std::ifstream genlib_in(shell.ctx.synth.mapped_genlib);
  ASSERT_TRUE(genlib_in.good());
  std::stringstream genlib_text;
  genlib_text << genlib_in.rdbuf();
  EXPECT_NE(genlib_text.str().find("GATE AND2 2 Y=A*B;"),
            std::string::npos);
  std::ifstream mapped_in(shell.ctx.synth.mapped_verilog);
  ASSERT_TRUE(mapped_in.good());
  std::stringstream mapped_text;
  mapped_text << mapped_in.rdbuf();
  EXPECT_NE(mapped_text.str().find("AND2"), std::string::npos);
  // The mapped netlist is three AND2 cells at logic depth 2
  // (the balanced four-input AND tree), so the arrival is
  // 2 * 0.6 = 1.2 ns against the 10 ns virtual clock: worst
  // slack 8.8 ns, no negative slack. The payload number is
  // parsed, not string-matched, as in the timing tests.
  const auto payload_number = [&mapped](const std::string& key) {
    const std::size_t at = mapped.payload.find(key + "=");
    EXPECT_NE(at, std::string::npos) << "missing " << key;
    if (at == std::string::npos) return 0.0;
    return std::stod(mapped.payload.substr(at + key.size() + 1));
  };
  EXPECT_NEAR(payload_number("worst_slack_ns"), 8.8, 1e-6);
  EXPECT_NEAR(payload_number("total_negative_slack_ns"), 0.0, 1e-6);
  // The next run_timing is the end-to-end STA on the mapped
  // netlist, and must reproduce the mapping record exactly.
  const CommandResult run = shell.Run("run_timing");
  ASSERT_TRUE(run.ok) << run.message;
  const auto run_number = [&run](const std::string& key) {
    const std::size_t at = run.payload.find(key + "=");
    EXPECT_NE(at, std::string::npos) << "missing " << key;
    if (at == std::string::npos) return 0.0;
    return std::stod(run.payload.substr(at + key.size() + 1));
  };
  EXPECT_NEAR(run_number("worst_slack_ns"),
              payload_number("worst_slack_ns"), 1e-6);
  EXPECT_NEAR(run_number("total_negative_slack_ns"), 0.0, 1e-6);
  // Immediate unit 9: report_qor after the mapping records a
  // compile CPU instead of n/a.
  const CommandResult qor = shell.Run("report_qor", {});
  ASSERT_TRUE(qor.ok) << qor.message;
  EXPECT_NE(qor.payload.find("compile_cpu_s="), std::string::npos);
  EXPECT_EQ(qor.payload.find("compile_cpu_s=n/a"), std::string::npos);
}

TEST(CoreCommands, SynthMapAreaWithExplicitLibertyAndNoTiming) {
  TimingTempDir dir;
  Shell shell;
  const std::string liberty = dir.Write("cells.lib", kSynthMapLiberty);
  ASSERT_TRUE(shell.Run("read_liberty", {"tt", liberty}).ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"})
                    .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult mapped =
      shell.Run("synth_map", {"-target", "area", "-liberty", liberty});
  ASSERT_TRUE(mapped.ok) << mapped.message;
  EXPECT_NE(mapped.payload.find("target=area"), std::string::npos);
  EXPECT_NE(mapped.payload.find("cells=3"), std::string::npos);
  EXPECT_NE(mapped.payload.find("types=AND2:3"), std::string::npos);
  EXPECT_NE(mapped.payload.find("timing=not_run"), std::string::npos);
  EXPECT_TRUE(shell.ctx.synth.mapped);
  EXPECT_FALSE(shell.ctx.synth.mapped_timing_judged);
  EXPECT_EQ(shell.ctx.timing.verilog_path,
            shell.ctx.synth.mapped_verilog);
}

TEST(CoreCommands, SynthMapRejectsBadOptionsAndLeavesState) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"})
                    .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  EXPECT_FALSE(shell.Run("synth_map", {"-bogus", "1"}).ok);
  EXPECT_FALSE(shell.Run("synth_map", {"-target"}).ok);
  EXPECT_FALSE(shell.Run("synth_map", {"-target", "sideways"}).ok);
  EXPECT_FALSE(
      shell.Run("synth_map", {"-target", "delay", "-target", "area"}).ok);
  const CommandResult missing_liberty = shell.Run(
      "synth_map", {"-liberty", dir.Path("no_such.lib")});
  EXPECT_FALSE(missing_liberty.ok);
  EXPECT_EQ(missing_liberty.diag, Diag::kIoError);
  // No Liberty recorded yet: the mapping fails naming it, and
  // every rejection above left the elaborated state untouched.
  EXPECT_FALSE(shell.Run("synth_map").ok);
  EXPECT_TRUE(shell.ctx.synth.elaborated);
  EXPECT_FALSE(shell.ctx.synth.mapped);
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kSynthMapLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("synth_map").ok);
}

TEST(CoreCommands, SynthMapRequiresAnElaboratedDesign) {
  Shell shell;
  const CommandResult bare = shell.Run("synth_map");
  EXPECT_FALSE(bare.ok);
  EXPECT_EQ(bare.diag, Diag::kInvalidConfiguration);
  TimingTempDir dir;
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"})
                    .ok);
  const CommandResult parsed_only = shell.Run("synth_map");
  EXPECT_FALSE(parsed_only.ok);
  EXPECT_EQ(parsed_only.diag, Diag::kInvalidConfiguration);
}

TEST(CoreCommands, SynthMapRejectsSequentialDesigns) {
  TimingTempDir dir;
  Shell shell;
  const std::string liberty = dir.Write("cells.lib", kSynthMapLiberty);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("demo.v", kSynthDemo), "demo"})
                    .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  EXPECT_EQ(shell.ctx.synth.elaborated_cells, 3);
  const CommandResult mapped =
      shell.Run("synth_map", {"-liberty", liberty});
  EXPECT_FALSE(mapped.ok);
  EXPECT_EQ(mapped.diag, Diag::kUnsupportedFeature);
  EXPECT_NE(mapped.message.find("combinational"), std::string::npos);
  // The rejection happened before the bridge, so the elaborated
  // state is untouched and the design is not marked mapped.
  EXPECT_TRUE(shell.ctx.synth.elaborated);
  EXPECT_EQ(shell.ctx.synth.elaborated_cells, 3);
  EXPECT_FALSE(shell.ctx.synth.mapped);
}

// The S3 loop fixtures: a two-output design whose outputs both
// compute the same four-input AND (AND is commutative, so ABC's
// structural hashing shares the tree), a Liberty with a slow
// cheap AND2 and a fast expensive AND2_FAST for the same
// function, plus INV and BUF. Area mapping shares the tree and
// drives y2 from y1 through a buffer (three AND2 cells plus one
// BUF, arrivals 1.2 ns on y1 and 1.6 ns on y2), and the classic
// strategy duplicates the tree instead (four AND2 cells,
// arrival 1.2 ns on both endpoints). The fast cell is in the
// library so the delay strategies have a faster cover to choose
// from, but ABC's map does not in fact pick it here (measured:
// the classic candidate is four AND2 cells); the loop's
// improvement comes from the buffer removal, and the tests
// assert the measured numbers, not the hoped-for ones. Both
// input pins of each two-input cell carry a timing arc at the
// same delay, so the STA arrival does not depend on which pin
// ABC's mapper wires the deep net to. The library also carries
// INV: ABC's map aborts on a library that yields exactly one
// root function, and INV is a second root here, so the loop's
// strategies run instead of hitting that upstream abort (the
// abort itself is covered by the facade's single-function
// refusal test).
const char kSynthOptLoopDemo[] = R"V(module optloop (a, b, c, d, y1, y2);
  input a, b, c, d;
  output y1, y2;
  assign y1 = ((a & b) & c) & d;
  assign y2 = ((a & c) & b) & d;
endmodule
)V";

TEST(CoreCommands, SynthBlocksFlatReportsPortsOnly) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell
                  .Run("read_verilog",
                       {dir.Write("optloop.v", kSynthOptLoopDemo), "optloop"})
                  .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult blocks = shell.Run("synth_blocks", {"optloop"});
  ASSERT_TRUE(blocks.ok) << blocks.message;
  EXPECT_EQ(blocks.payload,
            "module=optloop\n"
            "parent port=a direction=in\n"
            "parent port=b direction=in\n"
            "parent port=c direction=in\n"
            "parent port=d direction=in\n"
            "parent port=y1 direction=out\n"
            "parent port=y2 direction=out\n"
            "net name=a blocks= terminals=a\n"
            "net name=b blocks= terminals=b\n"
            "net name=c blocks= terminals=c\n"
            "net name=d blocks= terminals=d\n"
            "net name=y1 blocks= terminals=y1\n"
            "net name=y2 blocks= terminals=y2\n");
}

// The schematic for the flat optloop, from the live elaborated
// design: six $and gates in two unshared chains of three, with
// Yosys auto-generated instance names (file:line form, so the
// names contain colons) and internal wires named after their
// driving gate. Yosys's auto-name counter is process-global, so
// the numeric suffix depends on which designs this process
// elaborated before; the test takes the gate names from the
// payload itself and pins everything else exactly: the file
// and line each chain comes from, the chain order, the types,
// the ports, the net names, the endpoint topology, and the
// terminals.
TEST(CoreCommands, SynthSchematicFlatReportsGatesAndNets) {
  TimingTempDir dir;
  Shell shell;
  const std::string path = dir.Write("optloop.v", kSynthOptLoopDemo);
  ASSERT_TRUE(shell.Run("read_verilog", {path, "optloop"}).ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult schematic = shell.Run("synth_schematic", {"optloop"});
  ASSERT_TRUE(schematic.ok) << schematic.message;
  std::vector<std::string> gates;
  {
    std::istringstream in(schematic.payload);
    std::string line;
    const std::string prefix = "gate instance=";
    while (std::getline(in, line)) {
      if (line.rfind(prefix, 0) != 0) continue;
      const std::size_t space = line.find(' ', prefix.size());
      ASSERT_NE(space, std::string::npos);
      gates.push_back(line.substr(prefix.size(), space - prefix.size()));
    }
  }
  ASSERT_EQ(gates.size(), 6u);
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_NE(gates[i].find("$and$" + path + ":4$"), std::string::npos)
        << "y1 chain gate " << i << ": " << gates[i];
    EXPECT_NE(gates[i + 3].find("$and$" + path + ":5$"),
              std::string::npos)
        << "y2 chain gate " << i << ": " << gates[i + 3];
  }
  const std::string& g1 = gates[0];
  const std::string& g2 = gates[1];
  const std::string& g3 = gates[2];
  const std::string& g4 = gates[3];
  const std::string& g5 = gates[4];
  const std::string& g6 = gates[5];
  EXPECT_EQ(schematic.payload,
            "module=optloop\n"
            "parent port=a direction=in\n"
            "parent port=b direction=in\n"
            "parent port=c direction=in\n"
            "parent port=d direction=in\n"
            "parent port=y1 direction=out\n"
            "parent port=y2 direction=out\n"
            "gate instance=" + g1 + " type=$and ports=A:in,B:in,Y:out\n" +
            "gate instance=" + g2 + " type=$and ports=A:in,B:in,Y:out\n" +
            "gate instance=" + g3 + " type=$and ports=A:in,B:in,Y:out\n" +
            "gate instance=" + g4 + " type=$and ports=A:in,B:in,Y:out\n" +
            "gate instance=" + g5 + " type=$and ports=A:in,B:in,Y:out\n" +
            "gate instance=" + g6 + " type=$and ports=A:in,B:in,Y:out\n" +
            "net name=" + g1 + "_Y gates=" + g1 + ":Y," + g2 + ":A terminals= width=1\n" +
            "net name=" + g2 + "_Y gates=" + g2 + ":Y," + g3 + ":A terminals= width=1\n" +
            "net name=" + g4 + "_Y gates=" + g4 + ":Y," + g5 + ":A terminals= width=1\n" +
            "net name=" + g5 + "_Y gates=" + g5 + ":Y," + g6 + ":A terminals= width=1\n" +
            "net name=a gates=" + g1 + ":A," + g4 + ":A terminals=a width=1\n" +
            "net name=b gates=" + g1 + ":B," + g5 + ":B terminals=b width=1\n" +
            "net name=c gates=" + g2 + ":B," + g4 + ":B terminals=c width=1\n" +
            "net name=d gates=" + g3 + ":B," + g6 + ":B terminals=d width=1\n" +
            "net name=y1 gates=" + g3 + ":Y terminals=y1 width=1\n" +
            "net name=y2 gates=" + g6 + ":Y terminals=y2 width=1\n");
}

// The schematic for a nested leaf, from the live elaborated
// design: the adder is a single $add gate. A non-leaf module
// reports parents only: its only primitives are none, and its
// submodule wires are not schematic nets.
TEST(CoreCommands, SynthSchematicLeafReportsAdderGate) {
  TimingTempDir dir;
  Shell shell;
  const std::string path = dir.Write("top.v", kHierarchyVerilog);
  ASSERT_TRUE(shell.Run("read_verilog", {path, "top"}).ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult schematic = shell.Run("synth_schematic", {"adder"});
  ASSERT_TRUE(schematic.ok) << schematic.message;
  // The gate name carries Yosys's process-global auto-name
  // counter, so only its file-and-line prefix is pinned; the
  // rest of the payload is exact.
  std::string gate;
  {
    std::istringstream in(schematic.payload);
    std::string line;
    const std::string prefix = "gate instance=";
    while (std::getline(in, line)) {
      if (line.rfind(prefix, 0) != 0) continue;
      const std::size_t space = line.find(' ', prefix.size());
      ASSERT_NE(space, std::string::npos);
      gate = line.substr(prefix.size(), space - prefix.size());
    }
  }
  EXPECT_NE(gate.find("$add$" + path + ":4$"), std::string::npos)
      << "adder gate: " << gate;
  EXPECT_EQ(schematic.payload,
            "module=adder\n"
            "parent port=a direction=in\n"
            "parent port=b direction=in\n"
            "parent port=y direction=out\n"
            "gate instance=" + gate + " type=$add ports=A:in,B:in,Y:out\n" +
            "net name=a gates=" + gate + ":A terminals=a width=4\n" +
            "net name=b gates=" + gate + ":B terminals=b width=4\n" +
            "net name=y gates=" + gate + ":Y terminals=y width=4\n");
  const CommandResult mid = shell.Run("synth_schematic", {"mid"});
  ASSERT_TRUE(mid.ok) << mid.message;
  EXPECT_EQ(mid.payload,
            "module=mid\n"
            "parent port=a direction=in\n"
            "parent port=b direction=in\n"
            "parent port=y direction=out\n"
            "net name=a gates= terminals=a width=4\n"
            "net name=b gates= terminals=b width=4\n"
            "net name=y gates= terminals=y width=4\n");
}

// Why U4 reads the elaborated design, not the post-generic
// netlist: synth_generic replaces the live design with a
// flattened optimized netlist, so the hierarchy collapses to
// the root and a nested leaf module no longer exists to query.
// The flat case keeps working after generic (three shared
// $and gates, y2 driven from the shared tree), but the nested
// leaf is gone, so the schematic must come from the design the
// hierarchy itself reads.
TEST(CoreCommands, SynthSchematicNestedLeafFailsAfterGeneric) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell
                  .Run("read_verilog",
                       {dir.Write("top.v", kHierarchyVerilog), "top"})
                  .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  ASSERT_TRUE(shell.Run("synth_generic").ok) << "synth_generic failed";
  const CommandResult hierarchy = shell.Run("synth_hierarchy");
  ASSERT_TRUE(hierarchy.ok) << hierarchy.message;
  // The hierarchy collapses to the root, and its ports are
  // bit-blasted by the ABC netlist round trip.
  EXPECT_EQ(hierarchy.payload,
            "top=top\n"
            "node path=top module=top "
            "ports=a[0],a[1],a[2],a[3],b[0],b[1],b[2],b[3],sel,"
            "y[0],y[1],y[2],y[3]\n");
  EXPECT_FALSE(shell.Run("synth_schematic", {"adder"}).ok);
}

TEST(CoreCommands, SynthSchematicFlatAfterGenericReportsSharedTree) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell
                  .Run("read_verilog",
                       {dir.Write("optloop.v", kSynthOptLoopDemo), "optloop"})
                  .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  ASSERT_TRUE(shell.Run("synth_generic").ok) << "synth_generic failed";
  const CommandResult schematic = shell.Run("synth_schematic", {"optloop"});
  ASSERT_TRUE(schematic.ok) << schematic.message;
  // Three shared $and gates, not six: ABC hashes the two
  // logically equal AND trees into one, and y2 is driven from
  // the shared tree. The gate instance names embed a
  // process-specific temp path, so only the count and the y2
  // pass-through shape are asserted exactly.
  std::size_t gates = 0;
  std::size_t at = 0;
  while ((at = schematic.payload.find("gate instance=", at)) !=
         std::string::npos) {
    ++gates;
    ++at;
  }
  EXPECT_EQ(gates, 3u);
  EXPECT_NE(schematic.payload.find("net name=y2 gates= terminals=y2"),
            std::string::npos);
}

const char kSynthOptLiberty[] = R"LIB(library (eos_opt_fixture) {
  delay_model : "table_lookup";
  time_unit : "1ns";
  voltage_unit : "1v";
  capacitive_load_unit (1,pF);
  current_unit : "1mA";
  pulling_resistance_unit : "1kohm";
  leakage_power_unit : "1pW";
  input_threshold_pct_rise : 50;
  input_threshold_pct_fall : 50;
  output_threshold_pct_rise : 50;
  output_threshold_pct_fall : 50;
  slew_lower_threshold_pct_rise : 30;
  slew_lower_threshold_pct_fall : 30;
  slew_upper_threshold_pct_rise : 70;
  slew_upper_threshold_pct_fall : 70;
  slew_derate_from_library : 1.0;
  nom_process : 1.0;
  nom_temperature : 25.0;
  nom_voltage : 1.0;
  cell (AND2) {
    area : 2.0;
    pin (A) { direction : input; capacitance : 0.001; }
    pin (B) { direction : input; capacitance : 0.001; }
    pin (Y) {
      direction : output;
      function : "A*B";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.6"); }
        cell_fall (scalar) { values ("0.6"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
      timing () {
        related_pin : "B";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.6"); }
        cell_fall (scalar) { values ("0.6"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
  cell (AND2_FAST) {
    area : 4.0;
    pin (A) { direction : input; capacitance : 0.001; }
    pin (B) { direction : input; capacitance : 0.001; }
    pin (Y) {
      direction : output;
      function : "A*B";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.3"); }
        cell_fall (scalar) { values ("0.3"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
      timing () {
        related_pin : "B";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.3"); }
        cell_fall (scalar) { values ("0.3"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
  cell (INV) {
    area : 1.0;
    pin (A) { direction : input; capacitance : 0.001; }
    pin (Y) {
      direction : output;
      function : "!A";
      timing () {
        related_pin : "A";
        timing_sense : negative_unate;
        cell_rise (scalar) { values ("0.5"); }
        cell_fall (scalar) { values ("0.5"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
  cell (BUF) {
    area : 1.0;
    pin (A) { direction : input; capacitance : 0.001; }
    pin (Y) {
      direction : output;
      function : "A";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.4"); }
        cell_fall (scalar) { values ("0.4"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
}
)LIB";

const char kSynthOptSdcTight[] =
    "create_clock -name vclk -period 1\n"
    "set_input_delay -clock vclk 0 [all_inputs]\n"
    "set_output_delay -clock vclk 0 [all_outputs]\n";

// The clears case: a 1.25 ns virtual clock. The area incumbent
// arrives at 1.2 ns on y1 (slack +0.05 ns, inside the 0.1 ns
// margin, so y1 is in the working set) and at 1.6 ns on y2
// (slack -0.35 ns, violating), and the classic strategy's
// duplicate-tree candidate arrives at 1.2 ns on both, clearing
// every working-set endpoint in one iteration.
const char kSynthOptSdcClear[] =
    "create_clock -name vclk -period 1.25\n"
    "set_input_delay -clock vclk 0 [all_inputs]\n"
    "set_output_delay -clock vclk 0 [all_outputs]\n";

const char kSynthOptSdcRelaxed[] =
    "create_clock -name vclk -period 10\n"
    "set_input_delay -clock vclk 0 [all_inputs]\n"
    "set_output_delay -clock vclk 0 [all_outputs]\n";

// Runs the S3 fixture flow to an area-mapped design under the
// given SDC: scenario, Liberty, Verilog, SDC, elaborate,
// generic, area mapping. Returns false when any step fails.
bool SynthOptFlowToMapped(Shell& shell, TimingTempDir& dir,
                          const char* sdc) {
  if (!shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
           .ok) {
    return false;
  }
  if (!shell.Run("read_liberty",
                 {"tt", dir.Write("cells.lib", kSynthOptLiberty)})
           .ok) {
    return false;
  }
  if (!shell.Run("read_verilog",
                 {dir.Write("optloop.v", kSynthOptLoopDemo), "optloop"})
           .ok) {
    return false;
  }
  if (!shell.Run("read_sdc", {dir.Write("top.sdc", sdc)}).ok) {
    return false;
  }
  if (!shell.Run("elaborate").ok) return false;
  if (!shell.Run("synth_generic").ok) return false;
  return shell.Run("synth_map", {"-target", "area"}).ok;
}

double SynthPayloadNumber(const CommandResult& result,
                          const std::string& key) {
  const std::size_t at = result.payload.find(key + "=");
  EXPECT_NE(at, std::string::npos) << "missing " << key;
  if (at == std::string::npos) return 0.0;
  return std::stod(result.payload.substr(at + key.size() + 1));
}

TEST(CoreCommands, SynthOptClearsEveryWorkingSetEndpoint) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(SynthOptFlowToMapped(shell, dir, kSynthOptSdcClear));
  EXPECT_TRUE(shell.ctx.synth.mapped);
  EXPECT_EQ(shell.ctx.synth.mapped_target, "area");
  // The area incumbent shares one AND tree between the outputs
  // (both outputs compute the same four-input AND) and drives
  // y2 from y1 through a buffer: three AND2 cells plus one BUF,
  // arrivals 1.2 ns on y1 and 1.6 ns on y2. The classic
  // candidate duplicates the tree instead (four AND2 cells),
  // arriving at 1.2 ns on both endpoints.
  EXPECT_EQ(shell.ctx.synth.mapped_cells, 4);
  EXPECT_NE(shell.ctx.synth.mapped_types.find("AND2:3"),
            std::string::npos);
  EXPECT_NE(shell.ctx.synth.mapped_types.find("BUF:1"),
            std::string::npos);
  const CommandResult loop =
      shell.Run("synth_opt", {"-strategies", "classic"});
  ASSERT_TRUE(loop.ok) << loop.message;
  EXPECT_NE(loop.payload.find("iterations=1"), std::string::npos);
  EXPECT_NE(loop.payload.find("winner=classic"), std::string::npos);
  EXPECT_NE(loop.payload.find("committed=classic"), std::string::npos);
  EXPECT_NE(loop.payload.find("sta_sessions=1"), std::string::npos);
  EXPECT_NE(loop.payload.find("sta_evaluations=2"), std::string::npos);
  EXPECT_NE(loop.payload.find("still_violating=none"),
            std::string::npos);
  EXPECT_NE(loop.payload.find("equivalence_rejected=0"),
            std::string::npos);
  EXPECT_NE(loop.payload.find("failed_strategies=none"),
            std::string::npos);
  EXPECT_NEAR(SynthPayloadNumber(loop, "baseline_worst_slack_ns"),
              -0.35, 1e-4);
  EXPECT_NEAR(
      SynthPayloadNumber(loop, "baseline_total_negative_slack_ns"),
      -0.35, 1e-4);
  EXPECT_NEAR(SynthPayloadNumber(loop, "final_worst_slack_ns"), 0.05,
              1e-4);
  EXPECT_NEAR(
      SynthPayloadNumber(loop, "final_total_negative_slack_ns"),
      0.0, 1e-4);
  // Two working-set endpoints, both cleared: the endpoints list
  // carries two pin:slack entries.
  const std::size_t endpoints_at =
      loop.payload.find("endpoints=");
  ASSERT_NE(endpoints_at, std::string::npos);
  const std::size_t endpoints_end =
      loop.payload.find(' ', endpoints_at);
  const std::string endpoints = loop.payload.substr(
      endpoints_at + 10,
      endpoints_end == std::string::npos
          ? std::string::npos
          : endpoints_end - endpoints_at - 10);
  EXPECT_EQ(std::count(endpoints.begin(), endpoints.end(), ','), 1);
  EXPECT_TRUE(shell.ctx.synth.opt_run);
  EXPECT_EQ(shell.ctx.synth.opt_winner, "classic");
  EXPECT_EQ(shell.ctx.synth.opt_iterations, 1);
  EXPECT_TRUE(shell.ctx.synth.opt_still_violating.empty());
  EXPECT_EQ(shell.ctx.synth.opt_cells, 4);
  EXPECT_NE(shell.ctx.synth.opt_types.find("AND2:4"),
            std::string::npos);
  EXPECT_EQ(shell.ctx.timing.verilog_path,
            shell.ctx.synth.opt_verilog);
  // The next run_timing is the STA on the optimized netlist and
  // must reproduce the loop's final record.
  const CommandResult run = shell.Run("run_timing");
  ASSERT_TRUE(run.ok) << run.message;
  EXPECT_NEAR(SynthPayloadNumber(run, "worst_slack_ns"), 0.05, 1e-4);
  EXPECT_NEAR(SynthPayloadNumber(run, "total_negative_slack_ns"),
              0.0, 1e-4);
}

// The improves-but-does-not-clear case, at the tight 1 ns clock:
// the same classic candidate improves every working-set endpoint
// (y2 from -0.6 ns to -0.2 ns, y1 held at -0.2 ns, no regression)
// without clearing either, so a second iteration finds no
// further improvement and the loop reports exactly which
// endpoints still violate. This is the acceptance's second
// branch: improve every endpoint, or report the rest exactly.
TEST(CoreCommands, SynthOptImprovesEveryEndpointAndReportsTheRest) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(SynthOptFlowToMapped(shell, dir, kSynthOptSdcTight));
  const CommandResult loop =
      shell.Run("synth_opt", {"-strategies", "classic"});
  ASSERT_TRUE(loop.ok) << loop.message;
  EXPECT_NE(loop.payload.find("iterations=2"), std::string::npos);
  EXPECT_NE(loop.payload.find("winner=classic"), std::string::npos);
  EXPECT_NE(loop.payload.find("committed=classic"), std::string::npos);
  EXPECT_NE(loop.payload.find("sta_sessions=1"), std::string::npos);
  EXPECT_NE(loop.payload.find("sta_evaluations=3"), std::string::npos);
  EXPECT_NE(loop.payload.find("equivalence_rejected=0"),
            std::string::npos);
  EXPECT_NE(loop.payload.find("failed_strategies=none"),
            std::string::npos);
  EXPECT_EQ(loop.payload.find("still_violating=none"),
            std::string::npos);
  EXPECT_NEAR(SynthPayloadNumber(loop, "baseline_worst_slack_ns"),
              -0.6, 1e-4);
  EXPECT_NEAR(
      SynthPayloadNumber(loop, "baseline_total_negative_slack_ns"),
      -0.8, 1e-4);
  EXPECT_NEAR(SynthPayloadNumber(loop, "final_worst_slack_ns"), -0.2,
              1e-4);
  EXPECT_NEAR(
      SynthPayloadNumber(loop, "final_total_negative_slack_ns"),
      -0.4, 1e-4);
  EXPECT_TRUE(shell.ctx.synth.opt_run);
  EXPECT_EQ(shell.ctx.synth.opt_winner, "classic");
  EXPECT_EQ(shell.ctx.synth.opt_iterations, 2);
  EXPECT_EQ(shell.ctx.synth.opt_still_violating.size(), 2u);
  EXPECT_EQ(shell.ctx.synth.opt_cells, 4);
  EXPECT_NE(shell.ctx.synth.opt_types.find("AND2:4"),
            std::string::npos);
  EXPECT_EQ(shell.ctx.timing.verilog_path,
            shell.ctx.synth.opt_verilog);
  const CommandResult run = shell.Run("run_timing");
  ASSERT_TRUE(run.ok) << run.message;
  EXPECT_NEAR(SynthPayloadNumber(run, "worst_slack_ns"), -0.2, 1e-4);
  EXPECT_NEAR(SynthPayloadNumber(run, "total_negative_slack_ns"),
              -0.4, 1e-4);
}

TEST(CoreCommands, SynthOptLeavesACleanDesignAlone) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(SynthOptFlowToMapped(shell, dir, kSynthOptSdcRelaxed));
  // At the relaxed clock the area incumbent meets timing with
  // 8.8 ns of slack, so the working set is empty and no strategy
  // runs: the one STA evaluation is the baseline alone.
  const CommandResult loop = shell.Run("synth_opt");
  ASSERT_TRUE(loop.ok) << loop.message;
  EXPECT_NE(loop.payload.find("iterations=0"), std::string::npos);
  EXPECT_NE(loop.payload.find("winner=none"), std::string::npos);
  EXPECT_NE(loop.payload.find("working_set=none"), std::string::npos);
  EXPECT_NE(loop.payload.find("sta_evaluations=1"), std::string::npos);
  EXPECT_NE(loop.payload.find("sta_sessions=1"), std::string::npos);
  EXPECT_NE(loop.payload.find("still_violating=none"),
            std::string::npos);
  EXPECT_EQ(shell.ctx.synth.opt_iterations, 0);
  EXPECT_TRUE(shell.ctx.synth.opt_winner.empty());
  EXPECT_EQ(shell.ctx.synth.opt_verilog,
            shell.ctx.synth.mapped_verilog);
}

TEST(CoreCommands, SynthOptReportsStillViolatingAtTheCap) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(SynthOptFlowToMapped(shell, dir, kSynthOptSdcTight));
  // The area strategy on the area incumbent cannot improve it,
  // so the single allowed iteration ends with both endpoints
  // still violating, named exactly, and the incumbent untouched.
  const CommandResult loop = shell.Run(
      "synth_opt", {"-strategies", "area", "-max_iterations", "1"});
  ASSERT_TRUE(loop.ok) << loop.message;
  EXPECT_NE(loop.payload.find("iterations=1"), std::string::npos);
  EXPECT_NE(loop.payload.find("winner=none"), std::string::npos);
  EXPECT_NE(loop.payload.find("committed=none"), std::string::npos);
  EXPECT_NE(loop.payload.find("failed_strategies=none"),
            std::string::npos);
  EXPECT_NE(loop.payload.find("equivalence_rejected=0"),
            std::string::npos);
  EXPECT_EQ(loop.payload.find("still_violating=none"),
            std::string::npos);
  const std::size_t at = loop.payload.find("still_violating=");
  ASSERT_NE(at, std::string::npos);
  const std::size_t end = loop.payload.find(' ', at);
  const std::string still = loop.payload.substr(
      at + 16,
      end == std::string::npos ? std::string::npos : end - at - 16);
  EXPECT_EQ(std::count(still.begin(), still.end(), ','), 1);
  EXPECT_TRUE(shell.ctx.synth.opt_still_violating.size() == 2u);
  EXPECT_EQ(shell.ctx.synth.opt_verilog,
            shell.ctx.synth.mapped_verilog);
}

TEST(CoreCommands, SynthOptRejectsBadOptionsAndLeavesState) {
  Shell shell;
  EXPECT_FALSE(shell.Run("synth_opt").ok);
  TimingTempDir dir;
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("optloop.v", kSynthOptLoopDemo), "optloop"})
                    .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  EXPECT_FALSE(shell.Run("synth_opt", {"-bogus", "1"}).ok);
  EXPECT_FALSE(shell.Run("synth_opt", {"-strategies"}).ok);
  EXPECT_FALSE(
      shell.Run("synth_opt", {"-strategies", "sideways"}).ok);
  EXPECT_FALSE(
      shell.Run("synth_opt", {"-strategies", "classic,classic"})
          .ok);
  EXPECT_FALSE(shell.Run("synth_opt", {"-strategies", ""}).ok);
  EXPECT_FALSE(shell.Run("synth_opt", {"-margin"}).ok);
  EXPECT_FALSE(shell.Run("synth_opt", {"-margin", "-1"}).ok);
  EXPECT_FALSE(shell.Run("synth_opt", {"-margin", "abc"}).ok);
  EXPECT_FALSE(shell.Run("synth_opt", {"-max_iterations"}).ok);
  EXPECT_FALSE(shell.Run("synth_opt", {"-max_iterations", "0"}).ok);
  EXPECT_FALSE(shell.Run("synth_opt", {"-max_iterations", "x"}).ok);
  EXPECT_FALSE(shell.Run("synth_opt", {"-margin", "0.1", "-margin",
                                       "0.2"})
                   .ok);
  // Every rejection happened before any engine ran, and none of
  // them mapped the design, so the elaborated state is untouched.
  EXPECT_TRUE(shell.ctx.synth.elaborated);
  EXPECT_FALSE(shell.ctx.synth.mapped);
  EXPECT_FALSE(shell.ctx.synth.opt_run);
}

TEST(CoreCommands, SynthOptRequiresAnSdc) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kSynthOptLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("optloop.v", kSynthOptLoopDemo), "optloop"})
                    .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  ASSERT_TRUE(shell.Run("synth_map", {"-liberty", dir.Path("cells.lib")})
                    .ok);
  EXPECT_TRUE(shell.ctx.synth.mapped);
  const CommandResult loop = shell.Run("synth_opt");
  EXPECT_FALSE(loop.ok);
  EXPECT_NE(loop.message.find("SDC"), std::string::npos);
}

// S7: the exit report reads at any stage, and a stage not
// reached reports "none" rather than failing the report.
TEST(CoreCommands, SynthReportBeforeAnyStage) {
  Shell shell;
  const CommandResult report = shell.Run("synth_report");
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("stages=none"), std::string::npos);
  EXPECT_NE(report.payload.find("winner=none"), std::string::npos);
  EXPECT_NE(report.payload.find("deliverable=none"),
            std::string::npos);
  EXPECT_NE(report.payload.find("equivalence=not_run"),
            std::string::npos);
  EXPECT_NE(report.payload.find("strategies=none"),
            std::string::npos);
  EXPECT_FALSE(shell.Run("synth_report", {"extra"}).ok);
}

// S7: write_verilog copies the current netlist byte for byte:
// the mapped netlist before the loop, the loop winner after
// it, and a failed write leaves the previous record alone.
TEST(CoreCommands, SynthWriteVerilogWritesMappedThenOptWinner) {
  TimingTempDir dir;
  Shell shell;
  EXPECT_FALSE(shell.Run("write_verilog", {dir.Path("out.v")}).ok);
  ASSERT_TRUE(SynthOptFlowToMapped(shell, dir, kSynthOptSdcClear));
  const std::string mapped = shell.ctx.synth.mapped_verilog;
  const std::string dest1 = dir.Path("mapped_out.v");
  const CommandResult written = shell.Run("write_verilog", {dest1});
  ASSERT_TRUE(written.ok) << written.message;
  EXPECT_NE(written.payload.find("source=mapped"), std::string::npos);
  EXPECT_NE(written.payload.find(
                "cells=" + std::to_string(shell.ctx.synth.mapped_cells)),
            std::string::npos);
  {
    std::ifstream in_mapped(mapped, std::ios::binary);
    std::ostringstream mapped_text;
    mapped_text << in_mapped.rdbuf();
    std::ifstream in_out(dest1, std::ios::binary);
    std::ostringstream out_text;
    out_text << in_out.rdbuf();
    EXPECT_EQ(out_text.str(), mapped_text.str());
  }
  EXPECT_FALSE(shell.Run("write_verilog").ok);
  EXPECT_FALSE(
      shell.Run("write_verilog", {dest1, "extra"}).ok);
  EXPECT_FALSE(shell.Run("write_verilog",
                         {dir.Path("no_dir/out.v")})
                   .ok);
  EXPECT_EQ(shell.ctx.synth.output_verilog, dest1);
  EXPECT_EQ(shell.ctx.synth.output_source, "mapped");
  const CommandResult loop =
      shell.Run("synth_opt", {"-strategies", "classic"});
  ASSERT_TRUE(loop.ok) << loop.message;
  EXPECT_EQ(shell.ctx.synth.opt_winner, "classic");
  const std::string dest2 = dir.Path("opt_out.v");
  const CommandResult written2 = shell.Run("write_verilog", {dest2});
  ASSERT_TRUE(written2.ok) << written2.message;
  EXPECT_NE(written2.payload.find("source=opt"), std::string::npos);
  EXPECT_EQ(shell.ctx.synth.output_verilog, dest2);
  EXPECT_EQ(shell.ctx.synth.output_source, "opt");
  {
    std::ifstream in_opt(shell.ctx.synth.opt_verilog,
                         std::ios::binary);
    std::ostringstream opt_text;
    opt_text << in_opt.rdbuf();
    std::ifstream in_out(dest2, std::ios::binary);
    std::ostringstream out_text;
    out_text << in_out.rdbuf();
    EXPECT_EQ(out_text.str(), opt_text.str());
  }
  const CommandResult report = shell.Run("synth_report");
  ASSERT_TRUE(report.ok);
  EXPECT_NE(report.payload.find("stages="), std::string::npos);
  EXPECT_NE(report.payload.find("mapped"), std::string::npos);
  EXPECT_NE(report.payload.find("opt"), std::string::npos);
  EXPECT_NE(report.payload.find("output"), std::string::npos);
  EXPECT_NE(report.payload.find("winner=classic"), std::string::npos);
  EXPECT_NE(report.payload.find("output=" + dest2),
            std::string::npos);
  EXPECT_NE(report.payload.find("deliverable=" + dest2),
            std::string::npos);
}

// S7: the loop's per-strategy record carries each commit's
// own STA numbers and its equivalence proof, and the report
// names the winner by the lexicographic rule.
TEST(CoreCommands, SynthReportNamesWinnerAndPerStrategyMetrics) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(SynthOptFlowToMapped(shell, dir, kSynthOptSdcClear));
  const CommandResult loop =
      shell.Run("synth_opt", {"-strategies", "classic"});
  ASSERT_TRUE(loop.ok) << loop.message;
  ASSERT_EQ(shell.ctx.synth.opt_strategy_records.size(), 1u);
  EXPECT_NE(shell.ctx.synth.opt_strategy_records[0].find("classic:"),
            std::string::npos);
  EXPECT_NE(shell.ctx.synth.opt_strategy_records[0].find(
                "proof=equivalent"),
            std::string::npos);
  const CommandResult report = shell.Run("synth_report");
  ASSERT_TRUE(report.ok);
  EXPECT_NE(report.payload.find("winner=classic"), std::string::npos);
  EXPECT_NE(report.payload.find("strategies=classic:"),
            std::string::npos);
  EXPECT_NE(report.payload.find("proof=equivalent"),
            std::string::npos);
  EXPECT_NE(report.payload.find(
                "winner_rule=lexicographic:worst_slack_ns,"),
            std::string::npos);
  EXPECT_NEAR(SynthPayloadNumber(report, "opt_worst_slack_ns"), 0.05,
              1e-4);
  EXPECT_NEAR(
      SynthPayloadNumber(report, "opt_total_negative_slack_ns"), 0.0,
      1e-4);
  EXPECT_NE(report.payload.find("opt_still_violating=none"),
            std::string::npos);
}

// The changed-function design for the S7 equivalence tests:
// same ports as optloop, but y2 differs (OR in place of the
// second AND), so its mapped netlist must prove NOT
// equivalent to the optloop mapping.
const char kSynthOptLoopChanged[] = R"V(module optloop (a, b, c, d, y1, y2);
  input a, b, c, d;
  output y1, y2;
  assign y1 = ((a & b) & c) & d;
  assign y2 = ((a | c) & b) & d;
endmodule
)V";

// S7: check_equivalence proves the winner against the mapped
// netlist, rejects a changed function, reports an
// unconcludable proof as unknown, and leaves the session
// record untouched when a precondition fails.
TEST(CoreCommands, SynthCheckEquivalenceProvesWinnerAndRejects) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(SynthOptFlowToMapped(shell, dir, kSynthOptSdcClear));
  const CommandResult loop =
      shell.Run("synth_opt", {"-strategies", "classic"});
  ASSERT_TRUE(loop.ok) << loop.message;
  const std::string mapped = shell.ctx.synth.mapped_verilog;
  const std::string winner = shell.ctx.synth.opt_verilog;
  const std::string genlib = shell.ctx.synth.mapped_genlib;
  const CommandResult proof =
      shell.Run("check_equivalence", {mapped, winner});
  ASSERT_TRUE(proof.ok) << proof.message;
  EXPECT_NE(proof.payload.find("verdict=equivalent"),
            std::string::npos);
  EXPECT_TRUE(shell.ctx.synth.equiv_run);
  EXPECT_EQ(shell.ctx.synth.equiv_verdict, "equivalent");
  const CommandResult explicit_genlib = shell.Run(
      "check_equivalence", {mapped, winner, "-genlib", genlib});
  ASSERT_TRUE(explicit_genlib.ok) << explicit_genlib.message;
  // Precondition failures leave the record at the last proof.
  EXPECT_FALSE(shell.Run("check_equivalence", {mapped}).ok);
  EXPECT_FALSE(
      shell.Run("check_equivalence", {mapped, winner, "extra"}).ok);
  EXPECT_FALSE(shell.Run("check_equivalence",
                         {mapped, winner, "-bogus", "1"})
                   .ok);
  EXPECT_FALSE(shell.Run("check_equivalence",
                         {mapped, winner, "-genlib"})
                   .ok);
  EXPECT_FALSE(shell.Run("check_equivalence",
                         {mapped, winner, "-genlib", genlib,
                          "-genlib", genlib})
                   .ok);
  EXPECT_FALSE(shell.Run("check_equivalence",
                         {mapped, dir.Path("missing.v")})
                   .ok);
  EXPECT_EQ(shell.ctx.synth.equiv_verdict, "equivalent");
  // No genlib anywhere: a fresh shell with two readable
  // files still cannot prove without a library.
  {
    Shell bare;
    const std::string first = dir.Write("a.v", kSynthOptLoopDemo);
    const std::string second = dir.Write("b.v", kSynthOptLoopDemo);
    const CommandResult no_genlib =
        bare.Run("check_equivalence", {first, second});
    EXPECT_FALSE(no_genlib.ok);
    EXPECT_NE(no_genlib.message.find("genlib"), std::string::npos);
  }
  // A changed function proves not equivalent.
  {
    TimingTempDir dir2;
    Shell other;
    ASSERT_TRUE(other.Run("create_scenario",
                          {"func_tt", "func", "tt", "estimate"})
                    .ok);
    ASSERT_TRUE(other.Run("read_liberty",
                          {"tt", dir2.Write("cells.lib",
                                            kSynthOptLiberty)})
                    .ok);
    ASSERT_TRUE(other.Run("read_verilog",
                          {dir2.Write("optloop.v",
                                      kSynthOptLoopChanged),
                           "optloop"})
                    .ok);
    ASSERT_TRUE(
        other.Run("read_sdc",
                  {dir2.Write("top.sdc", kSynthOptSdcClear)})
            .ok);
    ASSERT_TRUE(other.Run("elaborate").ok);
    ASSERT_TRUE(other.Run("synth_generic").ok);
    ASSERT_TRUE(other.Run("synth_map", {"-target", "area"}).ok);
    const CommandResult changed = shell.Run(
        "check_equivalence",
        {mapped, other.ctx.synth.mapped_verilog});
    EXPECT_FALSE(changed.ok);
    EXPECT_NE(changed.message.find("not_equivalent"),
              std::string::npos);
    EXPECT_EQ(changed.diag, Diag::kProofUnknown);
    EXPECT_EQ(shell.ctx.synth.equiv_verdict, "not_equivalent");
  }
  // A file the prover cannot read at all is unknown, never a
  // pass, and the session records the unknown verdict.
  const std::string garbage = dir.Write("garbage.v", "not verilog\n");
  const CommandResult unknown =
      shell.Run("check_equivalence", {mapped, garbage});
  EXPECT_FALSE(unknown.ok);
  EXPECT_NE(unknown.message.find("unknown"), std::string::npos);
  EXPECT_EQ(unknown.diag, Diag::kProofUnknown);
  EXPECT_EQ(shell.ctx.synth.equiv_verdict, "unknown");
  const CommandResult report = shell.Run("synth_report");
  ASSERT_TRUE(report.ok);
  EXPECT_NE(report.payload.find("equivalence=unknown"),
            std::string::npos);
}

// S7, measured limit: ABC's mapped reader cannot convert
// assign-based RTL (the conversion fails with an empty
// network), so proving the RTL file against the mapped
// netlist is unknown, never a pass. The command reports
// exactly that, and the session records it. The proof that
// does stand for a strategy output is the loop's own gate:
// every committed candidate was proven kEquivalent against
// its incumbent, transitively from the mapped netlist.
TEST(CoreCommands, SynthCheckEquivalenceRtlVersusMappedIsUnknown) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(SynthOptFlowToMapped(shell, dir, kSynthOptSdcClear));
  const CommandResult loop =
      shell.Run("synth_opt", {"-strategies", "classic"});
  ASSERT_TRUE(loop.ok) << loop.message;
  const std::string rtl = dir.Write("rtl.v", kSynthOptLoopDemo);
  const CommandResult proof = shell.Run(
      "check_equivalence", {rtl, shell.ctx.synth.opt_verilog});
  EXPECT_FALSE(proof.ok);
  EXPECT_NE(proof.message.find("unknown"), std::string::npos);
  EXPECT_EQ(proof.diag, Diag::kProofUnknown);
  EXPECT_EQ(shell.ctx.synth.equiv_verdict, "unknown");
}

TEST(CoreCommands, ImportOdbLoadsTheDatabase) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kTimingDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());

  Shell shell;
  const CommandResult imported = shell.Run("import_odb", {db_path});
  ASSERT_TRUE(imported.ok) << imported.message;
  EXPECT_NE(imported.payload.find("design=top"), std::string::npos);
  EXPECT_NE(imported.payload.find("instances=2"), std::string::npos);
  EXPECT_NE(imported.payload.find("nets=3"), std::string::npos);
  ASSERT_TRUE(shell.ctx.timing.odb.has_value());
  const CommandResult missing =
      shell.Run("import_odb", {dir.Path("no_such.odb")});
  EXPECT_FALSE(missing.ok);
  EXPECT_EQ(missing.diag, Diag::kIoError);
}

TEST(CoreCommands, CollateralReadsFailFastOnMissingFiles) {
  TimingTempDir dir;
  Shell shell;
  // A missing collateral file is an io_error at the read command,
  // not a deferred surprise at run_timing time, and it records
  // nothing.
  const CommandResult liberty =
      shell.Run("read_liberty", {"tt", dir.Path("no_such.lib")});
  EXPECT_FALSE(liberty.ok);
  EXPECT_EQ(liberty.diag, Diag::kIoError);
  EXPECT_TRUE(shell.ctx.timing.liberty_by_corner.empty());
  const CommandResult verilog =
      shell.Run("read_verilog", {dir.Path("no_such.v"), "top"});
  EXPECT_FALSE(verilog.ok);
  EXPECT_EQ(verilog.diag, Diag::kIoError);
  EXPECT_TRUE(shell.ctx.timing.verilog_path.empty());
  const CommandResult sdc =
      shell.Run("read_sdc", {dir.Path("no_such.sdc")});
  EXPECT_FALSE(sdc.ok);
  EXPECT_EQ(sdc.diag, Diag::kIoError);
  EXPECT_TRUE(shell.ctx.timing.sdc_path.empty());
  const CommandResult spef =
      shell.Run("set_spef", {dir.Path("no_such.spef")});
  EXPECT_FALSE(spef.ok);
  EXPECT_EQ(spef.diag, Diag::kIoError);
  EXPECT_TRUE(shell.ctx.timing.spef_path.empty());
  // A directory is not a readable collateral file either.
  const CommandResult dir_as_liberty =
      shell.Run("read_liberty", {"tt", dir.Path("")});
  EXPECT_FALSE(dir_as_liberty.ok);
  EXPECT_EQ(dir_as_liberty.diag, Diag::kIoError);
}

TEST(CoreCommands, ReportTimingFullTable) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kTimingLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("top.v", kTimingVerilog), "top"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kTimingSdc)}).ok);
  const CommandResult report = shell.Run("report_timing", {});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scene=func_tt"), std::string::npos);
  EXPECT_NE(report.payload.find("paths=1"), std::string::npos);
  EXPECT_NE(report.payload.find("report=timing_report_func_tt"),
            std::string::npos);
  EXPECT_NE(report.payload.find("Startpoint: r1/Q"), std::string::npos);
  EXPECT_NE(report.payload.find("Endpoint: r2/D"), std::string::npos);
  EXPECT_NE(report.payload.find("slack (MET): 9.400"), std::string::npos);
  EXPECT_NE(report.payload.find("u1/Y"), std::string::npos);
  EXPECT_NE(report.payload.find("Fanout"), std::string::npos);
  EXPECT_NE(shell.ctx.reports.FindByName(shell.ctx.project,
                                         "timing_report_func_tt"),
            nullptr);
}

TEST(CoreCommands, ReportTimingHoldAndColumns) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kTimingLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("top.v", kTimingVerilog), "top"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kTimingSdc)}).ok);
  const CommandResult report = shell.Run(
      "report_timing", {"-delay_type", "min", "-columns", "point,arrival"});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("slack (MET): 0.450"), std::string::npos);
  EXPECT_NE(report.payload.find("Point"), std::string::npos);
  EXPECT_NE(report.payload.find("Arrival"), std::string::npos);
  EXPECT_EQ(report.payload.find("Fanout"), std::string::npos);
  EXPECT_EQ(report.payload.find("Cap"), std::string::npos);
}

TEST(CoreCommands, ReportTimingPathTypeEnd) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kTimingLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("top.v", kTimingVerilog), "top"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kTimingSdc)}).ok);
  const CommandResult report =
      shell.Run("report_timing", {"-path_type", "end"});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("Endpoint: r2/D"), std::string::npos);
  // The end type prints no startpoint and no point table.
  EXPECT_EQ(report.payload.find("Startpoint:"), std::string::npos);
  EXPECT_EQ(report.payload.find("u1/Y"), std::string::npos);
}

TEST(CoreCommands, ReportTimingToEndpoint) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kTimingLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("top.v", kTimingVerilog), "top"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kTimingSdc)}).ok);
  const CommandResult report =
      shell.Run("report_timing", {"-to", "r2/D"});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("paths=1"), std::string::npos);
  const CommandResult unknown =
      shell.Run("report_timing", {"-to", "r9/D"});
  EXPECT_FALSE(unknown.ok);
}

TEST(CoreCommands, ReportTimingFile) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kTimingLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("top.v", kTimingVerilog), "top"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kTimingSdc)}).ok);
  const std::string file = dir.Path("report.txt");
  const CommandResult report =
      shell.Run("report_timing", {"-file", file});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("file=" + file), std::string::npos);
  std::ifstream in(file);
  ASSERT_TRUE(in);
  std::stringstream contents;
  contents << in.rdbuf();
  EXPECT_NE(contents.str().find("Endpoint: r2/D"), std::string::npos);
  EXPECT_NE(contents.str().find("u1/Y"), std::string::npos);
}

TEST(CoreCommands, ReportTimingRejectsBadOptions) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kTimingLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("top.v", kTimingVerilog), "top"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kTimingSdc)}).ok);
  EXPECT_FALSE(shell.Run("report_timing", {"-bogus", "1"}).ok);
  EXPECT_FALSE(shell.Run("report_timing", {"-columns", "point,bogus"}).ok);
  EXPECT_FALSE(
      shell.Run("report_timing", {"-delay_type", "sideways"}).ok);
  EXPECT_FALSE(
      shell.Run("report_timing", {"-to", "r2/D", "-max_paths", "2"}).ok);
  EXPECT_FALSE(shell.Run("report_timing", {"-max_paths", "0"}).ok);
  EXPECT_FALSE(shell.Run("report_timing", {"-path_type", "wide"}).ok);
}

// ---- report_constraint (Immediate unit 6, review item I3) ----------
// The worst-violator tier: the per-class summary rows, the
// critical-range tables, the -verbose paths and pin tables, the
// tier 2 fail-fast, the scene selection, the file form, and the
// no-session failures.

// Sets up the two-flop timing fixture in the shell: one estimate
// scenario, the given Liberty, the netlist and the given SDC.
void BuildReportConstraintDesign(Shell& shell, const TimingTempDir& dir,
                                 const char* liberty, const char* sdc) {
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(
      shell.Run("read_liberty", {"tt", dir.Write("cells.lib", liberty)})
          .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("top.v", kTimingVerilog), "top"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", sdc)}).ok);
}

TEST(ReportConstraint, CleanDesignReportsZeroViolations) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const CommandResult report = shell.Run("report_constraint", {});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("source=verilog"), std::string::npos);
  EXPECT_NE(report.payload.find("scenes=1"), std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt violations=0 "
                                "worst_slack_ns=9.4"),
            std::string::npos);
  EXPECT_NE(report.payload.find("report=constraint_report_func_tt"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt class=max_delay "
                                "violator=- slack=9.4 cost=0 weight=1 "
                                "violations=0"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt class=min_delay "
                                "violator=- slack=0.45 cost=0 weight=1 "
                                "violations=0"),
            std::string::npos);
  // The plain fixture library carries no electrical limit: every
  // DRC class reports limit=none, never a clean worst value.
  EXPECT_NE(report.payload.find("scene=func_tt class=max_transition "
                                "violator=- limit=none weight=1 "
                                "violations=0"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt class=max_capacitance "
                                "violator=- limit=none weight=1 "
                                "violations=0"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt class=max_fanout "
                                "violator=- limit=none weight=1 "
                                "violations=0"),
            std::string::npos);
  EXPECT_NE(report.payload.find("max_transition: no limit defined "
                                "for this design."),
            std::string::npos);
  // No violations means no critical-range section at all.
  EXPECT_EQ(report.payload.find("Critical range"), std::string::npos);
  EXPECT_NE(shell.ctx.reports.FindByName(shell.ctx.project,
                                         "constraint_report_func_tt"),
            nullptr);
}

TEST(ReportConstraint, TightClockSetupViolation) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty,
                              kTimingSdcTight);
  const CommandResult report = shell.Run("report_constraint", {});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scene=func_tt violations=1 "
                                "worst_slack_ns=-0.1"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt class=max_delay "
                                "violator=r2/D slack=-0.1 cost=0.1 "
                                "weight=1 violations=1"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt class=min_delay "
                                "violator=- slack=0.45 cost=0 weight=1 "
                                "violations=0"),
            std::string::npos);
  EXPECT_NE(report.payload.find("Critical range (max_delay)"),
            std::string::npos);
  EXPECT_NE(report.payload.find("r2/D"), std::string::npos);
  // The default form shows no worst path: that is -verbose.
  EXPECT_EQ(report.payload.find("Worst max_delay path"),
            std::string::npos);
}

TEST(ReportConstraint, VerboseShowsWorstSetupPath) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty,
                              kTimingSdcTight);
  const CommandResult report =
      shell.Run("report_constraint", {"-verbose"});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("Worst max_delay path (r2/D):"),
            std::string::npos);
  EXPECT_NE(report.payload.find("Startpoint: r1/Q"), std::string::npos);
  EXPECT_NE(report.payload.find("slack (VIOLATED): -0.100"),
            std::string::npos);
  // No hold violator, so no hold path even in verbose form.
  EXPECT_EQ(report.payload.find("Worst min_delay path"),
            std::string::npos);
}

TEST(ReportConstraint, ElectricalViolations) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLibertyElectrical,
                              kTimingSdc);
  const CommandResult report = shell.Run("report_constraint", {});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scene=func_tt violations=3"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt class=max_transition "
                                "violator=u1/Y slack=-0.01 cost=0.01 "
                                "weight=1 violations=1"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt class=max_capacitance "
                                "violator=u1/Y slack=-0.009 cost=0.009 "
                                "weight=1 violations=1"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt class=max_fanout "
                                "violator=u1/Y slack=-1 cost=1 "
                                "weight=1 violations=1"),
            std::string::npos);
  EXPECT_NE(report.payload.find("Critical range (max_transition)"),
            std::string::npos);
  EXPECT_NE(report.payload.find("u1/Y"), std::string::npos);
  // The default DRC rows carry pin and slack only: the
  // value/limit columns are the -verbose form.
  EXPECT_EQ(report.payload.find("Value (ns)"), std::string::npos);
  EXPECT_EQ(report.payload.find("Limit (ns)"), std::string::npos);
}

TEST(ReportConstraint, VerboseShowsDrcPinTables) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLibertyElectrical,
                              kTimingSdc);
  const CommandResult report =
      shell.Run("report_constraint", {"-verbose"});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("Value (ns)"), std::string::npos);
  EXPECT_NE(report.payload.find("Limit (ns)"), std::string::npos);
  EXPECT_NE(report.payload.find("0.04"), std::string::npos);
  EXPECT_NE(report.payload.find("Value (pF)"), std::string::npos);
  EXPECT_NE(report.payload.find("Limit (pF)"), std::string::npos);
  EXPECT_NE(report.payload.find("0.001"), std::string::npos);
  EXPECT_NE(report.payload.find("Value (fanout)"), std::string::npos);
  EXPECT_NE(report.payload.find("Limit (fanout)"), std::string::npos);
}

TEST(ReportConstraint, AllViolatorsFailsFastWithTierTwoMessage) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const CommandResult report =
      shell.Run("report_constraint", {"-all_violators"});
  EXPECT_FALSE(report.ok);
  EXPECT_EQ(report.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(report.message.find("tier 2"), std::string::npos);
}

TEST(ReportConstraint, RejectsBadOptions) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  EXPECT_FALSE(shell.Run("report_constraint", {"-bogus"}).ok);
  EXPECT_FALSE(shell.Run("report_constraint", {"top"}).ok);
  EXPECT_FALSE(
      shell.Run("report_constraint", {"-scene", "func_xx"}).ok);
}

TEST(ReportConstraint, SceneSelectsOneScenario) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_ss", "func", "ss", "estimate"})
          .ok);
  const std::string liberty = dir.Write("cells.lib", kTimingLiberty);
  ASSERT_TRUE(shell.Run("read_liberty", {"tt", liberty}).ok);
  ASSERT_TRUE(shell.Run("read_liberty", {"ss", liberty}).ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("top.v", kTimingVerilog), "top"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kTimingSdc)}).ok);
  const CommandResult report =
      shell.Run("report_constraint", {"-scene", "func_ss"});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scenes=1"), std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_ss"), std::string::npos);
  EXPECT_EQ(report.payload.find("scene=func_tt"), std::string::npos);
}

TEST(ReportConstraint, FileWritesReport) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty,
                              kTimingSdcTight);
  const std::string file = dir.Path("constraint.txt");
  const CommandResult report =
      shell.Run("report_constraint", {"-file", file});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("file=" + file), std::string::npos);
  std::ifstream in(file);
  ASSERT_TRUE(in);
  std::stringstream contents;
  contents << in.rdbuf();
  EXPECT_NE(contents.str().find("Constraint"), std::string::npos);
  EXPECT_NE(contents.str().find("r2/D"), std::string::npos);
  EXPECT_NE(contents.str().find("max_delay"), std::string::npos);
}

TEST(ReportConstraint, NoTimingSessionFails) {
  Shell shell;
  EXPECT_FALSE(shell.Run("report_constraint", {}).ok);
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  EXPECT_FALSE(shell.Run("report_constraint", {}).ok);
}

// ---- report_analysis_coverage (Immediate unit 7, review item I4) ---
// The per-type summary and the per-pin detail form on the clean
// two-flop fixture: r1/D setup and hold untested (no clocked
// startpoint), r2/D setup and hold tested. The filters, the file
// form, and the no-session failures.

// The fixture design for the coverage command: the same
// BuildReportConstraintDesign setup, one estimate scenario.
TEST(ReportAnalysisCoverage, CleanDesignReportsSummaryAndDetail) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const CommandResult report = shell.Run("report_analysis_coverage", {});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("source=verilog"), std::string::npos);
  EXPECT_NE(report.payload.find("scenes=1"), std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt checks=4 tested=2 "
                                "untested=2 report=coverage_report_"
                                "func_tt"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt type=setup tested=1 "
                                "untested=1 percent=50.0"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt type=hold tested=1 "
                                "untested=1 percent=50.0"),
            std::string::npos);
  EXPECT_NE(report.payload.find(
                "scene=func_tt pin=r1/D related=r1/CK type=setup "
                "status=untested reason=no_startpoint_clock"),
            std::string::npos);
  EXPECT_NE(report.payload.find(
                "scene=func_tt pin=r1/D related=r1/CK type=hold "
                "status=untested reason=no_startpoint_clock"),
            std::string::npos);
  EXPECT_NE(report.payload.find(
                "scene=func_tt pin=r2/D related=r2/CK type=setup "
                "status=tested reason=-"),
            std::string::npos);
  EXPECT_NE(report.payload.find(
                "scene=func_tt pin=r2/D related=r2/CK type=hold "
                "status=tested reason=-"),
            std::string::npos);
  // The clean fixture defines no other check type.
  EXPECT_NE(report.payload.find(
                "No checks defined: out_setup, out_hold, recovery, "
                "removal, clock_gating_setup, clock_gating_hold, "
                "no-change, min_pulse_width."),
            std::string::npos);
  EXPECT_NE(shell.ctx.reports.FindByName(shell.ctx.project,
                                         "coverage_report_func_tt"),
            nullptr);
}

TEST(ReportAnalysisCoverage, StatusDetailsFiltersDetailRows) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const CommandResult tested = shell.Run(
      "report_analysis_coverage", {"-status_details", "tested"});
  ASSERT_TRUE(tested.ok) << tested.message;
  EXPECT_NE(tested.payload.find("pin=r2/D"), std::string::npos);
  EXPECT_EQ(tested.payload.find("pin=r1/D"), std::string::npos);
  // The summary still counts the untested rows.
  EXPECT_NE(tested.payload.find("scene=func_tt checks=4 tested=2 "
                                "untested=2"),
            std::string::npos);
  const CommandResult untested = shell.Run(
      "report_analysis_coverage", {"-status_details", "untested"});
  ASSERT_TRUE(untested.ok) << untested.message;
  EXPECT_NE(untested.payload.find("pin=r1/D"), std::string::npos);
  EXPECT_EQ(untested.payload.find("pin=r2/D"), std::string::npos);
}

TEST(ReportAnalysisCoverage, CheckTypeFiltersDetailRows) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const CommandResult report = shell.Run("report_analysis_coverage",
                                         {"-check_type", "hold"});
  ASSERT_TRUE(report.ok) << report.message;
  // The detail rows are filtered to hold only; the summary payload
  // lines still count every type.
  EXPECT_NE(report.payload.find("type=hold status="),
            std::string::npos);
  EXPECT_EQ(report.payload.find("type=setup status="),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt checks=4 tested=2 "
                                "untested=2"),
            std::string::npos);
}

TEST(ReportAnalysisCoverage, ExcludeUntestedDropsUntestedRows) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const CommandResult report =
      shell.Run("report_analysis_coverage", {"-exclude_untested"});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("pin=r2/D"), std::string::npos);
  EXPECT_EQ(report.payload.find("pin=r1/D"), std::string::npos);
  // The summary still counts the untested rows.
  EXPECT_NE(report.payload.find("scene=func_tt checks=4 tested=2 "
                                "untested=2"),
            std::string::npos);
}

TEST(ReportAnalysisCoverage, RejectsBadOptions) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  EXPECT_FALSE(shell.Run("report_analysis_coverage", {"-bogus"}).ok);
  EXPECT_FALSE(shell.Run("report_analysis_coverage", {"top"}).ok);
  EXPECT_FALSE(shell.Run("report_analysis_coverage",
                         {"-status_details", "all"})
                   .ok);
  EXPECT_FALSE(shell.Run("report_analysis_coverage",
                         {"-check_type", "max_skew"})
                   .ok);
  EXPECT_FALSE(shell.Run("report_analysis_coverage",
                         {"-scene", "func_xx"})
                   .ok);
}

TEST(ReportAnalysisCoverage, SceneSelectsOneScenario) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_ss", "func", "ss", "estimate"})
          .ok);
  const std::string liberty = dir.Write("cells.lib", kTimingLiberty);
  ASSERT_TRUE(shell.Run("read_liberty", {"tt", liberty}).ok);
  ASSERT_TRUE(shell.Run("read_liberty", {"ss", liberty}).ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("top.v", kTimingVerilog), "top"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kTimingSdc)}).ok);
  const CommandResult report =
      shell.Run("report_analysis_coverage", {"-scene", "func_ss"});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scenes=1"), std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_ss"), std::string::npos);
  EXPECT_EQ(report.payload.find("scene=func_tt"), std::string::npos);
}

TEST(ReportAnalysisCoverage, FileWritesReport) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const std::string file = dir.Path("coverage.txt");
  const CommandResult report =
      shell.Run("report_analysis_coverage", {"-file", file});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("file=" + file), std::string::npos);
  std::ifstream in(file);
  ASSERT_TRUE(in);
  std::stringstream contents;
  contents << in.rdbuf();
  EXPECT_NE(contents.str().find("Check Type"), std::string::npos);
  EXPECT_NE(contents.str().find("r1/D"), std::string::npos);
  EXPECT_NE(contents.str().find("no_startpoint_clock"),
            std::string::npos);
}

TEST(ReportAnalysisCoverage, NoTimingSessionFails) {
  Shell shell;
  EXPECT_FALSE(shell.Run("report_analysis_coverage", {}).ok);
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  EXPECT_FALSE(shell.Run("report_analysis_coverage", {}).ok);
}

// QoR and bottleneck reports (Immediate unit 9, review item I9).
TEST(ReportQor, CleanDesignReportsGroupsCellsAndDrc) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const CommandResult report = shell.Run("report_qor", {});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scene=func_tt setup_groups=1 "
                                "hold_groups=1 union_tns=0 "
                                "union_endpoints=0 every_group_tns=0 "
                                "every_group_endpoints=0 "
                                "hold_union_tns=0 hold_union_endpoints=0 "
                                "hold_every_group_tns=0 "
                                "hold_every_group_endpoints=0 cells=3 "
                                "area=22 compile_cpu_s=n/a "
                                "report=qor_report_func_tt"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt group=clk check=setup "
                                "wns=9.4 tns=0 violating_paths=0"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt group=clk check=hold "
                                "wns=0.45 tns=0 violating_paths=0"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt cell=DFF count=2 "
                                "area=20"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt cell=BUF count=1 "
                                "area=2"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt drc=max_transition "
                                "violations=0 worst_slack=no limit"),
            std::string::npos);
  EXPECT_NE(report.payload.find("Path groups (setup):"),
            std::string::npos);
  EXPECT_NE(report.payload.find("Cells: 3, total area 22"),
            std::string::npos);
  EXPECT_NE(report.payload.find("Compile CPU: n/a (no synthesis run "
                                "in this session)"),
            std::string::npos);
  EXPECT_NE(shell.ctx.reports.FindByName(shell.ctx.project,
                                         "qor_report_func_tt"),
            nullptr);
}

TEST(ReportQor, TightClockUnionVsEveryGroup) {
  // Two group_path commands both matching r2/D: the union TNS
  // counts the endpoint once (-0.1), the every-group TNS once
  // per group (-0.2).
  TimingTempDir dir;
  Shell shell;
  const std::string sdc =
      std::string(kTimingSdcTight) +
      "group_path -name fast -to [get_pins r2/D]\n"
      "group_path -name slow -to [get_pins r2/D]\n";
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, sdc.c_str());
  const CommandResult report = shell.Run("report_qor", {});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("union_tns=-0.1 union_endpoints=1 "
                                "every_group_tns=-0.2 "
                                "every_group_endpoints=2"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt group=fast check=setup "
                                "wns=-0.1 tns=-0.1 violating_paths=1"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt group=slow check=setup "
                                "wns=-0.1 tns=-0.1 violating_paths=1"),
            std::string::npos);
  // No clock-group row: the group_path exceptions take precedence.
  EXPECT_EQ(report.payload.find("group=clk check=setup"),
            std::string::npos);
}

TEST(ReportQor, RejectsBadOptions) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  EXPECT_FALSE(shell.Run("report_qor", {"-bogus"}).ok);
  EXPECT_FALSE(shell.Run("report_qor", {"top"}).ok);
  EXPECT_FALSE(shell.Run("report_qor", {"-scene", "func_xx"}).ok);
}

TEST(ReportQor, SceneSelectsOneScenario) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_ss", "func", "ss", "estimate"})
          .ok);
  const std::string liberty = dir.Write("cells.lib", kTimingLiberty);
  ASSERT_TRUE(shell.Run("read_liberty", {"tt", liberty}).ok);
  ASSERT_TRUE(shell.Run("read_liberty", {"ss", liberty}).ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("top.v", kTimingVerilog), "top"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kTimingSdc)}).ok);
  const CommandResult report =
      shell.Run("report_qor", {"-scene", "func_ss"});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scene=func_ss"), std::string::npos);
  EXPECT_EQ(report.payload.find("scene=func_tt"), std::string::npos);
}

TEST(ReportQor, FileWritesReport) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const std::string file = dir.Path("qor.txt");
  const CommandResult report = shell.Run("report_qor", {"-file", file});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("file=" + file), std::string::npos);
  std::ifstream in(file);
  ASSERT_TRUE(in);
  std::stringstream contents;
  contents << in.rdbuf();
  EXPECT_NE(contents.str().find("Path groups (setup):"),
            std::string::npos);
  EXPECT_NE(contents.str().find("Cells: 3, total area 22"),
            std::string::npos);
}

TEST(SetBottleneckCapture, AcceptsOnOffAndRejectsOther) {
  Shell shell;
  const CommandResult on = shell.Run("set_bottleneck_capture", {"on"});
  ASSERT_TRUE(on.ok) << on.message;
  EXPECT_NE(on.payload.find("bottleneck_capture=on"), std::string::npos);
  EXPECT_TRUE(shell.ctx.timing.bottleneck_capture);
  const CommandResult off = shell.Run("set_bottleneck_capture", {"off"});
  ASSERT_TRUE(off.ok) << off.message;
  EXPECT_NE(off.payload.find("bottleneck_capture=off"),
            std::string::npos);
  EXPECT_FALSE(shell.ctx.timing.bottleneck_capture);
  EXPECT_FALSE(shell.Run("set_bottleneck_capture", {"yes"}).ok);
  EXPECT_FALSE(shell.Run("set_bottleneck_capture", {}).ok);
  EXPECT_FALSE(shell.Run("set_bottleneck_capture", {"on", "off"}).ok);
}

TEST(ReportBottleneck, RequiresCaptureOn) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdcTight);
  const CommandResult run = shell.Run("run_timing");
  ASSERT_TRUE(run.ok) << run.message;
  const CommandResult report = shell.Run("report_bottleneck", {});
  ASSERT_FALSE(report.ok);
  EXPECT_NE(report.message.find("bottleneck capture was off"),
            std::string::npos);
}

TEST(ReportBottleneck, RequiresTimingRun) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdcTight);
  ASSERT_TRUE(shell.Run("set_bottleneck_capture", {"on"}).ok);
  const CommandResult report = shell.Run("report_bottleneck", {});
  ASSERT_FALSE(report.ok);
  EXPECT_NE(report.message.find("no timing results"),
            std::string::npos);
}

TEST(ReportBottleneck, RanksViolatingCells) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdcTight);
  ASSERT_TRUE(shell.Run("set_bottleneck_capture", {"on"}).ok);
  const CommandResult run = shell.Run("run_timing");
  ASSERT_TRUE(run.ok) << run.message;
  const CommandResult report = shell.Run("report_bottleneck", {});
  ASSERT_TRUE(report.ok) << report.message;
  // One violating path, through r1, u1 and r2: all three rank
  // with one path each, ties broken by worst slack then name.
  EXPECT_NE(report.payload.find("scene=all delay_type=max paths=1 "
                                "cells=3 report=bottleneck_report_all"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=all rank=1 cell=r1 type=DFF "
                                "paths=1 worst_slack=-0.1"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=all rank=2 cell=r2 type=DFF "
                                "paths=1 worst_slack=-0.1"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=all rank=3 cell=u1 type=BUF "
                                "paths=1 worst_slack=-0.1"),
            std::string::npos);
  EXPECT_NE(shell.ctx.reports.FindByName(shell.ctx.project,
                                         "bottleneck_report_all"),
            nullptr);
}

TEST(ReportBottleneck, MaxCellsLimitsRanking) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdcTight);
  ASSERT_TRUE(shell.Run("set_bottleneck_capture", {"on"}).ok);
  const CommandResult run = shell.Run("run_timing");
  ASSERT_TRUE(run.ok) << run.message;
  const CommandResult report =
      shell.Run("report_bottleneck", {"-max_cells", "1"});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("paths=1 cells=1"), std::string::npos);
  EXPECT_NE(report.payload.find("rank=1 cell=r1"), std::string::npos);
  EXPECT_EQ(report.payload.find("rank=2"), std::string::npos);
}

TEST(ReportBottleneck, HoldKindWithNoViolationsRanksEmpty) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdcTight);
  ASSERT_TRUE(shell.Run("set_bottleneck_capture", {"on"}).ok);
  const CommandResult run = shell.Run("run_timing");
  ASSERT_TRUE(run.ok) << run.message;
  const CommandResult report =
      shell.Run("report_bottleneck", {"-delay_type", "min"});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("delay_type=min paths=0 cells=0"),
            std::string::npos);
  EXPECT_NE(report.payload.find("(no violating paths captured)"),
            std::string::npos);
}

TEST(ReportBottleneck, CaptureOffClearsStoredPaths) {
  // A second run_timing with the capture off clears the saved
  // paths, so the report fails instead of ranking stale data.
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdcTight);
  ASSERT_TRUE(shell.Run("set_bottleneck_capture", {"on"}).ok);
  ASSERT_TRUE(shell.Run("run_timing").ok);
  ASSERT_TRUE(shell.Run("set_bottleneck_capture", {"off"}).ok);
  ASSERT_TRUE(shell.Run("run_timing").ok);
  const CommandResult report = shell.Run("report_bottleneck", {});
  ASSERT_FALSE(report.ok);
  EXPECT_NE(report.message.find("bottleneck capture was off"),
            std::string::npos);
}

TEST(ReportBottleneck, RejectsBadOptions) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdcTight);
  ASSERT_TRUE(shell.Run("set_bottleneck_capture", {"on"}).ok);
  ASSERT_TRUE(shell.Run("run_timing").ok);
  EXPECT_FALSE(shell.Run("report_bottleneck", {"-delay_type", "ave"}).ok);
  EXPECT_FALSE(shell.Run("report_bottleneck", {"-max_cells", "0"}).ok);
  EXPECT_FALSE(shell.Run("report_bottleneck", {"-max_cells", "x"}).ok);
  EXPECT_FALSE(shell.Run("report_bottleneck", {"-scene", "func_xx"}).ok);
  EXPECT_FALSE(shell.Run("report_bottleneck", {"top"}).ok);
}

// Check-specific reports (Immediate unit 10, review item T8).
void BuildGatedCheckDesign(Shell& shell, const TimingTempDir& dir,
                           const char* verilog) {
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kTimingLibertyGated)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("top.v", verilog), "top"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kTimingSdc)}).ok);
}

TEST(ReportGlobalTiming, CleanDesignReportsWorstPaths) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const CommandResult report = shell.Run("report_global_timing", {});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scene=func_tt setup_wns=9.4 "
                                "setup_tns=0 setup_violating=0 "
                                "hold_wns=0.45 hold_tns=0 "
                                "hold_violating=0 report="
                                "global_timing_report_func_tt"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt group=clk check=setup "
                                "endpoint=r2/D startpoint=r1/Q "
                                "slack=9.4"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt group=clk check=hold "
                                "endpoint=r2/D startpoint=r1/Q "
                                "slack=0.45"),
            std::string::npos);
  EXPECT_NE(report.payload.find("Global timing (scene func_tt):"),
            std::string::npos);
  EXPECT_NE(report.payload.find("Worst paths (setup):"),
            std::string::npos);
  EXPECT_NE(report.payload.find("Worst paths (hold):"),
            std::string::npos);
  EXPECT_NE(shell.ctx.reports.FindByName(shell.ctx.project,
                                         "global_timing_report_func_tt"),
            nullptr);
}

TEST(ReportGlobalTiming, DelayTypeSelectsKind) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const CommandResult report =
      shell.Run("report_global_timing", {"-delay_type", "min"});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("check=hold"), std::string::npos);
  EXPECT_EQ(report.payload.find("check=setup"), std::string::npos);
  EXPECT_EQ(report.payload.find("Worst paths (setup):"),
            std::string::npos);
  EXPECT_NE(report.payload.find("Worst paths (hold):"),
            std::string::npos);
}

TEST(ReportGlobalTiming, RejectsBadOptions) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  EXPECT_FALSE(shell.Run("report_global_timing", {"-bogus"}).ok);
  EXPECT_FALSE(shell.Run("report_global_timing", {"top"}).ok);
  EXPECT_FALSE(
      shell.Run("report_global_timing", {"-delay_type", "ave"}).ok);
  EXPECT_FALSE(
      shell.Run("report_global_timing", {"-scene", "func_xx"}).ok);
}

TEST(ReportGlobalTiming, FileWritesReport) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const std::string file = dir.Path("global-timing.txt");
  const CommandResult report =
      shell.Run("report_global_timing", {"-file", file});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("file=" + file), std::string::npos);
  std::ifstream in(file);
  ASSERT_TRUE(in);
  std::stringstream contents;
  contents << in.rdbuf();
  EXPECT_NE(contents.str().find("Global timing (scene func_tt):"),
            std::string::npos);
  EXPECT_NE(contents.str().find("r2/D"), std::string::npos);
}

TEST(ReportGlobalSlack, CleanDesignListsEveryEndpoint) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const CommandResult report = shell.Run("report_global_slack", {});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scene=func_tt check=setup pin=r2/D "
                                "slack=9.4"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt check=hold pin=r2/D "
                                "slack=0.45"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt check=setup "
                                "endpoints=1 violating=0"),
            std::string::npos);
  EXPECT_NE(report.payload.find("Global slack (scene func_tt):"),
            std::string::npos);
  EXPECT_NE(shell.ctx.reports.FindByName(shell.ctx.project,
                                         "global_slack_report_func_tt"),
            nullptr);
}

TEST(ReportGlobalSlack, SlackCeilingFilters) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const CommandResult report = shell.Run(
      "report_global_slack", {"-slack_lesser_than", "1.0"});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scene=func_tt check=hold pin=r2/D "
                                "slack=0.45"),
            std::string::npos);
  EXPECT_EQ(report.payload.find("check=setup pin=r2/D"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt check=setup "
                                "endpoints=0 violating=0"),
            std::string::npos);
}

TEST(ReportGlobalSlack, RejectsBadOptions) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  EXPECT_FALSE(shell.Run("report_global_slack", {"-bogus"}).ok);
  EXPECT_FALSE(shell.Run("report_global_slack", {"top"}).ok);
  EXPECT_FALSE(
      shell.Run("report_global_slack", {"-delay_type", "ave"}).ok);
  EXPECT_FALSE(shell.Run("report_global_slack",
                         {"-slack_lesser_than", "x"})
                   .ok);
  EXPECT_FALSE(
      shell.Run("report_global_slack", {"-scene", "func_xx"}).ok);
}

TEST(ReportGlobalSlack, NoTimingSessionFails) {
  Shell shell;
  EXPECT_FALSE(shell.Run("report_global_slack", {}).ok);
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  EXPECT_FALSE(shell.Run("report_global_slack", {}).ok);
}

TEST(ReportMinPeriod, CleanDesignReportsClockRow) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const CommandResult report = shell.Run("report_min_period", {});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scene=func_tt checks=1 violating=0 "
                                "worst_slack=9.4 report="
                                "min_period_report_func_tt"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt clock=clk period=10 "
                                "min_period=0.6 slack=9.4 "
                                "worst_pin=r2/D"),
            std::string::npos);
  EXPECT_NE(report.payload.find("Minimum period (scene func_tt):"),
            std::string::npos);
  EXPECT_NE(shell.ctx.reports.FindByName(shell.ctx.project,
                                         "min_period_report_func_tt"),
            nullptr);
}

TEST(ReportMinPeriod, VerboseListsPinChecks) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const CommandResult report =
      shell.Run("report_min_period", {"-verbose"});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scene=func_tt pin=r2/D clock=clk "
                                "period=10 min_period=0.6 slack=9.4"),
            std::string::npos);
}

TEST(ReportMinPeriod, TightClockReportsViolation) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdcTight);
  const CommandResult report = shell.Run("report_min_period", {});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scene=func_tt clock=clk period=0.5 "
                                "min_period=0.6 slack=-0.1 "
                                "worst_pin=r2/D"),
            std::string::npos);
}

TEST(ReportMinPeriod, RejectsBadOptions) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  EXPECT_FALSE(shell.Run("report_min_period", {"-bogus"}).ok);
  EXPECT_FALSE(shell.Run("report_min_period", {"top"}).ok);
  EXPECT_FALSE(shell.Run("report_min_period", {"-scene", "func_xx"}).ok);
}

TEST(ReportMinPulseWidth, ReportsWidths) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty,
                              kTimingSdcMinPulseWidth);
  const CommandResult report = shell.Run("report_min_pulse_width", {});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scene=func_tt checks=4 violating=0 "
                                "worst_slack=4 unevaluated=0 report="
                                "min_pulse_width_report_func_tt"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt pin=r1/CK "
                                "transition=rise width=5 min_width=1 "
                                "slack=4"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt pin=r2/CK "
                                "transition=fall width=5 min_width=1 "
                                "slack=4"),
            std::string::npos);
  EXPECT_NE(report.payload.find("Minimum pulse width (scene func_tt):"),
            std::string::npos);
  EXPECT_NE(shell.ctx.reports.FindByName(
                shell.ctx.project, "min_pulse_width_report_func_tt"),
            nullptr);
}

TEST(ReportMinPulseWidth, ViolatorsOnlyEmptyOnClean) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty,
                              kTimingSdcMinPulseWidth);
  const CommandResult report =
      shell.Run("report_min_pulse_width", {"-violators"});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("(no checks pass the selection)"),
            std::string::npos);
  EXPECT_EQ(report.payload.find("pin=r1/CK"), std::string::npos);
}

TEST(ReportMinPulseWidth, NoConstraintReportsEmpty) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const CommandResult report = shell.Run("report_min_pulse_width", {});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scene=func_tt checks=0 violating=0 "
                                "worst_slack=- unevaluated=0"),
            std::string::npos);
  EXPECT_NE(report.payload.find("(no checks pass the selection)"),
            std::string::npos);
}

TEST(ReportMinPulseWidth, RejectsBadOptions) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty,
                              kTimingSdcMinPulseWidth);
  EXPECT_FALSE(shell.Run("report_min_pulse_width", {"-bogus"}).ok);
  EXPECT_FALSE(shell.Run("report_min_pulse_width", {"top"}).ok);
  EXPECT_FALSE(
      shell.Run("report_min_pulse_width", {"-scene", "func_xx"}).ok);
}

TEST(ReportClockGatingCheck, UntestedEnableReportsReason) {
  TimingTempDir dir;
  Shell shell;
  BuildGatedCheckDesign(shell, dir, kTimingVerilogGated);
  const CommandResult report =
      shell.Run("report_clock_gating_check", {});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scene=func_tt checks=1 tested=0 "
                                "violating=0 report="
                                "clock_gating_check_report_func_tt"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt enable=g1/B clock=g1/A "
                                "setup_slack=- hold_slack=- "
                                "status=untested: no_startpoint_clock"),
            std::string::npos);
  EXPECT_NE(report.payload.find("Clock gating checks (scene func_tt):"),
            std::string::npos);
  EXPECT_NE(shell.ctx.reports.FindByName(
                shell.ctx.project,
                "clock_gating_check_report_func_tt"),
            nullptr);
}

TEST(ReportClockGatingCheck, TestedEnableReportsNoMeasuredSlack) {
  // The enable is driven by a flop clocked by clk, so the
  // coverage marks the check tested. But the engine produces
  // no gated-clock path end for this session (verified with
  // and without an explicit set_clock_gating_check in the
  // SDC), so there is no measured slack to report: the report
  // lists the check as tested with unmeasured slacks, never
  // as passing. If the engine ever produces a gated-clock
  // end, the slack columns will carry its values.
  TimingTempDir dir;
  Shell shell;
  BuildGatedCheckDesign(shell, dir, kTimingVerilogGatedTested);
  const CommandResult report =
      shell.Run("report_clock_gating_check", {});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scene=func_tt checks=1 tested=1 "
                                "violating=0"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=func_tt enable=g1/B clock=g1/A "
                                "setup_slack=- hold_slack=- "
                                "status=tested"),
            std::string::npos);
  EXPECT_NE(report.payload.find("no measured slack"),
            std::string::npos);
}

TEST(ReportClockGatingCheck, NoGateReportsEmpty) {
  TimingTempDir dir;
  Shell shell;
  BuildReportConstraintDesign(shell, dir, kTimingLiberty, kTimingSdc);
  const CommandResult report =
      shell.Run("report_clock_gating_check", {});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scene=func_tt checks=0 tested=0 "
                                "violating=0"),
            std::string::npos);
  EXPECT_NE(report.payload.find("No clock-gating checks defined: no "
                                "gated-clock enable in the design."),
            std::string::npos);
}

TEST(ReportClockGatingCheck, RejectsBadOptions) {
  TimingTempDir dir;
  Shell shell;
  BuildGatedCheckDesign(shell, dir, kTimingVerilogGated);
  EXPECT_FALSE(shell.Run("report_clock_gating_check", {"-bogus"}).ok);
  EXPECT_FALSE(shell.Run("report_clock_gating_check", {"top"}).ok);
  EXPECT_FALSE(
      shell.Run("report_clock_gating_check", {"-scene", "func_xx"}).ok);
}

// The timing_summary payload on the optloop acceptance chain:
// mapped, loop winner classic at worst slack +0.05 ns, run_timing
// on the optimized netlist. The payload carries the scene summary
// from the last run, the worst-violator rows, worst paths across
// endpoints, the coverage summary, and the QoR numbers, and
// registers no report binding of its own. The clock table is
// empty in the read_sdc-only flow (see below); the row rendering
// itself is covered by ShowsSessionClockRow.
TEST(TimingSummary, ReportsOptloopAcceptanceNumbers) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(SynthOptFlowToMapped(shell, dir, kSynthOptSdcClear));
  const CommandResult loop =
      shell.Run("synth_opt", {"-strategies", "classic"});
  ASSERT_TRUE(loop.ok) << loop.message;
  const CommandResult run = shell.Run("run_timing");
  ASSERT_TRUE(run.ok) << run.message;
  const std::size_t bindings_before =
      shell.ctx.reports.bindings(shell.ctx.project).size();
  const CommandResult summary = shell.Run("timing_summary");
  ASSERT_TRUE(summary.ok) << summary.message;
  // A query, not a report: no binding is registered.
  EXPECT_EQ(shell.ctx.reports.bindings(shell.ctx.project).size(),
            bindings_before);
  EXPECT_NE(summary.payload.find("source=verilog scenes=1"),
            std::string::npos);
  EXPECT_NE(summary.payload.find("scene=func_tt mode=func corner=tt "
                                 "parasitics=estimate"),
            std::string::npos);
  EXPECT_NEAR(SynthPayloadNumber(summary, "worst_slack_ns"), 0.05,
              1e-4);
  EXPECT_NEAR(SynthPayloadNumber(summary, "total_negative_slack_ns"),
              0.0, 1e-4);
  // No clock row in the read_sdc-only flow: read_sdc records
  // the SDC path only, so the clock the file defines never lands
  // in the session constraint set that report_clock (and this
  // query) reads. That is the documented report_clock boundary,
  // not a gap in this query. The row rendering itself is covered
  // by ShowsSessionClockRow below.
  EXPECT_EQ(summary.payload.find("clock name="), std::string::npos);
  // No violations anywhere, so the timing classes name no
  // violator and cost zero, and the DRC classes carry no limit.
  EXPECT_NE(summary.payload.find("scene=func_tt class=max_delay "
                                 "violator=- slack=0.05 cost=0 weight=1 "
                                 "violations=0"),
            std::string::npos);
  EXPECT_NE(summary.payload.find("scene=func_tt class=min_delay "
                                 "violator=-"),
            std::string::npos);
  EXPECT_NE(summary.payload.find("scene=func_tt class=max_transition "
                                 "violator=- limit=none weight=1 "
                                 "violations=0"),
            std::string::npos);
  EXPECT_NE(summary.payload.find("scene=func_tt class=max_capacitance "
                                 "violator=- limit=none weight=1 "
                                 "violations=0"),
            std::string::npos);
  EXPECT_NE(summary.payload.find("scene=func_tt class=max_fanout "
                                 "violator=- limit=none weight=1 "
                                 "violations=0"),
            std::string::npos);
  // Worst paths across endpoints, worst first: both outputs
  // arrive at 1.2 ns against the 1.25 ns clock.
  EXPECT_NE(summary.payload.find(
                "path scene=func_tt check=setup endpoint=y1"),
            std::string::npos);
  EXPECT_NE(summary.payload.find(
                "path scene=func_tt check=setup endpoint=y2"),
            std::string::npos);
  // Coverage totals: every defined check is counted, tested
  // plus untested.
  const std::size_t coverage_at =
      summary.payload.find("coverage scene=func_tt checks=");
  ASSERT_NE(coverage_at, std::string::npos);
  const std::size_t coverage_end =
      summary.payload.find('\n', coverage_at);
  const std::string coverage_line = summary.payload.substr(
      coverage_at,
      coverage_end == std::string::npos
          ? std::string::npos
          : coverage_end - coverage_at);
  const auto coverage_number = [&coverage_line](const char* key) {
    const std::size_t at = coverage_line.find(key);
    EXPECT_NE(at, std::string::npos) << "missing " << key;
    if (at == std::string::npos) return 0u;
    return static_cast<unsigned>(
        std::stoul(coverage_line.substr(at + std::strlen(key))));
  };
  const unsigned checks = coverage_number("checks=");
  const unsigned tested = coverage_number("tested=");
  const unsigned untested = coverage_number("untested=");
  EXPECT_GT(checks, 0u);
  EXPECT_EQ(tested + untested, checks);
  // QoR: the classic winner's four AND2 cells, area 8, and the
  // vclk setup group at WNS +0.05 ns with no violations.
  EXPECT_NE(summary.payload.find("qor scene=func_tt"), std::string::npos);
  EXPECT_NE(summary.payload.find("cells=4 area=8"), std::string::npos);
  EXPECT_NE(summary.payload.find("compile_cpu_s="), std::string::npos);
  EXPECT_EQ(summary.payload.find("compile_cpu_s=n/a"),
            std::string::npos);
  EXPECT_NE(summary.payload.find("qor scene=func_tt group=vclk "
                                 "check=setup wns=0.05 tns=0 "
                                 "violating_paths=0"),
            std::string::npos);
  EXPECT_NE(summary.payload.find("qor scene=func_tt cell=AND2 count=4 "
                                 "area=8"),
            std::string::npos);
}

// The clock row rendering, on a session that has both timing
// results and a session-defined clock. The interactive
// create_clock is a disjoint system from the SDC file
// (docs/read-sdc-audit.md section 5): it needs an open design,
// which the synth flow never opens, so the design is opened and
// the clock defined on its port BEFORE the flow runs, because
// create_design resets the timing session. Defining the clock
// does not disturb the STA run, it only adds the row to the
// session constraint set, and the query then renders it at
// 1.25 ns with the default waveform {0 0.625} and no uncertainty.
TEST(TimingSummary, ShowsSessionClockRow) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  const CommandResult clock = shell.Run(
      "create_clock", {"-period", "1.25", "-name", "vclk", "clk"});
  ASSERT_TRUE(clock.ok) << clock.message;
  ASSERT_TRUE(SynthOptFlowToMapped(shell, dir, kSynthOptSdcClear));
  const CommandResult loop =
      shell.Run("synth_opt", {"-strategies", "classic"});
  ASSERT_TRUE(loop.ok) << loop.message;
  const CommandResult run = shell.Run("run_timing");
  ASSERT_TRUE(run.ok) << run.message;
  const std::size_t bindings_before =
      shell.ctx.reports.bindings(shell.ctx.project).size();
  const CommandResult summary = shell.Run("timing_summary");
  ASSERT_TRUE(summary.ok) << summary.message;
  EXPECT_EQ(shell.ctx.reports.bindings(shell.ctx.project).size(),
            bindings_before);
  EXPECT_NE(summary.payload.find(
                "clock name=vclk generated=0 period=1.25 rise=0 "
                "fall=0.625 attrs= skew=none sources="),
            std::string::npos);
  // The timing rows are untouched by the session clock.
  EXPECT_NEAR(SynthPayloadNumber(summary, "worst_slack_ns"), 0.05,
              1e-4);
}

TEST(TimingSummary, RequiresTimingRun) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(SynthOptFlowToMapped(shell, dir, kSynthOptSdcClear));
  const CommandResult summary = shell.Run("timing_summary");
  ASSERT_FALSE(summary.ok);
  EXPECT_NE(summary.message.find("no timing results"),
            std::string::npos);
}

TEST(TimingSummary, RejectsBadOptions) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(SynthOptFlowToMapped(shell, dir, kSynthOptSdcClear));
  ASSERT_TRUE(
      shell.Run("synth_opt", {"-strategies", "classic"}).ok);
  ASSERT_TRUE(shell.Run("run_timing").ok);
  EXPECT_FALSE(shell.Run("timing_summary", {"-max_paths", "0"}).ok);
  EXPECT_FALSE(shell.Run("timing_summary", {"-max_paths", "x"}).ok);
  EXPECT_FALSE(shell.Run("timing_summary", {"-scene", "func_xx"}).ok);
  EXPECT_FALSE(shell.Run("timing_summary", {"top"}).ok);
  const CommandResult one =
      shell.Run("timing_summary", {"-scene", "func_tt"});
  ASSERT_TRUE(one.ok) << one.message;
  EXPECT_NE(one.payload.find("source=verilog scenes=1"),
            std::string::npos);
  EXPECT_EQ(one.payload.find("scene=func_xx"), std::string::npos);
}

TEST(CoreCommands, HelpListsAndDescribesCommands) {
  Shell shell;
  const CommandResult all = shell.Run("help", {});
  ASSERT_TRUE(all.ok) << all.message;
  EXPECT_NE(all.payload.find("report_timing"), std::string::npos);
  EXPECT_NE(all.payload.find("-columns"), std::string::npos);
  const CommandResult one = shell.Run("help", {"report_timing"});
  ASSERT_TRUE(one.ok) << one.message;
  EXPECT_NE(one.payload.find("report_timing [-delay_type"),
            std::string::npos);
  // The ::eos:: prefix is accepted and stripped.
  const CommandResult prefixed =
      shell.Run("help", {"eos::report_timing"});
  ASSERT_TRUE(prefixed.ok) << prefixed.message;
  EXPECT_EQ(prefixed.payload, one.payload);
  const CommandResult unknown = shell.Run("help", {"bogus"});
  EXPECT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.diag, Diag::kInvalidCommand);
  EXPECT_FALSE(shell.Run("help", {"a", "b"}).ok);
}

TEST(CoreCommands, HelpWildcardPatterns) {
  Shell shell;
  const auto lines = [](const std::string& payload) {
    return static_cast<std::size_t>(
               std::count(payload.begin(), payload.end(), '\n')) +
           1;
  };
  // "*tim*" matches every command whose name contains "tim", in
  // registry order, one "name usage" line each.
  const CommandResult tim = shell.Run("help", {"*tim*"});
  ASSERT_TRUE(tim.ok) << tim.message;
  EXPECT_EQ(lines(tim.payload), 9u);
  for (const char* name :
       {"check_timing", "close_timing", "report_timing", "run_timing",
        "set_max_time_borrow", "repair_timing",
        "report_global_timing", "timing_summary", "get_timing_paths"}) {
    EXPECT_NE(tim.payload.find(name), std::string::npos) << name;
  }
  EXPECT_EQ(tim.payload.find("create_clock"), std::string::npos);
  // A trailing star only: names ending in "_timing" lose
  // set_max_time_borrow, which ends in "borrow".
  const CommandResult suffix = shell.Run("help", {"*_timing"});
  ASSERT_TRUE(suffix.ok) << suffix.message;
  EXPECT_EQ(lines(suffix.payload), 6u);
  EXPECT_EQ(suffix.payload.find("set_max_time_borrow"),
            std::string::npos);
  EXPECT_NE(suffix.payload.find("repair_timing"), std::string::npos);
  EXPECT_NE(suffix.payload.find("check_timing"), std::string::npos);
  EXPECT_NE(suffix.payload.find("report_global_timing"),
            std::string::npos);
  // "?" matches exactly one character.
  const CommandResult one_char = shell.Run("help", {"?eport_timing"});
  ASSERT_TRUE(one_char.ok) << one_char.message;
  EXPECT_EQ(lines(one_char.payload), 1u);
  EXPECT_NE(one_char.payload.find("report_timing [-delay_type"),
            std::string::npos);
  // "*" alone lists everything, same as no argument.
  const CommandResult all = shell.Run("help", {});
  const CommandResult star = shell.Run("help", {"*"});
  ASSERT_TRUE(star.ok) << star.message;
  EXPECT_EQ(star.payload, all.payload);
  // The "eos::" prefix is stripped before matching.
  const CommandResult prefixed = shell.Run("help", {"eos::*tim*"});
  ASSERT_TRUE(prefixed.ok) << prefixed.message;
  EXPECT_EQ(prefixed.payload, tim.payload);
  // No match fails with the no-match message.
  const CommandResult none = shell.Run("help", {"bogus*"});
  EXPECT_FALSE(none.ok);
  EXPECT_EQ(none.diag, Diag::kInvalidCommand);
  EXPECT_NE(none.message.find("no commands match: bogus*"),
            std::string::npos);
  // A pattern without a leading star is a prefix glob: "tim*"
  // matches only timing_summary, the one command starting with
  // "tim".
  const CommandResult leading = shell.Run("help", {"tim*"});
  ASSERT_TRUE(leading.ok) << leading.message;
  EXPECT_EQ(lines(leading.payload), 1u);
  EXPECT_NE(leading.payload.find("timing_summary"),
            std::string::npos);
  // "floorplan*" matches the floorplan command and its query,
  // floorplan_summary, and nothing else.
  const CommandResult floorplan = shell.Run("help", {"floorplan*"});
  ASSERT_TRUE(floorplan.ok) << floorplan.message;
  EXPECT_EQ(lines(floorplan.payload), 2u);
  EXPECT_NE(floorplan.payload.find("floorplan [-die_area"),
            std::string::npos);
  EXPECT_NE(floorplan.payload.find("floorplan_summary"),
            std::string::npos);
}

TEST(CoreCommands, ReportTimingHelpFlags) {
  Shell shell;
  for (const char* flag : {"-help", "--help", "-h"}) {
    const CommandResult help = shell.Run("report_timing", {flag});
    ASSERT_TRUE(help.ok) << help.message;
    EXPECT_NE(help.payload.find("-columns"), std::string::npos);
    EXPECT_NE(help.payload.find("-path_type"), std::string::npos);
  }
  // A help flag wins over other arguments without running anything.
  const CommandResult mixed =
      shell.Run("report_timing", {"-delay_type", "sideways", "-help"});
  ASSERT_TRUE(mixed.ok) << mixed.message;
}

TEST(CoreCommands, CloseTimingClosesTheImportedDesign) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kClosureLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kClosureDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());

  Shell shell;
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kClosureLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kClosureSdc)}).ok);
  const CommandResult closed = shell.Run("close_timing", {"5"});
  ASSERT_TRUE(closed.ok) << closed.message;
  EXPECT_EQ(closed.diag, Diag::kVerifiedPass);
  EXPECT_NE(closed.payload.find("closed=1"), std::string::npos);
  EXPECT_NE(closed.payload.find("iterations=1"), std::string::npos);
  EXPECT_NE(closed.payload.find("report=timing_closure"),
            std::string::npos);
  const auto payload_number = [&closed](const std::string& key) {
    const std::size_t at = closed.payload.find(key + "=");
    EXPECT_NE(at, std::string::npos) << "missing " << key;
    if (at == std::string::npos) return 0.0;
    return std::stod(closed.payload.substr(at + key.size() + 1));
  };
  EXPECT_NEAR(payload_number("initial_worst_slack_ns"), -0.1, 1e-6);
  EXPECT_NEAR(payload_number("worst_slack_ns"), 0.5, 1e-6);
  // The accepted iteration is a ledger transaction and the loop's
  // report binding is registered at the final revision.
  EXPECT_EQ(shell.ctx.project.ledger.current_revision(), 1u);
  EXPECT_NE(shell.ctx.reports.FindByName(shell.ctx.project,
                                         "timing_closure"),
            nullptr);
  ASSERT_TRUE(shell.ctx.timing.odb.has_value());
  db::OdbInstanceInfo info;
  ASSERT_TRUE(shell.ctx.timing.odb->FindInstance("u1", &info));
  EXPECT_EQ(info.master, "BUF_X2");
}

TEST(CoreCommands, CloseTimingDiagnostics) {
  Shell shell;
  // A malformed bound is a configuration failure, before any
  // design question.
  const CommandResult bad_bound = shell.Run("close_timing", {"zero"});
  ASSERT_FALSE(bad_bound.ok);
  EXPECT_EQ(bad_bound.diag, Diag::kInvalidConfiguration);
  const CommandResult zero_bound = shell.Run("close_timing", {"0"});
  ASSERT_FALSE(zero_bound.ok);
  EXPECT_EQ(zero_bound.diag, Diag::kInvalidConfiguration);
  // A well-formed bound but no ODB design: closure edits the
  // physical database, so this is the unclosed-design diagnostic.
  const CommandResult no_design = shell.Run("close_timing", {"3"});
  ASSERT_FALSE(no_design.ok);
  EXPECT_EQ(no_design.diag, Diag::kUnclosedDesign);
}

// P1: eos::floorplan runs on the imported design, reports rows,
// tracks and placed IO, and fails fast on bad options and on
// missing session state. The DEF pins carry PORT rectangles:
// odb creates a placed BPin only from port geometry, so the
// shared kTimingDef pins (placement without a PORT) would report
// io_placed=0. This local DEF is kTimingDef with ports added;
// nothing else in the shared fixture is changed.
const char kFloorplanDef[] = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 2 ;
- u1 BUFX2 + PLACED ( 1000 1000 ) N ;
- u2 BUFX2 + PLACED ( 2000 1000 ) N ;
END COMPONENTS
PINS 2 ;
- in1 + NET n_in + DIRECTION INPUT + USE SIGNAL
  + PORT
  + LAYER metal1 ( -50 -50 ) ( 50 50 )
  + PLACED ( 0 5000 ) N ;
- out1 + NET n_out + DIRECTION OUTPUT + USE SIGNAL
  + PORT
  + LAYER metal1 ( -50 -50 ) ( 50 50 )
  + PLACED ( 10000 5000 ) N ;
END PINS
NETS 3 ;
- n_in ( PIN in1 ) ( u1 A ) ;
- n_mid ( u1 Y ) ( u2 A ) ;
- n_out ( u2 Y ) ( PIN out1 ) ;
END NETS
END DESIGN
)DEF";

TEST(CoreCommands, FloorplanRunsOnImportedDesign) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kFloorplanDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());

  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  const CommandResult result = shell.Run(
      "floorplan",
      {"-die_area", "0", "0", "10000", "10000", "-core_area", "0", "0",
       "10000", "10000", "-site", "core"});
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_NE(result.payload.find("die_area=0,0,10000,10000"),
            std::string::npos);
  EXPECT_NE(result.payload.find("core_area=0,0,10000,10000"),
            std::string::npos);
  EXPECT_NE(result.payload.find("io_placed=2"), std::string::npos);
  EXPECT_NE(result.payload.find("io_total=2"), std::string::npos);
  // Rows exist and the default run (no areas, no site) works too.
  EXPECT_NE(result.payload.find("rows="), std::string::npos);
  const CommandResult again = shell.Run("floorplan", {});
  ASSERT_TRUE(again.ok) << again.message;
}

TEST(CoreCommands, FloorplanFailsFastWithoutTechnology) {
  Shell shell;
  const CommandResult result = shell.Run("floorplan", {});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(result.message.find("no technology data"), std::string::npos);
}

TEST(CoreCommands, FloorplanFailsFastWithoutMappedDesign) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  Shell shell;
  shell.ctx.timing.odb = std::move(database);
  const CommandResult result = shell.Run("floorplan", {});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kUnclosedDesign);
  EXPECT_NE(result.message.find("no mapped design"), std::string::npos);
}

// F3: the floorplan_summary query over the imported-design
// floorplan: the header carries the die and core rectangles
// and the counts, one "row" line per row, one "pin" line per
// IO pin, and the counts match the lines. The track-grid count
// matches the floorplan payload's own count, so the query and
// the engine agree on the source.
TEST(CoreCommands, FloorplanSummaryReadsImportedFloorplan) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kFloorplanDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());

  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  const CommandResult floorplan = shell.Run(
      "floorplan",
      {"-die_area", "0", "0", "10000", "10000", "-core_area", "0", "0",
       "10000", "10000", "-site", "core"});
  ASSERT_TRUE(floorplan.ok) << floorplan.message;
  const CommandResult summary = shell.Run("floorplan_summary", {});
  ASSERT_TRUE(summary.ok) << summary.message;
  // The die is the requested die. The core is the extent the rows
  // cover, not the requested core: 21 sites of 460 DBU wide and
  // 3 rows of 2720 DBU tall, so 9660 by 8160. The floorplan
  // payload's core_area is the requested core instead.
  EXPECT_NE(summary.payload.find(
                "floorplan die=0,0,10000,10000 core=0,0,9660,8160"),
            std::string::npos);
  EXPECT_NE(summary.payload.find("io_placed=2"), std::string::npos);
  EXPECT_NE(summary.payload.find("io_total=2"), std::string::npos);
  // Rows: the site is 2.72 microns tall in a 10000 DBU core,
  // so three rows fit.
  EXPECT_NE(summary.payload.find("rows=3"), std::string::npos);
  const auto count_lines = [](const std::string& payload,
                              const std::string& prefix) {
    std::size_t count = 0;
    std::size_t at = 0;
    while ((at = payload.find(prefix, at)) != std::string::npos) {
      ++count;
      at += prefix.size();
    }
    return count;
  };
  EXPECT_EQ(count_lines(summary.payload, "\nrow rect="), 3u);
  EXPECT_EQ(count_lines(summary.payload, "\npin name="), 2u);
  // The two pins are the DEF's IO, each with its placed pin
  // rectangle (the 100 by 100 PORT rect around its PLACED
  // point, which straddles the die edge).
  EXPECT_NE(summary.payload.find(
                "pin name=in1 dir=in placed=1 rect=-50,4950,50,5050"),
            std::string::npos);
  EXPECT_NE(summary.payload.find(
                "pin name=out1 dir=out placed=1 rect=9950,4950,10050,5050"),
            std::string::npos);
  const auto track_grids = [](const std::string& payload) {
    const std::size_t at = payload.find("track_grids=");
    if (at == std::string::npos) return std::string();
    const std::size_t start = at + std::string("track_grids=").size();
    const std::size_t end = payload.find(' ', start);
    return payload.substr(start, end - start);
  };
  ASSERT_FALSE(track_grids(floorplan.payload).empty());
  EXPECT_NE(summary.payload.find("track_grids=" +
                                 track_grids(floorplan.payload)),
            std::string::npos);
}

// F3: floorplan_summary fails fast before any floorplan runs,
// with the same preconditions as floorplan itself.
TEST(CoreCommands, FloorplanSummaryFailsFastWithoutFloorplan) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kFloorplanDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());

  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  const CommandResult result = shell.Run("floorplan_summary", {});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(result.message.find("no floorplan"), std::string::npos);
}

TEST(CoreCommands, FloorplanSummaryFailsFastWithoutTechnology) {
  Shell shell;
  const CommandResult result = shell.Run("floorplan_summary", {});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(result.message.find("no technology data"), std::string::npos);
}

TEST(CoreCommands, FloorplanSummaryFailsFastWithoutMappedDesign) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  Shell shell;
  shell.ctx.timing.odb = std::move(database);
  const CommandResult result = shell.Run("floorplan_summary", {});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kUnclosedDesign);
  EXPECT_NE(result.message.find("no mapped design"), std::string::npos);
}

TEST(CoreCommands, FloorplanSummaryRejectsBadOptions) {
  Shell shell;
  const CommandResult result =
      shell.Run("floorplan_summary", {"-bogus"});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(result.message.find("floorplan_summary"), std::string::npos);
}

// F13: the signoff_summary query fixtures. Minimal PV and LVS
// evidence documents in the docs/gcd-pv-results.md and
// docs/gcd-lvs-magic.md shapes: same headings, result
// sentences, discrepancy paragraphs, and summary tables, with
// two DRC families instead of fourteen.
const char kSignoffPv[] =
    "# PV results (test fixture)\n"
    "\n"
    "## 2. DRC\n"
    "\n"
    "Result: the report database holds 3 rule categories and 0 "
    "violations.\n"
    "Coverage by rule family (categories per family): metal layers "
    "m1 2, m2 1; every remaining family is a single category.\n"
    "\n"
    "## 3. LVS\n"
    "\n"
    "### 3.4 Results\n"
    "\n"
    "68 cell classes compared. Every stdcell class gcd uses is "
    "topologically equivalent to its CDL definition except "
    "`sky130_fd_sc_hd__a21oi_2` (Section 3.5).\n"
    "\n"
    "### 3.5 The named discrepancies\n"
    "\n"
    "D1. Substrate net model (extraction artifact, named, not a "
    "layout error): the runset forces the substrate to a global "
    "net.\n"
    "\n"
    "D2. `sky130_fd_sc_hd__a21oi_2` (a real extraction-level "
    "difference, cause not fully resolved): inside the extracted "
    "cell the VGND net appears as two nets.\n"
    "\n"
    "### 3.6 LVS verdict\n"
    "\n"
    "LVS does not pass clean on gcd at this pin.\n"
    "\n"
    "## 4. Antenna\n"
    "\n"
    "Result: 0 net violations, 0 pin violations.\n"
    "\n"
    "## 5. Summary and tier position\n"
    "\n"
    "| Check | Verdict | Evidence |\n"
    "|---|---|---|\n"
    "| DRC (KLayout deck, 3 categories) | PASS, 0 violations | "
    "Section 2 |\n"
    "| Antenna (final geometry) | PASS, 0 net and 0 pin violations | "
    "Section 4 |\n"
    "| LVS (KLayout extraction + Netgen) | NOT CLEAN | Sections 3.4 "
    "to 3.6 |\n";

const char kSignoffLvs[] =
    "# LVS by the Magic extraction path (test fixture)\n"
    "\n"
    "## 4. Results\n"
    "\n"
    "Netgen compared 68 standard cell classes and the gcd top "
    "level. 67 of the 68 classes are topologically equivalent "
    "to their CDL definitions, with cell pin lists equivalent "
    "in every class. The single exception is "
    "`sky130_fd_sc_hd__a21oi_2` (Section 5).\n"
    "\n"
    "Top level: the flat totals agree exactly: 3,676 fingers "
    "extracted against 3,676 in the reference.\n"
    "\n"
    "## 5. D2 resolved at the cell level, and the successor residual\n"
    "\n"
    "### 5.2 Decision on D2\n"
    "\n"
    "D2 as recorded in unit 3.1 does not exist in this cell's "
    "geometry under the Magic extraction model. By the judging "
    "rule of Section 2, D2 is decided a modeling artifact of the "
    "KLayout extraction path.\n"
    "\n"
    "## 6. D1, and the LVS verdict for this path\n"
    "\n"
    "D1 is closed under this extraction model.\n"
    "\n"
    "LVS verdict for the Magic extraction path, as measured: "
    "NOT CLEAN, with one named residual. 67 of 68 standard "
    "cell classes are equivalent, and there are no property "
    "errors. The single residual is the a21oi_2 series midpoint "
    "representation of Section 5.3 (4 transistors in 2 instances, "
    "cause proven by controlled diagnostic).\n"
    "\n"
    "## 7. Summary\n"
    "\n"
    "| Item | Unit 3.1 (KLayout path) | Unit F3 (Magic path) |\n"
    "|---|---|---|\n"
    "| Cell classes equivalent | 67 of 68 (a21oi_2 excepted) | 67 of "
    "68 (a21oi_2 excepted) |\n"
    "| Flat devices (extracted vs reference) | 2,941 vs 2,937 | 3,676 "
    "vs 3,676 fingers |\n"
    "| D1 substrate split | Present (extraction artifact) | Closed "
    "(no split) |\n"
    "| D2 | VGND split in a21oi_2, cause unresolved | VGND split "
    "absent; decided KLayout modeling artifact |\n"
    "| Remaining residual | D1 and D2 | a21oi_2 series midpoint "
    "representation, cause proven |\n"
    "| Verdict | NOT CLEAN | NOT CLEAN, one named residual |\n";

// F13: signoff_summary parses the two reports into the
// machine-readable payload: the header verdicts and counts,
// one DRC row per family, one LVS row per path, one row per
// named discrepancy, and the successor residual. The first run
// baselines, so it is not stale.
TEST(CoreCommands, SignoffSummaryParsesReports) {
  TimingTempDir dir;
  const std::string pv = dir.Write("pv.md", kSignoffPv);
  const std::string lvs = dir.Write("lvs.md", kSignoffLvs);
  Shell shell;
  const CommandResult result =
      shell.Run("signoff_summary", {"-pv", pv, "-lvs", lvs});
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_NE(result.payload.find("signoff drc=pass lvs=fail "
                                "antenna=pass stale=0"),
            std::string::npos);
  EXPECT_NE(result.payload.find("drc_categories=3 drc_violations=0"),
            std::string::npos);
  EXPECT_NE(result.payload.find("antenna_net_violations=0 "
                                "antenna_pin_violations=0"),
            std::string::npos);
  EXPECT_NE(result.payload.find("pv_report=" + pv), std::string::npos);
  EXPECT_NE(result.payload.find("lvs_report=" + lvs),
            std::string::npos);
  EXPECT_NE(result.payload.find("fingerprint="), std::string::npos);
  EXPECT_NE(result.payload.find("drc family=m1 categories=2 "
                                "violations=0"),
            std::string::npos);
  EXPECT_NE(result.payload.find("drc family=m2 categories=1 "
                                "violations=0"),
            std::string::npos);
  EXPECT_NE(result.payload.find("lvs path=klayout classes_compared=68 "
                                "classes_equivalent=67 verdict=fail"),
            std::string::npos);
  EXPECT_NE(result.payload.find("lvs path=magic classes_compared=68 "
                                "classes_equivalent=67 "
                                "fingers_extracted=3676 "
                                "fingers_reference=3676 verdict=fail"),
            std::string::npos);
  EXPECT_NE(result.payload.find("lvs issue=D1 cell=none "
                                "kind=extraction_artifact path=klayout"),
            std::string::npos);
  EXPECT_NE(result.payload.find("lvs issue=D2 "
                                "cell=sky130_fd_sc_hd__a21oi_2 "
                                "kind=unresolved path=klayout"),
            std::string::npos);
  EXPECT_NE(result.payload.find("lvs issue=D1 cell=none kind=closed "
                                "path=magic"),
            std::string::npos);
  EXPECT_NE(result.payload.find("lvs issue=D2 "
                                "cell=sky130_fd_sc_hd__a21oi_2 "
                                "kind=resolved_artifact path=magic"),
            std::string::npos);
  EXPECT_NE(result.payload.find("lvs_residual "
                                "cell=sky130_fd_sc_hd__a21oi_2 "
                                "transistors=4 instances=2 cause=proven "
                                "path=magic"),
            std::string::npos);
}

// F13: a missing report fails fast with the path named, never
// with a silent empty panel.
TEST(CoreCommands, SignoffSummaryFailsFastOnMissingReport) {
  TimingTempDir dir;
  const std::string lvs = dir.Write("lvs.md", kSignoffLvs);
  const std::string missing = dir.Path("no-such-pv.md");
  Shell shell;
  const CommandResult result = shell.Run(
      "signoff_summary", {"-pv", missing, "-lvs", lvs});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kIoError);
  EXPECT_NE(result.message.find(missing), std::string::npos);
}

// F13: the default report paths are the gcd evidence documents
// relative to the working directory. Under ctest the working
// directory is the build tree, where they do not exist, so the
// failure names the default PV path. From the repository root
// they resolve, and the run parses the recorded gcd verdicts.
TEST(CoreCommands, SignoffSummaryDefaultPathsAreGcdEvidence) {
  Shell shell;
  const bool docs_present = std::ifstream("docs/gcd-pv-results.md") &&
                            std::ifstream("docs/gcd-lvs-magic.md");
  const CommandResult result = shell.Run("signoff_summary", {});
  if (!docs_present) {
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(result.diag, Diag::kIoError);
    EXPECT_NE(result.message.find("docs/gcd-pv-results.md"),
              std::string::npos);
    return;
  }
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_NE(result.payload.find("drc_categories=145"),
            std::string::npos);
}

// F13: an unparseable report fails fast with the path named.
TEST(CoreCommands, SignoffSummaryFailsFastOnUnparseableReport) {
  TimingTempDir dir;
  const std::string pv = dir.Write("pv.md", "# not a PV report\n");
  const std::string lvs = dir.Write("lvs.md", kSignoffLvs);
  Shell shell;
  const CommandResult result =
      shell.Run("signoff_summary", {"-pv", pv, "-lvs", lvs});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(result.message.find(pv), std::string::npos);
}

TEST(CoreCommands, SignoffSummaryRejectsBadOptions) {
  TimingTempDir dir;
  const std::string pv = dir.Write("pv.md", kSignoffPv);
  const std::string lvs = dir.Write("lvs.md", kSignoffLvs);
  Shell shell;
  const CommandResult bogus =
      shell.Run("signoff_summary", {"-bogus"});
  ASSERT_FALSE(bogus.ok);
  EXPECT_EQ(bogus.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(bogus.message.find("signoff_summary"), std::string::npos);
  const CommandResult positional = shell.Run(
      "signoff_summary", {"-pv", pv, "-lvs", lvs, "extra"});
  ASSERT_FALSE(positional.ok);
  EXPECT_EQ(positional.diag, Diag::kInvalidConfiguration);
  const CommandResult twice = shell.Run(
      "signoff_summary",
      {"-pv", pv, "-pv", pv, "-lvs", lvs});
  ASSERT_FALSE(twice.ok);
  EXPECT_EQ(twice.diag, Diag::kInvalidConfiguration);
  const CommandResult spaced = shell.Run(
      "signoff_summary",
      {"-pv", "/tmp/has a space.md", "-lvs", lvs});
  ASSERT_FALSE(spaced.ok);
  EXPECT_EQ(spaced.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(spaced.message.find("space"), std::string::npos);
}

// F13: the stale flag. The first run baselines with no design,
// so it is not stale. Loading a design adopts the baseline
// instead of flagging it stale. Changing the design after that
// is stale until the reports are re-run: rewriting a report
// (any byte change, even a trailing newline) re-baselines.
TEST(CoreCommands, SignoffSummaryStaleAfterDesignChange) {
  TimingTempDir dir;
  const std::string pv = dir.Write("pv.md", kSignoffPv);
  const std::string lvs = dir.Write("lvs.md", kSignoffLvs);
  Shell shell;
  const auto run = [&shell, &pv, &lvs]() {
    return shell.Run("signoff_summary", {"-pv", pv, "-lvs", lvs});
  };
  const CommandResult first = run();
  ASSERT_TRUE(first.ok) << first.message;
  EXPECT_NE(first.payload.find("stale=0"), std::string::npos);
  shell.ctx.synth.verilog_path = dir.Path("one.v");
  shell.ctx.synth.top_cell = "one";
  const CommandResult adopted = run();
  ASSERT_TRUE(adopted.ok) << adopted.message;
  EXPECT_NE(adopted.payload.find("stale=0"), std::string::npos);
  EXPECT_NE(adopted.payload.find("fingerprint="), std::string::npos);
  EXPECT_EQ(adopted.payload.find(first.payload.substr(
                first.payload.find("fingerprint="), 27)),
            std::string::npos);
  shell.ctx.synth.verilog_path = dir.Path("two.v");
  shell.ctx.synth.top_cell = "two";
  const CommandResult stale = run();
  ASSERT_TRUE(stale.ok) << stale.message;
  EXPECT_NE(stale.payload.find("stale=1"), std::string::npos);
  EXPECT_FALSE(dir.Write("pv.md", std::string(kSignoffPv) + "\n").empty());
  const CommandResult rerun = run();
  ASSERT_TRUE(rerun.ok) << rerun.message;
  EXPECT_NE(rerun.payload.find("stale=0"), std::string::npos);
}

// F13: close_design clears the signoff baseline, so the next
// run baselines fresh instead of reporting the old design stale.
TEST(CoreCommands, SignoffSummaryClearsOnCloseDesign) {
  TimingTempDir dir;
  const std::string pv = dir.Write("pv.md", kSignoffPv);
  const std::string lvs = dir.Write("lvs.md", kSignoffLvs);
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  const auto run = [&shell, &pv, &lvs]() {
    return shell.Run("signoff_summary", {"-pv", pv, "-lvs", lvs});
  };
  shell.ctx.synth.verilog_path = dir.Path("one.v");
  shell.ctx.synth.top_cell = "one";
  ASSERT_TRUE(run().ok);
  shell.ctx.synth.verilog_path = dir.Path("two.v");
  shell.ctx.synth.top_cell = "two";
  const CommandResult stale = run();
  ASSERT_TRUE(stale.ok) << stale.message;
  EXPECT_NE(stale.payload.find("stale=1"), std::string::npos);
  ASSERT_TRUE(shell.Run("close_design", {}).ok);
  const CommandResult fresh = run();
  ASSERT_TRUE(fresh.ok) << fresh.message;
  EXPECT_NE(fresh.payload.find("stale=0"), std::string::npos);
}

// F0: LEF and tech LEF for the synth-to-PD bridge tests. The cell
// LEF mirrors the kSynthMapLiberty cells (AND2, OR2, INV, BUF)
// with the same pin names, one site, and one routing layer for
// pin geometry; the tech LEF carries the database units and two
// routing layers. kBridgeLefNoBuf is the same LEF without the
// BUF macro, for the missing-macro failure test.
const char kBridgeLef[] = R"LEF(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
SITE core
  CLASS CORE ;
  SIZE 0.46 BY 2.72 ;
END core
MACRO AND2
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.92 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END A
  PIN B
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END B
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT 0.35 -0.05 0.45 0.05 ;
    END
  END Y
END AND2
MACRO OR2
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.92 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END A
  PIN B
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END B
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT 0.35 -0.05 0.45 0.05 ;
    END
  END Y
END OR2
MACRO INV
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.92 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT 0.35 -0.05 0.45 0.05 ;
    END
  END Y
END INV
MACRO BUF
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.92 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT 0.35 -0.05 0.45 0.05 ;
    END
  END Y
END BUF
END LIBRARY
)LEF";

const char kBridgeTlef[] = R"TLEF(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
LAYER metal1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.19 ;
  WIDTH 0.065 ;
END metal1
LAYER metal2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 0.19 ;
  WIDTH 0.065 ;
END metal2
END LIBRARY
)TLEF";

// The demo/synth optloop design, byte-identical to demo/synth/
// optloop.v: a four-input AND through two association orders,
// so the mapped netlist shares gates between the outputs.
const char kBridgeOptloop[] = R"V(module optloop (a, b, c, d, y1, y2);
  input a, b, c, d;
  output y1, y2;
  assign y1 = ((a & b) & c) & d;
  assign y2 = ((a & c) & b) & d;
endmodule
)V";

// F0: the full bridge chain on embedded fixtures. synth_map with
// the fixture Liberty gives the README numbers (4 cells, AND2:3
// and BUF:1); write_def turns that netlist into DEF with a die
// area computed from the LEF macro sizes (4 gates at 0.92 by
// 2.72 um, 60 percent utilization, 1000 database units per
// micron: a 5 by 5 um die); read_lef plus read_def load it; and
// floorplan runs on the loaded database with no import_odb step.
TEST(CoreCommands, BridgeWritesDefAndFloorplanRuns) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kSynthMapLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("optloop.v", kBridgeOptloop),
                         "optloop"})
                    .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  ASSERT_TRUE(shell.Run("synth_generic").ok);
  const CommandResult mapped =
      shell.Run("synth_map", {"-target", "area"});
  ASSERT_TRUE(mapped.ok) << mapped.message;
  EXPECT_NE(mapped.payload.find("cells=4"), std::string::npos);
  EXPECT_NE(mapped.payload.find("types=AND2:3,BUF:1"),
            std::string::npos);
  const std::string lef = dir.Write("cells.lef", kBridgeLef);
  const std::string tlef = dir.Write("tech.lef", kBridgeTlef);
  const std::string def_path = dir.Path("optloop.def");
  const CommandResult def = shell.Run(
      "write_def", {def_path, "-lef", lef, "-tlef", tlef});
  ASSERT_TRUE(def.ok) << def.message;
  EXPECT_NE(def.payload.find("design=optloop"), std::string::npos);
  EXPECT_NE(def.payload.find("cells=4"), std::string::npos);
  EXPECT_NE(def.payload.find("types=AND2:3,BUF:1"),
            std::string::npos);
  EXPECT_NE(def.payload.find("die_area=0,0,5000,5000"),
            std::string::npos);
  EXPECT_NE(def.payload.find("nets=8"), std::string::npos);
  EXPECT_NE(def.payload.find("pins=6"), std::string::npos);
  const CommandResult tech = shell.Run("read_lef", {tlef});
  ASSERT_TRUE(tech.ok) << tech.message;
  EXPECT_NE(tech.payload.find("masters=0"), std::string::npos);
  const CommandResult lib = shell.Run("read_lef", {lef});
  ASSERT_TRUE(lib.ok) << lib.message;
  EXPECT_NE(lib.payload.find("masters=4"), std::string::npos);
  const CommandResult loaded = shell.Run("read_def", {def_path});
  ASSERT_TRUE(loaded.ok) << loaded.message;
  EXPECT_NE(loaded.payload.find("design=optloop"),
            std::string::npos);
  EXPECT_NE(loaded.payload.find("instances=4"),
            std::string::npos);
  EXPECT_NE(loaded.payload.find("nets=8"), std::string::npos);
  const CommandResult floorplan = shell.Run("floorplan", {});
  ASSERT_TRUE(floorplan.ok) << floorplan.message;
  EXPECT_NE(floorplan.payload.find("die_area=0,0,5000,5000"),
            std::string::npos);
  EXPECT_NE(floorplan.payload.find("core_area=0,0,5000,5000"),
            std::string::npos);
  EXPECT_NE(floorplan.payload.find("rows=1"), std::string::npos);
  EXPECT_NE(floorplan.payload.find("io_placed=6"),
            std::string::npos);
  EXPECT_NE(floorplan.payload.find("io_total=6"),
            std::string::npos);
}

#ifdef EOS_MOCK_PDK_DIR
// F0: the same chain on the real demo design and the mock PDK
// from the source tree (paths come from compile definitions in
// tests/unit/CMakeLists.txt). This is the chain the GUI console
// demo runs, and it proves the mock Liberty maps, the mock LEF
// provides every mapped cell and the row site floorplan needs,
// and the written DEF loads and floorplans.
TEST(CoreCommands, BridgeRunsOnMockPdk) {
  TimingTempDir dir;
  Shell shell;
  const std::string mock_dir = EOS_MOCK_PDK_DIR;
  const std::string demo_dir = EOS_DEMO_SYNTH_DIR;
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", mock_dir + "/eos_mock.lib"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {demo_dir + "/optloop.v", "optloop"})
                    .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  ASSERT_TRUE(shell.Run("synth_generic").ok);
  const CommandResult mapped =
      shell.Run("synth_map", {"-target", "area"});
  ASSERT_TRUE(mapped.ok) << mapped.message;
  const auto payload_number = [](const CommandResult& result,
                                 const std::string& key) {
    // Keys are space-separated fields; match the leading space so
    // "cells=" never matches inside "genlib_cells=".
    const std::string field = " " + key + "=";
    const std::size_t at = result.payload.find(field);
    EXPECT_NE(at, std::string::npos) << "missing " << key;
    if (at == std::string::npos) return 0;
    return std::stoi(result.payload.substr(at + field.size()));
  };
  const int cells = payload_number(mapped, "cells");
  ASSERT_GT(cells, 0);
  const std::string def_path = dir.Path("optloop.def");
  const CommandResult def =
      shell.Run("write_def",
                {def_path, "-lef", mock_dir + "/eos_mock.lef",
                 "-tlef", mock_dir + "/eos_mock.tlef"});
  ASSERT_TRUE(def.ok) << def.message;
  EXPECT_NE(def.payload.find("design=optloop"), std::string::npos);
  EXPECT_EQ(payload_number(def, "cells"), cells);
  ASSERT_TRUE(shell.Run("read_lef", {mock_dir + "/eos_mock.tlef"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_lef", {mock_dir + "/eos_mock.lef"})
                    .ok);
  const CommandResult loaded = shell.Run("read_def", {def_path});
  ASSERT_TRUE(loaded.ok) << loaded.message;
  EXPECT_NE(loaded.payload.find("design=optloop"),
            std::string::npos);
  EXPECT_EQ(payload_number(loaded, "instances"), cells);
  const CommandResult floorplan = shell.Run("floorplan", {});
  ASSERT_TRUE(floorplan.ok) << floorplan.message;
  EXPECT_GT(payload_number(floorplan, "rows"), 0);
  EXPECT_NE(floorplan.payload.find("die_area="),
            std::string::npos);
}
#endif

TEST(CoreCommands, WriteDefFailsFastWithoutMappedDesign) {
  TimingTempDir dir;
  Shell shell;
  const CommandResult result =
      shell.Run("write_def",
                {dir.Path("out.def"), "-lef",
                 dir.Write("cells.lef", kBridgeLef), "-tlef",
                 dir.Write("tech.lef", kBridgeTlef)});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(result.message.find("no mapped design"),
            std::string::npos);
}

TEST(CoreCommands, WriteDefFailsFastWhenCellHasNoLefMacro) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kSynthMapLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("optloop.v", kBridgeOptloop),
                         "optloop"})
                    .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  ASSERT_TRUE(shell.Run("synth_generic").ok);
  ASSERT_TRUE(shell.Run("synth_map", {"-target", "area"}).ok);
  // The LEF below is kBridgeLef without the BUF macro: the
  // mapped netlist needs BUF, so write_def must name it.
  std::string no_buf = kBridgeLef;
  const std::size_t at = no_buf.find("MACRO BUF\n");
  ASSERT_NE(at, std::string::npos);
  const std::size_t end = no_buf.find("END BUF\n", at);
  ASSERT_NE(end, std::string::npos);
  no_buf.erase(at, end + 8 - at);
  const CommandResult result = shell.Run(
      "write_def",
      {dir.Path("out.def"), "-lef", dir.Write("nobuf.lef", no_buf),
       "-tlef", dir.Write("tech.lef", kBridgeTlef)});
  ASSERT_FALSE(result.ok);
  EXPECT_NE(result.message.find("BUF"), std::string::npos);
}

TEST(CoreCommands, ReadDefFailsFastWithoutLef) {
  TimingTempDir dir;
  Shell shell;
  const CommandResult result =
      shell.Run("read_def", {dir.Path("no_such.def")});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kIoError);
  const CommandResult no_tech =
      shell.Run("read_def", {dir.Write("empty.def", "VERSION 5.8 ;\n")});
  ASSERT_FALSE(no_tech.ok);
  EXPECT_EQ(no_tech.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(no_tech.message.find("read_lef"), std::string::npos);
}

TEST(CoreCommands, ReadLefFailsFastAfterDesignLoaded) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kSynthMapLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("optloop.v", kBridgeOptloop),
                         "optloop"})
                    .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  ASSERT_TRUE(shell.Run("synth_generic").ok);
  ASSERT_TRUE(shell.Run("synth_map", {"-target", "area"}).ok);
  const std::string lef = dir.Write("cells.lef", kBridgeLef);
  const std::string tlef = dir.Write("tech.lef", kBridgeTlef);
  const std::string def_path = dir.Path("optloop.def");
  ASSERT_TRUE(shell.Run("write_def", {def_path, "-lef", lef,
                                      "-tlef", tlef})
                    .ok);
  ASSERT_TRUE(shell.Run("read_lef", {tlef}).ok);
  ASSERT_TRUE(shell.Run("read_lef", {lef}).ok);
  ASSERT_TRUE(shell.Run("read_def", {def_path}).ok);
  const CommandResult again = shell.Run("read_lef", {lef});
  ASSERT_FALSE(again.ok);
  EXPECT_EQ(again.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(again.message.find("design already loaded"),
            std::string::npos);
  const CommandResult def_again = shell.Run("read_def", {def_path});
  ASSERT_FALSE(def_again.ok);
  EXPECT_EQ(def_again.diag, Diag::kInvalidConfiguration);
}

TEST(CoreCommands, WriteDefRejectsBadOptions) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kSynthMapLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("optloop.v", kBridgeOptloop),
                         "optloop"})
                    .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  ASSERT_TRUE(shell.Run("synth_generic").ok);
  ASSERT_TRUE(shell.Run("synth_map", {"-target", "area"}).ok);
  const std::string lef = dir.Write("cells.lef", kBridgeLef);
  const std::string tlef = dir.Write("tech.lef", kBridgeTlef);
  const std::string def_path = dir.Path("out.def");
  const CommandResult unknown = shell.Run(
      "write_def", {def_path, "-lef", lef, "-tlef", tlef, "-bogus"});
  ASSERT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.diag, Diag::kInvalidConfiguration);
  const CommandResult dup_lef = shell.Run(
      "write_def",
      {def_path, "-lef", lef, "-lef", lef, "-tlef", tlef});
  ASSERT_FALSE(dup_lef.ok);
  EXPECT_EQ(dup_lef.diag, Diag::kInvalidConfiguration);
  const CommandResult no_tlef =
      shell.Run("write_def", {def_path, "-lef", lef});
  ASSERT_FALSE(no_tlef.ok);
  EXPECT_EQ(no_tlef.diag, Diag::kInvalidConfiguration);
  const CommandResult bad_area = shell.Run(
      "write_def",
      {def_path, "-lef", lef, "-tlef", tlef, "-die_area", "0", "0",
       "wide", "5000"});
  ASSERT_FALSE(bad_area.ok);
  EXPECT_EQ(bad_area.diag, Diag::kInvalidConfiguration);
  const CommandResult empty_area = shell.Run(
      "write_def",
      {def_path, "-lef", lef, "-tlef", tlef, "-die_area", "0", "0",
       "0", "5000"});
  ASSERT_FALSE(empty_area.ok);
  EXPECT_NE(empty_area.message.find("non-empty"),
            std::string::npos);
}

TEST(CoreCommands, FloorplanRejectsBadOptions) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kTimingDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());
  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);

  const CommandResult unknown = shell.Run("floorplan", {"-bogus"});
  ASSERT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.diag, Diag::kInvalidConfiguration);
  const CommandResult duplicate =
      shell.Run("floorplan", {"-site", "core", "-site", "core"});
  ASSERT_FALSE(duplicate.ok);
  const CommandResult missing =
      shell.Run("floorplan", {"-die_area", "0", "0"});
  ASSERT_FALSE(missing.ok);
  const CommandResult not_a_number = shell.Run(
      "floorplan", {"-die_area", "0", "0", "wide", "10000"});
  ASSERT_FALSE(not_a_number.ok);
  const CommandResult unknown_site =
      shell.Run("floorplan", {"-site", "no_such_site"});
  ASSERT_FALSE(unknown_site.ok);
  EXPECT_NE(unknown_site.message.find("unknown site"), std::string::npos);
}

// P2: eos::place runs global then detailed placement on the
// floorplanned imported design, reports placed/total/hpwl/overlaps,
// and states placed STA unavailability when no timing session is
// configured. It fails fast on an un-floorplanned design, on bad
// options, and on missing session state. The DEF is the same
// PORT-carrying fixture as the floorplan tests.
TEST(CoreCommands, PlaceRunsOnFloorplannedDesign) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kFloorplanDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());

  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  ASSERT_TRUE(shell.Run("floorplan", {}).ok);
  const CommandResult result = shell.Run("place", {});
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_NE(result.payload.find("placed=2"), std::string::npos);
  EXPECT_NE(result.payload.find("total=2"), std::string::npos);
  EXPECT_NE(result.payload.find("overlaps=0"), std::string::npos);
  EXPECT_NE(result.payload.find("hpwl="), std::string::npos);
  // No timing session is configured, so the payload states
  // unavailability with the reason instead of inventing a slack.
  EXPECT_NE(result.payload.find("sta_slack_ns=unavailable"),
            std::string::npos);
  // A second place in the same session succeeds (the runner clears
  // its STA session before returning).
  const CommandResult again = shell.Run("place", {});
  ASSERT_TRUE(again.ok) << again.message;
}

TEST(CoreCommands, PlaceFailsFastWithoutTechnology) {
  Shell shell;
  const CommandResult result = shell.Run("place", {});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(result.message.find("no technology data"), std::string::npos);
}

TEST(CoreCommands, PlaceFailsFastWithoutMappedDesign) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  Shell shell;
  shell.ctx.timing.odb = std::move(database);
  const CommandResult result = shell.Run("place", {});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kUnclosedDesign);
  EXPECT_NE(result.message.find("no mapped design"), std::string::npos);
}

TEST(CoreCommands, PlaceFailsFastOnUnfloorplannedDesign) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kFloorplanDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());
  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  // No floorplan run: the design has no rows.
  const CommandResult result = shell.Run("place", {});
  ASSERT_FALSE(result.ok);
  EXPECT_NE(result.message.find("floorplan"), std::string::npos);
}

TEST(CoreCommands, PlaceRejectsBadOptions) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kFloorplanDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());
  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  ASSERT_TRUE(shell.Run("floorplan", {}).ok);

  const CommandResult unknown = shell.Run("place", {"-bogus"});
  ASSERT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.diag, Diag::kInvalidConfiguration);
  const CommandResult duplicate =
      shell.Run("place", {"-timing_driven", "-timing_driven"});
  ASSERT_FALSE(duplicate.ok);
  EXPECT_EQ(duplicate.diag, Diag::kInvalidConfiguration);
}

TEST(CoreCommands, PlaceTimingDrivenWithoutLibertyFailsFast) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kFloorplanDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());
  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  ASSERT_TRUE(shell.Run("floorplan", {}).ok);
  // Timing-driven placement needs a Liberty-loaded STA session;
  // this session has none, so it fails fast instead of silently
  // running untimed placement.
  const CommandResult result = shell.Run("place", {"-timing_driven"});
  ASSERT_FALSE(result.ok);
  EXPECT_NE(result.message.find("Liberty"), std::string::npos);
}

// F4: the place_summary query over the placed imported
// design: the header carries the die and core rectangles and
// the counts from the place record, one "inst" line per placed
// instance with its master size, and no slack line when no
// timing was configured. The HPWL matches the place payload's
// own number, so the query and the engine agree on the source.
TEST(CoreCommands, PlaceSummaryReadsPlacedDesign) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kFloorplanDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());

  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  ASSERT_TRUE(shell.Run(
                  "floorplan",
                  {"-die_area", "0", "0", "10000", "10000", "-core_area",
                   "0", "0", "10000", "10000", "-site", "core"})
                  .ok);
  const CommandResult placed = shell.Run("place", {});
  ASSERT_TRUE(placed.ok) << placed.message;
  const CommandResult summary = shell.Run("place_summary", {});
  ASSERT_TRUE(summary.ok) << summary.message;
  // The die is the requested die. The core is the extent the
  // rows cover, not the requested core: 21 sites of 460 DBU
  // wide and 3 rows of 2720 DBU tall, so 9660 by 8160.
  EXPECT_NE(summary.payload.find(
                "placement die=0,0,10000,10000 core=0,0,9660,8160 "
                "placed=2 total=2"),
            std::string::npos);
  EXPECT_NE(summary.payload.find("overlaps=0"), std::string::npos);
  const auto hpwl = [](const std::string& payload) {
    const std::size_t at = payload.find("hpwl=");
    if (at == std::string::npos) return std::string();
    const std::size_t start = at + std::string("hpwl=").size();
    const std::size_t end = payload.find(' ', start);
    return payload.substr(start, end - start);
  };
  ASSERT_FALSE(hpwl(placed.payload).empty());
  EXPECT_NE(summary.payload.find("hpwl=" + hpwl(placed.payload)),
            std::string::npos);
  // One inst line per placed instance, with the master size
  // from the LEF macro (BUFX2 is 0.46 by 2.72 microns, so 460
  // by 2720 database units).
  EXPECT_NE(summary.payload.find("inst name=u1 master=BUFX2"),
            std::string::npos);
  EXPECT_NE(summary.payload.find("inst name=u2 master=BUFX2"),
            std::string::npos);
  EXPECT_NE(summary.payload.find("w=460 h=2720"), std::string::npos);
  // No timing was configured, so the summary carries no slack
  // line instead of inventing one.
  EXPECT_EQ(summary.payload.find("slack "), std::string::npos);
}

// F4: place_summary carries one slack line per scene the place
// run measured post-place slack for, with the same number the
// place payload reported. The fixture path is in1 through two
// 0.3 ns buffers to out1 against a 10 ns virtual clock with
// 1 ns of input and output delay each, so the slack is
// positive; the test asserts the line and its sign, not the
// exact value, so estimated wire delay never makes it brittle.
TEST(CoreCommands, PlaceSummaryReportsPostPlaceSlack) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kFloorplanDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());

  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  ASSERT_TRUE(shell.Run(
                  "read_liberty",
                  {"tt", dir.Write("cells.lib", kPlaceLiberty)})
                  .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kPlaceSdc)}).ok);
  ASSERT_TRUE(shell.Run("create_scenario",
                        {"func_tt", "func", "tt", "estimate"})
                  .ok);
  ASSERT_TRUE(shell.Run(
                  "floorplan",
                  {"-die_area", "0", "0", "10000", "10000", "-core_area",
                   "0", "0", "10000", "10000", "-site", "core"})
                  .ok);
  const CommandResult placed = shell.Run("place", {});
  ASSERT_TRUE(placed.ok) << placed.message;
  EXPECT_EQ(placed.payload.find("sta_slack_ns=unavailable"),
            std::string::npos);
  const CommandResult summary = shell.Run("place_summary", {});
  ASSERT_TRUE(summary.ok) << summary.message;
  const std::size_t at =
      summary.payload.find("slack scene=func_tt worst_slack_ns=");
  ASSERT_NE(at, std::string::npos);
  const std::size_t start =
      at + std::string("slack scene=func_tt worst_slack_ns=").size();
  const double slack =
      std::stod(summary.payload.substr(start));
  EXPECT_GT(slack, 0.0);
}

// F4: place_summary fails fast before any place runs, with the
// same preconditions as place itself.
TEST(CoreCommands, PlaceSummaryFailsFastWithoutPlacement) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kFloorplanDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());

  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  ASSERT_TRUE(shell.Run("floorplan", {}).ok);
  const CommandResult result = shell.Run("place_summary", {});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(result.message.find("no placement"), std::string::npos);
}

TEST(CoreCommands, PlaceSummaryFailsFastWithoutTechnology) {
  Shell shell;
  const CommandResult result = shell.Run("place_summary", {});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(result.message.find("no technology data"), std::string::npos);
}

TEST(CoreCommands, PlaceSummaryFailsFastWithoutMappedDesign) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  Shell shell;
  shell.ctx.timing.odb = std::move(database);
  const CommandResult result = shell.Run("place_summary", {});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kUnclosedDesign);
  EXPECT_NE(result.message.find("no mapped design"), std::string::npos);
}

TEST(CoreCommands, PlaceSummaryRejectsBadOptions) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kFloorplanDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());
  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  ASSERT_TRUE(shell.Run("floorplan", {}).ok);
  ASSERT_TRUE(shell.Run("place", {}).ok);

  const CommandResult result = shell.Run("place_summary", {"-bogus"});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
}

// P3: eos::cts option parsing and session preconditions. The
// engine run itself is covered by the pd.cts unit tests; these
// tests pin the command layer: unknown and duplicate options,
// the required -clock, and the fail-fast order (technology,
// mapped design, Liberty) before any engine runs.
TEST(CoreCommands, CtsRejectsBadOptions) {
  Shell shell;
  const CommandResult unknown = shell.Run("cts", {"-bogus"});
  ASSERT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(unknown.message.find("unknown cts option"),
            std::string::npos);
  const CommandResult duplicate =
      shell.Run("cts", {"-clock", "clk", "-clock", "clk"});
  ASSERT_FALSE(duplicate.ok);
  EXPECT_EQ(duplicate.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(duplicate.message.find("given twice"), std::string::npos);
  const CommandResult missing_value = shell.Run("cts", {"-clock"});
  ASSERT_FALSE(missing_value.ok);
  EXPECT_EQ(missing_value.diag, Diag::kInvalidConfiguration);
  const CommandResult no_clock = shell.Run("cts", {});
  ASSERT_FALSE(no_clock.ok);
  EXPECT_EQ(no_clock.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(no_clock.message.find("needs a clock net"),
            std::string::npos);
}

TEST(CoreCommands, CtsFailsFastWithoutTechnology) {
  Shell shell;
  const CommandResult result = shell.Run("cts", {"-clock", "clk"});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(result.message.find("no technology data"), std::string::npos);
}

TEST(CoreCommands, CtsFailsFastWithoutLiberty) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kFloorplanDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());
  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  // No read_liberty in this session, so cts fails fast before
  // any engine runs.
  const CommandResult result = shell.Run("cts", {"-clock", "clk"});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(result.message.find("no Liberty loaded"), std::string::npos);
}

// P4/P5: eos::route runs global plus detailed routing on the
// floorplanned and placed imported design, reports
// routed/total/guides/overflow plus the per-layer congestion
// entries, the detailed violation count, and the loop iterations
// against the bound, and states routed STA unavailability when no
// timing session is configured. The engine run itself is covered
// by the pd.route unit tests; these tests pin the command layer:
// the happy path, unknown and duplicate options, non-integer
// layers, and the fail-fast order (technology, mapped design)
// before any engine runs. The DEF is the same PORT-carrying
// fixture as the floorplan tests.
TEST(CoreCommands, RouteRunsOnPlacedDesign) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kRouteLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kFloorplanDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());
  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  ASSERT_TRUE(
      shell.Run("read_liberty",
                {"tt", dir.Write("cells.lib", kTimingLiberty)}).ok);
  ASSERT_TRUE(shell.Run("floorplan", {}).ok);
  ASSERT_TRUE(shell.Run("place", {}).ok);
  const CommandResult result = shell.Run("route", {});
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_NE(result.payload.find("routed=3"), std::string::npos);
  EXPECT_NE(result.payload.find("total=3"), std::string::npos);
  EXPECT_NE(result.payload.find("guides="), std::string::npos);
  EXPECT_NE(result.payload.find("overflow=0"), std::string::npos);
  EXPECT_NE(result.payload.find("layer_1="), std::string::npos);
  EXPECT_NE(result.payload.find("layer_2="), std::string::npos);
  EXPECT_NE(result.payload.find("violations="), std::string::npos);
  EXPECT_NE(result.payload.find("detailed_iterations="),
            std::string::npos);
  EXPECT_NE(result.payload.find("detailed_bound=64"), std::string::npos);
  EXPECT_NE(result.payload.find("stopped_at_bound="), std::string::npos);
  EXPECT_NE(result.payload.find("liberty_corner=tt"), std::string::npos);
  // No timing session is configured, so the payload states
  // unavailability with the reason instead of inventing a slack.
  EXPECT_NE(result.payload.find("sta_slack_ns=unavailable"),
            std::string::npos);
  // A second route in the same session succeeds (the runner
  // clears its STA session before returning).
  const CommandResult again = shell.Run("route", {});
  ASSERT_TRUE(again.ok) << again.message;
}

TEST(CoreCommands, RouteRejectsBadOptions) {
  Shell shell;
  const CommandResult unknown = shell.Run("route", {"-bogus"});
  ASSERT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(unknown.message.find("unknown route option"),
            std::string::npos);
  const CommandResult duplicate =
      shell.Run("route", {"-min_layer", "1", "-min_layer", "1"});
  ASSERT_FALSE(duplicate.ok);
  EXPECT_EQ(duplicate.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(duplicate.message.find("given twice"), std::string::npos);
  const CommandResult missing_value = shell.Run("route", {"-max_layer"});
  ASSERT_FALSE(missing_value.ok);
  EXPECT_EQ(missing_value.diag, Diag::kInvalidConfiguration);
  const CommandResult not_a_number =
      shell.Run("route", {"-min_layer", "wide"});
  ASSERT_FALSE(not_a_number.ok);
  EXPECT_EQ(not_a_number.diag, Diag::kInvalidConfiguration);
}

TEST(CoreCommands, ExtractParasiticsRejectsBadArguments) {
  // Argument shape is validated before any session state is
  // touched, so these run on an empty context.
  Shell shell;
  const CommandResult no_args = shell.Run("extract_parasitics", {});
  ASSERT_FALSE(no_args.ok);
  EXPECT_EQ(no_args.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(no_args.message.find("usage"), std::string::npos);
  const CommandResult two_positional =
      shell.Run("extract_parasitics", {"a.spef", "b.spef"});
  ASSERT_FALSE(two_positional.ok);
  EXPECT_EQ(two_positional.diag, Diag::kInvalidConfiguration);
  const CommandResult unknown =
      shell.Run("extract_parasitics", {"a.spef", "-bogus"});
  ASSERT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(unknown.message.find("unknown extract_parasitics option"),
            std::string::npos);
  const CommandResult duplicate = shell.Run(
      "extract_parasitics", {"a.spef", "-model", "m.rules", "-model",
                             "m.rules"});
  ASSERT_FALSE(duplicate.ok);
  EXPECT_EQ(duplicate.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(duplicate.message.find("given twice"), std::string::npos);
  const CommandResult missing_value =
      shell.Run("extract_parasitics", {"a.spef", "-model"});
  ASSERT_FALSE(missing_value.ok);
  EXPECT_EQ(missing_value.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(missing_value.message.find("needs a value"),
            std::string::npos);
  const CommandResult missing_model =
      shell.Run("extract_parasitics", {"a.spef"});
  ASSERT_FALSE(missing_model.ok);
  EXPECT_EQ(missing_model.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(missing_model.message.find("-model"), std::string::npos);
}

TEST(CoreCommands, RouteAcceptsDetailedOptions) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kRouteLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kFloorplanDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());
  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  ASSERT_TRUE(
      shell.Run("read_liberty",
                {"tt", dir.Write("cells.lib", kTimingLiberty)}).ok);
  ASSERT_TRUE(shell.Run("floorplan", {}).ok);
  ASSERT_TRUE(shell.Run("place", {}).ok);
  const CommandResult result = shell.Run(
      "route", {"-max_iterations", "1", "-bottom_layer", "metal1",
                "-top_layer", "metal2"});
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_NE(result.payload.find("detailed_bound=1"), std::string::npos);
  EXPECT_NE(result.payload.find("detailed_iterations=1"),
            std::string::npos);
  EXPECT_NE(result.payload.find("violations="), std::string::npos);
  // Duplicate and non-integer detailed options fail at the
  // command layer, before any engine runs.
  const CommandResult duplicate = shell.Run(
      "route", {"-max_iterations", "1", "-max_iterations", "2"});
  ASSERT_FALSE(duplicate.ok);
  EXPECT_EQ(duplicate.diag, Diag::kInvalidConfiguration);
  const CommandResult not_a_number =
      shell.Run("route", {"-max_iterations", "many"});
  ASSERT_FALSE(not_a_number.ok);
  EXPECT_EQ(not_a_number.diag, Diag::kInvalidConfiguration);
}

TEST(CoreCommands, RouteRejectsZeroIterations) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kRouteLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kFloorplanDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());
  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  ASSERT_TRUE(
      shell.Run("read_liberty",
                {"tt", dir.Write("cells.lib", kTimingLiberty)}).ok);
  ASSERT_TRUE(shell.Run("floorplan", {}).ok);
  ASSERT_TRUE(shell.Run("place", {}).ok);
  const CommandResult result =
      shell.Run("route", {"-max_iterations", "0"});
  ASSERT_FALSE(result.ok);
}

TEST(CoreCommands, RouteFailsFastWithoutLiberty) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kTimingLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kFloorplanDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());
  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  ASSERT_TRUE(shell.Run("floorplan", {}).ok);
  ASSERT_TRUE(shell.Run("place", {}).ok);
  // No read_liberty in this session, so route fails fast before
  // any engine runs.
  const CommandResult result = shell.Run("route", {});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(result.message.find("no Liberty loaded"), std::string::npos);
}

TEST(CoreCommands, RouteFailsFastWithoutTechnology) {
  Shell shell;
  const CommandResult result = shell.Run("route", {});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(result.message.find("no technology data"), std::string::npos);
}

// S6 backfill: HasDesign mirrors the project's design state.
TEST(CommandContext, HasDesignTracksCreateDesign) {
  Shell shell;
  EXPECT_FALSE(shell.ctx.HasDesign());
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  EXPECT_TRUE(shell.ctx.HasDesign());
}

// P7: eos::finish runs filler insertion on the routed design and
// eos::write_gds streams it out, with the finish-before-write
// ordering enforced at the command layer. The engine run itself is
// covered by the pd.finish unit tests; these tests pin the command
// layer: the happy path, the write-before-finish failure, and the
// argument-shape failures on an empty context.
TEST(CoreCommands, FinishAndWriteGdsRunOnRoutedDesign) {
  TimingTempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kFinishLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kFloorplanDef)).ok());
  const std::string db_path = dir.Path("top.odb");
  ASSERT_TRUE(database.WriteDb(db_path).ok());
  Shell shell;
  ASSERT_TRUE(shell.Run("import_odb", {db_path}).ok);
  ASSERT_TRUE(
      shell.Run("read_liberty",
                {"tt", dir.Write("cells.lib", kTimingLiberty)}).ok);
  ASSERT_TRUE(shell.Run("floorplan", {}).ok);
  ASSERT_TRUE(shell.Run("place", {}).ok);
  ASSERT_TRUE(shell.Run("route", {}).ok);
  // Stream-out before finish fails fast with the finish message.
  const CommandResult early =
      shell.Run("write_gds", {dir.Path("early.gds")});
  ASSERT_FALSE(early.ok);
  EXPECT_NE(early.message.find("finish has not run"),
            std::string::npos);
  const CommandResult finished = shell.Run("finish", {});
  ASSERT_TRUE(finished.ok) << finished.message;
  EXPECT_NE(finished.payload.find("fillers="), std::string::npos);
  EXPECT_NE(finished.payload.find("pdn=false"), std::string::npos);
  const CommandResult written =
      shell.Run("write_gds", {dir.Path("top.gds")});
  ASSERT_TRUE(written.ok) << written.message;
  EXPECT_NE(written.payload.find("gds="), std::string::npos);
  EXPECT_NE(written.payload.find("size="), std::string::npos);
  EXPECT_NE(written.payload.find("instances="), std::string::npos);
  EXPECT_NE(written.payload.find("structures="), std::string::npos);
}

TEST(CoreCommands, FinishRejectsBadOptions) {
  Shell shell;
  const CommandResult unknown = shell.Run("finish", {"-bogus"});
  ASSERT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(unknown.message.find("unknown finish option"),
            std::string::npos);
  const CommandResult duplicate = shell.Run("finish", {"-pdn", "-pdn"});
  ASSERT_FALSE(duplicate.ok);
  EXPECT_EQ(duplicate.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(duplicate.message.find("given twice"), std::string::npos);
  const CommandResult missing_value =
      shell.Run("finish", {"-fill_config"});
  ASSERT_FALSE(missing_value.ok);
  EXPECT_EQ(missing_value.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(missing_value.message.find("needs a value"),
            std::string::npos);
  const CommandResult positional =
      shell.Run("finish", {"stray"});
  ASSERT_FALSE(positional.ok);
  EXPECT_EQ(positional.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(positional.message.find("usage"), std::string::npos);
}

TEST(CoreCommands, WriteGdsRejectsBadArguments) {
  Shell shell;
  const CommandResult no_args = shell.Run("write_gds", {});
  ASSERT_FALSE(no_args.ok);
  EXPECT_EQ(no_args.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(no_args.message.find("usage"), std::string::npos);
  const CommandResult two_positional =
      shell.Run("write_gds", {"a.gds", "b.gds"});
  ASSERT_FALSE(two_positional.ok);
  EXPECT_EQ(two_positional.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(two_positional.message.find("usage"), std::string::npos);
  const CommandResult unknown =
      shell.Run("write_gds", {"-bogus"});
  ASSERT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(unknown.message.find("unknown write_gds option"),
            std::string::npos);
}

TEST(CoreCommands, SynthStatsFailsFastWhenNotElaborated) {
  Shell shell;
  const CommandResult fresh = shell.Run("synth_stats");
  EXPECT_FALSE(fresh.ok);
  EXPECT_EQ(fresh.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(fresh.message.find("no elaborated design: eos::elaborate first"),
            std::string::npos);
  TimingTempDir dir;
  Shell parsed;
  ASSERT_TRUE(parsed
                  .Run("read_verilog",
                       {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"})
                  .ok);
  const CommandResult unelaborated = parsed.Run("synth_stats");
  EXPECT_FALSE(unelaborated.ok);
  EXPECT_EQ(unelaborated.diag, Diag::kInvalidConfiguration);
}

TEST(CoreCommands, SynthStatsRejectsBadArguments) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell
                  .Run("read_verilog",
                       {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"})
                  .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult extra = shell.Run("synth_stats", {"optloop"});
  EXPECT_FALSE(extra.ok);
  EXPECT_EQ(extra.diag, Diag::kInvalidConfiguration);
}

// The stats for the flat optloop, from the live elaborated
// design: six $and gates in two unshared chains of three, no
// sequential cells. The wires count is the live engine's own
// number, pinned from a run of this test, not derived by hand:
// the elaborated design's wire set includes the internal chain
// wires plus the ports.
TEST(CoreCommands, SynthStatsFlatReportsExactPayload) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell
                  .Run("read_verilog",
                       {dir.Write("optloop.v", kSynthOptLoopDemo), "optloop"})
                  .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult stats = shell.Run("synth_stats");
  ASSERT_TRUE(stats.ok) << stats.message;
  EXPECT_EQ(stats.payload,
            "module=optloop\n"
            "cells=6 wires=10 depth=3 sequential=0\n"
            "type $and 6\n");
}

// After synth_generic the live design is the generic netlist,
// so synth_stats reports the generic result: three $and gates
// at depth 2, the shared product folded away.
TEST(CoreCommands, SynthStatsAfterGenericReportsGenericNetlist) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell
                  .Run("read_verilog",
                       {dir.Write("optloop.v", kSynthOptLoopDemo), "optloop"})
                  .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  ASSERT_TRUE(shell.Run("synth_generic").ok);
  const CommandResult stats = shell.Run("synth_stats");
  ASSERT_TRUE(stats.ok) << stats.message;
  EXPECT_NE(stats.payload.find("module=optloop"), std::string::npos);
  EXPECT_NE(stats.payload.find("cells=3"), std::string::npos);
  EXPECT_NE(stats.payload.find("depth=2"), std::string::npos);
  EXPECT_NE(stats.payload.find("type $and 3"), std::string::npos);
}

// F1: synth_summary fails fast until synth_map ran, even on an
// elaborated and generically optimized design.
TEST(CoreCommands, SynthSummaryFailsFastWhenNotMapped) {
  TimingTempDir dir;
  Shell shell;
  const CommandResult fresh = shell.Run("synth_summary");
  EXPECT_FALSE(fresh.ok);
  EXPECT_EQ(fresh.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(fresh.message.find("no mapped design: eos::synth_map first"),
            std::string::npos);
  ASSERT_TRUE(SynthOptFlowToMapped(shell, dir, kSynthOptSdcClear));
  ASSERT_TRUE(shell.Run("synth_summary").ok);
  // A fresh shell that stops at elaborate has no mapped record
  // either.
  TimingTempDir dir2;
  Shell elaborated;
  ASSERT_TRUE(elaborated
                  .Run("read_verilog",
                       {dir2.Write("optloop.v", kSynthOptLoopDemo),
                        "optloop"})
                  .ok);
  ASSERT_TRUE(elaborated.Run("elaborate").ok);
  EXPECT_FALSE(elaborated.Run("synth_summary").ok);
}

TEST(CoreCommands, SynthSummaryRejectsBadArguments) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(SynthOptFlowToMapped(shell, dir, kSynthOptSdcClear));
  const CommandResult extra = shell.Run("synth_summary", {"optloop"});
  EXPECT_FALSE(extra.ok);
  EXPECT_EQ(extra.diag, Diag::kInvalidConfiguration);
}

// F1: a mapped-only design reports the mapped record with the
// opt and equiv lines at their placeholders: the loop never ran
// and no proof ran. Slack is asserted numerically, like the
// loop tests: the STA values are float-derived, so the payload
// carries full-precision digits (-0.350000029), not the rounded
// README number.
TEST(CoreCommands, SynthSummaryMappedOnlyReportsPlaceholders) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(SynthOptFlowToMapped(shell, dir, kSynthOptSdcClear));
  const CommandResult summary = shell.Run("synth_summary");
  ASSERT_TRUE(summary.ok) << summary.message;
  const std::string mapped = shell.ctx.synth.mapped_verilog;
  const std::string liberty = shell.ctx.synth.mapped_liberty;
  EXPECT_NE(summary.payload.find("mapped target=area liberty=" +
                                 liberty + " verilog=" + mapped +
                                 " cells=4 types=AND2:3,BUF:1 "
                                 "judged=true"),
            std::string::npos);
  EXPECT_NEAR(SynthPayloadNumber(summary, "worst_slack_ns"), -0.35,
              1e-4);
  EXPECT_NEAR(SynthPayloadNumber(summary, "tns_ns"), -0.35, 1e-4);
  EXPECT_NE(summary.payload.find(
                "opt run=false winner=none iterations=0 verilog=none "
                "cells=0 types=none worst_slack_ns=none tns_ns=none"),
            std::string::npos);
  EXPECT_NE(summary.payload.find(
                "equiv run=false verdict=none first=none second=none"),
            std::string::npos);
}

// F1: the full demo chain, the numbers demo/synth/README.md
// asserts: mapped 4 cells (AND2:3, BUF:1) at worst slack -0.35
// ns, then loop winner classic at worst slack +0.05 ns with
// equivalence equivalent. Slack is asserted numerically (see
// the mapped-only test); the opt slack is parsed after the opt
// line, since the payload carries two worst_slack_ns keys.
TEST(CoreCommands, SynthSummaryFullChainReportsExactPayload) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(SynthOptFlowToMapped(shell, dir, kSynthOptSdcClear));
  const CommandResult loop =
      shell.Run("synth_opt", {"-strategies", "classic"});
  ASSERT_TRUE(loop.ok) << loop.message;
  const std::string mapped = shell.ctx.synth.mapped_verilog;
  const std::string winner = shell.ctx.synth.opt_verilog;
  const CommandResult proof =
      shell.Run("check_equivalence", {mapped, winner});
  ASSERT_TRUE(proof.ok) << proof.message;
  const std::string liberty = shell.ctx.synth.mapped_liberty;
  const CommandResult summary = shell.Run("synth_summary");
  ASSERT_TRUE(summary.ok) << summary.message;
  EXPECT_NE(summary.payload.find("mapped target=area liberty=" +
                                 liberty + " verilog=" + mapped +
                                 " cells=4 types=AND2:3,BUF:1 "
                                 "judged=true"),
            std::string::npos);
  EXPECT_NEAR(SynthPayloadNumber(summary, "worst_slack_ns"), -0.35,
              1e-4);
  EXPECT_NEAR(SynthPayloadNumber(summary, "tns_ns"), -0.35, 1e-4);
  EXPECT_NE(summary.payload.find("opt run=true winner=classic "
                                 "iterations=1 verilog=" +
                                 winner +
                                 " cells=4 types=AND2:4"),
            std::string::npos);
  const auto payload_number_after =
      [&summary](const std::string& key, const std::string& after) {
        const std::size_t start = summary.payload.find(after);
        EXPECT_NE(start, std::string::npos) << "missing " << after;
        if (start == std::string::npos) return 0.0;
        const std::size_t at =
            summary.payload.find(key + "=", start);
        EXPECT_NE(at, std::string::npos) << "missing " << key;
        if (at == std::string::npos) return 0.0;
        return std::stod(summary.payload.substr(at + key.size() + 1));
      };
  EXPECT_NEAR(payload_number_after("worst_slack_ns", "opt run=true"),
              0.05, 1e-4);
  EXPECT_NEAR(payload_number_after("tns_ns", "opt run=true"), 0.0,
              1e-4);
  EXPECT_NE(summary.payload.find("equiv run=true verdict=equivalent "
                                 "first=" +
                                 mapped + " second=" + winner),
            std::string::npos);
}

// F1: a mapping without timing setup reports the mapped record
// unjudged: the slack fields are none, never an invented zero.
TEST(CoreCommands, SynthSummaryUnjudgedReportsNoSlack) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", kSynthMapLiberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"})
                    .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  ASSERT_TRUE(shell.Run("synth_map", {"-target", "area"}).ok);
  EXPECT_FALSE(shell.ctx.synth.mapped_timing_judged);
  const CommandResult summary = shell.Run("synth_summary");
  ASSERT_TRUE(summary.ok) << summary.message;
  EXPECT_NE(summary.payload.find("judged=false"),
            std::string::npos);
  EXPECT_NE(summary.payload.find("worst_slack_ns=none"),
            std::string::npos);
  EXPECT_NE(summary.payload.find("tns_ns=none"), std::string::npos);
}

// Panel rework: synth_issues fails fast like the other synth
// queries when no elaborated design exists.
TEST(CoreCommands, SynthIssuesFailsFastWhenNotElaborated) {
  Shell shell;
  const CommandResult fresh = shell.Run("synth_issues");
  EXPECT_FALSE(fresh.ok);
  EXPECT_EQ(fresh.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(fresh.message.find("no elaborated design: eos::elaborate first"),
            std::string::npos);
  TimingTempDir dir;
  Shell parsed;
  ASSERT_TRUE(parsed
                  .Run("read_verilog",
                       {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"})
                  .ok);
  const CommandResult unelaborated = parsed.Run("synth_issues");
  EXPECT_FALSE(unelaborated.ok);
  EXPECT_EQ(unelaborated.diag, Diag::kInvalidConfiguration);
}

TEST(CoreCommands, SynthIssuesRejectsBadArguments) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell
                  .Run("read_verilog",
                       {dir.Write("optdemo.v", kSynthOptDemo), "optdemo"})
                  .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult extra = shell.Run("synth_issues", {"optloop"});
  EXPECT_FALSE(extra.ok);
  EXPECT_EQ(extra.diag, Diag::kInvalidConfiguration);
}

// The flat optloop is clean: no issues, and the payload names
// what was checked and which netlist the (empty) list
// describes. The checked line is the honest list: the command
// does not claim width coverage it does not implement.
TEST(CoreCommands, SynthIssuesCleanDesignReportsCheckedAndNetlist) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell
                  .Run("read_verilog",
                       {dir.Write("optloop.v", kSynthOptLoopDemo), "optloop"})
                  .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult issues = shell.Run("synth_issues");
  ASSERT_TRUE(issues.ok) << issues.message;
  EXPECT_EQ(issues.payload,
            "checked=hierarchy resolution, multiple drivers, "
            "undriven nets, combinational loops, inferred "
            "latches, unknown cell types, unconnected ports, "
            "top ports, empty modules\n"
            "netlist=elaborated\n");
}

// After synth_generic the issues describe the generic
// netlist, not the elaborated one: the payload says so.
TEST(CoreCommands, SynthIssuesAfterGenericReportsGenericNetlist) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell
                  .Run("read_verilog",
                       {dir.Write("optloop.v", kSynthOptLoopDemo), "optloop"})
                  .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  ASSERT_TRUE(shell.Run("synth_generic").ok);
  const CommandResult issues = shell.Run("synth_issues");
  ASSERT_TRUE(issues.ok) << issues.message;
  EXPECT_NE(issues.payload.find("netlist=generic"), std::string::npos);
}

// A design instantiating a module that does not exist: the
// elaboration succeeds (the cell is opaque), and synth_issues
// reports the unresolved reference as an error with the
// owning module, the instance path, and the cell name.
TEST(CoreCommands, SynthIssuesUnresolvedReferenceReportsError) {
  static const char* kBroken =
      "module broken_top(input wire a, output wire y);\n"
      "  does_not_exist u_missing(.a(a), .y(y));\n"
      "endmodule\n";
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell
                  .Run("read_verilog",
                       {dir.Write("broken.v", kBroken), "broken_top"})
                  .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  const CommandResult issues = shell.Run("synth_issues");
  ASSERT_TRUE(issues.ok) << issues.message;
  EXPECT_NE(issues.payload.find("netlist=elaborated"),
            std::string::npos);
  EXPECT_NE(issues.payload.find(
                "issue severity=error code=unresolved_reference "
                "module=broken_top path=broken_top "
                "target=u_missing"),
            std::string::npos);
  EXPECT_NE(issues.payload.find("does_not_exist"),
            std::string::npos);
}

// Panel rework: the engine-log drain feeds the GUI log
// window. After elaborate the context holds the engine's own
// lines for the command (the hierarchy pass reports its
// steps), and a second drain adds nothing new.
TEST(CoreCommands, EngineLogDrainsAfterElaborate) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(shell
                  .Run("read_verilog",
                       {dir.Write("optloop.v", kSynthOptLoopDemo), "optloop"})
                  .ok);
  ASSERT_TRUE(shell.Run("elaborate").ok);
  EXPECT_FALSE(shell.ctx.engine_log_lines.empty());
  bool saw_hierarchy = false;
  for (const std::string& line : shell.ctx.engine_log_lines) {
    if (line.find("HIERARCHY") != std::string::npos) saw_hierarchy = true;
  }
  EXPECT_TRUE(saw_hierarchy);
  const std::size_t drained = shell.ctx.engine_log_lines.size();
  ASSERT_TRUE(shell.Run("synth_stats").ok);
  EXPECT_EQ(shell.ctx.engine_log_lines.size(), drained);
}

// PR4 validation: set_wire_rc storage and rejection, repair_timing
// option validation, and the wire-RC fail-fast backstop. None of
// these pins loop-dependent outcomes: the repair cases fail
// before any engine setup (option validation runs before any
// file is opened; RC resolution runs before the engine is
// built), and set_wire_rc never touches the engine.

// Synthetic LEF/DEF/liberty for the fail-fast tests, in the
// same shape as the floorplan fixtures: one site, two routing
// layers, one two-pin macro; two placed instances, two placed
// top-level pins, three nets. The LEF deliberately carries no
// RPERSQ/CPERSQDIST/EDGECAPACITANCE data, so no layer is
// complete and RC resolution must fail fast.
const char kRepairLef[] = R"LEF(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.005 ;
SITE core
  CLASS CORE ;
  SIZE 0.46 BY 2.72 ;
END core
LAYER metal1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.19 ;
  WIDTH 0.065 ;
END metal1
LAYER metal2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 0.19 ;
  WIDTH 0.065 ;
END metal2
MACRO BUFX2
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT 0.35 -0.05 0.45 0.05 ;
    END
  END Y
END BUFX2
END LIBRARY
)LEF";

const char kRepairDef[] = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 2 ;
- u1 BUFX2 + PLACED ( 1000 1000 ) N ;
- u2 BUFX2 + PLACED ( 2000 1000 ) N ;
END COMPONENTS
PINS 2 ;
- in1 + NET n_in + DIRECTION INPUT + USE SIGNAL
  + PORT
  + LAYER metal1 ( -50 -50 ) ( 50 50 )
  + PLACED ( 0 5000 ) N ;
- out1 + NET n_out + DIRECTION OUTPUT + USE SIGNAL
  + PORT
  + LAYER metal1 ( -50 -50 ) ( 50 50 )
  + PLACED ( 10000 5000 ) N ;
END PINS
NETS 3 ;
- n_in ( PIN in1 ) ( u1 A ) ;
- n_mid ( u1 Y ) ( u2 A ) ;
- n_out ( u2 Y ) ( PIN out1 ) ;
END NETS
END DESIGN
)DEF";

const char kRepairLiberty[] = R"LIB(library (eos_repair_fixture) {
  delay_model : "table_lookup" ;
  time_unit : "1ns" ;
  voltage_unit : "1v" ;
  capacitive_load_unit (1,pF) ;
  current_unit : "1mA" ;
  pulling_resistance_unit : "1kohm" ;
  leakage_power_unit : "1pW" ;
  nom_process : 1.0 ;
  nom_temperature : 25.0 ;
  nom_voltage : 1.0 ;
  cell (BUFX2) {
    area : 1.0 ;
    pin (A) {
      direction : input ;
      capacitance : 0.001 ;
    }
    pin (Y) {
      direction : output ;
      function : "A" ;
      capacitance : 0.001 ;
      timing () {
        related_pin : "A" ;
        cell_rise (scalar) { values ("0.1") ; }
        cell_fall (scalar) { values ("0.1") ; }
      }
    }
  }
}
)LIB";

TEST(RepairCommands, SetWireRcStoresOverrideAndRejectsBadValues) {
  Shell shell;
  const CommandResult ok =
      shell.Run("set_wire_rc", {"metal1", "1000", "2e-9"});
  ASSERT_TRUE(ok.ok) << ok.message;
  EXPECT_NE(ok.payload.find("layer=metal1"), std::string::npos);
  const auto it = shell.ctx.wire_rc_overrides.find("metal1");
  ASSERT_NE(it, shell.ctx.wire_rc_overrides.end());
  EXPECT_DOUBLE_EQ(it->second.first, 1000.0);
  EXPECT_DOUBLE_EQ(it->second.second, 2e-9);

  const CommandResult wrong_argc =
      shell.Run("set_wire_rc", {"metal1", "1000"});
  ASSERT_FALSE(wrong_argc.ok);
  EXPECT_EQ(wrong_argc.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(wrong_argc.message.find("usage: eos::set_wire_rc"),
            std::string::npos);

  const CommandResult not_numbers =
      shell.Run("set_wire_rc", {"metal1", "abc", "2e-9"});
  ASSERT_FALSE(not_numbers.ok);
  EXPECT_EQ(not_numbers.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(not_numbers.message.find("must be numbers"),
            std::string::npos);

  const CommandResult nan_value =
      shell.Run("set_wire_rc", {"metal1", "nan", "2e-9"});
  ASSERT_FALSE(nan_value.ok);
  EXPECT_NE(nan_value.message.find("must be numbers"),
            std::string::npos);

  const CommandResult zero =
      shell.Run("set_wire_rc", {"metal1", "0", "2e-9"});
  ASSERT_FALSE(zero.ok);
  EXPECT_NE(zero.message.find("must be positive"), std::string::npos);

  const CommandResult negative =
      shell.Run("set_wire_rc", {"metal1", "1000", "-2e-9"});
  ASSERT_FALSE(negative.ok);
  EXPECT_NE(negative.message.find("must be positive"),
            std::string::npos);

  // A rejected call leaves the stored override untouched.
  const auto kept = shell.ctx.wire_rc_overrides.find("metal1");
  ASSERT_NE(kept, shell.ctx.wire_rc_overrides.end());
  EXPECT_DOUBLE_EQ(kept->second.first, 1000.0);
}

TEST(RepairCommands, SetWireRcOverrideClearsOnCloseDesign) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("set_wire_rc", {"metal1", "1000", "2e-9"}).ok);
  ASSERT_EQ(shell.ctx.wire_rc_overrides.size(), 1u);
  ASSERT_TRUE(shell.Run("close_design").ok);
  EXPECT_TRUE(shell.ctx.wire_rc_overrides.empty());
}

TEST(RepairCommands, RepairTimingRejectsAmbiguousAndPartialOptions) {
  TimingTempDir dir;
  const std::string def = dir.Write("top.def", "");
  const std::string lib = dir.Write("cells.lib", "");
  const std::string lef = dir.Write("tech.lef", "");
  const std::string sdc = dir.Write("top.sdc", "");
  Shell shell;

  const CommandResult missing_def =
      shell.Run("repair_timing", {"-liberty", lib, "-lef", lef});
  ASSERT_FALSE(missing_def.ok);
  EXPECT_EQ(missing_def.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(missing_def.message.find("missing required option -def"),
            std::string::npos);

  const CommandResult unknown =
      shell.Run("repair_timing",
                {"-def", def, "-liberty", lib, "-lef", lef, "-bogus"});
  ASSERT_FALSE(unknown.ok);
  EXPECT_NE(unknown.message.find("unknown option"), std::string::npos);

  const CommandResult ambiguous = shell.Run(
      "repair_timing",
      {"-def", def, "-liberty", lib, "-lef", lef, "-sdc", sdc,
       "-clock", "clk", "-period", "1"});
  ASSERT_FALSE(ambiguous.ok);
  EXPECT_NE(ambiguous.message.find("ambiguous"), std::string::npos);

  const CommandResult partial_wire = shell.Run(
      "repair_timing",
      {"-def", def, "-liberty", lib, "-lef", lef, "-wire_r", "1000"});
  ASSERT_FALSE(partial_wire.ok);
  EXPECT_NE(partial_wire.message.find("must be given together"),
            std::string::npos);

  const CommandResult zero_wire = shell.Run(
      "repair_timing",
      {"-def", def, "-liberty", lib, "-lef", lef, "-wire_r", "0",
       "-wire_c", "1e-9"});
  ASSERT_FALSE(zero_wire.ok);
  EXPECT_NE(zero_wire.message.find("must be positive"),
            std::string::npos);

  const CommandResult tie_port_no_tie = shell.Run(
      "repair_timing",
      {"-def", def, "-liberty", lib, "-lef", lef,
       "-tie_port", "LOGIC1/Z"});
  ASSERT_FALSE(tie_port_no_tie.ok);
  EXPECT_NE(tie_port_no_tie.message.find("-tie_port/-separation require -tie"),
            std::string::npos);

  const CommandResult tie_no_port = shell.Run(
      "repair_timing",
      {"-def", def, "-liberty", lib, "-lef", lef, "-tie"});
  ASSERT_FALSE(tie_no_port.ok);
  EXPECT_NE(tie_no_port.message.find("-tie requires at least one -tie_port"),
            std::string::npos);

  const CommandResult end_pin_alone = shell.Run(
      "repair_timing",
      {"-def", def, "-liberty", lib, "-lef", lef, "-hold",
       "-end_pin", "u1/D"});
  ASSERT_FALSE(end_pin_alone.ok);
  EXPECT_NE(end_pin_alone.message.find("must be given together"),
            std::string::npos);
}

TEST(RepairCommands, RepairTimingFailsFastWithoutWireRcData) {
  TimingTempDir dir;
  const std::string lef = dir.Write("tech.lef", kRepairLef);
  const std::string def = dir.Write("top.def", kRepairDef);
  const std::string lib = dir.Write("cells.lib", kRepairLiberty);
  Shell shell;

  // No -wire_layer and no complete tech layer: the default
  // resolution fails before any engine setup.
  const CommandResult no_source = shell.Run(
      "repair_timing",
      {"-def", def, "-liberty", lib, "-lef", lef, "-design"});
  ASSERT_FALSE(no_source.ok) << no_source.payload;
  EXPECT_EQ(no_source.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(no_source.message.find("no wire RC source"),
            std::string::npos);

  // Naming an incomplete layer fails with the layer in the
  // message, still before any engine setup.
  const CommandResult no_layer_data = shell.Run(
      "repair_timing",
      {"-def", def, "-liberty", lib, "-lef", lef, "-design",
       "-wire_layer", "metal1"});
  ASSERT_FALSE(no_layer_data.ok) << no_layer_data.payload;
  EXPECT_EQ(no_layer_data.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(no_layer_data.message.find("no wire RC data for layer metal1"),
            std::string::npos);
}

// ---- SDC command family (Immediate unit 3) -----------------------
// Every new or renamed command gets valid-option and failure-mode
// coverage, not just the happy path.

TEST(SdcCommands, OldCreateClockFormFailsFastNamingBindClock) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  const CommandResult old = shell.Run("create_clock", {"c1", "clk"});
  ASSERT_FALSE(old.ok) << old.payload;
  EXPECT_EQ(old.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(old.message.find("bind_clock"), std::string::npos);
  // The old form created nothing.
  EXPECT_NE(shell.Run("list_constraints").payload.find("constraints=0"),
            std::string::npos);
  // A bare non-flag call that is not the old two-arg form still
  // fails, on the missing period rather than the migration note.
  const CommandResult bare = shell.Run("create_clock", {"clk"});
  ASSERT_FALSE(bare.ok);
  EXPECT_NE(bare.message.find("-period"), std::string::npos);
}

TEST(SdcCommands, CreateClockStoresPeriodWaveformAndName) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk2", "in"}).ok);
  // Period only, single target: the name defaults to the target.
  const CommandResult period_only =
      shell.Run("create_clock", {"-period", "10", "clk"});
  ASSERT_TRUE(period_only.ok) << period_only.message;
  EXPECT_NE(period_only.payload.find("target=clk"), std::string::npos);
  EXPECT_NE(period_only.payload.find("status=bound"), std::string::npos);
  const CommandResult listed = shell.Run("list_constraints");
  EXPECT_NE(listed.payload.find("kind=clock name=clk"),
            std::string::npos);
  EXPECT_NE(listed.payload.find("params=period_ns=10"),
            std::string::npos);
  // Waveform plus an explicit name.
  const CommandResult waved = shell.Run(
      "create_clock",
      {"-period", "10", "-waveform", "0", "5", "-name", "sys", "clk"});
  ASSERT_TRUE(waved.ok) << waved.message;
  EXPECT_NE(waved.payload.find("params=period_ns=10;waveform_rise_ns=0;"
                               "waveform_fall_ns=5"),
            std::string::npos);
  // Several targets need an explicit name and share it.
  const CommandResult multi = shell.Run(
      "create_clock",
      {"-period", "8", "-name", "bus", "clk", "clk2"});
  ASSERT_TRUE(multi.ok) << multi.message;
  EXPECT_EQ(std::count(multi.payload.begin(), multi.payload.end(), '\n'),
            1);
  EXPECT_NE(multi.payload.find("target=clk"), std::string::npos);
  EXPECT_NE(multi.payload.find("target=clk2"), std::string::npos);
  // Payloads carry no name field; the shared name shows in the list.
  const CommandResult multi_listed = shell.Run("list_constraints");
  EXPECT_NE(multi_listed.payload.find("kind=clock name=bus target=clk "),
            std::string::npos);
  EXPECT_NE(multi_listed.payload.find("kind=clock name=bus target=clk2 "),
            std::string::npos);
  // A single-object get_ports wrapper unwraps to the port.
  const CommandResult wrapped = shell.Run(
      "create_clock", {"-period", "4", "[get_ports clk]"});
  ASSERT_TRUE(wrapped.ok) << wrapped.message;
  EXPECT_NE(wrapped.payload.find("target=clk"), std::string::npos);
}

TEST(SdcCommands, CreateClockReplaceByDefaultAndAddKeepsBoth) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(
      shell.Run("create_clock", {"-period", "10", "-name", "sys", "clk"})
          .ok);
  ASSERT_TRUE(
      shell.Run("create_clock", {"-period", "20", "-name", "sys", "clk"})
          .ok);
  const CommandResult replaced = shell.Run("list_constraints");
  EXPECT_EQ(std::count(replaced.payload.begin(), replaced.payload.end(),
                       '\n'),
            0);
  EXPECT_NE(replaced.payload.find("period_ns=20"), std::string::npos);
  EXPECT_EQ(replaced.payload.find("period_ns=10"), std::string::npos);
  // With -add, the redefinition stacks instead of replacing.
  ASSERT_TRUE(shell.Run("create_clock", {"-period", "30", "-name", "sys",
                                         "-add", "clk"})
                  .ok);
  const CommandResult stacked = shell.Run("list_constraints");
  EXPECT_EQ(std::count(stacked.payload.begin(), stacked.payload.end(),
                       '\n'),
            1);
  EXPECT_NE(stacked.payload.find("period_ns=30"), std::string::npos);
  // A construction record sharing the name survives replacement.
  Shell mixed;
  ASSERT_TRUE(mixed.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(mixed.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(mixed.Run("bind_clock", {"sys", "clk"}).ok);
  ASSERT_TRUE(mixed.Run("create_clock", {"-period", "10", "-name", "sys",
                                         "clk"})
                  .ok);
  const CommandResult kept = mixed.Run("list_constraints");
  EXPECT_EQ(std::count(kept.payload.begin(), kept.payload.end(), '\n'), 1);
  EXPECT_NE(kept.payload.find("params=period_ns=10"), std::string::npos);
}

TEST(SdcCommands, CreateClockFailureModes) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk2", "in"}).ok);
  const std::vector<std::vector<std::string>> bad = {
      {"clk"},                                        // no -period
      {"-period", "0", "clk"},                        // not positive
      {"-period", "-5", "clk"},                       // not positive
      {"-period", "abc", "clk"},                      // not a number
      {"-period", "10", "-waveform", "0", "clk"},     // one edge
      {"-period", "10", "-waveform", "0", "5", "7",
       "clk"},  // extra number becomes a positional, multi-target
      {"-period", "10", "-waveform", "5", "5", "clk"},  // rise == fall
      {"-period", "10", "-waveform", "7", "2", "clk"},  // rise > fall
      {"-period", "10", "-waveform", "-1", "5", "clk"},  // negative rise
      {"-period", "10", "-waveform", "0", "11", "clk"},  // fall > period
      {"-period", "10", "clk", "clk2"},               // multi, no -name
      {"-period", "10", "-bogus", "1", "clk"},        // unknown option
      {"-period", "10", "-period", "20", "clk"},      // duplicate option
      {"-period", "10"},                              // no target
      {"-period"},                                    // missing value
      {"-period", "10", "-name", "", "clk"},          // empty name
      {"-period", "10", "[get_ports clk clk2]"},      // multi wrapper
  };
  for (const std::vector<std::string>& args : bad) {
    const CommandResult result = shell.Run("create_clock", args);
    ASSERT_FALSE(result.ok) << "args accepted: " << result.payload;
    EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  }
}

TEST(SdcCommands, CreateGeneratedClockStoresDivisionAndMultiply) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"out", "out"}).ok);
  const CommandResult divided = shell.Run(
      "create_generated_clock",
      {"-source", "clk", "-master_clock", "sys", "-divide_by", "2",
       "out"});
  ASSERT_TRUE(divided.ok) << divided.message;
  EXPECT_NE(divided.payload.find("target=out"), std::string::npos);
  EXPECT_NE(divided.payload.find("source=clk"), std::string::npos);
  EXPECT_NE(divided.payload.find("master_clock=sys"), std::string::npos);
  EXPECT_NE(divided.payload.find("divide_by=2"), std::string::npos);
  const CommandResult multiplied = shell.Run(
      "create_generated_clock",
      {"-source", "clk", "-master_clock", "sys", "-multiply_by", "3",
       "-invert", "-name", "fast", "out"});
  ASSERT_TRUE(multiplied.ok) << multiplied.message;
  EXPECT_NE(multiplied.payload.find("multiply_by=3"), std::string::npos);
  EXPECT_NE(multiplied.payload.find("invert=1"), std::string::npos);
  const CommandResult combinational = shell.Run(
      "create_generated_clock",
      {"-source", "clk", "-combinational", "-name", "comb", "out"});
  ASSERT_TRUE(combinational.ok) << combinational.message;
  EXPECT_NE(combinational.payload.find("combinational=1"),
            std::string::npos);
}

TEST(SdcCommands, CreateGeneratedClockFailureModes) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"out", "out"}).ok);
  const std::vector<std::vector<std::string>> bad = {
      {"-master_clock", "sys", "-divide_by", "2", "out"},  // no -source
      {"-source", "clk", "-divide_by", "2", "out"},  // no master, not comb
      {"-source", "clk", "-master_clock", "sys", "out"},  // no ratio
      {"-source", "clk", "-master_clock", "sys", "-divide_by", "0",
       "out"},
      {"-source", "clk", "-master_clock", "sys", "-multiply_by", "-2",
       "out"},
      {"-source", "clk", "-master_clock", "sys", "-divide_by", "1.5",
       "out"},
      {"-source", "clk", "-master_clock", "sys", "-divide_by", "2"},
      {"-source", "clk", "-combinational", "-divide_by", "2", "-divide_by",
       "4", "out"},  // duplicate option
  };
  for (const std::vector<std::string>& args : bad) {
    const CommandResult result =
        shell.Run("create_generated_clock", args);
    ASSERT_FALSE(result.ok) << "args accepted: " << result.payload;
    EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  }
}

TEST(SdcCommands, ClockGatingCheckStoresSetupHoldAndEdge) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  const CommandResult both = shell.Run(
      "set_clock_gating_check", {"-setup", "0.5", "-hold", "0.2", "clk"});
  ASSERT_TRUE(both.ok) << both.message;
  EXPECT_NE(both.payload.find("setup_ns=0.5"), std::string::npos);
  EXPECT_NE(both.payload.find("hold_ns=0.2"), std::string::npos);
  EXPECT_NE(both.payload.find("edge=both"), std::string::npos);
  const CommandResult high = shell.Run("set_clock_gating_check",
                                       {"-setup", "0.5", "-high", "clk"});
  ASSERT_TRUE(high.ok) << high.message;
  EXPECT_NE(high.payload.find("edge=high"), std::string::npos);
  const std::vector<std::vector<std::string>> bad = {
      {"clk"},                                  // neither setup nor hold
      {"-setup", "0.5", "-high", "-low", "clk"},  // conflicting edges
      {"-setup", "abc", "clk"},                // bad number
      {"-setup", "0.5", "-bogus", "clk"},      // unknown option
      {"-setup", "0.5"},                       // no target
  };
  for (const std::vector<std::string>& args : bad) {
    const CommandResult result = shell.Run("set_clock_gating_check", args);
    ASSERT_FALSE(result.ok) << "args accepted: " << result.payload;
    EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  }
}

TEST(SdcCommands, DataCheckStoresCrossProductWithFromParam) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"a", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"b", "out"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"c", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"d", "out"}).ok);
  const CommandResult single = shell.Run(
      "set_data_check", {"-from", "a", "-to", "b", "-setup", "1"});
  ASSERT_TRUE(single.ok) << single.message;
  EXPECT_NE(single.payload.find("target=b"), std::string::npos);
  EXPECT_NE(single.payload.find("from=a"), std::string::npos);
  EXPECT_NE(single.payload.find("setup_ns=1"), std::string::npos);
  const CommandResult cross = shell.Run(
      "set_data_check",
      {"-from", "a", "-from", "c", "-to", "b", "-to", "d", "-hold",
       "0.25"});
  ASSERT_TRUE(cross.ok) << cross.message;
  EXPECT_EQ(std::count(cross.payload.begin(), cross.payload.end(), '\n'),
            3);
  EXPECT_NE(cross.payload.find("target=d"), std::string::npos);
  EXPECT_NE(cross.payload.find("from=c"), std::string::npos);
  const std::vector<std::vector<std::string>> bad = {
      {"-to", "b", "-setup", "1"},        // no -from
      {"-from", "a", "-setup", "1"},      // no -to
      {"-from", "a", "-to", "b"},         // neither setup nor hold
      {"-from", "a", "-to", "b", "-setup", "1", "stray"},  // positional
  };
  for (const std::vector<std::string>& args : bad) {
    const CommandResult result = shell.Run("set_data_check", args);
    ASSERT_FALSE(result.ok) << "args accepted: " << result.payload;
    EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  }
}

TEST(SdcCommands, MinPulseWidthStoresValueAndEdge) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  const CommandResult both =
      shell.Run("set_min_pulse_width", {"0.3", "clk"});
  ASSERT_TRUE(both.ok) << both.message;
  EXPECT_NE(both.payload.find("value_ns=0.3"), std::string::npos);
  EXPECT_NE(both.payload.find("edge=both"), std::string::npos);
  const CommandResult low =
      shell.Run("set_min_pulse_width", {"0.3", "-low", "clk"});
  ASSERT_TRUE(low.ok) << low.message;
  EXPECT_NE(low.payload.find("edge=low"), std::string::npos);
  const std::vector<std::vector<std::string>> bad = {
      {"clk"},                          // value missing (arity)
      {"0.3"},                          // target missing (arity)
      {"0", "clk"},                     // not positive
      {"-1", "clk"},                    // not positive
      {"abc", "clk"},                   // not a number
      {"0.3", "-high", "-low", "clk"},  // conflicting edges
  };
  for (const std::vector<std::string>& args : bad) {
    const CommandResult result = shell.Run("set_min_pulse_width", args);
    ASSERT_FALSE(result.ok) << "args accepted: " << result.payload;
    EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  }
}

TEST(SdcCommands, SenseStoresTypeAndClockNames) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  const CommandResult positive =
      shell.Run("set_sense", {"-positive", "clk"});
  ASSERT_TRUE(positive.ok) << positive.message;
  EXPECT_NE(positive.payload.find("sense=positive"), std::string::npos);
  const CommandResult stopped = shell.Run(
      "set_sense", {"-stop_propagation", "-clock", "sys", "clk"});
  ASSERT_TRUE(stopped.ok) << stopped.message;
  EXPECT_NE(stopped.payload.find("sense=stop_propagation"),
            std::string::npos);
  EXPECT_NE(stopped.payload.find("sense_clocks=sys"), std::string::npos);
  const std::vector<std::vector<std::string>> bad = {
      {"clk"},                             // no type flag
      {"-positive", "-negative", "clk"},  // two type flags
      {"-positive"},                       // no pin
  };
  for (const std::vector<std::string>& args : bad) {
    const CommandResult result = shell.Run("set_sense", args);
    ASSERT_FALSE(result.ok) << "args accepted: " << result.payload;
    EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  }
}

TEST(SdcCommands, IdealNetworkStoresNoPropagateFlag) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  const CommandResult plain = shell.Run("set_ideal_network", {"clk"});
  ASSERT_TRUE(plain.ok) << plain.message;
  EXPECT_NE(plain.payload.find("target=clk"), std::string::npos);
  EXPECT_EQ(plain.payload.find("params="), std::string::npos);
  const CommandResult held = shell.Run("set_ideal_network",
                                       {"-no_propagate", "clk"});
  ASSERT_TRUE(held.ok) << held.message;
  EXPECT_NE(held.payload.find("no_propagate=1"), std::string::npos);
  const CommandResult none = shell.Run("set_ideal_network", {});
  ASSERT_FALSE(none.ok);
  EXPECT_EQ(none.diag, Diag::kInvalidConfiguration);
}

TEST(SdcCommands, IdealLatencyAndTransitionStoreValueEdgeDelay) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  const CommandResult latency = shell.Run(
      "set_ideal_latency", {"1.5", "-rise", "-min", "clk"});
  ASSERT_TRUE(latency.ok) << latency.message;
  EXPECT_NE(latency.payload.find("value_ns=1.5"), std::string::npos);
  EXPECT_NE(latency.payload.find("edge=rise"), std::string::npos);
  EXPECT_NE(latency.payload.find("delay=min"), std::string::npos);
  const CommandResult transition = shell.Run(
      "set_ideal_transition", {"0.25", "clk"});
  ASSERT_TRUE(transition.ok) << transition.message;
  EXPECT_NE(transition.payload.find("value_ns=0.25"), std::string::npos);
  EXPECT_NE(transition.payload.find("edge=both"), std::string::npos);
  EXPECT_NE(transition.payload.find("delay=both"), std::string::npos);
  const std::vector<std::vector<std::string>> bad = {
      {"-1", "clk"},   // negative latency
      {"abc", "clk"},  // not a number
      {"1.5"},         // no target
  };
  for (const std::vector<std::string>& args : bad) {
    ASSERT_FALSE(shell.Run("set_ideal_latency", args).ok);
    ASSERT_FALSE(shell.Run("set_ideal_transition", args).ok);
  }
}

TEST(SdcCommands, GroupPathStoresEndpointsAndRedefinitionReplaces) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"a", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"b", "out"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"c", "out"}).ok);
  const CommandResult grouped = shell.Run(
      "group_path",
      {"-name", "grp", "-from", "a", "-to", "b", "-weight", "2",
       "-critical_range", "0.5"});
  ASSERT_TRUE(grouped.ok) << grouped.message;
  EXPECT_NE(grouped.payload.find("target=b"), std::string::npos);
  EXPECT_NE(grouped.payload.find("group=grp"), std::string::npos);
  EXPECT_NE(grouped.payload.find("from=a"), std::string::npos);
  EXPECT_NE(grouped.payload.find("weight=2"), std::string::npos);
  EXPECT_NE(grouped.payload.find("critical_range=0.5"),
            std::string::npos);
  // Redefining the same group replaces its rows.
  const CommandResult redefined = shell.Run(
      "group_path", {"-name", "grp", "-to", "c"});
  ASSERT_TRUE(redefined.ok) << redefined.message;
  EXPECT_NE(redefined.payload.find("target=c"), std::string::npos);
  const CommandResult listed = shell.Run("list_constraints");
  EXPECT_EQ(std::count(listed.payload.begin(), listed.payload.end(),
                       '\n'),
            0);
  EXPECT_NE(listed.payload.find("target=c"), std::string::npos);
  const std::vector<std::vector<std::string>> bad = {
      {"-from", "a", "-to", "b"},                    // no -name
      {"-name", "grp"},                              // no endpoints
      {"-name", "grp", "-to", "b", "-critical_range", "-0.5"},
      {"-name", "grp", "-to", "b", "-weight", "abc"},
      {"-name", "grp", "-to", "b", "stray"},          // positional
  };
  for (const std::vector<std::string>& args : bad) {
    const CommandResult result = shell.Run("group_path", args);
    ASSERT_FALSE(result.ok) << "args accepted: " << result.payload;
    EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  }
}

TEST(SdcCommands, MaxTimeBorrowStoresValue) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  const CommandResult borrowed =
      shell.Run("set_max_time_borrow", {"0.4", "clk"});
  ASSERT_TRUE(borrowed.ok) << borrowed.message;
  EXPECT_NE(borrowed.payload.find("value_ns=0.4"), std::string::npos);
  const std::vector<std::vector<std::string>> bad = {
      {"clk"},         // value missing (arity)
      {"0.4"},         // target missing (arity)
      {"-0.1", "clk"}, // negative borrow
      {"abc", "clk"},  // not a number
  };
  for (const std::vector<std::string>& args : bad) {
    const CommandResult result = shell.Run("set_max_time_borrow", args);
    ASSERT_FALSE(result.ok) << "args accepted: " << result.payload;
    EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  }
}

// ---- write_sdc (Immediate unit 8, review item I5) ------------------
// One file, the session constraint set, explicit get_* collections
// for every target. The tests pin the exact emitted text, the merge
// rules, the skip reporting, the failure modes, -compress, and the
// STA round-trip against the hand-written fixture SDC.

// Reads a whole text file back into a string.
static std::string ReadTextFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

// The standard authoring design: ports clk, clk2, d, q, out; one
// dff instance u1 with pins u1/CK, u1/D, u1/Q on nets.
static void BuildWriteSdcDesign(Shell& shell) {
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk2", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"d", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"q", "out"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"out", "out"}).ok);
  ASSERT_TRUE(shell.Run("create_cell", {"dff"}).ok);
  ASSERT_TRUE(shell.Run("create_instance", {"u1", "dff"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"clk_net"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"n1"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"n2"}).ok);
  ASSERT_TRUE(shell.Run("connect", {"u1", "CK", "clk_net"}).ok);
  ASSERT_TRUE(shell.Run("connect", {"u1", "D", "n1"}).ok);
  ASSERT_TRUE(shell.Run("connect", {"u1", "Q", "n2"}).ok);
}

TEST(WriteSdc, EmptySetWritesHeaderOnly) {
  TimingTempDir dir;
  Shell shell;
  const std::string path = dir.Path("empty.sdc");
  const CommandResult written = shell.Run("write_sdc", {path});
  ASSERT_TRUE(written.ok) << written.message;
  EXPECT_EQ(written.payload, "sdc=" + path + " constraints=0 skipped=0");
  const std::string text = ReadTextFile(path);
  EXPECT_NE(text.find("# EOS write_sdc: single-scenario form"),
            std::string::npos);
  EXPECT_NE(text.find("# scenario: (none)"), std::string::npos);
  EXPECT_NE(text.find("# 0 constraints emitted, 0 rows skipped."),
            std::string::npos);
  EXPECT_EQ(text.find("create_clock"), std::string::npos);
  // A second write is byte-identical: no timestamp is written.
  const std::string again_path = dir.Path("empty2.sdc");
  ASSERT_TRUE(shell.Run("write_sdc", {again_path}).ok);
  EXPECT_EQ(text, ReadTextFile(again_path));
}

TEST(WriteSdc, EmitsAllElevenKinds) {
  TimingTempDir dir;
  Shell shell;
  BuildWriteSdcDesign(shell);
  ASSERT_TRUE(shell.Run("create_clock",
                        {"-period", "10", "-waveform", "0", "5", "-name",
                         "sys", "clk"})
                  .ok);
  ASSERT_TRUE(shell.Run("create_generated_clock",
                        {"-source", "clk", "-master_clock", "sys",
                         "-divide_by", "2", "-name", "div", "out"})
                  .ok);
  ASSERT_TRUE(shell.Run("set_clock_gating_check",
                        {"-setup", "0.1", "-hold", "0.2", "-high", "clk"})
                  .ok);
  ASSERT_TRUE(shell.Run("set_data_check",
                        {"-from", "d", "-to", "q", "-setup", "1"})
                  .ok);
  ASSERT_TRUE(
      shell.Run("set_min_pulse_width", {"0.3", "-low", "u1/CK"}).ok);
  ASSERT_TRUE(shell.Run("set_sense", {"-positive", "-clock", "sys", "clk"})
                  .ok);
  ASSERT_TRUE(shell.Run("set_ideal_network", {"-no_propagate", "clk"}).ok);
  ASSERT_TRUE(shell.Run("set_ideal_latency", {"1.5", "-rise", "-min", "clk"})
                  .ok);
  ASSERT_TRUE(shell.Run("set_ideal_transition", {"0.25", "clk"}).ok);
  ASSERT_TRUE(shell.Run("group_path",
                        {"-name", "grp", "-from", "d", "-through", "u1/CK",
                         "-to", "q", "-weight", "2", "-critical_range",
                         "0.5"})
                  .ok);
  ASSERT_TRUE(shell.Run("set_max_time_borrow", {"0.4", "clk"}).ok);
  const std::string path = dir.Path("all.sdc");
  const CommandResult written = shell.Run("write_sdc", {path});
  ASSERT_TRUE(written.ok) << written.message;
  EXPECT_EQ(written.payload, "sdc=" + path + " constraints=11 skipped=0");
  const std::string text = ReadTextFile(path);
  EXPECT_NE(text.find("create_clock -period 10 -waveform {0 5} -name sys "
                      "[get_ports clk]\n"),
            std::string::npos);
  EXPECT_NE(text.find("create_generated_clock -source [get_ports clk] "
                      "-master_clock sys -divide_by 2 -name div "
                      "[get_ports out]\n"),
            std::string::npos);
  EXPECT_NE(text.find("set_clock_gating_check -setup 0.1 -hold 0.2 -high "
                      "[get_ports clk]\n"),
            std::string::npos);
  EXPECT_NE(text.find("set_data_check -from [get_ports d] -to [get_ports "
                      "q] -setup 1\n"),
            std::string::npos);
  EXPECT_NE(text.find("set_min_pulse_width -low 0.3 [get_pins u1/CK]\n"),
            std::string::npos);
  EXPECT_NE(text.find("set_sense -type clock -positive -clock {sys} "
                      "[get_ports clk]\n"),
            std::string::npos);
  EXPECT_NE(text.find("set_ideal_network -no_propagate [get_ports clk]\n"),
            std::string::npos);
  EXPECT_NE(text.find("set_ideal_latency -rise -min 1.5 [get_ports clk]\n"),
            std::string::npos);
  EXPECT_NE(text.find("set_ideal_transition 0.25 [get_ports clk]\n"),
            std::string::npos);
  EXPECT_NE(text.find("group_path -name grp -from [get_ports d] -through "
                      "[get_pins u1/CK] -to [get_ports q] -weight 2 "
                      "-critical_range 0.5\n"),
            std::string::npos);
  EXPECT_NE(text.find("set_max_time_borrow 0.4 [get_ports clk]\n"),
            std::string::npos);
}

TEST(WriteSdc, ClockMergingAndAdd) {
  TimingTempDir dir;
  Shell shell;
  BuildWriteSdcDesign(shell);
  ASSERT_TRUE(shell.Run("create_clock",
                        {"-period", "10", "-name", "sys", "clk", "clk2"})
                  .ok);
  ASSERT_TRUE(shell.Run("create_clock",
                        {"-period", "20", "-name", "sys", "-add", "clk"})
                  .ok);
  const std::string path = dir.Path("merged.sdc");
  const CommandResult written = shell.Run("write_sdc", {path});
  ASSERT_TRUE(written.ok) << written.message;
  EXPECT_EQ(written.payload, "sdc=" + path + " constraints=3 skipped=0");
  const std::string text = ReadTextFile(path);
  // Same name and parameters merge into one line over both targets.
  EXPECT_NE(text.find("create_clock -period 10 -name sys "
                      "[get_ports {clk clk2}]\n"),
            std::string::npos);
  // The -add row keeps its own line, with -add, in id order after it.
  EXPECT_NE(text.find("create_clock -period 20 -name sys -add "
                      "[get_ports clk]\n"),
            std::string::npos);
  // The unmerged single-target form must not appear.
  EXPECT_EQ(text.find("create_clock -period 10 -name sys [get_ports clk]\n"),
            std::string::npos);
}

TEST(WriteSdc, GroupPathMergesByName) {
  TimingTempDir dir;
  Shell shell;
  BuildWriteSdcDesign(shell);
  ASSERT_TRUE(shell.Run("group_path", {"-name", "grp", "-from", "d", "-to",
                                       "q", "-to", "out"})
                  .ok);
  ASSERT_TRUE(shell.Run("group_path",
                        {"-name", "starts", "-from", "d", "-from", "clk"})
                  .ok);
  const std::string path = dir.Path("groups.sdc");
  const CommandResult written = shell.Run("write_sdc", {path});
  ASSERT_TRUE(written.ok) << written.message;
  EXPECT_EQ(written.payload, "sdc=" + path + " constraints=4 skipped=0");
  const std::string text = ReadTextFile(path);
  EXPECT_NE(text.find("group_path -name grp -from [get_ports d] -to "
                      "[get_ports {q out}]\n"),
            std::string::npos);
  EXPECT_NE(text.find("group_path -name starts -from [get_ports {d clk}]\n"),
            std::string::npos);
}

TEST(WriteSdc, DataCheckWithBothMarginsSplits) {
  TimingTempDir dir;
  Shell shell;
  BuildWriteSdcDesign(shell);
  // Different setup and hold margins: upstream takes one margin
  // with -setup/-hold as flags, so the row emits two lines.
  ASSERT_TRUE(shell.Run("set_data_check", {"-from", "d", "-to", "q",
                                           "-setup", "1", "-hold", "0.5"})
                  .ok);
  // Equal margins emit one line with both flags.
  ASSERT_TRUE(shell.Run("set_data_check", {"-from", "d", "-to", "q",
                                           "-setup", "0.5", "-hold", "0.5"})
                  .ok);
  const std::string path = dir.Path("data-check.sdc");
  const CommandResult written = shell.Run("write_sdc", {path});
  ASSERT_TRUE(written.ok) << written.message;
  EXPECT_EQ(written.payload, "sdc=" + path + " constraints=2 skipped=0");
  const std::string text = ReadTextFile(path);
  EXPECT_NE(text.find("set_data_check -from [get_ports d] -to [get_ports "
                      "q] -setup 1\n"),
            std::string::npos);
  EXPECT_NE(text.find("set_data_check -from [get_ports d] -to [get_ports "
                      "q] -hold 0.5\n"),
            std::string::npos);
  EXPECT_NE(text.find("set_data_check -from [get_ports d] -to [get_ports "
                      "q] -setup -hold 0.5\n"),
            std::string::npos);
}

TEST(WriteSdc, SkipsAreReportedNeverSilent) {
  TimingTempDir dir;
  Shell shell;
  BuildWriteSdcDesign(shell);
  ASSERT_TRUE(shell.Run("bind_clock", {"sys", "clk"}).ok);
  ASSERT_TRUE(shell.Run("create_clock", {"-period", "10", "missing"}).ok);
  ASSERT_TRUE(shell.Run("create_clock", {"-period", "10", "clk_net"}).ok);
  ASSERT_TRUE(shell.Run("add_constraint", {"false_path", "fp1", "clk"}).ok);
  const std::string path = dir.Path("skipped.sdc");
  const CommandResult written = shell.Run("write_sdc", {path});
  ASSERT_TRUE(written.ok) << written.message;
  EXPECT_NE(written.payload.find("constraints=0 skipped=4"),
            std::string::npos);
  EXPECT_NE(written.payload.find("skipped: clock sys clk (construction "
                                 "record, not an SDC clock)"),
            std::string::npos);
  EXPECT_NE(written.payload.find("skipped: clock missing missing "
                                 "(unresolved target)"),
            std::string::npos);
  EXPECT_NE(written.payload.find("skipped: clock clk_net clk_net (clock "
                                 "target is not a port or pin)"),
            std::string::npos);
  EXPECT_NE(written.payload.find("skipped: false_path fp1 clk (no stored "
                                 "parameters)"),
            std::string::npos);
  const std::string text = ReadTextFile(path);
  EXPECT_EQ(text.find("create_clock"), std::string::npos);
  EXPECT_NE(text.find("# Skipped rows (not emitted):"), std::string::npos);
  EXPECT_NE(text.find("#   skipped: clock sys clk (construction record, "
                      "not an SDC clock)"),
            std::string::npos);
}

TEST(WriteSdc, FailureModes) {
  TimingTempDir dir;
  Shell shell;
  BuildWriteSdcDesign(shell);
  const CommandResult unknown =
      shell.Run("write_sdc", {"-bogus", "out.sdc"});
  ASSERT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.diag, Diag::kInvalidConfiguration);
  const CommandResult extra = shell.Run("write_sdc", {"a.sdc", "b.sdc"});
  ASSERT_FALSE(extra.ok);
  EXPECT_EQ(extra.diag, Diag::kInvalidConfiguration);
  const CommandResult none = shell.Run("write_sdc", {});
  ASSERT_FALSE(none.ok);
  EXPECT_EQ(none.diag, Diag::kInvalidConfiguration);
  const CommandResult unwritable =
      shell.Run("write_sdc", {"/nonexistent-dir-xyz/out.sdc"});
  ASSERT_FALSE(unwritable.ok);
  EXPECT_EQ(unwritable.diag, Diag::kIoError);
}

TEST(WriteSdc, CompressWritesGzip) {
  TimingTempDir dir;
  Shell shell;
  BuildWriteSdcDesign(shell);
  ASSERT_TRUE(shell.Run("create_clock", {"-period", "10", "clk"}).ok);
  const std::string path = dir.Path("out.sdc.gz");
  const CommandResult written =
      shell.Run("write_sdc", {"-compress", path});
  ASSERT_TRUE(written.ok) << written.message;
  EXPECT_EQ(written.payload, "sdc=" + path + " constraints=1 skipped=0");
  std::ifstream in(path, std::ios::binary);
  ASSERT_TRUE(in.good());
  char magic[2] = {0, 0};
  in.read(magic, 2);
  EXPECT_EQ(static_cast<unsigned char>(magic[0]), 0x1f);
  EXPECT_EQ(static_cast<unsigned char>(magic[1]), 0x8b);
}

TEST(WriteSdc, RoundTripMatchesHandWrittenSdc) {
  TimingTempDir dir;
  Shell author;
  BuildWriteSdcDesign(author);
  ASSERT_TRUE(author.Run("create_clock", {"-period", "10", "clk"}).ok);
  const std::string path = dir.Path("roundtrip.sdc");
  ASSERT_TRUE(author.Run("write_sdc", {path}).ok);
  // A fresh shell reads the emitted file and runs timing on it: the
  // slack must match the hand-written fixture SDC exactly.
  Shell reader;
  ASSERT_TRUE(
      reader.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(reader.Run("read_liberty",
                         {"tt", dir.Write("cells.lib", kTimingLiberty)})
                    .ok);
  ASSERT_TRUE(reader.Run("read_verilog",
                         {dir.Write("top.v", kTimingVerilog), "top"})
                    .ok);
  ASSERT_TRUE(reader.Run("read_sdc", {path}).ok);
  const CommandResult run = reader.Run("run_timing");
  ASSERT_TRUE(run.ok) << run.message;
  const auto payload_number = [&run](const std::string& key) {
    const std::size_t at = run.payload.find(key + "=");
    EXPECT_NE(at, std::string::npos) << "missing " << key;
    if (at == std::string::npos) return 0.0;
    return std::stod(run.payload.substr(at + key.size() + 1));
  };
  EXPECT_NEAR(payload_number("worst_slack_ns"), 9.4, 1e-6);
  EXPECT_NEAR(payload_number("worst_hold_slack_ns"), 0.45, 1e-6);
}

TEST(WriteSdc, CompressedRoundTripThroughSta) {
  TimingTempDir dir;
  Shell author;
  BuildWriteSdcDesign(author);
  ASSERT_TRUE(author.Run("create_clock", {"-period", "10", "clk"}).ok);
  const std::string path = dir.Path("timing.sdc.gz");
  ASSERT_TRUE(author.Run("write_sdc", {"-compress", path}).ok);
  Shell reader;
  ASSERT_TRUE(
      reader.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(reader.Run("read_liberty",
                         {"tt", dir.Write("cells.lib", kTimingLiberty)})
                    .ok);
  ASSERT_TRUE(reader.Run("read_verilog",
                         {dir.Write("top.v", kTimingVerilog), "top"})
                    .ok);
  ASSERT_TRUE(reader.Run("read_sdc", {path}).ok);
  const CommandResult run = reader.Run("run_timing");
  ASSERT_TRUE(run.ok) << run.message;
  const std::size_t at = run.payload.find("worst_slack_ns=");
  ASSERT_NE(at, std::string::npos);
  EXPECT_NEAR(std::stod(run.payload.substr(at + 15)), 9.4, 1e-6);
}

// ---- check_timing (Immediate unit 4, review item I1) ----------------
// Every valid scenario and failure mode gets coverage: the
// verdict rules, each runnable check, the not-run reporting,
// the include/exclude set algebra, and the option failures.

// The shared check_timing fixture: a clock port, two data
// inputs, and one output.
void BuildCheckDesign(Shell& shell) {
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"a", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"b", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"y", "out"}).ok);
}

TEST(CheckTiming, NoDesignFailsFast) {
  Shell shell;
  const CommandResult result = shell.Run("check_timing");
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kUnclosedDesign);
}

TEST(CheckTiming, EmptyDesignIsIncompleteNotPass) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  const CommandResult result = shell.Run("check_timing");
  ASSERT_TRUE(result.ok) << result.message;
  // Nothing to find, but four default checks cannot run, so the
  // verdict is incomplete, never a clean pass.
  EXPECT_NE(result.payload.find("verdict=incomplete"), std::string::npos);
  EXPECT_NE(result.payload.find("check=no_input_delay status=pass "
                               "findings=0"),
            std::string::npos);
  EXPECT_NE(result.payload.find("check=no_output_delay status=pass "
                               "findings=0"),
            std::string::npos);
  EXPECT_NE(result.payload.find("check=unconstrained_endpoints "
                               "status=pass findings=0"),
            std::string::npos);
  EXPECT_NE(result.payload.find("check=generated_clocks status=pass "
                               "findings=0"),
            std::string::npos);
  EXPECT_NE(result.payload.find(
                "check=loops status=not_run findings=0 reason="),
            std::string::npos);
  EXPECT_NE(result.payload.find("check=no_driving_cell status=not_run"),
            std::string::npos);
  EXPECT_NE(result.payload.find("check=partial_input_delay "
                               "status=not_run"),
            std::string::npos);
  EXPECT_NE(result.payload.find("check=pulse_clock_cell_type "
                               "status=not_run"),
            std::string::npos);
}

TEST(CheckTiming, NoInputDelayFlagsUncoveredPorts) {
  Shell shell;
  BuildCheckDesign(shell);
  ASSERT_TRUE(shell.Run("create_clock", {"-period", "10", "clk"}).ok);
  ASSERT_TRUE(
      shell.Run("add_constraint", {"input_delay", "d_a", "a"}).ok);
  const CommandResult result =
      shell.Run("check_timing", {"-verbose"});
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_NE(result.payload.find("verdict=fail"), std::string::npos);
  EXPECT_NE(result.payload.find("check=no_input_delay status=fail "
                               "findings=1"),
            std::string::npos);
  EXPECT_NE(result.payload.find("finding=no_input_delay b missing bound "
                               "input_delay"),
            std::string::npos);
  // The clock port is excluded: it needs no input delay.
  EXPECT_EQ(result.payload.find("finding=no_input_delay clk"),
            std::string::npos);
  // The output is missing its output delay and has no covering
  // constraint at all.
  EXPECT_NE(result.payload.find("check=no_output_delay status=fail "
                               "findings=1"),
            std::string::npos);
  EXPECT_NE(result.payload.find("finding=no_output_delay y missing bound "
                               "output_delay"),
            std::string::npos);
  EXPECT_NE(result.payload.find("check=unconstrained_endpoints "
                               "status=fail findings=1"),
            std::string::npos);
  EXPECT_NE(result.payload.find("finding=unconstrained_endpoints y no "
                               "covering constraint"),
            std::string::npos);
}

TEST(CheckTiming, FullCoveragePassesRunnableChecks) {
  Shell shell;
  BuildCheckDesign(shell);
  ASSERT_TRUE(shell.Run("create_clock", {"-period", "10", "clk"}).ok);
  ASSERT_TRUE(
      shell.Run("add_constraint", {"input_delay", "d_a", "a"}).ok);
  ASSERT_TRUE(
      shell.Run("add_constraint", {"input_delay", "d_b", "b"}).ok);
  ASSERT_TRUE(
      shell.Run("add_constraint", {"output_delay", "d_y", "y"}).ok);
  const CommandResult result = shell.Run("check_timing");
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_NE(result.payload.find("check=no_input_delay status=pass "
                               "findings=0"),
            std::string::npos);
  EXPECT_NE(result.payload.find("check=no_output_delay status=pass "
                               "findings=0"),
            std::string::npos);
  EXPECT_NE(result.payload.find("check=unconstrained_endpoints "
                               "status=pass findings=0"),
            std::string::npos);
  EXPECT_NE(result.payload.find("check=generated_clocks status=pass "
                               "findings=0"),
            std::string::npos);
  // Still incomplete: the four un-runnable default checks did
  // not run, and that is never a clean pass.
  EXPECT_NE(result.payload.find("verdict=incomplete"), std::string::npos);
}

TEST(CheckTiming, UnconstrainedEndpointsExemptions) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"y1", "out"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"y2", "out"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"y3", "out"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"y4", "out"}).ok);
  ASSERT_TRUE(
      shell.Run("add_constraint", {"max_delay", "m1", "y1"}).ok);
  ASSERT_TRUE(
      shell.Run("add_constraint", {"false_path", "f2", "y2"}).ok);
  ASSERT_TRUE(
      shell.Run("add_constraint", {"output_delay", "d3", "y3"}).ok);
  const CommandResult result =
      shell.Run("check_timing", {"-verbose"});
  ASSERT_TRUE(result.ok) << result.message;
  // y1 has a max delay, so it is not unconstrained, but it is
  // still missing its output delay.
  EXPECT_EQ(result.payload.find("finding=unconstrained_endpoints y1"),
            std::string::npos);
  EXPECT_NE(result.payload.find("finding=no_output_delay y1"),
            std::string::npos);
  // y2 is deliberately false-pathed: an exemption, not a
  // missing requirement.
  EXPECT_EQ(result.payload.find("finding=unconstrained_endpoints y2"),
            std::string::npos);
  // y3 has its output delay: neither check flags it.
  EXPECT_EQ(result.payload.find("finding=no_output_delay y3"),
            std::string::npos);
  EXPECT_EQ(result.payload.find("finding=unconstrained_endpoints y3"),
            std::string::npos);
  // y4 has nothing: both checks flag it.
  EXPECT_NE(result.payload.find("finding=no_output_delay y4"),
            std::string::npos);
  EXPECT_NE(result.payload.find("finding=unconstrained_endpoints y4"),
            std::string::npos);
}

TEST(CheckTiming, GeneratedClockMasterAndSource) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"div", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"div2", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"div3", "in"}).ok);
  ASSERT_TRUE(
      shell.Run("create_clock",
                {"-period", "10", "-name", "sys", "clk"})
          .ok);
  // A well-formed generated clock passes.
  ASSERT_TRUE(shell.Run("create_generated_clock",
                        {"-source", "clk", "-master_clock", "sys",
                         "-divide_by", "2", "div"})
                  .ok);
  const CommandResult clean = shell.Run("check_timing");
  ASSERT_TRUE(clean.ok) << clean.message;
  EXPECT_NE(clean.payload.find("check=generated_clocks status=pass "
                              "findings=0"),
            std::string::npos);
  // An undefined master clock is a finding.
  ASSERT_TRUE(shell.Run("create_generated_clock",
                        {"-source", "clk", "-master_clock", "nope",
                         "-divide_by", "2", "div2"})
                  .ok);
  const CommandResult missing_master =
      shell.Run("check_timing", {"-verbose"});
  ASSERT_TRUE(missing_master.ok) << missing_master.message;
  EXPECT_NE(missing_master.payload.find(
                "check=generated_clocks status=fail findings=1"),
            std::string::npos);
  EXPECT_NE(missing_master.payload.find(
                "finding=generated_clocks div2 master clock nope is "
                "not defined"),
            std::string::npos);
  // A source that names no design object is a finding, and a
  // combinational generated clock needs no master.
  ASSERT_TRUE(shell.Run("create_generated_clock",
                        {"-source", "ghost", "-master_clock", "sys",
                         "-divide_by", "2", "div3"})
                  .ok);
  const CommandResult missing_source =
      shell.Run("check_timing", {"-verbose"});
  ASSERT_TRUE(missing_source.ok) << missing_source.message;
  EXPECT_NE(missing_source.payload.find(
                "finding=generated_clocks div3 source ghost does not "
                "exist in the design"),
            std::string::npos);
}

TEST(CheckTiming, GeneratedClockCombinationalNeedsNoMaster) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"div", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_generated_clock",
                        {"-source", "clk", "-combinational", "div"})
                  .ok);
  const CommandResult result = shell.Run("check_timing");
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_NE(result.payload.find("check=generated_clocks status=pass "
                              "findings=0"),
            std::string::npos);
}

TEST(CheckTiming, GeneratedClockUnresolvedTargetIsAFinding) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(
      shell.Run("create_clock",
                {"-period", "10", "-name", "sys", "clk"})
          .ok);
  ASSERT_TRUE(shell.Run("create_generated_clock",
                        {"-source", "clk", "-master_clock", "sys",
                         "-divide_by", "2", "ghost"})
                  .ok);
  const CommandResult result =
      shell.Run("check_timing", {"-verbose"});
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_NE(result.payload.find("check=generated_clocks status=fail "
                              "findings=1"),
            std::string::npos);
  EXPECT_NE(result.payload.find("finding=generated_clocks ghost target "
                              "ghost is unresolved"),
            std::string::npos);
}

TEST(CheckTiming, MulticyclePathIsOptional) {
  Shell shell;
  BuildCheckDesign(shell);
  ASSERT_TRUE(
      shell.Run("add_constraint", {"multicycle", "mc1", "ghost"}).ok);
  // Optional checks do not run by default.
  const CommandResult plain = shell.Run("check_timing");
  ASSERT_TRUE(plain.ok) << plain.message;
  EXPECT_EQ(plain.payload.find("check=multicycle_path"),
            std::string::npos);
  // Included, the unresolved multicycle row is a finding.
  const CommandResult included = shell.Run(
      "check_timing", {"-include", "multicycle_path", "-verbose"});
  ASSERT_TRUE(included.ok) << included.message;
  EXPECT_NE(included.payload.find("check=multicycle_path status=fail "
                                 "findings=1"),
            std::string::npos);
  EXPECT_NE(included.payload.find(
                "finding=multicycle_path mc1 target ghost is unresolved"),
            std::string::npos);
  // A bound multicycle row passes.
  ASSERT_TRUE(
      shell.Run("add_constraint", {"multicycle", "mc2", "y"}).ok);
  const CommandResult bound = shell.Run(
      "check_timing", {"-include", "multicycle_path"});
  ASSERT_TRUE(bound.ok) << bound.message;
  EXPECT_NE(bound.payload.find("check=multicycle_path status=fail "
                              "findings=1"),
            std::string::npos);
}

TEST(CheckTiming, IncludeExcludeAndFailureModes) {
  Shell shell;
  BuildCheckDesign(shell);
  // Excluding a default check removes it from the output.
  const CommandResult excluded =
      shell.Run("check_timing", {"-exclude", "no_input_delay"});
  ASSERT_TRUE(excluded.ok) << excluded.message;
  EXPECT_EQ(excluded.payload.find("check=no_input_delay"),
            std::string::npos);
  EXPECT_NE(excluded.payload.find("check=no_output_delay"),
            std::string::npos);
  // Including an optional check adds it, in table order after
  // the defaults.
  const CommandResult included = shell.Run(
      "check_timing", {"-include", "multicycle_path"});
  ASSERT_TRUE(included.ok) << included.message;
  EXPECT_NE(included.payload.find("check=multicycle_path status=pass "
                                 "findings=0"),
            std::string::npos);
  const std::vector<std::vector<std::string>> bad = {
      {"-include", "no_such_check"}, // unknown check
      {"-exclude", "no_such_check"}, // unknown check
      {"-exclude", "multicycle_path"}, // not in the run
      {"-exclude", "clock_crossing"},  // not in the run
      {"-verbose", "-summary"},        // conflicting forms
      {"stray"},                       // positional argument
      {"-exclude", "generated_clocks", "-exclude", "loops",
       "-exclude", "no_driving_cell", "-exclude", "no_input_delay",
       "-exclude", "no_output_delay", "-exclude", "partial_input_delay",
       "-exclude", "pulse_clock_cell_type", "-exclude",
       "unconstrained_endpoints"},     // empty run set
  };
  for (const std::vector<std::string>& args : bad) {
    const CommandResult result = shell.Run("check_timing", args);
    ASSERT_FALSE(result.ok) << "args accepted: " << result.payload;
    EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  }
  // The unknown-check failure names the known checks.
  const CommandResult unknown =
      shell.Run("check_timing", {"-include", "no_such_check"});
  ASSERT_FALSE(unknown.ok);
  EXPECT_NE(unknown.message.find("known checks: "), std::string::npos);
  EXPECT_NE(unknown.message.find("unconstrained_endpoints"),
            std::string::npos);}

class GuiShowTempDir {
 public:
  GuiShowTempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("eos_registry_gui_show_test_" + std::to_string(counter_++))) {
    std::filesystem::create_directories(path_);
  }
  ~GuiShowTempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  std::string Path(const std::string& name) const {
    return (path_ / name).string();
  }
  void WriteExecutable(const std::string& name,
                       const std::string& text) const {
    const std::filesystem::path file = path_ / name;
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file);
    out << text;
    out.close();
    std::filesystem::permissions(
        file,
        std::filesystem::perms::owner_read |
            std::filesystem::perms::owner_write |
            std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace);
  }

 private:
  static inline int counter_ = 0;
  std::filesystem::path path_;
};

TEST(GuiShow, FindBuildLayoutSibling) {
  GuiShowTempDir dir;
  dir.WriteExecutable("build/apps/eos-gui/eos-gui", "#!/bin/sh\nexit 0\n");
  EXPECT_EQ(FindGuiExecutable(dir.Path("build/apps/eos"), ""),
            dir.Path("build/apps/eos-gui/eos-gui"));
}

TEST(GuiShow, FindInstalledLayoutSibling) {
  GuiShowTempDir dir;
  dir.WriteExecutable("bin/eos-gui", "#!/bin/sh\nexit 0\n");
  EXPECT_EQ(FindGuiExecutable(dir.Path("bin"), ""),
            dir.Path("bin/eos-gui"));
}

TEST(GuiShow, FindPathFallback) {
  GuiShowTempDir dir;
  dir.WriteExecutable("elsewhere/eos-gui", "#!/bin/sh\nexit 0\n");
  EXPECT_EQ(FindGuiExecutable(dir.Path("nowhere"), dir.Path("elsewhere")),
            dir.Path("elsewhere/eos-gui"));
}

TEST(GuiShow, FindPrefersSiblingOverPath) {
  GuiShowTempDir dir;
  dir.WriteExecutable("build/apps/eos-gui/eos-gui", "#!/bin/sh\nexit 0\n");
  dir.WriteExecutable("elsewhere/eos-gui", "#!/bin/sh\nexit 0\n");
  EXPECT_EQ(FindGuiExecutable(dir.Path("build/apps/eos"),
                              dir.Path("elsewhere")),
            dir.Path("build/apps/eos-gui/eos-gui"));
}

TEST(GuiShow, FindSkipsNonExecutable) {
  GuiShowTempDir dir;
  const std::filesystem::path file =
      std::filesystem::path(dir.Path("build/apps/eos-gui/eos-gui"));
  std::filesystem::create_directories(file.parent_path());
  std::ofstream out(file);
  out << "not executable\n";
  out.close();
  EXPECT_EQ(FindGuiExecutable(dir.Path("build/apps/eos"), ""), "");
  EXPECT_EQ(FindGuiExecutable(dir.Path("nowhere"), ""), "");
}

TEST(GuiShow, LaunchSucceedsForTrue) {
  std::string error = "unset";
  EXPECT_TRUE(LaunchGuiDetached("/bin/true", &error));
}

TEST(GuiShow, LaunchFailsForBadInterpreter) {
  GuiShowTempDir dir;
  dir.WriteExecutable("broken", "#!/definitely/not/a/real/interpreter\n");
  std::string error;
  EXPECT_FALSE(LaunchGuiDetached(dir.Path("broken"), &error));
  EXPECT_FALSE(error.empty());
}

TEST(GuiShow, LaunchFailsForMissingPath) {
  std::string error;
  EXPECT_FALSE(LaunchGuiDetached("/definitely/not/a/real/path", &error));
  EXPECT_FALSE(error.empty());
}

TEST(GuiShow, DispatchCallsInstalledHandler) {
  Shell shell;
  bool shown = false;
  SetGuiShowHandler([&shown]() { shown = true; });
  const CommandResult result = shell.Run("gui_show");
  SetGuiShowHandler(nullptr);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.payload, "viewer=shown");
  EXPECT_TRUE(shown);
}

TEST(GuiShow, DispatchRejectsArguments) {
  Shell shell;
  const CommandResult result = shell.Run("gui_show", {"extra"});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
}

TEST(GuiShow, DispatchFailsWhenNoViewerFound) {
  Shell shell;
  SetGuiShowHandler(nullptr);
  GuiShowTempDir dir;
  std::filesystem::create_directories(dir.Path("empty"));
  const char* saved_path = std::getenv("PATH");
  const std::string saved = saved_path == nullptr ? "" : saved_path;
  ::setenv("PATH", dir.Path("empty").c_str(), 1);
  const CommandResult result = shell.Run("gui_show");
  ::setenv("PATH", saved.c_str(), 1);
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kIoError);
  EXPECT_NE(result.message.find("not found"), std::string::npos);
}

// ---- report_clock (Immediate unit 5, review item I2) ----------------
// Every table and failure mode gets coverage: the main table, the
// generated-clock table, the waveform default, derived periods and
// waveforms, unknown masters, construction records, the -skew
// column, and the option failures.

TEST(ReportClock, NoDesignReportsZero) {
  Shell shell;
  const CommandResult result = shell.Run("report_clock");
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.payload, "clocks=0");
}

TEST(ReportClock, EmptyDesignReportsZero) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  const CommandResult result = shell.Run("report_clock");
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.payload, "clocks=0");
}

TEST(ReportClock, SingleClockTable) {
  Shell shell;
  BuildCheckDesign(shell);
  ASSERT_TRUE(shell.Run("create_clock", {"-period", "10", "clk"}).ok);
  const CommandResult result = shell.Run("report_clock");
  ASSERT_TRUE(result.ok) << result.message;
  // The whole payload, pinned exactly: header, separator, one
  // row with the default waveform {0 5}, closing separator.
  // Columns pad to the widest cell with two spaces between.
  EXPECT_EQ(result.payload,
            "Clock  Period  Waveform  Attrs  Sources\n"
            "---------------------------------------\n"
            "clk    10      {0 5}            {clk}  \n"
            "---------------------------------------\n");
  // No generated table and no construction section.
  EXPECT_EQ(result.payload.find("Generated Clock"), std::string::npos);
  EXPECT_EQ(result.payload.find("construction records"),
            std::string::npos);
}

TEST(ReportClock, ExplicitWaveformAndMultipleSources) {
  Shell shell;
  BuildCheckDesign(shell);
  ASSERT_TRUE(shell.Run("create_port", {"clk2", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_clock",
                        {"-period", "10", "-waveform", "2", "5",
                         "-name", "sys", "clk", "clk2"})
                  .ok);
  const CommandResult result = shell.Run("report_clock");
  ASSERT_TRUE(result.ok) << result.message;
  // One row for the shared name, both sources in braces.
  EXPECT_NE(result.payload.find("sys"), std::string::npos);
  EXPECT_NE(result.payload.find("{2 5}"), std::string::npos);
  EXPECT_NE(result.payload.find("{clk clk2}"), std::string::npos);
}

TEST(ReportClock, SameNameStackedPeriodsStaySeparate) {
  Shell shell;
  BuildCheckDesign(shell);
  ASSERT_TRUE(shell.Run("create_port", {"clk2", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_clock",
                        {"-period", "10", "-name", "sys", "clk"})
                  .ok);
  ASSERT_TRUE(shell.Run("create_clock",
                        {"-period", "20", "-name", "sys", "-add",
                         "clk2"})
                  .ok);
  const CommandResult result = shell.Run("report_clock");
  ASSERT_TRUE(result.ok) << result.message;
  // Two rows share the name sys, one per period, never collapsed.
  EXPECT_NE(result.payload.find("10"), std::string::npos);
  EXPECT_NE(result.payload.find("20"), std::string::npos);
  EXPECT_NE(result.payload.find("{clk}"), std::string::npos);
  EXPECT_NE(result.payload.find("{clk2}"), std::string::npos);
}

TEST(ReportClock, GeneratedClockTables) {
  Shell shell;
  BuildCheckDesign(shell);
  ASSERT_TRUE(shell.Run("create_port", {"div", "in"}).ok);
  ASSERT_TRUE(
      shell.Run("create_clock", {"-period", "10", "-name", "sys",
                                 "clk"})
          .ok);
  ASSERT_TRUE(shell.Run("create_generated_clock",
                        {"-source", "clk", "-master_clock", "sys",
                         "-divide_by", "2", "div"})
                  .ok);
  const CommandResult result = shell.Run("report_clock");
  ASSERT_TRUE(result.ok) << result.message;
  // Main table: div is generated, derived period 20 and waveform
  // {0 10}; the legend names the G letter.
  EXPECT_NE(result.payload.find("G - generated_clock"),
            std::string::npos);
  EXPECT_NE(result.payload.find("20"), std::string::npos);
  EXPECT_NE(result.payload.find("{0 10}"), std::string::npos);
  // Generated table: master source clk, generated source {div},
  // master sys, divide_by(2).
  EXPECT_NE(result.payload.find("Generated Clock"), std::string::npos);
  EXPECT_NE(result.payload.find("Master Clock"), std::string::npos);
  EXPECT_NE(result.payload.find("divide_by(2)"), std::string::npos);
  EXPECT_NE(result.payload.find("{div}"), std::string::npos);
}

TEST(ReportClock, GeneratedClockMultiply) {
  Shell shell;
  BuildCheckDesign(shell);
  ASSERT_TRUE(shell.Run("create_port", {"gen", "in"}).ok);
  ASSERT_TRUE(
      shell.Run("create_clock", {"-period", "10", "-name", "sys",
                                 "clk"})
          .ok);
  ASSERT_TRUE(shell.Run("create_generated_clock",
                        {"-source", "clk", "-master_clock", "sys",
                         "-multiply_by", "2", "gen"})
                  .ok);
  const CommandResult result = shell.Run("report_clock");
  ASSERT_TRUE(result.ok) << result.message;
  // Period 10 times 1/2 is 5; waveform {0 5} times 1/2 is {0 2.5}.
  EXPECT_NE(result.payload.find("{0 2.5}"), std::string::npos);
  EXPECT_NE(result.payload.find("multiply_by(2)"), std::string::npos);
}

TEST(ReportClock, GeneratedClockInvert) {
  Shell shell;
  BuildCheckDesign(shell);
  ASSERT_TRUE(shell.Run("create_port", {"gen", "in"}).ok);
  ASSERT_TRUE(
      shell.Run("create_clock", {"-period", "10", "-name", "sys",
                                 "clk"})
          .ok);
  ASSERT_TRUE(shell.Run("create_generated_clock",
                        {"-source", "clk", "-master_clock", "sys",
                         "-divide_by", "1", "-invert", "gen"})
                  .ok);
  const CommandResult result = shell.Run("report_clock");
  ASSERT_TRUE(result.ok) << result.message;
  // Inverting the default waveform {0 5} of period 10 gives
  // {5 10}; the period is unchanged.
  EXPECT_NE(result.payload.find("{5 10}"), std::string::npos);
  EXPECT_NE(result.payload.find("invert"), std::string::npos);
  EXPECT_NE(result.payload.find("divide_by(1)"), std::string::npos);
}

TEST(ReportClock, GeneratedClockChainsThroughMaster) {
  Shell shell;
  BuildCheckDesign(shell);
  ASSERT_TRUE(shell.Run("create_port", {"div2", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"div4", "in"}).ok);
  ASSERT_TRUE(
      shell.Run("create_clock", {"-period", "10", "-name", "sys",
                                 "clk"})
          .ok);
  ASSERT_TRUE(shell.Run("create_generated_clock",
                        {"-source", "clk", "-master_clock", "sys",
                         "-divide_by", "2", "div2"})
                  .ok);
  ASSERT_TRUE(shell.Run("create_generated_clock",
                        {"-source", "div2", "-master_clock", "div2",
                         "-divide_by", "2", "div4"})
                  .ok);
  const CommandResult result = shell.Run("report_clock");
  ASSERT_TRUE(result.ok) << result.message;
  // div2 derives 20 from sys; div4 derives 40 from div2.
  EXPECT_NE(result.payload.find("40"), std::string::npos);
  EXPECT_NE(result.payload.find("{0 20}"), std::string::npos);
}

TEST(ReportClock, GeneratedClockUnknownMasterShowsDash) {
  Shell shell;
  BuildCheckDesign(shell);
  ASSERT_TRUE(shell.Run("create_port", {"div", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_generated_clock",
                        {"-source", "clk", "-master_clock", "ghost",
                         "-divide_by", "2", "div"})
                  .ok);
  const CommandResult result = shell.Run("report_clock");
  ASSERT_TRUE(result.ok) << result.message;
  // The report succeeds: reporting a broken definition is not a
  // failure, check_timing is the gate that flags it. No derived
  // period or waveform appears anywhere in the payload.
  EXPECT_EQ(result.payload.find("20"), std::string::npos);
  EXPECT_EQ(result.payload.find("{0 10}"), std::string::npos);
  // The generated table still lists the definition with its
  // named master.
  EXPECT_NE(result.payload.find("ghost"), std::string::npos);
  EXPECT_NE(result.payload.find("divide_by(2)"), std::string::npos);
}

TEST(ReportClock, GeneratedClockCombinationalNeedsNoMaster) {
  Shell shell;
  BuildCheckDesign(shell);
  ASSERT_TRUE(shell.Run("create_port", {"div", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_generated_clock",
                        {"-source", "clk", "-combinational", "div"})
                  .ok);
  const CommandResult result = shell.Run("report_clock");
  ASSERT_TRUE(result.ok) << result.message;
  // No master means no period source: nothing derives, and no
  // number or waveform appears in the payload at all.
  EXPECT_EQ(result.payload.find("10"), std::string::npos);
  EXPECT_EQ(result.payload.find("{0"), std::string::npos);
  EXPECT_NE(result.payload.find("combinational"), std::string::npos);
}

TEST(ReportClock, SameNameSdcAndGeneratedStaySeparate) {
  Shell shell;
  BuildCheckDesign(shell);
  ASSERT_TRUE(shell.Run("create_port", {"div", "in"}).ok);
  ASSERT_TRUE(
      shell.Run("create_clock", {"-period", "10", "-name", "foo",
                                 "clk"})
          .ok);
  ASSERT_TRUE(shell.Run("create_generated_clock",
                        {"-source", "clk", "-master_clock", "foo",
                         "-divide_by", "2", "-name", "foo", "div"})
                  .ok);
  const CommandResult result = shell.Run("report_clock");
  ASSERT_TRUE(result.ok) << result.message;
  // The SDC row keeps period 10; the generated row derives 20
  // from its own master, not from the SDC row sharing the name.
  EXPECT_NE(result.payload.find("10"), std::string::npos);
  EXPECT_NE(result.payload.find("20"), std::string::npos);
  EXPECT_NE(result.payload.find("{0 10}"), std::string::npos);
}

TEST(ReportClock, UnresolvedClockMarked) {
  Shell shell;
  BuildCheckDesign(shell);
  ASSERT_TRUE(shell.Run("create_clock", {"-period", "10", "ghost"})
                  .ok);
  const CommandResult result = shell.Run("report_clock");
  ASSERT_TRUE(result.ok) << result.message;
  // The unresolved target is marked with the EOS-only u letter,
  // and the legend names it.
  EXPECT_NE(result.payload.find("u - unresolved target (EOS)"),
            std::string::npos);
  EXPECT_NE(result.payload.find("ghost"), std::string::npos);
  EXPECT_NE(result.payload.find("{ghost}"), std::string::npos);
}

TEST(ReportClock, ConstructionRecordListedSeparately) {
  Shell shell;
  BuildCheckDesign(shell);
  ASSERT_TRUE(shell.Run("bind_clock", {"clk_net", "clk"}).ok);
  const CommandResult result = shell.Run("report_clock");
  ASSERT_TRUE(result.ok) << result.message;
  // No period means no main-table row: the whole payload is the
  // construction-records list, pinned exactly.
  EXPECT_EQ(result.payload,
            "Clock construction records (bind_clock, no period):\n"
            "Name     Target\n"
            "---------------\n"
            "clk_net  clk   \n"
            "---------------\n");
}

TEST(ReportClock, SkewColumnReportsNoUncertainty) {
  Shell shell;
  BuildCheckDesign(shell);
  ASSERT_TRUE(shell.Run("create_clock", {"-period", "10", "clk"}).ok);
  const CommandResult skewed = shell.Run("report_clock", {"-skew"});
  ASSERT_TRUE(skewed.ok) << skewed.message;
  EXPECT_NE(skewed.payload.find("Skew"), std::string::npos);
  EXPECT_NE(skewed.payload.find("none"), std::string::npos);
  // The gap is named in the payload, not hidden.
  EXPECT_NE(skewed.payload.find(
                "skew: no clock uncertainty is defined for any "
                "clock (set_clock_uncertainty is not an EOS "
                "command yet)"),
            std::string::npos);
  // Without -skew there is no Skew column and no footer.
  const CommandResult plain = shell.Run("report_clock");
  ASSERT_TRUE(plain.ok) << plain.message;
  EXPECT_EQ(plain.payload.find("Skew"), std::string::npos);
  EXPECT_EQ(plain.payload.find("skew:"), std::string::npos);
}

TEST(ReportClock, UnknownOptionFailsFast) {
  Shell shell;
  BuildCheckDesign(shell);
  const CommandResult result =
      shell.Run("report_clock", {"-nonsense"});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
}

TEST(ReportClock, PositionalArgumentFailsFast) {
  Shell shell;
  BuildCheckDesign(shell);
  const CommandResult result = shell.Run("report_clock", {"clk"});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
}

TEST(ReportClock, SkewGivenTwiceFailsFast) {
  Shell shell;
  BuildCheckDesign(shell);
  const CommandResult result =
      shell.Run("report_clock", {"-skew", "-skew"});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
}

// ---- collections C1-C3 (Immediate unit 11, review items C1-C3) --------

void BuildCollectionDesign(Shell& shell) {
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_cell", {"AND2"}).ok);
  ASSERT_TRUE(shell.Run("create_cell", {"$dff"}).ok);
  ASSERT_TRUE(shell.Run("create_cell", {"$_DFF_PN0_"}).ok);
  ASSERT_TRUE(shell.Run("create_cell", {"BUF_X2"}).ok);
  ASSERT_TRUE(shell.Run("create_instance", {"u_and", "AND2"}).ok);
  ASSERT_TRUE(shell.Run("create_instance", {"u_flop", "$dff"}).ok);
  ASSERT_TRUE(shell.Run("create_instance", {"u_flop2", "$_DFF_PN0_"}).ok);
  ASSERT_TRUE(shell.Run("create_instance", {"u_buf", "BUF_X2"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"a", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"b", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"y", "out"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"io", "inout"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"div", "out"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"n_and"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"n_q"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"clk_net"}).ok);
  ASSERT_TRUE(shell.Run("connect", {"u_and", "A", "n_and"}).ok);
  ASSERT_TRUE(shell.Run("connect", {"u_and", "Y", "n_q"}).ok);
  ASSERT_TRUE(shell.Run("connect", {"u_flop", "D", "n_q"}).ok);
  ASSERT_TRUE(shell.Run("connect", {"u_flop", "Q", "n_and"}).ok);
  ASSERT_TRUE(shell.Run("connect", {"u_flop", "CK", "clk_net"}).ok);
  ASSERT_TRUE(shell.Run("create_clock",
                        {"-period", "10", "-name", "sys", "clk"})
                  .ok);
  ASSERT_TRUE(shell.Run("create_clock",
                        {"-period", "4", "-name", "fast", "a"})
                  .ok);
  ASSERT_TRUE(shell.Run("create_generated_clock",
                        {"-source", "clk", "-master_clock", "sys",
                         "-divide_by", "2", "div"})
                  .ok);
}

// The pin the CoreSetIsComplete test pins: this unit adds twenty
// commands (C1: get_cells, get_nets, get_pins, get_ports,
// get_clocks, all_inputs, all_outputs, all_registers, all_clocks;
// C2: filter_collection, sort_collection, get_attribute,
// set_attribute; C3: add_to_collection, append_to_collection,
// remove_from_collection, compare_collections, index_collection,
// query_objects, foreach_in_collection). get_objects is the
// generic alias and exists already, so it adds no pin.
TEST(Collections, VocabularyCountsAndDeterministicOrder) {
  Shell shell;
  BuildCollectionDesign(shell);

  const CommandResult cells = shell.Run("get_cells");
  ASSERT_TRUE(cells.ok) << cells.message;
  EXPECT_NE(cells.payload.find("collection=eos_coll_1"), std::string::npos);
  EXPECT_NE(cells.payload.find("count=4"), std::string::npos);
  const CommandResult cell_list = shell.Run("list_collection", {"eos_coll_1"});
  ASSERT_TRUE(cell_list.ok);
  EXPECT_EQ(cell_list.payload, "u_and\nu_buf\nu_flop\nu_flop2");

  const CommandResult nets = shell.Run("get_nets");
  ASSERT_TRUE(nets.ok);
  EXPECT_NE(nets.payload.find("count=3"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_2"}).payload,
            "clk_net\nn_and\nn_q");

  const CommandResult pins = shell.Run("get_pins");
  ASSERT_TRUE(pins.ok);
  EXPECT_NE(pins.payload.find("count=5"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_3"}).payload,
            "u_and/A\nu_and/Y\nu_flop/CK\nu_flop/D\nu_flop/Q");

  const CommandResult ports = shell.Run("get_ports");
  ASSERT_TRUE(ports.ok);
  EXPECT_NE(ports.payload.find("count=6"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_4"}).payload,
            "a\nb\nclk\ndiv\nio\ny");

  const CommandResult clocks = shell.Run("get_clocks");
  ASSERT_TRUE(clocks.ok);
  EXPECT_NE(clocks.payload.find("count=3"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_5"}).payload,
            "div\nfast\nsys");

  const CommandResult inputs = shell.Run("all_inputs");
  ASSERT_TRUE(inputs.ok);
  EXPECT_NE(inputs.payload.find("count=3"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_6"}).payload,
            "a\nb\nclk");

  const CommandResult outputs = shell.Run("all_outputs");
  ASSERT_TRUE(outputs.ok);
  EXPECT_NE(outputs.payload.find("count=2"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_7"}).payload,
            "div\ny");

  const CommandResult regs = shell.Run("all_registers");
  ASSERT_TRUE(regs.ok);
  EXPECT_NE(regs.payload.find("count=2"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_8"}).payload,
            "u_flop\nu_flop2");

  const CommandResult all_clocks = shell.Run("all_clocks");
  ASSERT_TRUE(all_clocks.ok);
  EXPECT_NE(all_clocks.payload.find("count=3"), std::string::npos);

  // get_cells means instances; masters come from get_objects cell.
  const CommandResult masters = shell.Run("get_objects", {"cell", "*"});
  ASSERT_TRUE(masters.ok);
  EXPECT_NE(masters.payload.find("count=4"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_10"}).payload,
            "$_DFF_PN0_\n$dff\nAND2\nBUF_X2");

  // get_objects is the generic alias, clock kind included.
  const CommandResult alias = shell.Run("get_objects", {"clock", "s*"});
  ASSERT_TRUE(alias.ok);
  EXPECT_NE(alias.payload.find("count=1"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_11"}).payload, "sys");
}

TEST(Collections, PatternMatchingAndEmptyResultsAreOk) {
  Shell shell;
  BuildCollectionDesign(shell);

  const CommandResult none = shell.Run("get_nets", {"z*"});
  ASSERT_TRUE(none.ok);
  EXPECT_NE(none.payload.find("count=0"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_1"}).payload, "");
  EXPECT_EQ(shell.Run("sizeof_collection", {"eos_coll_1"}).payload,
            "count=0");

  const CommandResult one = shell.Run("get_cells", {"u_flop*"});
  ASSERT_TRUE(one.ok);
  EXPECT_NE(one.payload.find("count=2"), std::string::npos);

  const CommandResult bad_kind = shell.Run("get_objects", {"widget", "*"});
  ASSERT_FALSE(bad_kind.ok);
  EXPECT_EQ(bad_kind.diag, Diag::kInvalidConfiguration);

  const CommandResult bad_option =
      shell.Run("get_nets", {"-bogus", "n*"});
  ASSERT_FALSE(bad_option.ok);
  EXPECT_EQ(bad_option.diag, Diag::kInvalidConfiguration);

  const CommandResult two_patterns =
      shell.Run("get_nets", {"n*", "clk*"});
  ASSERT_FALSE(two_patterns.ok);
  EXPECT_EQ(two_patterns.diag, Diag::kInvalidConfiguration);
}

TEST(Collections, NoDesignFailsFast) {
  Shell shell;
  EXPECT_EQ(shell.Run("get_cells").diag, Diag::kUnclosedDesign);
  EXPECT_EQ(shell.Run("get_clocks").diag, Diag::kUnclosedDesign);
  EXPECT_EQ(shell.Run("all_registers").diag, Diag::kUnclosedDesign);
  EXPECT_EQ(shell.Run("all_clocks").diag, Diag::kUnclosedDesign);
}

TEST(Collections, FilterLanguage) {
  Shell shell;
  BuildCollectionDesign(shell);

  // n_and and n_q each carry two pins; clk_net carries one.
  const CommandResult fanout = shell.Run(
      "get_nets", {"-filter", "fanout >= 2"});
  ASSERT_TRUE(fanout.ok) << fanout.message;
  EXPECT_NE(fanout.payload.find("count=2"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_1"}).payload,
            "n_and\nn_q");

  // &&, ||, and parentheses compose; =~ is glob matching.
  const CommandResult filtered = shell.Run(
      "filter_collection", {"eos_coll_1", "(name =~ n_* && fanout == 2)"});
  ASSERT_TRUE(filtered.ok) << filtered.message;
  EXPECT_NE(filtered.payload.find("count=2"), std::string::npos);

  const CommandResult or_filter = shell.Run(
      "get_ports", {"-filter", "direction == in || direction == out"});
  ASSERT_TRUE(or_filter.ok) << or_filter.message;
  EXPECT_NE(or_filter.payload.find("count=5"), std::string::npos);

  const CommandResult name_glob = shell.Run(
      "get_cells", {"-filter", "cell =~ *FF*"});
  ASSERT_TRUE(name_glob.ok) << name_glob.message;
  EXPECT_NE(name_glob.payload.find("count=1"), std::string::npos);

  const CommandResult bad_attr = shell.Run(
      "get_nets", {"-filter", "nonsense == 1"});
  ASSERT_FALSE(bad_attr.ok);
  EXPECT_EQ(bad_attr.diag, Diag::kInvalidConfiguration);

  const CommandResult bad_syntax = shell.Run(
      "get_nets", {"-filter", "fanout => 2"});
  ASSERT_FALSE(bad_syntax.ok);
  EXPECT_EQ(bad_syntax.diag, Diag::kInvalidConfiguration);

  const CommandResult bad_number = shell.Run(
      "get_nets", {"-filter", "name > 2"});
  ASSERT_FALSE(bad_number.ok);
  EXPECT_EQ(bad_number.diag, Diag::kInvalidConfiguration);
}

TEST(Collections, Attributes) {
  Shell shell;
  BuildCollectionDesign(shell);

  const CommandResult dir = shell.Run("get_attribute", {"clk", "direction"});
  ASSERT_TRUE(dir.ok);
  EXPECT_EQ(dir.payload, "object=clk attribute=direction value=in");

  const CommandResult cell = shell.Run("get_attribute", {"u_flop", "cell"});
  ASSERT_TRUE(cell.ok);
  EXPECT_EQ(cell.payload, "object=u_flop attribute=cell value=$dff");

  const CommandResult is_reg =
      shell.Run("get_attribute", {"u_flop", "is_register"});
  ASSERT_TRUE(is_reg.ok);
  EXPECT_NE(is_reg.payload.find("value=true"), std::string::npos);
  const CommandResult not_reg =
      shell.Run("get_attribute", {"u_and", "is_register"});
  ASSERT_TRUE(not_reg.ok);
  EXPECT_NE(not_reg.payload.find("value=false"), std::string::npos);

  const CommandResult fanout =
      shell.Run("get_attribute", {"n_and", "fanout"});
  ASSERT_TRUE(fanout.ok);
  EXPECT_NE(fanout.payload.find("value=2"), std::string::npos);

  const CommandResult pin = shell.Run("get_attribute", {"u_flop/CK", "pin"});
  ASSERT_TRUE(pin.ok);
  EXPECT_EQ(pin.payload, "object=u_flop/CK attribute=pin value=CK");
  const CommandResult pin_net =
      shell.Run("get_attribute", {"u_flop/CK", "net"});
  ASSERT_TRUE(pin_net.ok);
  EXPECT_NE(pin_net.payload.find("value=clk_net"), std::string::npos);
  const CommandResult pin_inst =
      shell.Run("get_attribute", {"u_flop/CK", "instance"});
  ASSERT_TRUE(pin_inst.ok);
  EXPECT_NE(pin_inst.payload.find("value=u_flop"), std::string::npos);

  const CommandResult period =
      shell.Run("get_attribute", {"sys", "period"});
  ASSERT_TRUE(period.ok);
  EXPECT_EQ(period.payload, "object=sys attribute=period value=10");

  // "div" names both a port and a clock: a bare name resolves to
  // the design object first (the documented order), so the port
  // answers here and the clock is read through its handle below.
  const CommandResult div_port =
      shell.Run("get_attribute", {"div", "direction"});
  ASSERT_TRUE(div_port.ok);
  EXPECT_EQ(div_port.payload, "object=div attribute=direction value=out");
  const CommandResult source =
      shell.Run("get_attribute", {"sys", "source"});
  ASSERT_TRUE(source.ok);
  EXPECT_NE(source.payload.find("value=clk"), std::string::npos);

  // A singleton handle addresses its member.
  ASSERT_TRUE(shell.Run("get_cells", {"u_flop"}).ok);
  const CommandResult via_handle =
      shell.Run("get_attribute", {"eos_coll_1", "cell"});
  ASSERT_TRUE(via_handle.ok);
  EXPECT_NE(via_handle.payload.find("value=$dff"), std::string::npos);

  // A multi-member handle cannot address one object.
  ASSERT_TRUE(shell.Run("get_cells").ok);
  const CommandResult multi =
      shell.Run("get_attribute", {"eos_coll_2", "cell"});
  ASSERT_FALSE(multi.ok);
  EXPECT_EQ(multi.diag, Diag::kInvalidConfiguration);

  // The div clock's attributes are read through its singleton
  // handle: the bare name "div" is the port.
  ASSERT_TRUE(shell.Run("get_clocks", {"div"}).ok);
  const CommandResult gen_period =
      shell.Run("get_attribute", {"eos_coll_3", "period"});
  ASSERT_TRUE(gen_period.ok);
  EXPECT_EQ(gen_period.payload, "object=div attribute=period value=20");
  const CommandResult gen_wave =
      shell.Run("get_attribute", {"eos_coll_3", "waveform"});
  ASSERT_TRUE(gen_wave.ok);
  EXPECT_EQ(gen_wave.payload, "object=div attribute=waveform value={0 10}");
  const CommandResult is_gen =
      shell.Run("get_attribute", {"eos_coll_3", "is_generated"});
  ASSERT_TRUE(is_gen.ok);
  EXPECT_NE(is_gen.payload.find("value=true"), std::string::npos);
  const CommandResult master =
      shell.Run("get_attribute", {"eos_coll_3", "master_clock"});
  ASSERT_TRUE(master.ok);
  EXPECT_NE(master.payload.find("value=sys"), std::string::npos);

  // Unknown attributes and objects fail fast, naming the class set.
  const CommandResult unknown =
      shell.Run("get_attribute", {"u_and", "period"});
  ASSERT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(unknown.message.find("cell, is_register"), std::string::npos);
  const CommandResult missing =
      shell.Run("get_attribute", {"nope", "name"});
  ASSERT_FALSE(missing.ok);
  EXPECT_EQ(missing.diag, Diag::kInvalidConfiguration);
}

TEST(Collections, SetAttributeAndLifetime) {
  Shell shell;
  BuildCollectionDesign(shell);

  const CommandResult set =
      shell.Run("set_attribute", {"u_and", "dont_touch", "true"});
  ASSERT_TRUE(set.ok);
  EXPECT_EQ(set.payload,
            "object=u_and attribute=dont_touch value=true");
  const CommandResult get =
      shell.Run("get_attribute", {"u_and", "dont_touch"});
  ASSERT_TRUE(get.ok);
  EXPECT_EQ(get.payload,
            "object=u_and attribute=dont_touch value=true");

  // A filter over the user attribute sees it; untagged objects
  // read the empty string.
  ASSERT_TRUE(shell.Run("get_cells").ok);
  const CommandResult tagged = shell.Run(
      "filter_collection", {"eos_coll_1", "dont_touch == true"});
  ASSERT_TRUE(tagged.ok);
  EXPECT_NE(tagged.payload.find("count=1"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_2"}).payload, "u_and");

  // Intrinsic attributes are read-only.
  const CommandResult read_only =
      shell.Run("set_attribute", {"u_and", "cell", "OTHER"});
  ASSERT_FALSE(read_only.ok);
  EXPECT_EQ(read_only.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(read_only.message.find("read-only"), std::string::npos);

  // Rollback does not clear collections or user attributes: the
  // ledger revision does not delete design objects.
  ASSERT_TRUE(shell.Run("begin_transaction", {"tag"}).ok);
  ASSERT_TRUE(shell.Run("commit_transaction", {"pass"}).ok);
  ASSERT_TRUE(shell.Run("rollback", {"0"}).ok);
  EXPECT_EQ(shell.Run("sizeof_collection", {"eos_coll_1"}).payload,
            "count=4");
  EXPECT_EQ(shell.Run("get_attribute", {"u_and", "dont_touch"}).payload,
            "object=u_and attribute=dont_touch value=true");

  // Restore clears both: the handle is unknown and the tag is gone.
  const std::string path = "registry_test_collections.eosproj";
  std::remove(path.c_str());
  ASSERT_TRUE(shell.Run("checkpoint", {path}).ok);
  ASSERT_TRUE(shell.Run("restore", {path}).ok);
  EXPECT_EQ(shell.Run("sizeof_collection", {"eos_coll_1"}).diag,
            Diag::kInvalidConfiguration);
  const CommandResult gone =
      shell.Run("get_attribute", {"u_and", "dont_touch"});
  ASSERT_FALSE(gone.ok);
  EXPECT_EQ(gone.diag, Diag::kInvalidConfiguration);
  std::remove(path.c_str());
}

TEST(Collections, Algebra) {
  Shell shell;
  BuildCollectionDesign(shell);

  // Two overlapping instance collections: {u_flop, u_flop2} and all.
  const CommandResult first = shell.Run("get_cells", {"u_flop*"});
  ASSERT_TRUE(first.ok) << first.message;
  EXPECT_NE(first.payload.find("count=2"), std::string::npos);
  const CommandResult second = shell.Run("get_cells", {"u_*"});
  ASSERT_TRUE(second.ok) << second.message;
  EXPECT_NE(second.payload.find("count=4"), std::string::npos);

  const CommandResult added =
      shell.Run("add_to_collection", {"eos_coll_1", "eos_coll_2"});
  ASSERT_TRUE(added.ok);
  EXPECT_NE(added.payload.find("count=4"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_3"}).payload,
            "u_and\nu_buf\nu_flop\nu_flop2");
  // The inputs are untouched by add_to_collection.
  EXPECT_EQ(shell.Run("sizeof_collection", {"eos_coll_1"}).payload,
            "count=2");

  const CommandResult different = shell.Run(
      "compare_collections", {"eos_coll_1", "eos_coll_2"});
  ASSERT_TRUE(different.ok);
  EXPECT_EQ(different.payload,
            "equal=0 count1=2 count2=4 common=2 only1=0 only2=2");
  const CommandResult equal = shell.Run(
      "compare_collections", {"eos_coll_3", "eos_coll_2"});
  ASSERT_TRUE(equal.ok);
  EXPECT_EQ(equal.payload,
            "equal=1 count1=4 count2=4 common=4 only1=0 only2=0");

  const CommandResult removed = shell.Run(
      "remove_from_collection", {"eos_coll_2", "eos_coll_1"});
  ASSERT_TRUE(removed.ok);
  EXPECT_NE(removed.payload.find("count=2"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_4"}).payload,
            "u_and\nu_buf");

  const CommandResult kept = shell.Run(
      "remove_from_collection", {"-intersect", "eos_coll_2", "eos_coll_1"});
  ASSERT_TRUE(kept.ok);
  EXPECT_NE(kept.payload.find("count=2"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_5"}).payload,
            "u_flop\nu_flop2");

  const CommandResult appended =
      shell.Run("append_to_collection", {"eos_coll_1", "eos_coll_2"});
  ASSERT_TRUE(appended.ok);
  EXPECT_EQ(appended.payload, "collection=eos_coll_1 count=4");

  const CommandResult first_member =
      shell.Run("index_collection", {"eos_coll_3", "0"});
  ASSERT_TRUE(first_member.ok);
  EXPECT_NE(first_member.payload.find("count=1"), std::string::npos);
  EXPECT_NE(first_member.payload.find("object=u_and"), std::string::npos);
  const CommandResult out_of_range =
      shell.Run("index_collection", {"eos_coll_3", "4"});
  ASSERT_FALSE(out_of_range.ok);
  EXPECT_EQ(out_of_range.diag, Diag::kInvalidConfiguration);

  const CommandResult query =
      shell.Run("query_objects", {"eos_coll_3", "-limit", "2"});
  ASSERT_TRUE(query.ok);
  EXPECT_EQ(query.payload, "u_and\nu_buf\ntruncated=1 shown=2 total=4");
  const CommandResult query_all = shell.Run("query_objects", {"eos_coll_3"});
  ASSERT_TRUE(query_all.ok);
  EXPECT_EQ(query_all.payload, "u_and\nu_buf\nu_flop\nu_flop2");

  const CommandResult unknown =
      shell.Run("add_to_collection", {"eos_coll_3", "eos_coll_99"});
  ASSERT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.diag, Diag::kInvalidConfiguration);
}

TEST(Collections, SortByAttribute) {
  Shell shell;
  BuildCollectionDesign(shell);

  ASSERT_TRUE(shell.Run("get_clocks").ok);
  const CommandResult by_period =
      shell.Run("sort_collection", {"eos_coll_1", "period"});
  ASSERT_TRUE(by_period.ok);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_2"}).payload,
            "fast\nsys\ndiv");
  const CommandResult down =
      shell.Run("sort_collection", {"eos_coll_1", "period", "-descending"});
  ASSERT_TRUE(down.ok);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_3"}).payload,
            "div\nsys\nfast");
  const CommandResult by_name =
      shell.Run("sort_collection", {"eos_coll_1", "name"});
  ASSERT_TRUE(by_name.ok);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_4"}).payload,
            "div\nfast\nsys");

  const CommandResult unknown =
      shell.Run("sort_collection", {"eos_coll_1", "nonsense"});
  ASSERT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.diag, Diag::kInvalidConfiguration);
}

TEST(Collections, Traversal) {
  Shell shell;
  BuildCollectionDesign(shell);

  const CommandResult cells = shell.Run("get_cells", {"u_*"});
  ASSERT_TRUE(cells.ok);
  EXPECT_NE(cells.payload.find("count=4"), std::string::npos);

  // Pins of the instances.
  const CommandResult pins = shell.Run(
      "get_pins", {"-of_objects", "eos_coll_1"});
  ASSERT_TRUE(pins.ok) << pins.message;
  EXPECT_NE(pins.payload.find("count=5"), std::string::npos);

  // Nets of those pins.
  const CommandResult nets = shell.Run(
      "get_nets", {"-of_objects", "eos_coll_2"});
  ASSERT_TRUE(nets.ok) << nets.message;
  EXPECT_NE(nets.payload.find("count=3"), std::string::npos);

  // Instances with pins on those nets.
  const CommandResult back = shell.Run(
      "get_cells", {"-of_objects", "eos_coll_3"});
  ASSERT_TRUE(back.ok) << back.message;
  EXPECT_NE(back.payload.find("count=2"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_4"}).payload,
            "u_and\nu_flop");

  // Clocks bound to the clock port: sys only. The generated div is
  // bound to its target, the div port, not to its source clk.
  ASSERT_TRUE(shell.Run("get_ports", {"clk"}).ok);
  const CommandResult clocks = shell.Run(
      "get_clocks", {"-of_objects", "eos_coll_5"});
  ASSERT_TRUE(clocks.ok) << clocks.message;
  EXPECT_NE(clocks.payload.find("count=1"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_6"}).payload, "sys");

  // The generated clock traverses from its target port.
  ASSERT_TRUE(shell.Run("get_ports", {"div"}).ok);
  const CommandResult gen = shell.Run(
      "get_clocks", {"-of_objects", "eos_coll_7"});
  ASSERT_TRUE(gen.ok) << gen.message;
  EXPECT_NE(gen.payload.find("count=1"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_8"}).payload, "div");

  // Ports do not traverse, and a clock cannot be a pin source.
  const CommandResult ports_fail = shell.Run(
      "get_ports", {"-of_objects", "eos_coll_1"});
  ASSERT_FALSE(ports_fail.ok);
  EXPECT_EQ(ports_fail.diag, Diag::kInvalidConfiguration);
  const CommandResult clock_source = shell.Run(
      "get_pins", {"-of_objects", "eos_coll_6"});
  ASSERT_FALSE(clock_source.ok);
  EXPECT_EQ(clock_source.diag, Diag::kInvalidConfiguration);
}

TEST(Collections, ForeachWithFakeHost) {
  Shell shell;
  BuildCollectionDesign(shell);
  ASSERT_TRUE(shell.Run("get_cells", {"u_*"}).ok);

  std::vector<std::pair<std::string, std::string>> seen;
  std::vector<std::string> bodies;
  shell.ctx.eval_script = [&seen, &bodies](
      const std::string& var, const std::string& value,
      const std::string& body, std::string& error) {
    seen.emplace_back(var, value);
    bodies.push_back(body);
    return true;
  };
  const CommandResult done = shell.Run(
      "foreach_in_collection", {"eos_coll_1", "cell", "puts $cell"});
  ASSERT_TRUE(done.ok) << done.message;
  EXPECT_EQ(done.payload, "iterations=4");
  ASSERT_EQ(seen.size(), 4u);
  EXPECT_EQ(seen[0].first, "cell");
  EXPECT_EQ(seen[0].second, "u_and");
  EXPECT_EQ(seen[1].second, "u_buf");
  EXPECT_EQ(seen[2].second, "u_flop");
  EXPECT_EQ(seen[3].second, "u_flop2");
  ASSERT_EQ(bodies.size(), 4u);
  EXPECT_EQ(bodies[0], "puts $cell");

  // A body failure fails fast naming the member.
  shell.ctx.eval_script = [](const std::string&, const std::string&,
                             const std::string&, std::string& error) {
    error = "boom";
    return false;
  };
  const CommandResult failed = shell.Run(
      "foreach_in_collection", {"eos_coll_1", "cell", "puts $cell"});
  ASSERT_FALSE(failed.ok);
  EXPECT_NE(failed.message.find("u_and"), std::string::npos);
  EXPECT_NE(failed.message.find("boom"), std::string::npos);

  const CommandResult unknown = shell.Run(
      "foreach_in_collection", {"eos_coll_99", "cell", "puts $cell"});
  ASSERT_FALSE(unknown.ok);
}

TEST(Collections, ForeachWithoutScriptHostFailsFast) {
  Shell shell;
  BuildCollectionDesign(shell);
  ASSERT_TRUE(shell.Run("get_cells").ok);
  // No eval_script installed on this shell: the GUI console shape.
  const CommandResult result = shell.Run(
      "foreach_in_collection", {"eos_coll_1", "cell", "puts $cell"});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(result.message.find("script host"), std::string::npos);
}

TEST(Collections, HandlesAsSdcTargets) {
  Shell shell;
  BuildCollectionDesign(shell);

  // A three-port handle expands to three clock rows sharing the name.
  ASSERT_TRUE(shell.Run("get_ports", {"?"}).ok);
  const CommandResult clocked = shell.Run(
      "create_clock", {"-period", "8", "-name", "bus_clk", "eos_coll_1"});
  ASSERT_TRUE(clocked.ok) << clocked.message;
  EXPECT_NE(clocked.payload.find("target=a"), std::string::npos);
  EXPECT_NE(clocked.payload.find("target=b"), std::string::npos);
  EXPECT_NE(clocked.payload.find("target=y"), std::string::npos);
  const CommandResult bus = shell.Run("get_clocks", {"bus_clk"});
  ASSERT_TRUE(bus.ok);
  EXPECT_NE(bus.payload.find("count=1"), std::string::npos);
  const CommandResult period =
      shell.Run("get_attribute", {"bus_clk", "period"});
  ASSERT_TRUE(period.ok);
  EXPECT_NE(period.payload.find("value=8"), std::string::npos);

  // -source needs exactly one object: a two-member handle fails.
  const CommandResult source = shell.Run(
      "create_generated_clock",
      {"-source", "eos_coll_1", "-master_clock", "sys", "-divide_by", "2",
       "div2"});
  ASSERT_FALSE(source.ok);
  EXPECT_EQ(source.diag, Diag::kInvalidConfiguration);

  // An unknown handle is not a target at all.
  const CommandResult unknown = shell.Run(
      "create_clock", {"-period", "8", "-name", "nope", "eos_coll_99"});
  ASSERT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.diag, Diag::kInvalidConfiguration);
}

TEST(Collections, BusBracketEscaping) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"data[0]"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"data[1]"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"star*"}).ok);

  // Brackets are literal in patterns: data[0] matches only data[0].
  const CommandResult literal = shell.Run("get_nets", {"data[0]"});
  ASSERT_TRUE(literal.ok);
  EXPECT_NE(literal.payload.find("count=1"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_1"}).payload, "data[0]");

  // A glob star still spans the brackets.
  const CommandResult spanned = shell.Run("get_nets", {"data[*]"});
  ASSERT_TRUE(spanned.ok);
  EXPECT_NE(spanned.payload.find("count=2"), std::string::npos);

  // A backslash escapes the star to a literal star.
  const CommandResult escaped = shell.Run("get_nets", {"star\\*"});
  ASSERT_TRUE(escaped.ok);
  EXPECT_NE(escaped.payload.find("count=1"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_3"}).payload, "star*");
}

// get_timing_paths (Immediate unit 12): the command, its
// options, and the collection operations over path members.
void BuildTimingPathsDesign(Shell& shell, const TimingTempDir& dir,
                            const char* liberty = kTimingLiberty,
                            const char* verilog = kTimingVerilog,
                            const char* sdc = kTimingSdc) {
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(shell.Run("read_liberty",
                        {"tt", dir.Write("cells.lib", liberty)})
                    .ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("top.v", verilog), "top"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", sdc)}).ok);
}

// A project design mirroring the timing fixture's pins, so
// traversal over a path collection resolves: r1/Q, u1/A, u1/Y
// and r2/D exist as pins, n1 and n2 as their nets, and clk as a
// clock (the path's group name). The timing session design is a
// separate store, so both designs coexist.
void BuildPathMirrorDesign(Shell& shell) {
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_cell", {"DFF"}).ok);
  ASSERT_TRUE(shell.Run("create_cell", {"BUF"}).ok);
  ASSERT_TRUE(shell.Run("create_instance", {"r1", "DFF"}).ok);
  ASSERT_TRUE(shell.Run("create_instance", {"u1", "BUF"}).ok);
  ASSERT_TRUE(shell.Run("create_instance", {"r2", "DFF"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"d", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"q", "out"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"n1"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"n2"}).ok);
  ASSERT_TRUE(shell.Run("connect", {"r1", "Q", "n1"}).ok);
  ASSERT_TRUE(shell.Run("connect", {"u1", "A", "n1"}).ok);
  ASSERT_TRUE(shell.Run("connect", {"u1", "Y", "n2"}).ok);
  ASSERT_TRUE(shell.Run("connect", {"r2", "D", "n2"}).ok);
  ASSERT_TRUE(shell.Run("create_clock",
                        {"-period", "10", "-name", "clk", "clk"})
                  .ok);
}

TEST(GetTimingPaths, ReturnsCollectionWithSceneLineAndDisplayName) {
  TimingTempDir dir;
  Shell shell;
  BuildTimingPathsDesign(shell, dir);
  const CommandResult result = shell.Run("get_timing_paths", {});
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_NE(result.payload.find("collection=eos_coll_1"),
            std::string::npos);
  EXPECT_NE(result.payload.find("count=1"), std::string::npos);
  EXPECT_NE(result.payload.find("scene=func_tt paths=1"),
            std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_1"}).payload,
            "func_tt:r2/D#1");
  // The path attributes read off the stored report.
  const CommandResult slack =
      shell.Run("get_attribute", {"func_tt:r2/D#1", "slack"});
  ASSERT_TRUE(slack.ok) << slack.message;
  EXPECT_EQ(slack.payload,
            "object=func_tt:r2/D#1 attribute=slack value=9.4");
  const CommandResult startpoint =
      shell.Run("get_attribute", {"func_tt:r2/D#1", "startpoint"});
  ASSERT_TRUE(startpoint.ok) << startpoint.message;
  EXPECT_NE(startpoint.payload.find("value=r1/Q"), std::string::npos);
  const CommandResult group =
      shell.Run("get_attribute", {"func_tt:r2/D#1", "group"});
  ASSERT_TRUE(group.ok) << group.message;
  EXPECT_NE(group.payload.find("value=clk"), std::string::npos);
  const CommandResult points =
      shell.Run("get_attribute", {"func_tt:r2/D#1", "points"});
  ASSERT_TRUE(points.ok) << points.message;
  EXPECT_NE(points.payload.find("value=4"), std::string::npos);
  const CommandResult rank =
      shell.Run("get_attribute", {"func_tt:r2/D#1", "rank"});
  ASSERT_TRUE(rank.ok) << rank.message;
  EXPECT_NE(rank.payload.find("value=1"), std::string::npos);
  // Intrinsic path attributes are read-only.
  const CommandResult read_only =
      shell.Run("set_attribute", {"func_tt:r2/D#1", "slack", "0"});
  ASSERT_FALSE(read_only.ok);
  EXPECT_EQ(read_only.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(read_only.message.find("read-only"), std::string::npos);
}

TEST(GetTimingPaths, DelayTypeMinReturnsHoldPaths) {
  TimingTempDir dir;
  Shell shell;
  BuildTimingPathsDesign(shell, dir);
  const CommandResult result =
      shell.Run("get_timing_paths", {"-delay_type", "min"});
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_NE(result.payload.find("count=1"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_1"}).payload,
            "func_tt:r2/D#1");
  const CommandResult slack =
      shell.Run("get_attribute", {"func_tt:r2/D#1", "slack"});
  ASSERT_TRUE(slack.ok) << slack.message;
  EXPECT_NE(slack.payload.find("value=0.45"), std::string::npos);
  const CommandResult delay_type =
      shell.Run("get_attribute", {"func_tt:r2/D#1", "delay_type"});
  ASSERT_TRUE(delay_type.ok) << delay_type.message;
  EXPECT_NE(delay_type.payload.find("value=min"), std::string::npos);
}

TEST(GetTimingPaths, FilterSlackLessThanOnTightFixture) {
  TimingTempDir dir;
  Shell shell;
  BuildTimingPathsDesign(shell, dir, kTimingLiberty, kTimingVerilog,
                         kTimingSdcTight);
  // The tight clock violates setup: slack -0.1 ns, so the
  // filter keeps the one path.
  const CommandResult result = shell.Run(
      "get_timing_paths", {"-filter", "slack < 0"});
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_NE(result.payload.find("count=1"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_1"}).payload,
            "func_tt:r2/D#1");

  // On the clean clock nothing violates, so the same filter
  // yields an empty collection, never an error.
  TimingTempDir clean_dir;
  Shell clean;
  BuildTimingPathsDesign(clean, clean_dir);
  const CommandResult empty = clean.Run(
      "get_timing_paths", {"-filter", "slack < 0"});
  ASSERT_TRUE(empty.ok) << empty.message;
  EXPECT_NE(empty.payload.find("count=0"), std::string::npos);
  EXPECT_EQ(clean.Run("list_collection", {"eos_coll_1"}).payload, "");
}

TEST(GetTimingPaths, ToAndNworstTwoOnTwoPathFixture) {
  TimingTempDir dir;
  Shell shell;
  BuildTimingPathsDesign(shell, dir, kTimingLibertyTwoPath,
                         kTimingVerilogTwoPath, kTimingSdc);
  const CommandResult result = shell.Run(
      "get_timing_paths", {"-to", "r2/D", "-nworst", "2"});
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_NE(result.payload.find("count=2"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_1"}).payload,
            "func_tt:r2/D#1\nfunc_tt:r2/D#2");
  // Worst first: the slow path through m1/A slacks 9.0 ns.
  const CommandResult slow_slack =
      shell.Run("get_attribute", {"func_tt:r2/D#1", "slack"});
  ASSERT_TRUE(slow_slack.ok) << slow_slack.message;
  EXPECT_NE(slow_slack.payload.find("value=9"), std::string::npos);
}

TEST(GetTimingPaths, GroupSelectionAndUnknownGroup) {
  TimingTempDir dir;
  Shell shell;
  BuildTimingPathsDesign(shell, dir);
  const CommandResult result =
      shell.Run("get_timing_paths", {"-group", "clk"});
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_NE(result.payload.find("count=1"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_1"}).payload,
            "func_tt:r2/D#1");

  const CommandResult unknown =
      shell.Run("get_timing_paths", {"-group", "nope"});
  ASSERT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(unknown.message.find("nope"), std::string::npos);
  EXPECT_NE(unknown.message.find("clk"), std::string::npos);
}

TEST(GetTimingPaths, SceneSelectsOneScenario) {
  TimingTempDir dir;
  Shell shell;
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"})
          .ok);
  ASSERT_TRUE(
      shell.Run("create_scenario", {"func_ss", "func", "ss", "estimate"})
          .ok);
  const std::string liberty = dir.Write("cells.lib", kTimingLiberty);
  ASSERT_TRUE(shell.Run("read_liberty", {"tt", liberty}).ok);
  ASSERT_TRUE(shell.Run("read_liberty", {"ss", liberty}).ok);
  ASSERT_TRUE(shell.Run("read_verilog",
                        {dir.Write("top.v", kTimingVerilog), "top"})
                    .ok);
  ASSERT_TRUE(shell.Run("read_sdc", {dir.Write("top.sdc", kTimingSdc)}).ok);
  const CommandResult one =
      shell.Run("get_timing_paths", {"-scene", "func_ss"});
  ASSERT_TRUE(one.ok) << one.message;
  EXPECT_NE(one.payload.find("count=1"), std::string::npos);
  EXPECT_NE(one.payload.find("scene=func_ss paths=1"),
            std::string::npos);
  EXPECT_EQ(one.payload.find("scene=func_tt"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_1"}).payload,
            "func_ss:r2/D#1");

  const CommandResult both = shell.Run("get_timing_paths", {});
  ASSERT_TRUE(both.ok) << both.message;
  EXPECT_NE(both.payload.find("count=2"), std::string::npos);
  EXPECT_NE(both.payload.find("scene=func_ss paths=1"),
            std::string::npos);
  EXPECT_NE(both.payload.find("scene=func_tt paths=1"),
            std::string::npos);
}

TEST(GetTimingPaths, UnknownSceneEndpointAndFromFailFast) {
  TimingTempDir dir;
  Shell shell;
  // The project design comes first: create_design resets the
  // timing session, so building it after the timing design
  // would wipe the scenarios, Liberty, and SDC.
  // A session clock named "sys" on the clk port, for the
  // -through clock check below.
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_clock",
                        {"-period", "10", "-name", "sys", "clk"})
                    .ok);
  BuildTimingPathsDesign(shell, dir);
  const CommandResult scene =
      shell.Run("get_timing_paths", {"-scene", "func_xx"});
  ASSERT_FALSE(scene.ok);
  EXPECT_EQ(scene.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(scene.message.find("func_xx"), std::string::npos);

  const CommandResult endpoint =
      shell.Run("get_timing_paths", {"-to", "r9/D"});
  ASSERT_FALSE(endpoint.ok);
  EXPECT_EQ(endpoint.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(endpoint.message.find("r9/D"), std::string::npos);

  const CommandResult from =
      shell.Run("get_timing_paths", {"-from", "nope"});
  ASSERT_FALSE(from.ok);
  EXPECT_EQ(from.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(from.message.find("nope"), std::string::npos);

  // A clock under -through fails naming it as a clock. The
  // clock must be named differently from any pin: "clk" names
  // the top-level port pin first, so the test clock is "sys".
  const CommandResult through =
      shell.Run("get_timing_paths", {"-through", "sys"});
  ASSERT_FALSE(through.ok);
  EXPECT_EQ(through.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(through.message.find("sys"), std::string::npos);
  EXPECT_NE(through.message.find("cannot be a -through object"),
            std::string::npos);

  // Bad options fail fast, as with the other commands.
  EXPECT_FALSE(shell.Run("get_timing_paths", {"-bogus"}).ok);
  EXPECT_FALSE(shell.Run("get_timing_paths", {"top"}).ok);
  EXPECT_FALSE(shell.Run("get_timing_paths", {"-nworst", "0"}).ok);
  EXPECT_FALSE(shell.Run("get_timing_paths", {"-max_paths", "0"}).ok);
  EXPECT_FALSE(
      shell.Run("get_timing_paths", {"-delay_type", "ave"}).ok);
}

TEST(GetTimingPaths, EmptyResultIsAnEmptyCollection) {
  // -from names a real object (the primary input d) with no
  // clocked path to any endpoint: an empty collection, never an
  // error.
  TimingTempDir dir;
  Shell shell;
  BuildTimingPathsDesign(shell, dir);
  const CommandResult result =
      shell.Run("get_timing_paths", {"-from", "d"});
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_NE(result.payload.find("collection=eos_coll_1"),
            std::string::npos);
  EXPECT_NE(result.payload.find("count=0"), std::string::npos);
  EXPECT_EQ(result.payload.find("scene="), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_1"}).payload, "");
}

TEST(GetTimingPaths, CollectionTraversalOverPaths) {
  TimingTempDir dir;
  Shell shell;
  // Mirror first: create_design resets the timing session, so
  // the project design must exist before the timing design
  // is loaded.
  BuildPathMirrorDesign(shell);
  BuildTimingPathsDesign(shell, dir);
  ASSERT_TRUE(shell.Run("get_timing_paths", {}).ok);

  // Every point pin of the path.
  const CommandResult pins = shell.Run(
      "get_pins", {"-of_objects", "eos_coll_1"});
  ASSERT_TRUE(pins.ok) << pins.message;
  EXPECT_NE(pins.payload.find("count=4"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_2"}).payload,
            "r1/Q\nr2/D\nu1/A\nu1/Y");

  // Nets of the path's pins.
  const CommandResult nets = shell.Run(
      "get_nets", {"-of_objects", "eos_coll_1"});
  ASSERT_TRUE(nets.ok) << nets.message;
  EXPECT_NE(nets.payload.find("count=2"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_3"}).payload,
            "n1\nn2");

  // Owning instances of the path's pins.
  const CommandResult cells = shell.Run(
      "get_cells", {"-of_objects", "eos_coll_1"});
  ASSERT_TRUE(cells.ok) << cells.message;
  EXPECT_NE(cells.payload.find("count=3"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_4"}).payload,
            "r1\nr2\nu1");

  // The clock named by the path's group.
  const CommandResult clocks = shell.Run(
      "get_clocks", {"-of_objects", "eos_coll_1"});
  ASSERT_TRUE(clocks.ok) << clocks.message;
  EXPECT_NE(clocks.payload.find("count=1"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_5"}).payload,
            "clk");

  // Ports do not traverse from a path.
  const CommandResult ports_fail = shell.Run(
      "get_ports", {"-of_objects", "eos_coll_1"});
  ASSERT_FALSE(ports_fail.ok);
  EXPECT_EQ(ports_fail.diag, Diag::kInvalidConfiguration);

  // "cell" is not a path attribute.
  const CommandResult unknown_attr =
      shell.Run("get_attribute", {"func_tt:r2/D#1", "cell"});
  ASSERT_FALSE(unknown_attr.ok);
  EXPECT_EQ(unknown_attr.diag, Diag::kInvalidConfiguration);

  // A path is not an SDC target: passing its handle where a
  // design object is needed fails fast naming the reason.
  const CommandResult sdc_fail = shell.Run(
      "create_clock",
      {"-period", "8", "-name", "path_clk", "eos_coll_1"});
  ASSERT_FALSE(sdc_fail.ok);
  EXPECT_EQ(sdc_fail.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(sdc_fail.message.find("no SDC target"), std::string::npos);
}

TEST(GetTimingPaths, CollectionOpsOverPaths) {
  TimingTempDir dir;
  Shell shell;
  // Mirror first: create_design resets the timing session, so
  // the project design must exist before the timing design
  // is loaded.
  BuildPathMirrorDesign(shell);
  BuildTimingPathsDesign(shell, dir, kTimingLibertyTwoPath,
                         kTimingVerilogTwoPath, kTimingSdc);
  ASSERT_TRUE(
      shell.Run("get_timing_paths", {"-to", "r2/D", "-nworst", "2"})
          .ok);

  // Sort by slack, worst first; descending reverses it.
  const CommandResult sorted =
      shell.Run("sort_collection", {"eos_coll_1", "slack"});
  ASSERT_TRUE(sorted.ok) << sorted.message;
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_2"}).payload,
            "func_tt:r2/D#1\nfunc_tt:r2/D#2");
  const CommandResult down = shell.Run(
      "sort_collection", {"eos_coll_1", "slack", "-descending"});
  ASSERT_TRUE(down.ok) << down.message;
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_3"}).payload,
            "func_tt:r2/D#2\nfunc_tt:r2/D#1");

  // Filter by slack keeps only the slow path.
  const CommandResult filtered = shell.Run(
      "filter_collection", {"eos_coll_1", "slack < 9.2"});
  ASSERT_TRUE(filtered.ok) << filtered.message;
  EXPECT_NE(filtered.payload.find("count=1"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_4"}).payload,
            "func_tt:r2/D#1");

  // Index to a singleton, then read its attributes through the
  // handle.
  ASSERT_TRUE(
      shell.Run("index_collection", {"eos_coll_2", "0"}).ok);
  const CommandResult slack =
      shell.Run("get_attribute", {"eos_coll_5", "slack"});
  ASSERT_TRUE(slack.ok) << slack.message;
  EXPECT_NE(slack.payload.find("value=9"), std::string::npos);
  const CommandResult startpoint =
      shell.Run("get_attribute", {"eos_coll_5", "startpoint"});
  ASSERT_TRUE(startpoint.ok) << startpoint.message;
  EXPECT_NE(startpoint.payload.find("value=r1/Q"), std::string::npos);

  // foreach binds the loop variable to the path display name;
  // the attribute is read off that name afterwards.
  std::vector<std::string> seen;
  shell.ctx.eval_script = [&seen](const std::string& var,
                                   const std::string& value,
                                   const std::string& body,
                                   std::string& error) {
    EXPECT_EQ(var, "path");
    seen.push_back(value);
    return true;
  };
  const CommandResult done = shell.Run(
      "foreach_in_collection", {"eos_coll_1", "path", "puts $path"});
  ASSERT_TRUE(done.ok) << done.message;
  EXPECT_EQ(done.payload, "iterations=2");
  ASSERT_EQ(seen.size(), 2u);
  EXPECT_EQ(seen[0], "func_tt:r2/D#1");
  EXPECT_EQ(seen[1], "func_tt:r2/D#2");
  const CommandResult loop_slack =
      shell.Run("get_attribute", {seen[0], "slack"});
  ASSERT_TRUE(loop_slack.ok) << loop_slack.message;
  EXPECT_NE(loop_slack.payload.find("value=9"), std::string::npos);

  // A user attribute round-trips on the path member.
  const CommandResult set = shell.Run(
      "set_attribute", {"func_tt:r2/D#1", "reviewed", "yes"});
  ASSERT_TRUE(set.ok) << set.message;
  EXPECT_EQ(set.payload,
            "object=func_tt:r2/D#1 attribute=reviewed value=yes");
  const CommandResult get = shell.Run(
      "get_attribute", {"func_tt:r2/D#1", "reviewed"});
  ASSERT_TRUE(get.ok) << get.message;
  EXPECT_EQ(get.payload,
            "object=func_tt:r2/D#1 attribute=reviewed value=yes");

  // Restore clears the path collections (and the tag with them).
  const std::string path = "registry_test_timing_paths.eosproj";
  std::remove(path.c_str());
  ASSERT_TRUE(shell.Run("checkpoint", {path}).ok);
  ASSERT_TRUE(shell.Run("restore", {path}).ok);
  EXPECT_EQ(shell.Run("sizeof_collection", {"eos_coll_1"}).diag,
            Diag::kInvalidConfiguration);
  const CommandResult gone = shell.Run(
      "get_attribute", {"func_tt:r2/D#1", "reviewed"});
  ASSERT_FALSE(gone.ok);
  EXPECT_EQ(gone.diag, Diag::kInvalidConfiguration);
  std::remove(path.c_str());
}

TEST(GetTimingPaths, SetupAndHoldMergeKeepsBothDistinct) {
  TimingTempDir dir;
  Shell shell;
  BuildTimingPathsDesign(shell, dir);
  ASSERT_TRUE(shell.Run("get_timing_paths", {}).ok);
  ASSERT_TRUE(
      shell.Run("get_timing_paths", {"-delay_type", "min"}).ok);
  // The two paths share a display name (same scene, endpoint
  // and rank) but differ in kind, so the merge keeps both.
  const CommandResult merged = shell.Run(
      "add_to_collection", {"eos_coll_1", "eos_coll_2"});
  ASSERT_TRUE(merged.ok) << merged.message;
  EXPECT_NE(merged.payload.find("count=2"), std::string::npos);
  EXPECT_EQ(shell.Run("list_collection", {"eos_coll_3"}).payload,
            "func_tt:r2/D#1\nfunc_tt:r2/D#1");
  // Sorted by slack the hold path (0.45 ns) comes first; the
  // singletons prove the two members are the two kinds.
  ASSERT_TRUE(
      shell.Run("sort_collection", {"eos_coll_3", "slack"}).ok);
  ASSERT_TRUE(
      shell.Run("index_collection", {"eos_coll_4", "0"}).ok);
  const CommandResult hold_type =
      shell.Run("get_attribute", {"eos_coll_5", "delay_type"});
  ASSERT_TRUE(hold_type.ok) << hold_type.message;
  EXPECT_NE(hold_type.payload.find("value=min"), std::string::npos);
  const CommandResult hold_slack =
      shell.Run("get_attribute", {"eos_coll_5", "slack"});
  ASSERT_TRUE(hold_slack.ok) << hold_slack.message;
  EXPECT_NE(hold_slack.payload.find("value=0.45"), std::string::npos);
  ASSERT_TRUE(
      shell.Run("index_collection", {"eos_coll_4", "1"}).ok);
  const CommandResult setup_type =
      shell.Run("get_attribute", {"eos_coll_6", "delay_type"});
  ASSERT_TRUE(setup_type.ok) << setup_type.message;
  EXPECT_NE(setup_type.payload.find("value=max"), std::string::npos);
  const CommandResult setup_slack =
      shell.Run("get_attribute", {"eos_coll_6", "slack"});
  ASSERT_TRUE(setup_slack.ok) << setup_slack.message;
  EXPECT_NE(setup_slack.payload.find("value=9.4"), std::string::npos);
}

TEST(ReportBottleneck, ReadsPathsFromCollection) {
  TimingTempDir dir;
  Shell shell;
  // Mirror first: create_design resets the timing session, so
  // the project design must exist before the timing design
  // is loaded.
  BuildPathMirrorDesign(shell);
  BuildTimingPathsDesign(shell, dir, kTimingLiberty, kTimingVerilog,
                         kTimingSdcTight);
  // No run_timing, no capture: without -paths this fails (see
  // RequiresTimingRun). With -paths it ranks from the
  // collection instead.
  ASSERT_TRUE(shell.Run("get_timing_paths", {}).ok);
  const CommandResult report =
      shell.Run("report_bottleneck", {"-paths", "eos_coll_1"});
  ASSERT_TRUE(report.ok) << report.message;
  EXPECT_NE(report.payload.find("scene=all delay_type=max paths=1 "
                                "cells=3 report=bottleneck_report_all"),
            std::string::npos);
  EXPECT_NE(report.payload.find("scene=all rank=1 cell=r1 type=DFF "
                                "paths=1 worst_slack=-0.1"),
            std::string::npos);
  EXPECT_NE(report.payload.find("Ranking from the path collection "
                                "eos_coll_1."),
            std::string::npos);

  // -scene resolves against the collection's scenes.
  const CommandResult scoped = shell.Run(
      "report_bottleneck", {"-paths", "eos_coll_1", "-scene", "func_tt"});
  ASSERT_TRUE(scoped.ok) << scoped.message;
  EXPECT_NE(scoped.payload.find("scene=func_tt"), std::string::npos);
  const CommandResult unknown_scene = shell.Run(
      "report_bottleneck",
      {"-paths", "eos_coll_1", "-scene", "func_xx"});
  ASSERT_FALSE(unknown_scene.ok);
  EXPECT_NE(unknown_scene.message.find("func_xx"), std::string::npos);

  // An unknown handle, or a collection that holds no paths,
  // fails fast.
  const CommandResult unknown_handle =
      shell.Run("report_bottleneck", {"-paths", "eos_coll_99"});
  ASSERT_FALSE(unknown_handle.ok);
  EXPECT_NE(unknown_handle.message.find("eos_coll_99"),
            std::string::npos);
  ASSERT_TRUE(shell.Run("get_cells", {"r1"}).ok);
  const CommandResult wrong_kind = shell.Run(
      "report_bottleneck", {"-paths", "eos_coll_2"});
  ASSERT_FALSE(wrong_kind.ok);
  EXPECT_NE(wrong_kind.message.find("path collection"),
            std::string::npos);
}

}  // namespace
}  // namespace eos::ui
