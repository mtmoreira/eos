// Unit tests: analysis/timing, the timing service over scenes.
//
// The Verilog fixture is the unit 2.2 STA fixture (a Liberty
// library with one flip-flop and one buffer, a two-flop netlist, a
// 10 ns and a 0.5 ns SDC): by construction the reg-to-reg delay is
// 0.6 ns, so the expected metrics are the pinned upstream values
// the STA facade tests already hold (setup worst slack 9.4 ns and
// -0.1 ns, TNS 0 and -0.1 ns), plus the hold pair measured with
// the pinned upstream executable on this identical fixture on
// 2026-10-25 (worst hold slack 0.45 ns, hold TNS 0 at the 10 ns
// clock).
//
// The ODB fixture is a synthetic LEF/DEF pair whose masters are
// the same DFF and BUF cells wired the same way, so the ODB form
// of the service must land on the same numbers through the ODB
// facade's Verilog writer.
#include "eos/analysis/timing.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace eos::analysis {
namespace {

using foundation::ErrorCode;

const char kLiberty[] = R"LIB(library (eos_fixture) {
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

const char kLibertyElectrical[] = R"LIB(library (eos_fixture_timing_elec) {
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

const char kVerilog[] = R"V(module top (clk, d, q);
  input clk, d;
  output q;
  wire n1, n2;

  DFF r1 (.D(d), .CK(clk), .Q(n1));
  BUF u1 (.A(n1), .Y(n2));
  DFF r2 (.D(n2), .CK(clk), .Q(q));
endmodule
)V";

const char kSdc[] = "create_clock -name clk -period 10 [get_ports clk]\n";
const char kSdcTight[] =
    "create_clock -name clk -period 0.5 [get_ports clk]\n";

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
MACRO BUF
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
END BUF
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
- u1 BUF + PLACED ( 2000 1000 ) N ;
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

class TempDir {
 public:
  TempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("eos_timing_test_" + std::to_string(counter_++))) {
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

db::Scenario MakeScenario(db::ScenarioId id, const std::string& name,
                          const std::string& corner,
                          db::ParasiticsLevel level) {
  db::Scenario scenario;
  scenario.id = id;
  scenario.name = name;
  scenario.mode = "func";
  scenario.corner = corner;
  scenario.parasitics = level;
  return scenario;
}

SceneAnalysis EstimateScene(db::ScenarioId id, const std::string& name,
                            const std::string& corner,
                            const std::string& liberty) {
  SceneAnalysis scene;
  scene.scenario = MakeScenario(id, name, corner,
                                db::ParasiticsLevel::kEstimate);
  scene.liberty_path = liberty;
  return scene;
}

double Metric(const SceneResult& result, const std::string& name) {
  const auto it = result.metrics.find(name);
  EXPECT_NE(it, result.metrics.end()) << "missing metric " << name;
  if (it == result.metrics.end()) return 0.0;
  EXPECT_EQ(it->second.unit, "ns");
  return it->second.value;
}

void ExpectFixtureMetrics(const SceneResult& result, double worst_slack,
                          double tns) {
  EXPECT_NEAR(Metric(result, "worst_slack_ns"), worst_slack,
              1e-6 * std::abs(worst_slack) + 1e-7);
  EXPECT_NEAR(Metric(result, "total_negative_slack_ns"), tns,
              1e-6 * std::abs(tns) + 1e-7);
}

TEST(TimingService, VerilogDesignAnalyzedPerScene) {
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kLiberty);
  const VerilogDesign design{dir.Write("top.v", kVerilog), "top",
                             dir.Write("top.sdc", kSdc)};
  TimingService service;
  const auto results = service.Analyze(
      design, {EstimateScene(1, "func_tt", "tt", liberty),
               EstimateScene(2, "func_ss", "ss", liberty)});
  ASSERT_TRUE(results.ok()) << results.status().message();
  ASSERT_EQ(results.value().size(), 2u);
  EXPECT_EQ(results.value()[0].scenario.name, "func_tt");
  EXPECT_EQ(results.value()[1].scenario.name, "func_ss");
  for (const SceneResult& result : results.value()) {
    ExpectFixtureMetrics(result, 9.4, 0.0);
    EXPECT_NEAR(Metric(result, "worst_hold_slack_ns"), 0.45, 1e-6);
    EXPECT_NEAR(Metric(result, "total_negative_hold_slack_ns"), 0.0, 1e-9);
    EXPECT_TRUE(result.setup_violating_endpoints.empty());
    EXPECT_TRUE(result.hold_violating_endpoints.empty());
  }
}

