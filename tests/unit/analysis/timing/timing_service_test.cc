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
#include <map>
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

// The two-path fixture library: the base fixture cells plus a
// MUX whose A input is the slow arc (0.4 ns) and whose B input
// is the fast one (0.1 ns). A scene loads one Liberty file, so
// the MUX lives in the same library here, unlike the facade
// tests, which load it as a second library.
const char kLibertyTwoPath[] = R"LIB(library (eos_fixture) {
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
const char kVerilogTwoPath[] = R"V(module top (clk, d, d2, sel, q);
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

const char kSdc[] = "create_clock -name clk -period 10 [get_ports clk]\n";
const char kSdcTight[] =
    "create_clock -name clk -period 0.5 [get_ports clk]\n";
// The clock named differently from its port: "clk" resolves to
// the top-level port pin first, so a from-clock test must use a
// name no pin carries (see the facade tests).
const char kSdcSys[] = "create_clock -name sys -period 10 [get_ports clk]\n";

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

// Analysis coverage (Immediate unit 7, review item I4): the
// service runs one engine session per scene and returns the
// facade's coverage pins, so the fixture's two setup and two hold
// checks come back with r1/D untested (no clocked startpoint) and
// r2/D tested.
TEST(TimingService, AnalysisCoveragePerScene) {
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kLiberty);
  const VerilogDesign design{dir.Write("top.v", kVerilog), "top",
                             dir.Write("top.sdc", kSdc)};
  TimingService service;
  const auto results = service.AnalyzeCoverage(
      design, {EstimateScene(1, "func_tt", "tt", liberty)});
  ASSERT_TRUE(results.ok()) << results.status().message();
  ASSERT_EQ(results.value().size(), 1u);
  const CoverageResult& result = results.value()[0];
  EXPECT_EQ(result.scenario.name, "func_tt");
  ASSERT_EQ(result.pins.size(), 4u);
  std::size_t tested = 0;
  std::size_t untested = 0;
  for (const CoveragePin& pin : result.pins) {
    if (pin.tested) {
      ++tested;
      EXPECT_EQ(pin.pin, "r2/D");
      EXPECT_TRUE(pin.untested_reason.empty());
    } else {
      ++untested;
      EXPECT_EQ(pin.pin, "r1/D");
      EXPECT_EQ(pin.untested_reason, "no_startpoint_clock");
    }
  }
  EXPECT_EQ(tested, 2u);
  EXPECT_EQ(untested, 2u);
}

