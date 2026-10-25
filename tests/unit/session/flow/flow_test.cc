// Unit tests: session/flow, the timing closure loop.
//
// The fixtures are the ECO units' synthetic designs, embedded
// here and written to a temporary directory per test. The sizing
// chain (flip-flop, buffer, flip-flop, 1 ns clock) has setup
// slack set by the buffer's Liberty delay: delay d gives slack
// 1 - 0.2 (clock to Q) - d - 0.1 (setup). The fanout fixture is
// the timing-eco one: a flip-flop whose clock-to-Q delay is a
// Liberty table over output capacitance drives eight data pins,
// slack -0.9 ns unbuffered and +0.45 ns behind a buffer. Every
// expected number below is a fixture construction value, not a
// measurement of any real cell.
#include "eos/session/flow.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "eos/session/reports.h"

namespace eos::session {
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
MACRO BUF_X4
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
END BUF_X4
MACRO WIDE
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
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END Y
END WIDE
END LIBRARY
)LEF";

std::string ChainDef(const std::string& buffer_master) {
  return std::string(R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 3 ;
- r1 DFF + PLACED ( 1000 1000 ) N ;
- u1 )DEF") +
         buffer_master + R"DEF( + PLACED ( 2000 1000 ) N ;
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
}

const char kFanoutDef[] = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 9 ;
- r1 DFF + PLACED ( 1000 1000 ) N ;
- r2 DFF + PLACED ( 2000 1000 ) N ;
- r3 DFF + PLACED ( 3000 1000 ) N ;
- r4 DFF + PLACED ( 4000 1000 ) N ;
- r5 DFF + PLACED ( 5000 1000 ) N ;
- r6 DFF + PLACED ( 6000 1000 ) N ;
- r7 DFF + PLACED ( 7000 1000 ) N ;
- r8 DFF + PLACED ( 8000 1000 ) N ;
- r9 DFF + PLACED ( 9000 1000 ) N ;
END COMPONENTS
PINS 2 ;
- clk + NET clk + DIRECTION INPUT + USE SIGNAL + PLACED ( 0 5000 ) N ;
- d + NET d + DIRECTION INPUT + USE SIGNAL + PLACED ( 0 6000 ) N ;
END PINS
NETS 3 ;
- clk ( PIN clk ) ( r1 CK ) ( r2 CK ) ( r3 CK ) ( r4 CK ) ( r5 CK ) ( r6 CK ) ( r7 CK ) ( r8 CK ) ( r9 CK ) ;
- d ( PIN d ) ( r1 D ) ;
- wide ( r1 Q ) ( r2 D ) ( r3 D ) ( r4 D ) ( r5 D ) ( r6 D ) ( r7 D ) ( r8 D ) ( r9 D ) ;
END NETS
END DESIGN
)DEF";

// One Liberty text for the chain with the buffer delays as
// parameters (the transactions fixture's generator, extended to
// three variants).
std::string ChainLiberty(double hold_constraint,
                         const std::vector<std::pair<std::string, double>>&
                             buffers) {
  std::string text = R"LIB(library (eos_flow_fixture) {
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
  for (int i = 0; i < 2; ++i) {
    const std::size_t at = text.find("HOLD");
    text.replace(at, 4, std::to_string(hold_constraint));
  }
  for (const auto& buffer : buffers) {
    text += "  cell (" + buffer.first + ") {\n"
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
            std::to_string(buffer.second) +
            "\"); }\n"
            "        cell_fall (scalar) { values (\"" +
            std::to_string(buffer.second) +
            "\"); }\n"
            "        rise_transition (scalar) { values (\"0.05\"); }\n"
            "        fall_transition (scalar) { values (\"0.05\"); }\n"
            "      }\n"
            "    }\n"
            "  }\n";
  }
  text += "}\n";
  return text;
}