TEST(TimingService, TightClockShowsViolationPerScene) {
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kLiberty);
  const VerilogDesign design{dir.Write("top.v", kVerilog), "top",
                             dir.Write("top.sdc", kSdcTight)};
  TimingService service;
  const auto results = service.Analyze(
      design, {EstimateScene(1, "func_tt", "tt", liberty)});
  ASSERT_TRUE(results.ok()) << results.status().message();
  ASSERT_EQ(results.value().size(), 1u);
  ExpectFixtureMetrics(results.value()[0], -0.1, -0.1);
  // The scene result surfaces the violating endpoint behind the
  // negative metrics: r2's D pin at the same -0.1 ns, and no hold
  // endpoint (the hold pair stays positive on this fixture).
  const SceneResult& tight = results.value()[0];
  ASSERT_EQ(tight.setup_violating_endpoints.size(), 1u);
  EXPECT_EQ(tight.setup_violating_endpoints[0].pin, "r2/D");
  EXPECT_NEAR(tight.setup_violating_endpoints[0].slack_ns, -0.1,
              1e-6 * 0.1 + 1e-7);
  EXPECT_TRUE(tight.hold_violating_endpoints.empty());
}


TEST(TimingService, ElectricalMetricsAreReportedPerScene) {
  // The electrical fixture (unit F5): the buffer output violates
  // all three limits by construction (see the STA facade tests),
  // so the scene must carry the worst values and a count of 1
  // for each check, in the check's own unit.
  TempDir dir;
  const std::string liberty = dir.Write("cells_elec.lib", kLibertyElectrical);
  const VerilogDesign design{dir.Write("top.v", kVerilog), "top",
                             dir.Write("top.sdc", kSdc)};
  TimingService service;
  const auto results = service.Analyze(
      design, {EstimateScene(1, "func_tt", "tt", liberty)});
  ASSERT_TRUE(results.ok()) << results.status().message();
  ASSERT_EQ(results.value().size(), 1u);
  const SceneResult& scene = results.value()[0];
  const auto metric = [&scene](const std::string& name,
                               const std::string& unit) {
    const auto it = scene.metrics.find(name);
    EXPECT_NE(it, scene.metrics.end()) << "missing metric " << name;
    if (it == scene.metrics.end()) return 0.0;
    EXPECT_EQ(it->second.unit, unit) << "metric " << name;
    return it->second.value;
  };
  EXPECT_NEAR(metric("max_transition_ns", "ns"), 0.05, 1e-6);
  EXPECT_NEAR(metric("max_transition_violations", "pins"), 1.0, 1e-9);
  EXPECT_NEAR(metric("max_capacitance_pf", "pF"), 0.01, 1e-9);
  EXPECT_NEAR(metric("max_capacitance_violations", "pins"), 1.0, 1e-9);
  EXPECT_NEAR(metric("max_fanout", "fanout"), 2.0, 1e-9);
  EXPECT_NEAR(metric("max_fanout_violations", "pins"), 1.0, 1e-9);
}

TEST(TimingService, ElectricalMetricsAreZeroWithoutLimits) {
  // The plain fixture library carries no electrical limit: the
  // metrics are present, with worst value 0 and count 0.
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kLiberty);
  const VerilogDesign design{dir.Write("top.v", kVerilog), "top",
                             dir.Write("top.sdc", kSdc)};
  TimingService service;
  const auto results = service.Analyze(
      design, {EstimateScene(1, "func_tt", "tt", liberty)});
  ASSERT_TRUE(results.ok()) << results.status().message();
  const SceneResult& scene = results.value()[0];
  EXPECT_EQ(scene.metrics.at("max_transition_violations").value, 0.0);
  EXPECT_EQ(scene.metrics.at("max_capacitance_violations").value, 0.0);
  EXPECT_EQ(scene.metrics.at("max_fanout_violations").value, 0.0);
  EXPECT_EQ(scene.metrics.at("max_transition_ns").value, 0.0);
}