// QoR data (Immediate unit 9, review item I9): one service call
// per scene returns the per-group endpoint slacks, the cell
// census and area, the scalar metrics and the three electrical
// checks, so report_qor needs no second run.
TEST(TimingService, AnalyzeQorReportsGroupsCellsAndDrc) {
  TempDir dir;
  const std::string liberty = dir.Write("cells_elec.lib", kLibertyElectrical);
  const VerilogDesign design{dir.Write("top.v", kVerilog), "top",
                             dir.Write("top.sdc", kSdc)};
  TimingService service;
  const auto results = service.AnalyzeQor(
      design, {EstimateScene(1, "func_tt", "tt", liberty)});
  ASSERT_TRUE(results.ok()) << results.status().message();
  ASSERT_EQ(results.value().size(), 1u);
  const QorResult& result = results.value()[0];
  EXPECT_EQ(result.scenario.name, "func_tt");
  const auto metric = [&result](const std::string& name,
                                const std::string& unit) {
    const auto it = result.metrics.find(name);
    EXPECT_NE(it, result.metrics.end()) << "missing metric " << name;
    if (it == result.metrics.end()) return 0.0;
    EXPECT_EQ(it->second.unit, unit) << "metric " << name;
    return it->second.value;
  };
  EXPECT_NEAR(metric("worst_slack_ns", "ns"), 9.4, 1e-6);
  EXPECT_NEAR(metric("total_negative_slack_ns", "ns"), 0.0, 1e-9);
  EXPECT_NEAR(metric("worst_hold_slack_ns", "ns"), 0.45, 1e-6);
  ASSERT_EQ(result.setup_groups.size(), 1u);
  EXPECT_EQ(result.setup_groups[0].name, "clk");
  ASSERT_EQ(result.setup_groups[0].endpoints.size(), 1u);
  EXPECT_EQ(result.setup_groups[0].endpoints[0].pin, "r2/D");
  EXPECT_NEAR(result.setup_groups[0].endpoints[0].slack_ns, 9.4, 1e-6);
  ASSERT_EQ(result.hold_groups.size(), 1u);
  EXPECT_EQ(result.hold_groups[0].name, "clk");
  ASSERT_EQ(result.hold_groups[0].endpoints.size(), 1u);
  EXPECT_NEAR(result.hold_groups[0].endpoints[0].slack_ns, 0.45, 1e-6);
  EXPECT_EQ(result.total_cells, 3u);
  EXPECT_NEAR(result.total_area, 22.0, 1e-9);
  ASSERT_EQ(result.cells.size(), 2u);
  EXPECT_EQ(result.cells[0].cell, "DFF");
  EXPECT_EQ(result.cells[0].count, 2u);
  EXPECT_NEAR(result.cells[0].area, 20.0, 1e-9);
  EXPECT_EQ(result.cells[1].cell, "BUF");
  EXPECT_EQ(result.cells[1].count, 1u);
  EXPECT_NEAR(result.cells[1].area, 2.0, 1e-9);
  EXPECT_TRUE(result.max_transition_check.has_limit);
  EXPECT_EQ(result.max_transition_check.violation_count, 1u);
  EXPECT_NEAR(result.max_transition_check.worst_slack, -0.01, 1e-6);
  EXPECT_NEAR(metric("max_transition_violations", "pins"), 1.0, 1e-9);
  EXPECT_TRUE(result.max_capacitance_check.has_limit);
  EXPECT_EQ(result.max_capacitance_check.violation_count, 1u);
  EXPECT_TRUE(result.max_fanout_check.has_limit);
  EXPECT_EQ(result.max_fanout_check.violation_count, 1u);
}