const char kFanoutLiberty[] = R"LIB(library (eos_fanout_fixture) {
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

  lu_table_template (del_cap) {
    variable_1 : total_output_net_capacitance;
    index_1 ("0.005, 0.08");
  }

  cell (DFF) {
    area : 10.0;
    ff (IQ, IQN) {
      next_state : "D";
      clocked_on : "CK";
    }
    pin (D) {
      direction : input;
      capacitance : 0.01;
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
        cell_rise (del_cap) { values ("0.2, 1.8"); }
        cell_fall (del_cap) { values ("0.2, 1.8"); }
        rise_transition (del_cap) { values ("0.05, 0.1"); }
        fall_transition (del_cap) { values ("0.05, 0.1"); }
      }
    }
  }

  cell (BUF_X4) {
    area : 4.0;
    pin (A) {
      direction : input;
      capacitance : 0.005;
    }
    pin (Y) {
      direction : output;
      function : "A";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (del_cap) { values ("0.1, 0.25"); }
        cell_fall (del_cap) { values ("0.1, 0.25"); }
        rise_transition (del_cap) { values ("0.02, 0.05"); }
        fall_transition (del_cap) { values ("0.02, 0.05"); }
      }
    }
  }
}
)LIB";

class TempDir {
 public:
  TempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("eos_flow_test_" + std::to_string(counter_++))) {
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

 private:
  static inline int counter_ = 0;
  std::filesystem::path path_;
};

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

// Registers the scenes in the project the way the command layer
// would, so the loop's report binding snapshots them.
void AddScenarios(Project& project,
                  const std::vector<analysis::SceneAnalysis>& scenes) {
  for (const analysis::SceneAnalysis& scene : scenes) {
    ASSERT_TRUE(project.scenarios
                    .Create(scene.scenario.name, scene.scenario.mode,
                            scene.scenario.corner, scene.scenario.parasitics)
                    .ok());
  }
}

double WorstSlack(const analysis::SceneResult& scene) {
  return scene.metrics.at("worst_slack_ns").value;
}

void LoadChain(db::OdbDatabase& db, const TempDir& dir,
               const std::string& buffer_master) {
  ASSERT_TRUE(db.ReadLef(dir.Write("cells.lef", kLef)).ok());
  ASSERT_TRUE(db.ReadDef(dir.Write("top.def", ChainDef(buffer_master))).ok());
}

TEST(TimingGate, GateNeedsEveryMetricCleanInEveryScene) {
  const auto scene = [](double setup, double setup_tns, double hold,
                        double hold_tns) {
    analysis::SceneResult result;
    result.scenario.name = "func_tt";
    result.metrics["worst_slack_ns"] = {setup, "ns"};
    result.metrics["total_negative_slack_ns"] = {setup_tns, "ns"};
    result.metrics["worst_hold_slack_ns"] = {hold, "ns"};
    result.metrics["total_negative_hold_slack_ns"] = {hold_tns, "ns"};
    return result;
  };
  EXPECT_TRUE(TimingGateMet({scene(0.5, 0.0, 0.4, 0.0)}));
  EXPECT_FALSE(TimingGateMet({scene(-0.1, -0.1, 0.4, 0.0)}));
  EXPECT_FALSE(TimingGateMet({scene(0.5, 0.0, -0.1, -0.1)}));
  EXPECT_FALSE(TimingGateMet({}));
  analysis::SceneResult incomplete;
  incomplete.scenario.name = "func_tt";
  incomplete.metrics["worst_slack_ns"] = {0.5, "ns"};
  EXPECT_FALSE(TimingGateMet({incomplete}));
}

