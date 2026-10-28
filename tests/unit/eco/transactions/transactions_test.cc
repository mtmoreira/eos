// Unit tests: eco/transactions, the ECO transaction engine.
//
// The verdict tests are pure: scene metric maps in, verdict out.
// The protocol tests run the engine end to end on the synthetic
// sizing fixture (flip-flop, weak buffer, flip-flop, 1 ns clock):
// by construction the weak buffer makes the setup slack -0.1 ns,
// and the strong variant makes it +0.5 ns, so a sizing edit that
// swaps to the strong variant must commit. The rollback test adds
// a second scene whose Liberty gives the strong variant so little
// delay that the same swap creates a hold violation there (hold
// slack +0.15 ns before, -0.10 ns after, by the fixture's numbers):
// the all-scene rule must refuse it, and the design and the ledger
// must both end as if the edit was never attempted. The ledger
// wrap shown here is the caller's part of the protocol (L5 owns
// the ledger): begin, run the ECO, commit or abort with the
// outcome.
#include "eos/eco/transactions.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "eos/eco/timing_eco.h"
#include "eos/session/ledger.h"

namespace eos::eco {
namespace {

using foundation::ErrorCode;

const char kLef[] = R"LEF(VERSION 5.8 ;
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

const char kDef[] = R"DEF(VERSION 5.8 ;
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

// One Liberty text with the fixture's numbers as parameters: the
// buffer delays per variant and the hold constraint are what
// differ between the setup corner and the hold corner.
std::string Liberty(double hold_constraint, double buf_x1_delay,
                    double buf_x2_delay) {
  const std::string header = R"LIB(library (eos_eco_fixture) {
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
        rise_constraint (scalar) { values ("HOLD"); }
        fall_constraint (scalar) { values ("HOLD"); }
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
)LIB";
  const auto buffer = [](const std::string& name, double delay) {
    return "  cell (" + name + ") {\n"
           "    area : 2.0;\n"
           "    pin (A) {\n"
           "      direction : input;\n"
           "      capacitance : 0.001;\n"
           "    }\n"
           "    pin (Y) {\n"
           "      direction : output;\n"
           "      function : \"A\";\n"
           "      timing () {\n"
           "        related_pin : \"A\";\n"
           "        timing_sense : positive_unate;\n"
           "        cell_rise (scalar) { values (\"" +
           std::to_string(delay) +
           "\"); }\n"
           "        cell_fall (scalar) { values (\"" +
           std::to_string(delay) +
           "\"); }\n"
           "        rise_transition (scalar) { values (\"0.05\"); }\n"
           "        fall_transition (scalar) { values (\"0.05\"); }\n"
           "      }\n"
           "    }\n"
           "  }\n";
  };
  std::string text = header;
  const std::size_t hold_pos = text.find("HOLD");
  text.replace(hold_pos, 4, std::to_string(hold_constraint));
  const std::size_t hold_pos2 = text.find("HOLD");
  text.replace(hold_pos2, 4, std::to_string(hold_constraint));
  text += buffer("BUF_X1", buf_x1_delay);
  text += buffer("BUF_X2", buf_x2_delay);
  text += "}\n";
  return text;
}

class TempDir {
 public:
  TempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("eos_eco_txn_test_" + std::to_string(counter_++))) {
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  std::string Write(const std::string& name, const std::string& text) const {
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

analysis::SceneResult MakeScene(const std::string& name, double setup,
                                double setup_tns, double hold,
                                double hold_tns) {
  analysis::SceneResult scene;
  scene.scenario.name = name;
  scene.metrics["worst_slack_ns"] = {setup, "ns"};
  scene.metrics["total_negative_slack_ns"] = {setup_tns, "ns"};
  scene.metrics["worst_hold_slack_ns"] = {hold, "ns"};
  scene.metrics["total_negative_hold_slack_ns"] = {hold_tns, "ns"};
  return scene;
}

analysis::SceneAnalysis EstimateScene(db::ScenarioId id,
                                      const std::string& name,
                                      const std::string& corner,
                                      const std::string& liberty) {
  analysis::SceneAnalysis scene;
  scene.scenario.id = id;
  scene.scenario.name = name;
  scene.scenario.mode = "func";
  scene.scenario.corner = corner;
  scene.scenario.parasitics = db::ParasiticsLevel::kEstimate;
  scene.liberty_path = liberty;
  return scene;
}

session::MetricMap LedgerMetrics(const analysis::SceneResult& scene) {
  session::MetricMap metrics;
  for (const auto& entry : scene.metrics) {
    metrics[entry.first] = session::MetricValue{entry.second.value,
                                                entry.second.unit};
  }
  return metrics;
}

double Metric(const analysis::SceneResult& scene, const std::string& name) {
  return scene.metrics.at(name).value;
}

void LoadFixture(db::OdbDatabase& db, const TempDir& dir) {
  ASSERT_TRUE(db.ReadLef(dir.Write("cells.lef", kLef)).ok());
  ASSERT_TRUE(db.ReadDef(dir.Write("top.def", kDef)).ok());
}

// Drives the ledger the way the protocol prescribes: the caller
// begins a transaction, the ECO runs inside it, and the caller
// commits or aborts the ledger transaction with the outcome, so a
// rejected ECO leaves no ledger record at all.
void WrapInLedger(session::Ledger& ledger, const EcoOutcome& outcome) {
  session::TransactionSpec spec;
  spec.input_revision = ledger.current_revision();
  spec.label = outcome.label;
  spec.before_metrics = LedgerMetrics(outcome.before[0]);
  if (!outcome.after.empty()) {
    spec.after_metrics = LedgerMetrics(outcome.after[0]);
  }
  ASSERT_TRUE(ledger.BeginTransaction(spec).ok());
  if (outcome.committed) {
    ASSERT_TRUE(ledger.CommitPending(session::ProofStatus::kUnknown).ok());
  } else {
    ASSERT_TRUE(ledger.AbortPending().ok());
  }
}

TEST(EcoVerdict, ImprovementWithoutRegressionIsAccepted) {
  const auto verdict = EvaluateAllScenes(
      {MakeScene("func_tt", -0.1, -0.1, 0.95, 0.0)},
      {MakeScene("func_tt", 0.5, 0.0, 0.35, 0.0)});
  ASSERT_TRUE(verdict.ok()) << verdict.status().message();
  // The hold slack fell from 0.95 to 0.35 but stayed clean:
  // consuming slack above zero is not a regression.
  EXPECT_TRUE(verdict.value().accepted);
  EXPECT_FALSE(verdict.value().regressed);
  EXPECT_TRUE(verdict.value().improved);
}

TEST(EcoVerdict, HoldCrossingInAnotherSceneIsARegression) {
  const auto verdict = EvaluateAllScenes(
      {MakeScene("func_tt", -0.1, -0.1, 0.95, 0.0),
       MakeScene("func_ff", 0.4, 0.0, 0.15, 0.0)},
      {MakeScene("func_tt", 0.5, 0.0, 0.35, 0.0),
       MakeScene("func_ff", 0.75, 0.0, -0.1, -0.1)});
  ASSERT_TRUE(verdict.ok());
  EXPECT_FALSE(verdict.value().accepted);
  EXPECT_TRUE(verdict.value().regressed);
  EXPECT_EQ(verdict.value().scene_name, "func_ff");
  EXPECT_EQ(verdict.value().metric_name, "worst_hold_slack_ns");
}

TEST(EcoVerdict, WorseningAnExistingViolationIsARegression) {
  const auto verdict = EvaluateAllScenes(
      {MakeScene("func_tt", -0.1, -0.1, 0.5, 0.0)},
      {MakeScene("func_tt", -0.3, -0.3, 0.5, 0.0)});
  ASSERT_TRUE(verdict.ok());
  EXPECT_FALSE(verdict.value().accepted);
  EXPECT_TRUE(verdict.value().regressed);
}

TEST(EcoVerdict, NoImprovementIsNotAccepted) {
  const auto verdict = EvaluateAllScenes(
      {MakeScene("func_tt", 0.5, 0.0, 0.5, 0.0)},
      {MakeScene("func_tt", 0.5, 0.0, 0.5, 0.0)});
  ASSERT_TRUE(verdict.ok());
  EXPECT_FALSE(verdict.value().accepted);
  EXPECT_FALSE(verdict.value().regressed);
  EXPECT_FALSE(verdict.value().improved);
}

// A scene with electrical metrics (unit F5): the four timing
// metrics of MakeScene plus one electrical check (transition:
// violating-pin count and worst value in ns). The other two
// checks are clean (count 0) on both sides of every test below.
analysis::SceneResult MakeElectricalScene(const std::string& name,
                                          double setup, double setup_tns,
                                          double hold, double hold_tns,
                                          double transition_violations,
                                          double max_transition_ns) {
  analysis::SceneResult scene =
      MakeScene(name, setup, setup_tns, hold, hold_tns);
  scene.metrics["max_transition_ns"] = {max_transition_ns, "ns"};
  scene.metrics["max_transition_violations"] = {transition_violations,
                                                "pins"};
  scene.metrics["max_capacitance_pf"] = {0.0, "pF"};
  scene.metrics["max_capacitance_violations"] = {0.0, "pins"};
  scene.metrics["max_fanout"] = {0.0, "fanout"};
  scene.metrics["max_fanout_violations"] = {0.0, "pins"};
  return scene;
}

TEST(EcoVerdict, NewElectricalViolationRejectsATimingImprovement) {
  const auto verdict = EvaluateAllScenes(
      {MakeElectricalScene("func_tt", -0.1, -0.1, 0.5, 0.0, 0.0, 0.03)},
      {MakeElectricalScene("func_tt", 0.5, 0.0, 0.5, 0.0, 1.0, 0.05)});
  ASSERT_TRUE(verdict.ok()) << verdict.status().message();
  EXPECT_FALSE(verdict.value().accepted);
  EXPECT_TRUE(verdict.value().regressed);
  EXPECT_EQ(verdict.value().scene_name, "func_tt");
  EXPECT_EQ(verdict.value().metric_name, "max_transition_violations");
}

TEST(EcoVerdict, WorseningAnExistingElectricalViolationIsARegression) {
  const auto verdict = EvaluateAllScenes(
      {MakeElectricalScene("func_tt", -0.1, -0.1, 0.5, 0.0, 1.0, 0.05)},
      {MakeElectricalScene("func_tt", 0.5, 0.0, 0.5, 0.0, 1.0, 0.08)});
  ASSERT_TRUE(verdict.ok());
  EXPECT_FALSE(verdict.value().accepted);
  EXPECT_TRUE(verdict.value().regressed);
  EXPECT_EQ(verdict.value().metric_name, "max_transition_violations");
}

TEST(EcoVerdict, UnchangedBaselineViolationDoesNotBlockATimingFix) {
  // The baseline violation persists at the same count and the
  // same worst value: nothing was introduced beyond the
  // recorded baseline, so the timing improvement is accepted.
  const auto verdict = EvaluateAllScenes(
      {MakeElectricalScene("func_tt", -0.1, -0.1, 0.5, 0.0, 1.0, 0.05)},
      {MakeElectricalScene("func_tt", 0.5, 0.0, 0.5, 0.0, 1.0, 0.05)});
  ASSERT_TRUE(verdict.ok());
  EXPECT_TRUE(verdict.value().accepted);
  EXPECT_FALSE(verdict.value().regressed);
}

TEST(EcoVerdict, ElectricalImprovementAloneIsNotAccepted) {
  const auto verdict = EvaluateAllScenes(
      {MakeElectricalScene("func_tt", 0.5, 0.0, 0.5, 0.0, 1.0, 0.05)},
      {MakeElectricalScene("func_tt", 0.5, 0.0, 0.5, 0.0, 0.0, 0.03)});
  ASSERT_TRUE(verdict.ok());
  EXPECT_FALSE(verdict.value().accepted);
  EXPECT_FALSE(verdict.value().regressed);
  EXPECT_FALSE(verdict.value().improved);
}

TEST(EcoVerdict, MismatchedScenesAndMetricsAreErrors) {
  const auto missing_scene = EvaluateAllScenes(
      {MakeScene("func_tt", 0.5, 0.0, 0.5, 0.0)},
      {MakeScene("func_ss", 0.5, 0.0, 0.5, 0.0)});
  EXPECT_FALSE(missing_scene.ok());
  EXPECT_EQ(missing_scene.status().code(), ErrorCode::kInvalidArgument);
  analysis::SceneResult incomplete;
  incomplete.scenario.name = "func_tt";
  incomplete.metrics["worst_slack_ns"] = {0.5, "ns"};
  const auto missing_metric = EvaluateAllScenes(
      {MakeScene("func_tt", 0.5, 0.0, 0.5, 0.0)}, {incomplete});
  EXPECT_FALSE(missing_metric.ok());
  EXPECT_EQ(missing_metric.status().code(), ErrorCode::kInvalidArgument);
}

TEST(EcoTransaction, SizingEditThatFixesAViolationCommits) {
  TempDir dir;
  db::OdbDatabase db;
  LoadFixture(db, dir);
  EcoRequest request;
  request.label = "size u1 BUF_X1 -> BUF_X2";
  request.sdc_path =
      dir.Write("top.sdc", "create_clock -name clk -period 1 [get_ports clk]\n");
  request.scenes = {EstimateScene(1, "func_tt", "tt",
                                  dir.Write("tt.lib", Liberty(0.05, 0.8, 0.2)))};
  const auto outcome = RunEcoTransaction(
      db, request, [](db::OdbDatabase& design) {
        return ResizeInstance(design, "u1", "BUF_X2");
      });
  ASSERT_TRUE(outcome.ok()) << outcome.status().message();
  EXPECT_TRUE(outcome.value().committed) << outcome.value().reason;
  ASSERT_EQ(outcome.value().before.size(), 1u);
  ASSERT_EQ(outcome.value().after.size(), 1u);
  EXPECT_NEAR(Metric(outcome.value().before[0], "worst_slack_ns"), -0.1,
              1e-6);
  EXPECT_NEAR(Metric(outcome.value().after[0], "worst_slack_ns"), 0.5,
              1e-6);
  EXPECT_NE(outcome.value().parasitics_note.find("estimate"),
            std::string::npos);
  db::OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance("u1", &info));
  EXPECT_EQ(info.master, "BUF_X2");

  session::Ledger ledger;
  WrapInLedger(ledger, outcome.value());
  EXPECT_EQ(ledger.current_revision(), 1u);
  ASSERT_EQ(ledger.history().size(), 1u);
  EXPECT_NEAR(ledger.history()[0].after_metrics.at("worst_slack_ns").value,
              0.5, 1e-6);
}

TEST(EcoTransaction, SizingEditWithACrossSceneHoldRegressionRollsBack) {
  TempDir dir;
  db::OdbDatabase db;
  LoadFixture(db, dir);
  EcoRequest request;
  request.label = "size u1 BUF_X1 -> BUF_X2";
  request.sdc_path =
      dir.Write("top.sdc", "create_clock -name clk -period 1 [get_ports clk]\n");
  request.scenes = {
      EstimateScene(1, "func_tt", "tt",
                    dir.Write("tt.lib", Liberty(0.05, 0.8, 0.2))),
      EstimateScene(2, "func_ff", "ff",
                    dir.Write("ff.lib", Liberty(0.35, 0.3, 0.05)))};
  const auto outcome = RunEcoTransaction(
      db, request, [](db::OdbDatabase& design) {
        return ResizeInstance(design, "u1", "BUF_X2");
      });
  ASSERT_TRUE(outcome.ok()) << outcome.status().message();
  EXPECT_FALSE(outcome.value().committed);
  EXPECT_NE(outcome.value().reason.find("func_ff"), std::string::npos)
      << outcome.value().reason;
  EXPECT_NE(outcome.value().reason.find("worst_hold_slack_ns"),
            std::string::npos)
      << outcome.value().reason;
  // The design is as if never attempted.
  db::OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance("u1", &info));
  EXPECT_EQ(info.master, "BUF_X1");
  EXPECT_EQ(db.InstanceCount(), 3u);
  EXPECT_EQ(db.NetCount(), 5u);
  // And so is the ledger: the aborted wrap leaves no record and
  // no revision.
  session::Ledger ledger;
  WrapInLedger(ledger, outcome.value());
  EXPECT_EQ(ledger.current_revision(), 0u);
  EXPECT_TRUE(ledger.history().empty());
  EXPECT_FALSE(ledger.HasPending());
}

TEST(EcoTransaction, ExtractedSceneIsRefusedBeforeAnyEdit) {
  TempDir dir;
  db::OdbDatabase db;
  LoadFixture(db, dir);
  EcoRequest request;
  request.label = "size u1";
  request.sdc_path =
      dir.Write("top.sdc", "create_clock -name clk -period 1 [get_ports clk]\n");
  analysis::SceneAnalysis scene = EstimateScene(
      1, "func_tt", "tt", dir.Write("tt.lib", Liberty(0.05, 0.8, 0.2)));
  scene.scenario.parasitics = db::ParasiticsLevel::kExtracted;
  scene.spef_path = dir.Path("parasitics.spef");
  request.scenes = {scene};
  bool edit_ran = false;
  const auto outcome = RunEcoTransaction(
      db, request, [&edit_ran](db::OdbDatabase& design) {
        edit_ran = true;
        return ResizeInstance(design, "u1", "BUF_X2");
      });
  ASSERT_FALSE(outcome.ok());
  EXPECT_EQ(outcome.status().code(), ErrorCode::kInvalidArgument);
  EXPECT_FALSE(edit_ran);
}

TEST(EcoTransaction, FailedEditRollsBackAndReturnsTheError) {
  TempDir dir;
  db::OdbDatabase db;
  LoadFixture(db, dir);
  EcoRequest request;
  request.label = "size u1 to a missing master";
  request.sdc_path =
      dir.Write("top.sdc", "create_clock -name clk -period 1 [get_ports clk]\n");
  request.scenes = {EstimateScene(1, "func_tt", "tt",
                                  dir.Write("tt.lib", Liberty(0.05, 0.8, 0.2)))};
  const auto outcome = RunEcoTransaction(
      db, request, [](db::OdbDatabase& design) {
        return ResizeInstance(design, "u1", "NOPE");
      });
  ASSERT_FALSE(outcome.ok());
  EXPECT_EQ(outcome.status().code(), ErrorCode::kNotFound);
  db::OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance("u1", &info));
  EXPECT_EQ(info.master, "BUF_X1");
}