TEST(TimingService, AnalyzeQorUnionTnsEqualsMetricsTns) {
  // The I9 worked example, at the service level: two group_path
  // commands both matching r2/D put the endpoint in both named
  // groups, so the union TNS (each endpoint once) is -0.1 and
  // equals the scalar total_negative_slack_ns, while the
  // every-group TNS (once per group) is -0.2.
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kLiberty);
  const std::string sdc =
      std::string(kSdcTight) +
      "group_path -name fast -to [get_pins r2/D]\n"
      "group_path -name slow -to [get_pins r2/D]\n";
  const VerilogDesign design{dir.Write("top.v", kVerilog), "top",
                             dir.Write("top.sdc", sdc)};
  TimingService service;
  const auto results = service.AnalyzeQor(
      design, {EstimateScene(1, "func_tt", "tt", liberty)});
  ASSERT_TRUE(results.ok()) << results.status().message();
  ASSERT_EQ(results.value().size(), 1u);
  const QorResult& result = results.value()[0];
  ASSERT_EQ(result.setup_groups.size(), 2u);
  EXPECT_EQ(result.setup_groups[0].name, "fast");
  EXPECT_EQ(result.setup_groups[1].name, "slow");
  std::map<std::string, double> by_pin;
  double every_group_tns = 0.0;
  for (const PathGroupResult& group : result.setup_groups) {
    ASSERT_EQ(group.endpoints.size(), 1u);
    EXPECT_EQ(group.endpoints[0].pin, "r2/D");
    EXPECT_NEAR(group.endpoints[0].slack_ns, -0.1, 1e-6 * 0.1 + 1e-7);
    by_pin[group.endpoints[0].pin] = group.endpoints[0].slack_ns;
    every_group_tns += group.endpoints[0].slack_ns;
  }
  double union_tns = 0.0;
  for (const auto& entry : by_pin) union_tns += entry.second;
  EXPECT_NEAR(union_tns, -0.1, 1e-6 * 0.1 + 1e-7);
  EXPECT_NEAR(every_group_tns, -0.2, 1e-6 * 0.2 + 1e-7);
  const auto metric = result.metrics.find("total_negative_slack_ns");
  ASSERT_NE(metric, result.metrics.end());
  EXPECT_NEAR(metric->second.value, union_tns, 1e-9);
  ASSERT_EQ(result.hold_groups.size(), 2u);
  for (const PathGroupResult& group : result.hold_groups) {
    ASSERT_EQ(group.endpoints.size(), 1u);
    EXPECT_NEAR(group.endpoints[0].slack_ns, 0.45, 1e-6);
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

TEST(TimingService, ElectricalViolatorsAreReportedPerScene) {
  // The violator lists (Immediate unit 6) carry every violating
  // pin per check with the check's own worst values, and the kept
  // check records name the worst pin, its limit and its slack, so
  // the verdict-shaped report can cost the exact sum of the
  // violations. The electrical fixture violates all three checks
  // on u1/Y only.
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
  ASSERT_EQ(scene.max_transition_violators.size(), 1u);
  EXPECT_EQ(scene.max_transition_violators[0].pin, "u1/Y");
  EXPECT_NEAR(scene.max_transition_violators[0].slack, -0.01, 1e-6);
  EXPECT_TRUE(scene.max_transition_check.has_limit);
  EXPECT_EQ(scene.max_transition_check.worst_pin, "u1/Y");
  EXPECT_NEAR(scene.max_transition_check.worst_slack, -0.01, 1e-6);
  ASSERT_EQ(scene.max_capacitance_violators.size(), 1u);
  EXPECT_EQ(scene.max_capacitance_violators[0].pin, "u1/Y");
  EXPECT_NEAR(scene.max_capacitance_violators[0].slack, -0.009, 1e-9);
  EXPECT_TRUE(scene.max_capacitance_check.has_limit);
  EXPECT_EQ(scene.max_capacitance_check.worst_pin, "u1/Y");
  ASSERT_EQ(scene.max_fanout_violators.size(), 1u);
  EXPECT_EQ(scene.max_fanout_violators[0].pin, "u1/Y");
  EXPECT_NEAR(scene.max_fanout_violators[0].slack, -1.0, 1e-9);
  EXPECT_TRUE(scene.max_fanout_check.has_limit);
  EXPECT_EQ(scene.max_fanout_check.worst_pin, "u1/Y");
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
  // No limit means no violators and no worst pin: the kept check
  // records report has_limit false, so the report can tell "no
  // limit defined" apart from "limit defined and clean".
  EXPECT_FALSE(scene.max_transition_check.has_limit);
  EXPECT_FALSE(scene.max_capacitance_check.has_limit);
  EXPECT_FALSE(scene.max_fanout_check.has_limit);
  EXPECT_TRUE(scene.max_transition_violators.empty());
  EXPECT_TRUE(scene.max_capacitance_violators.empty());
  EXPECT_TRUE(scene.max_fanout_violators.empty());
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

// S6 backfill: ReportPaths at the service level. The fixture's
// data path is 0.5 ns (0.2 CK->Q plus 0.3 BUF) against a 10 ns
// clock with a 0.1 ns setup constraint, so the setup worst path
// arrives at 0.5 ns, is required at 9.9 ns and slacks 9.4 ns,
// and the hold worst path slacks 0.45 ns.
TEST(TimingService, ReportPathsReturnsSetupWorstPath) {
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kLiberty);
  const VerilogDesign design{dir.Write("top.v", kVerilog), "top",
                             dir.Write("top.sdc", kSdc)};
  TimingService service;
  PathReportRequest request;
  const auto reports = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, request);
  ASSERT_TRUE(reports.ok()) << reports.status().message();
  ASSERT_EQ(reports.value().size(), 1u);
  ASSERT_EQ(reports.value()[0].paths.size(), 1u);
  const PathReport& path = reports.value()[0].paths[0];
  EXPECT_EQ(path.endpoint, "r2/D");
  EXPECT_NEAR(path.slack_ns, 9.4, 1e-6);
  EXPECT_NEAR(path.arrival_ns, 0.5, 1e-6);
  EXPECT_NEAR(path.required_ns, 9.9, 1e-6);
  EXPECT_FALSE(path.points.empty());
  EXPECT_FALSE(path.startpoint.empty());
}