TEST(ClosureLoop, SizingClosesTheViolationAndRecordsTheLedger) {
  TempDir dir;
  db::OdbDatabase db;
  LoadChain(db, dir, "BUF_X1");
  CloseTimingRequest request;
  request.sdc_path =
      dir.Write("top.sdc", "create_clock -name clk -period 1 [get_ports clk]\n");
  request.scenes = {EstimateScene(
      1, "func_tt", "tt",
      dir.Write("tt.lib", ChainLiberty(0.05, {{"BUF_X1", 0.8},
                                              {"BUF_X2", 0.2}})))};
  request.max_iterations = 5;
  request.sizing_families = {{"BUF_X1", "BUF_X2"}};
  Project project;
  AddScenarios(project, request.scenes);

  const auto outcome = RunTimingClosure(project, db, request);
  ASSERT_TRUE(outcome.ok()) << outcome.status().message();
  EXPECT_TRUE(outcome.value().closed) << outcome.value().stop_reason;
  ASSERT_EQ(outcome.value().steps.size(), 1u);
  EXPECT_EQ(outcome.value().steps[0].label, "size u1 BUF_X1 -> BUF_X2");
  EXPECT_EQ(outcome.value().candidates_tried, 1u);
  EXPECT_EQ(outcome.value().candidates_rejected, 0u);
  EXPECT_NEAR(WorstSlack(outcome.value().initial[0]), -0.1, 1e-6);
  EXPECT_NEAR(WorstSlack(outcome.value().final[0]), 0.5, 1e-6);
  db::OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance("u1", &info));
  EXPECT_EQ(info.master, "BUF_X2");
  // One accepted iteration, one ledger transaction, carrying the
  // flattened per-scene metrics.
  EXPECT_EQ(project.ledger.current_revision(), 1u);
  ASSERT_EQ(project.ledger.history().size(), 1u);
  EXPECT_EQ(project.ledger.history()[0].label,
            "close_timing: size u1 BUF_X1 -> BUF_X2");
  EXPECT_NEAR(project.ledger.history()[0].after_metrics
                  .at("func_tt/worst_slack_ns")
                  .value,
              0.5, 1e-6);
  // The loop's own report binding, proof pass because it closed.
  ReportRegistry reports;
  const ReportBinding* binding =
      reports.FindByName(project, "timing_closure");
  ASSERT_NE(binding, nullptr);
  const ArtifactRecord* artifact =
      project.ledger.FindArtifact(binding->artifact_id);
  ASSERT_NE(artifact, nullptr);
  EXPECT_EQ(artifact->proof, ProofStatus::kPass);
  EXPECT_TRUE(project.ledger.ArtifactCurrent(binding->artifact_id));
}

TEST(ClosureLoop, CrossSceneRegressionIsRefusedAndNothingCommits) {
  TempDir dir;
  db::OdbDatabase db;
  LoadChain(db, dir, "BUF_X1");
  CloseTimingRequest request;
  request.sdc_path =
      dir.Write("top.sdc", "create_clock -name clk -period 1 [get_ports clk]\n");
  request.scenes = {
      EstimateScene(1, "func_tt", "tt",
                    dir.Write("tt.lib", ChainLiberty(0.05, {{"BUF_X1", 0.8},
                                                            {"BUF_X2", 0.2}}))),
      // The hold-tight corner: the same swap that fixes setup in
      // func_tt drops this scene's hold slack to -0.10 ns (the
      // transactions fixture's numbers).
      EstimateScene(2, "func_ff", "ff",
                    dir.Write("ff.lib", ChainLiberty(0.35, {{"BUF_X1", 0.3},
                                                            {"BUF_X2", 0.05}})))};
  request.max_iterations = 5;
  request.sizing_families = {{"BUF_X1", "BUF_X2"}};
  Project project;
  AddScenarios(project, request.scenes);

  const auto outcome = RunTimingClosure(project, db, request);
  ASSERT_TRUE(outcome.ok()) << outcome.status().message();
  EXPECT_FALSE(outcome.value().closed);
  EXPECT_TRUE(outcome.value().steps.empty());
  EXPECT_EQ(outcome.value().candidates_tried, 1u);
  EXPECT_EQ(outcome.value().candidates_rejected, 1u);
  EXPECT_NE(outcome.value().stop_reason.find("worst_hold_slack_ns"),
            std::string::npos)
      << outcome.value().stop_reason;
  // Design and ledger as if the loop never ran an edit.
  db::OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance("u1", &info));
  EXPECT_EQ(info.master, "BUF_X1");
  EXPECT_EQ(project.ledger.current_revision(), 0u);
  EXPECT_TRUE(project.ledger.history().empty());
  // But the run itself is on record, with unknown proof: a loop
  // that did not close timing certifies nothing.
  ReportRegistry reports;
  const ReportBinding* binding =
      reports.FindByName(project, "timing_closure");
  ASSERT_NE(binding, nullptr);
  const ArtifactRecord* artifact =
      project.ledger.FindArtifact(binding->artifact_id);
  ASSERT_NE(artifact, nullptr);
  EXPECT_EQ(artifact->proof, ProofStatus::kUnknown);
}