TEST(TimingService, OdbDesignAnalyzedThroughNetlist) {
  TempDir dir;
  db::OdbDatabase database;
  ASSERT_TRUE(database.ReadLef(dir.Write("cells.lef", kLef)).ok());
  ASSERT_TRUE(database.ReadDef(dir.Write("top.def", kDef)).ok());
  TimingService service;
  const auto results = service.Analyze(
      database, dir.Write("top.sdc", kSdc),
      {EstimateScene(1, "func_tt", "tt", dir.Write("cells.lib", kLiberty))});
  ASSERT_TRUE(results.ok()) << results.status().message();
  ASSERT_EQ(results.value().size(), 1u);
  ExpectFixtureMetrics(results.value()[0], 9.4, 0.0);
  EXPECT_NEAR(Metric(results.value()[0], "worst_hold_slack_ns"), 0.45,
              1e-6);
}

TEST(TimingService, ExtractedSceneWithoutSpefIsRejected) {
  TempDir dir;
  const VerilogDesign design{dir.Write("top.v", kVerilog), "top",
                             dir.Write("top.sdc", kSdc)};
  SceneAnalysis scene;
  scene.scenario = MakeScenario(1, "func_tt_x", "tt",
                                db::ParasiticsLevel::kExtracted);
  scene.liberty_path = dir.Write("cells.lib", kLiberty);
  TimingService service;
  const auto results = service.Analyze(design, {scene});
  ASSERT_FALSE(results.ok());
  EXPECT_EQ(results.status().code(), ErrorCode::kInvalidArgument);
}

TEST(TimingService, EstimateSceneWithSpefIsRejected) {
  TempDir dir;
  const VerilogDesign design{dir.Write("top.v", kVerilog), "top",
                             dir.Write("top.sdc", kSdc)};
  SceneAnalysis scene = EstimateScene(1, "func_tt", "tt",
                                      dir.Write("cells.lib", kLiberty));
  scene.spef_path = dir.Path("parasitics.spef");
  TimingService service;
  const auto results = service.Analyze(design, {scene});
  ASSERT_FALSE(results.ok());
  EXPECT_EQ(results.status().code(), ErrorCode::kInvalidArgument);
}

TEST(TimingService, DuplicateAndEmptySceneListsAreRejected) {
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kLiberty);
  const VerilogDesign design{dir.Write("top.v", kVerilog), "top",
                             dir.Write("top.sdc", kSdc)};
  TimingService service;
  const auto empty = service.Analyze(design, {});
  ASSERT_FALSE(empty.ok());
  EXPECT_EQ(empty.status().code(), ErrorCode::kInvalidArgument);
  const auto duplicate = service.Analyze(
      design, {EstimateScene(1, "func_tt", "tt", liberty),
               EstimateScene(2, "func_tt_again", "tt", liberty)});
  ASSERT_FALSE(duplicate.ok());
  EXPECT_EQ(duplicate.status().code(), ErrorCode::kInvalidArgument);
}

TEST(TimingService, MissingLibertyFileFailsWithIoError) {
  TempDir dir;
  const VerilogDesign design{dir.Write("top.v", kVerilog), "top",
                             dir.Write("top.sdc", kSdc)};
  TimingService service;
  const auto results = service.Analyze(
      design, {EstimateScene(1, "func_tt", "tt", dir.Path("no_such.lib"))});
  ASSERT_FALSE(results.ok());
  EXPECT_EQ(results.status().code(), ErrorCode::kIoError);
  EXPECT_NE(results.status().message().find("func_tt"), std::string::npos);
}

TEST(TimingService, OdbWithoutTopBlockIsInvalidState) {
  TempDir dir;
  db::OdbDatabase database;
  TimingService service;
  const auto results = service.Analyze(
      database, dir.Write("top.sdc", kSdc),
      {EstimateScene(1, "func_tt", "tt", dir.Write("cells.lib", kLiberty))});
  ASSERT_FALSE(results.ok());
  EXPECT_EQ(results.status().code(), ErrorCode::kInvalidState);
}

}  // namespace
}  // namespace eos::analysis