TEST(TimingService, ReportPathsHoldKindAndEndpointFilter) {
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kLiberty);
  const VerilogDesign design{dir.Write("top.v", kVerilog), "top",
                             dir.Write("top.sdc", kSdc)};
  TimingService service;
  PathReportRequest hold;
  hold.kind = CheckKind::kHold;
  const auto held = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, hold);
  ASSERT_TRUE(held.ok()) << held.status().message();
  ASSERT_EQ(held.value()[0].paths.size(), 1u);
  EXPECT_NEAR(held.value()[0].paths[0].slack_ns, 0.45, 1e-6);

  PathReportRequest to;
  to.endpoint = "r2/D";
  const auto filtered = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, to);
  ASSERT_TRUE(filtered.ok()) << filtered.status().message();
  ASSERT_EQ(filtered.value()[0].paths.size(), 1u);
  EXPECT_EQ(filtered.value()[0].paths[0].endpoint, "r2/D");

  PathReportRequest unknown;
  unknown.endpoint = "r9/D";
  const auto failed = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, unknown);
  EXPECT_FALSE(failed.ok());
}

TEST(TimingService, ReportPathsRespectsMaxPathsAndSlackFilter) {
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kLiberty);
  const VerilogDesign design{dir.Write("top.v", kVerilog), "top",
                             dir.Write("top.sdc", kSdc)};
  TimingService service;
  PathReportRequest capped;
  capped.max_paths = 1;
  capped.slack_lesser_than = 9.5;
  const auto one = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, capped);
  ASSERT_TRUE(one.ok()) << one.status().message();
  ASSERT_EQ(one.value()[0].paths.size(), 1u);
  EXPECT_LT(one.value()[0].paths[0].slack_ns, 9.5);

  PathReportRequest none;
  none.slack_lesser_than = -1.0;
  const auto empty = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, none);
  ASSERT_TRUE(empty.ok()) << empty.status().message();
  EXPECT_TRUE(empty.value()[0].paths.empty());

  PathReportRequest zero;
  zero.max_paths = 0;
  const auto rejected = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, zero);
  ASSERT_FALSE(rejected.ok());
  EXPECT_EQ(rejected.status().code(), ErrorCode::kInvalidArgument);
}

// get_timing_paths (Immediate unit 12): nworst, -from,
// -through, -group, -max_paths and -slack_less_than at the
// service level. The two-path fixture gives two distinct paths
// to one endpoint, so nworst and the filters discriminate.
TEST(TimingService, ReportPathsNworstTwoReturnsBothPathsWorstFirst) {
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kLibertyTwoPath);
  const VerilogDesign design{dir.Write("top.v", kVerilogTwoPath), "top",
                             dir.Write("top.sdc", kSdc)};
  TimingService service;
  PathReportRequest request;
  request.endpoint = "r2/D";
  request.nworst = 2;
  const auto reports = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, request);
  ASSERT_TRUE(reports.ok()) << reports.status().message();
  ASSERT_EQ(reports.value().size(), 1u);
  ASSERT_EQ(reports.value()[0].paths.size(), 2u);
  const PathReport& slow = reports.value()[0].paths[0];
  EXPECT_EQ(slow.endpoint, "r2/D");
  EXPECT_EQ(slow.startpoint, "r1/Q");
  EXPECT_NEAR(slow.slack_ns, 9.0, 1e-6);
  EXPECT_NEAR(slow.arrival_ns, 0.9, 1e-6);
  EXPECT_EQ(slow.group, "clk");
  const PathReport& fast = reports.value()[0].paths[1];
  EXPECT_EQ(fast.endpoint, "r2/D");
  EXPECT_EQ(fast.startpoint, "r3/Q");
  EXPECT_NEAR(fast.slack_ns, 9.3, 1e-6);
  EXPECT_NEAR(fast.arrival_ns, 0.6, 1e-6);
  EXPECT_EQ(fast.group, "clk");
  // The two paths have distinct point sequences.
  std::vector<std::string> slow_pins;
  for (const PathPoint& point : slow.points) {
    slow_pins.push_back(point.pin);
  }
  std::vector<std::string> fast_pins;
  for (const PathPoint& point : fast.points) {
    fast_pins.push_back(point.pin);
  }
  EXPECT_NE(slow_pins, fast_pins);
  // nworst = 1 returns the worst path only, equal to the
  // existing single-path result.
  PathReportRequest one;
  one.endpoint = "r2/D";
  one.nworst = 1;
  const auto single = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, one);
  ASSERT_TRUE(single.ok()) << single.status().message();
  ASSERT_EQ(single.value()[0].paths.size(), 1u);
  EXPECT_EQ(single.value()[0].paths[0].startpoint, "r1/Q");
  EXPECT_NEAR(single.value()[0].paths[0].slack_ns, 9.0, 1e-6);
}