TEST(ClosureLoop, BufferingClosesTheHighFanoutViolation) {
  TempDir dir;
  db::OdbDatabase db;
  ASSERT_TRUE(db.ReadLef(dir.Write("cells.lef", kLef)).ok());
  ASSERT_TRUE(db.ReadDef(dir.Write("top.def", kFanoutDef)).ok());
  CloseTimingRequest request;
  request.sdc_path =
      dir.Write("top.sdc", "create_clock -name clk -period 1 [get_ports clk]\n");
  request.scenes = {EstimateScene(1, "func_tt", "tt",
                                  dir.Write("tt.lib", kFanoutLiberty))};
  request.max_iterations = 3;
  request.buffer_masters = {"BUF_X4"};
  Project project;
  AddScenarios(project, request.scenes);

  const auto outcome = RunTimingClosure(project, db, request);
  ASSERT_TRUE(outcome.ok()) << outcome.status().message();
  EXPECT_TRUE(outcome.value().closed) << outcome.value().stop_reason;
  ASSERT_EQ(outcome.value().steps.size(), 1u);
  EXPECT_NE(outcome.value().steps[0].label.find("buffer net wide"),
            std::string::npos);
  EXPECT_NEAR(WorstSlack(outcome.value().initial[0]), -0.9, 0.01);
  EXPECT_NEAR(WorstSlack(outcome.value().final[0]), 0.45, 0.01);
  EXPECT_EQ(db.InstanceCount(), 10u);
  EXPECT_EQ(project.ledger.current_revision(), 1u);
}

TEST(ClosureLoop, CleanDesignClosesWithNoEditAndNoTransaction) {
  TempDir dir;
  db::OdbDatabase db;
  LoadChain(db, dir, "BUF_X2");
  CloseTimingRequest request;
  request.sdc_path =
      dir.Write("top.sdc", "create_clock -name clk -period 1 [get_ports clk]\n");
  request.scenes = {EstimateScene(
      1, "func_tt", "tt",
      dir.Write("tt.lib", ChainLiberty(0.05, {{"BUF_X1", 0.8},
                                              {"BUF_X2", 0.2}})))};
  request.max_iterations = 5;
  request.sizing_families = {{"BUF_X1", "BUF_X2"}};
  Project project;
  AddScenarios(project, request.scenes);

  const auto outcome = RunTimingClosure(project, db, request);
  ASSERT_TRUE(outcome.ok()) << outcome.status().message();
  EXPECT_TRUE(outcome.value().closed);
  EXPECT_TRUE(outcome.value().steps.empty());
  EXPECT_EQ(outcome.value().candidates_tried, 0u);
  EXPECT_EQ(project.ledger.current_revision(), 0u);
  ReportRegistry reports;
  ASSERT_NE(reports.FindByName(project, "timing_closure"), nullptr);
}

TEST(ClosureLoop, IterationBoundStopsTheSearchMidClimb) {
  const auto liberty = [](const TempDir& dir) {
    return dir.Write("tt.lib", ChainLiberty(0.05, {{"BUF_X1", 1.4},
                                                   {"BUF_X2", 0.8},
                                                   {"BUF_X4", 0.2}}));
  };
  {
    TempDir dir;
    db::OdbDatabase db;
    LoadChain(db, dir, "BUF_X1");
    CloseTimingRequest request;
    request.sdc_path = dir.Write(
        "top.sdc", "create_clock -name clk -period 1 [get_ports clk]\n");
    request.scenes = {EstimateScene(1, "func_tt", "tt", liberty(dir))};
    request.max_iterations = 1;
    request.sizing_families = {{"BUF_X1", "BUF_X2", "BUF_X4"}};
    Project project;
    AddScenarios(project, request.scenes);
    const auto outcome = RunTimingClosure(project, db, request);
    ASSERT_TRUE(outcome.ok()) << outcome.status().message();
    EXPECT_FALSE(outcome.value().closed);
    ASSERT_EQ(outcome.value().steps.size(), 1u);
    EXPECT_NE(outcome.value().stop_reason.find("iteration bound"),
              std::string::npos)
        << outcome.value().stop_reason;
    EXPECT_NEAR(WorstSlack(outcome.value().final[0]), -0.1, 1e-6);
    db::OdbInstanceInfo info;
    ASSERT_TRUE(db.FindInstance("u1", &info));
    EXPECT_EQ(info.master, "BUF_X2");
  }
  {
    TempDir dir;
    db::OdbDatabase db;
    LoadChain(db, dir, "BUF_X1");
    CloseTimingRequest request;
    request.sdc_path = dir.Write(
        "top.sdc", "create_clock -name clk -period 1 [get_ports clk]\n");
    request.scenes = {EstimateScene(1, "func_tt", "tt", liberty(dir))};
    request.max_iterations = 5;
    request.sizing_families = {{"BUF_X1", "BUF_X2", "BUF_X4"}};
    Project project;
    AddScenarios(project, request.scenes);
    const auto outcome = RunTimingClosure(project, db, request);
    ASSERT_TRUE(outcome.ok()) << outcome.status().message();
    EXPECT_TRUE(outcome.value().closed) << outcome.value().stop_reason;
    ASSERT_EQ(outcome.value().steps.size(), 2u);
    EXPECT_NEAR(WorstSlack(outcome.value().final[0]), 0.5, 1e-6);
    EXPECT_EQ(project.ledger.current_revision(), 2u);
  }
}