// Unit 2.9: a caller-supplied before-analysis (the closure loop
// holds one for the design as it stands) is judged against
// exactly as the engine's own analysis would be: same verdict,
// same before and after numbers.
TEST(EcoTransaction, KnownBeforeIsJudgedLikeTheEnginesOwnAnalysis) {
  TempDir dir;
  db::OdbDatabase db;
  LoadFixture(db, dir);
  EcoRequest request;
  request.label = "size u1 BUF_X1 -> BUF_X2";
  request.sdc_path =
      dir.Write("top.sdc", "create_clock -name clk -period 1 [get_ports clk]\n");
  request.scenes = {EstimateScene(1, "func_tt", "tt",
                                  dir.Write("tt.lib", Liberty(0.05, 0.8, 0.2)))};
  analysis::TimingService timing;
  const auto before = timing.Analyze(db, request.sdc_path, request.scenes);
  ASSERT_TRUE(before.ok()) << before.status().message();
  request.known_before = before.value();
  const auto outcome = RunEcoTransaction(
      db, request, [](db::OdbDatabase& design) {
        return ResizeInstance(design, "u1", "BUF_X2");
      });
  ASSERT_TRUE(outcome.ok()) << outcome.status().message();
  EXPECT_TRUE(outcome.value().committed) << outcome.value().reason;
  ASSERT_EQ(outcome.value().before.size(), 1u);
  EXPECT_NEAR(Metric(outcome.value().before[0], "worst_slack_ns"), -0.1,
              1e-6);
  EXPECT_NEAR(Metric(outcome.value().after[0], "worst_slack_ns"), 0.5, 1e-6);
  db::OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance("u1", &info));
  EXPECT_EQ(info.master, "BUF_X2");
}

