// Unit tests: ui/registry and the core command set.
#include "eos/ui/registry.h"

#include <algorithm>
#include <cstdio>
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
  // adds synth_opt; S4 adds read_filelist.
  EXPECT_EQ(shell.registry.Names().size(), 43u);
  EXPECT_NE(shell.registry.Find("read_filelist"), nullptr);
  EXPECT_NE(shell.registry.Find("synth_opt"), nullptr);
  EXPECT_NE(shell.registry.Find("create_design"), nullptr);
  EXPECT_NE(shell.registry.Find("restore"), nullptr);
  EXPECT_NE(shell.registry.Find("report_timing"), nullptr);
  EXPECT_NE(shell.registry.Find("synth_map"), nullptr);
  EXPECT_NE(shell.registry.Find("help"), nullptr);
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
  const CommandResult bound = shell.Run("create_clock", {"c1", "clk"});
  ASSERT_TRUE(bound.ok);
  EXPECT_NE(bound.payload.find("status=bound"), std::string::npos);

  const CommandResult unresolved =
      shell.Run("create_clock", {"c2", "missing_clk"});
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
  ASSERT_TRUE(shell.Run("create_clock", {"c1", "clk"}).ok);
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
  ASSERT_TRUE(shell.Run("create_clock", {"c1", "clk"}).ok);
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
  ASSERT_TRUE(uncovered.Run("create_clock", {"c1", "missing_clk"}).ok);
  ASSERT_TRUE(uncovered.Run("register_report", {"timing", "pass"}).ok);
  const CommandResult blocked = uncovered.Run("export_check", {"timing"});
  ASSERT_FALSE(blocked.ok);
  EXPECT_NE(blocked.payload.find("blocker=coverage_incomplete"),
            std::string::npos);

  // Proof: a report registered without a pass blocks on its proof.
  Shell unproven;
  ASSERT_TRUE(unproven.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(unproven.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(unproven.Run("create_clock", {"c1", "clk"}).ok);
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
  // A second run cannot re-register the same report names.
  const CommandResult again = shell.Run("run_timing");
  EXPECT_FALSE(again.ok);
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
  EXPECT_LE(shell.ctx.synth.generic_after_and,
            shell.ctx.synth.generic_before_and);
  EXPECT_LE(shell.ctx.synth.generic_after_levels,
            shell.ctx.synth.generic_before_levels);
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

}  // namespace
}  // namespace eos::ui