TEST(ClosureLoop, ExtractedSceneIsRefusedBeforeAnyEdit) {
  TempDir dir;
  db::OdbDatabase db;
  LoadChain(db, dir, "BUF_X1");
  CloseTimingRequest request;
  request.sdc_path =
      dir.Write("top.sdc", "create_clock -name clk -period 1 [get_ports clk]\n");
  analysis::SceneAnalysis scene = EstimateScene(
      1, "func_tt", "tt",
      dir.Write("tt.lib", ChainLiberty(0.05, {{"BUF_X1", 0.8},
                                              {"BUF_X2", 0.2}})));
  scene.scenario.parasitics = db::ParasiticsLevel::kExtracted;
  scene.spef_path = dir.Write("parasitics.spef", "");
  request.scenes = {scene};
  request.max_iterations = 5;
  request.sizing_families = {{"BUF_X1", "BUF_X2"}};
  Project project;
  const auto outcome = RunTimingClosure(project, db, request);
  ASSERT_FALSE(outcome.ok());
  EXPECT_EQ(outcome.status().code(), ErrorCode::kInvalidArgument);
  db::OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance("u1", &info));
  EXPECT_EQ(info.master, "BUF_X1");
  ReportRegistry reports;
  EXPECT_EQ(reports.FindByName(project, "timing_closure"), nullptr);
}

TEST(ClosureLoop, MalformedRequestsAreRefusedBeforeAnyEdit) {
  TempDir dir;
  db::OdbDatabase db;
  LoadChain(db, dir, "BUF_X1");
  const std::string liberty =
      dir.Write("tt.lib", ChainLiberty(0.05, {{"BUF_X1", 0.8},
                                              {"BUF_X2", 0.2}}));
  Project project;
  const auto base = [&]() {
    CloseTimingRequest request;
    request.sdc_path = dir.Write(
        "top.sdc", "create_clock -name clk -period 1 [get_ports clk]\n");
    request.scenes = {EstimateScene(1, "func_tt", "tt", liberty)};
    request.max_iterations = 3;
    return request;
  };
  {
    CloseTimingRequest request = base();
    request.max_iterations = 0;
    EXPECT_EQ(RunTimingClosure(project, db, request).status().code(),
              ErrorCode::kInvalidArgument);
  }
  {
    CloseTimingRequest request = base();
    request.sizing_families = {{}};
    EXPECT_EQ(RunTimingClosure(project, db, request).status().code(),
              ErrorCode::kInvalidArgument);
  }
  {
    // WIDE carries an extra input pin: not a sizing variant.
    CloseTimingRequest request = base();
    request.sizing_families = {{"BUF_X1", "WIDE"}};
    EXPECT_EQ(RunTimingClosure(project, db, request).status().code(),
              ErrorCode::kInvalidArgument);
  }
  {
    CloseTimingRequest request = base();
    request.buffer_masters = {"DFF"};
    EXPECT_EQ(RunTimingClosure(project, db, request).status().code(),
              ErrorCode::kInvalidArgument);
  }
  db::OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance("u1", &info));
  EXPECT_EQ(info.master, "BUF_X1");
  ReportRegistry reports;
  EXPECT_EQ(reports.FindByName(project, "timing_closure"), nullptr);
}

}  // namespace
}  // namespace eos::session