TEST(TimingService, ReportPathsFromFilterSelectsStartpoint) {
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kLibertyTwoPath);
  const VerilogDesign design{dir.Write("top.v", kVerilogTwoPath), "top",
                             dir.Write("top.sdc", kSdc)};
  TimingService service;
  PathReportRequest slow;
  slow.from = {"r1/Q"};
  const auto slow_paths = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, slow);
  ASSERT_TRUE(slow_paths.ok()) << slow_paths.status().message();
  ASSERT_EQ(slow_paths.value()[0].paths.size(), 1u);
  EXPECT_EQ(slow_paths.value()[0].paths[0].startpoint, "r1/Q");
  EXPECT_NEAR(slow_paths.value()[0].paths[0].slack_ns, 9.0, 1e-6);

  PathReportRequest fast;
  fast.from = {"r3/Q"};
  const auto fast_paths = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, fast);
  ASSERT_TRUE(fast_paths.ok()) << fast_paths.status().message();
  ASSERT_EQ(fast_paths.value()[0].paths.size(), 1u);
  EXPECT_EQ(fast_paths.value()[0].paths[0].startpoint, "r3/Q");

  // A from-clock selects paths launched by that clock: both
  // paths start at flops clocked by sys. The clock must be
  // named differently from its port (see kSdcSys): "clk" would
  // resolve to the top-level port pin first. nworst and
  // max_paths both need raising here: without -to, -max_paths
  // caps the total across endpoints, so the default 1 would
  // keep only the worst path.
  const VerilogDesign sys_design{
      dir.Write("top_sys.v", kVerilogTwoPath), "top",
      dir.Write("top_sys.sdc", kSdcSys)};
  PathReportRequest clocked;
  clocked.from = {"sys"};
  clocked.nworst = 2;
  clocked.max_paths = 2;
  const auto clocked_paths = service.ReportPaths(
      sys_design, {EstimateScene(1, "func_tt", "tt", liberty)}, clocked);
  ASSERT_TRUE(clocked_paths.ok()) << clocked_paths.status().message();
  ASSERT_EQ(clocked_paths.value()[0].paths.size(), 2u);

  // A from name that matches no object fails fast.
  PathReportRequest unknown;
  unknown.from = {"nope"};
  const auto failed = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, unknown);
  EXPECT_FALSE(failed.ok());
  EXPECT_EQ(failed.status().code(), ErrorCode::kInvalidArgument);

  // A from name matching an object with no path to the
  // endpoints yields an empty result, never an error: the
  // primary input d launches no clocked path to r2/D.
  PathReportRequest input;
  input.from = {"d"};
  const auto empty = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, input);
  ASSERT_TRUE(empty.ok()) << empty.status().message();
  EXPECT_TRUE(empty.value()[0].paths.empty());
}

TEST(TimingService, ReportPathsThroughFilterSelectsIntermediate) {
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kLibertyTwoPath);
  const VerilogDesign design{dir.Write("top.v", kVerilogTwoPath), "top",
                             dir.Write("top.sdc", kSdc)};
  TimingService service;
  PathReportRequest slow;
  slow.through = {"u1"};
  const auto slow_paths = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, slow);
  ASSERT_TRUE(slow_paths.ok()) << slow_paths.status().message();
  ASSERT_EQ(slow_paths.value()[0].paths.size(), 1u);
  EXPECT_EQ(slow_paths.value()[0].paths[0].startpoint, "r1/Q");

  PathReportRequest net;
  net.through = {"n2"};
  const auto net_paths = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, net);
  ASSERT_TRUE(net_paths.ok()) << net_paths.status().message();
  ASSERT_EQ(net_paths.value()[0].paths.size(), 1u);
  EXPECT_EQ(net_paths.value()[0].paths[0].startpoint, "r1/Q");

  PathReportRequest both;
  both.through = {"m1/Y"};
  both.nworst = 2;
  both.max_paths = 2;
  const auto both_paths = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, both);
  ASSERT_TRUE(both_paths.ok()) << both_paths.status().message();
  ASSERT_EQ(both_paths.value()[0].paths.size(), 2u);

  PathReportRequest unknown;
  unknown.through = {"nope"};
  const auto failed = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, unknown);
  EXPECT_FALSE(failed.ok());
  EXPECT_EQ(failed.status().code(), ErrorCode::kInvalidArgument);
}