// Unit 2.9: a supplied before-analysis that does not cover the
// request's scenes is refused before any edit runs.
TEST(EcoTransaction, KnownBeforeWithMismatchedScenesIsRefused) {
  TempDir dir;
  db::OdbDatabase db;
  LoadFixture(db, dir);
  EcoRequest request;
  request.label = "size u1 BUF_X1 -> BUF_X2";
  request.sdc_path =
      dir.Write("top.sdc", "create_clock -name clk -period 1 [get_ports clk]\n");
  request.scenes = {EstimateScene(1, "func_tt", "tt",
                                  dir.Write("tt.lib", Liberty(0.05, 0.8, 0.2)))};
  request.known_before = {MakeScene("func_ss", -0.1, -0.1, 0.5, 0.0)};
  bool edit_ran = false;
  const auto outcome = RunEcoTransaction(
      db, request, [&edit_ran](db::OdbDatabase& design) {
        edit_ran = true;
        return ResizeInstance(design, "u1", "BUF_X2");
      });
  ASSERT_FALSE(outcome.ok());
  EXPECT_EQ(outcome.status().code(), ErrorCode::kInvalidArgument);
  EXPECT_FALSE(edit_ran);
  db::OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance("u1", &info));
  EXPECT_EQ(info.master, "BUF_X1");
}

}  // namespace
}  // namespace eos::eco