TEST(TimingService, ReportPathsGroupSelection) {
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kLiberty);
  const VerilogDesign design{dir.Write("top.v", kVerilog), "top",
                             dir.Write("top.sdc", kSdc)};
  TimingService service;
  PathReportRequest grouped;
  grouped.group = "clk";
  const auto paths = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, grouped);
  ASSERT_TRUE(paths.ok()) << paths.status().message();
  ASSERT_EQ(paths.value()[0].paths.size(), 1u);
  EXPECT_EQ(paths.value()[0].paths[0].endpoint, "r2/D");
  EXPECT_EQ(paths.value()[0].paths[0].group, "clk");

  PathReportRequest unknown;
  unknown.group = "nope";
  const auto failed = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, unknown);
  EXPECT_FALSE(failed.ok());
  EXPECT_EQ(failed.status().code(), ErrorCode::kInvalidArgument);
  EXPECT_NE(failed.status().message().find("clk"), std::string::npos);
}

TEST(TimingService, ReportPathsUnconstrainedEndpointFails) {
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kLiberty);
  const VerilogDesign design{dir.Write("top.v", kVerilog), "top",
                             dir.Write("top.sdc", kSdc)};
  TimingService service;
  PathReportRequest request;
  request.endpoint = "q";
  const auto failed = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, request);
  EXPECT_FALSE(failed.ok());
  EXPECT_EQ(failed.status().code(), ErrorCode::kInvalidArgument);
}

TEST(TimingService, ReportPathsMaxPathsTruncatesNworst) {
  // Without -to, -max_paths caps the total across endpoints:
  // nworst = 2 collects two paths from the fixture's one
  // endpoint, then the cap keeps the worst only. (With -to the
  // endpoint branch returns the nworst paths directly, so this
  // test omits -to.)
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kLibertyTwoPath);
  const VerilogDesign design{dir.Write("top.v", kVerilogTwoPath), "top",
                             dir.Write("top.sdc", kSdc)};
  TimingService service;
  PathReportRequest request;
  request.nworst = 2;
  request.max_paths = 1;
  const auto reports = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, request);
  ASSERT_TRUE(reports.ok()) << reports.status().message();
  ASSERT_EQ(reports.value()[0].paths.size(), 1u);
  EXPECT_EQ(reports.value()[0].paths[0].startpoint, "r1/Q");
  EXPECT_NEAR(reports.value()[0].paths[0].slack_ns, 9.0, 1e-6);
}

TEST(TimingService, ReportPathsSlackCeilingOnTightClock) {
  // On the 0.5 ns clock the slow path slacks -0.5 ns and the
  // fast path 0.2 ns, so the ceiling discriminates between them.
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kLibertyTwoPath);
  const VerilogDesign design{dir.Write("top.v", kVerilogTwoPath), "top",
                             dir.Write("top.sdc", kSdcTight)};
  TimingService service;
  PathReportRequest violating;
  violating.nworst = 2;
  violating.slack_lesser_than = 0.0;
  const auto one = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, violating);
  ASSERT_TRUE(one.ok()) << one.status().message();
  ASSERT_EQ(one.value()[0].paths.size(), 1u);
  EXPECT_NEAR(one.value()[0].paths[0].slack_ns, -0.5, 1e-6);

  PathReportRequest both;
  both.nworst = 2;
  both.max_paths = 2;
  both.slack_lesser_than = 0.25;
  const auto two = service.ReportPaths(
      design, {EstimateScene(1, "func_tt", "tt", liberty)}, both);
  ASSERT_TRUE(two.ok()) << two.status().message();
  ASSERT_EQ(two.value()[0].paths.size(), 2u);
}

// Check-specific report data (Immediate unit 10, review item
// T8): the per-endpoint slacks, the per-group worst paths, the
// minimum-period check and the empty pulse-width list on the
// clean fixture, which defines no minimum width.
TEST(TimingService, AnalyzeCheckReportsCleanFixture) {
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kLiberty);
  const VerilogDesign design{dir.Write("top.v", kVerilog), "top",
                             dir.Write("top.sdc", kSdc)};
  TimingService service;
  const auto results = service.AnalyzeCheckReports(
      design, {EstimateScene(1, "func_tt", "tt", liberty)});
  ASSERT_TRUE(results.ok()) << results.status().message();
  ASSERT_EQ(results.value().size(), 1u);
  const CheckReportsResult& result = results.value()[0];
  EXPECT_EQ(result.scenario.name, "func_tt");
  // Only r2/D is a constrained endpoint: r1/D is driven by the
  // unclocked primary input.
  ASSERT_EQ(result.setup_endpoints.size(), 1u);
  EXPECT_EQ(result.setup_endpoints[0].pin, "r2/D");
  EXPECT_NEAR(result.setup_endpoints[0].slack_ns, 9.4, 1e-6);
  ASSERT_EQ(result.hold_endpoints.size(), 1u);
  EXPECT_EQ(result.hold_endpoints[0].pin, "r2/D");
  EXPECT_NEAR(result.hold_endpoints[0].slack_ns, 0.45, 1e-6);
  ASSERT_EQ(result.setup_groups.size(), 1u);
  EXPECT_EQ(result.setup_groups[0].name, "clk");
  ASSERT_EQ(result.setup_group_paths.size(), 1u);
  EXPECT_EQ(result.setup_group_paths[0].group, "clk");
  EXPECT_EQ(result.setup_group_paths[0].path.endpoint, "r2/D");
  EXPECT_EQ(result.setup_group_paths[0].path.startpoint, "r1/Q");
  EXPECT_NEAR(result.setup_group_paths[0].path.slack_ns, 9.4, 1e-6);
  ASSERT_EQ(result.hold_group_paths.size(), 1u);
  EXPECT_EQ(result.hold_group_paths[0].group, "clk");
  EXPECT_EQ(result.hold_group_paths[0].path.endpoint, "r2/D");
  // The derived minimum period: the 10 ns clock's group has
  // worst setup slack 9.4 ns, so the clock can close at 0.6 ns.
  ASSERT_EQ(result.min_period_clocks.size(), 1u);
  EXPECT_EQ(result.min_period_clocks[0].clock, "clk");
  EXPECT_NEAR(result.min_period_clocks[0].period_ns, 10.0, 1e-6);
  EXPECT_NEAR(result.min_period_clocks[0].min_period_ns, 0.6, 1e-6);
  EXPECT_NEAR(result.min_period_clocks[0].slack_ns, 9.4, 1e-6);
  EXPECT_EQ(result.min_period_clocks[0].worst_pin, "r2/D");
  ASSERT_EQ(result.min_period_endpoints.size(), 1u);
  EXPECT_EQ(result.min_period_endpoints[0].pin, "r2/D");
  EXPECT_EQ(result.min_period_endpoints[0].clock, "clk");
  EXPECT_NEAR(result.min_period_endpoints[0].min_period_ns, 0.6, 1e-6);
  EXPECT_NEAR(result.min_period_endpoints[0].slack_ns, 9.4, 1e-6);
  EXPECT_TRUE(result.min_pulse_width_checks.empty());
  // The clean fixture defines setup and hold checks at both D
  // pins, so the coverage carries four rows and no gating or
  // pulse-width rows.
  EXPECT_EQ(result.coverage_pins.size(), 4u);
  for (const CoveragePin& row : result.coverage_pins) {
    EXPECT_TRUE(row.check_type == "setup" || row.check_type == "hold")
        << row.check_type;
  }
}

}  // namespace
}  // namespace eos::analysis
