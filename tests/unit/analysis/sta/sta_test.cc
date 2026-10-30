// Unit tests: analysis/sta (the EOS facade over the imported
// OpenSTA).
//
// The fixtures are small synthetic texts, embedded here and written
// to a temporary directory per test: a Liberty library with one
// flip-flop (setup 0.1 ns, clock to Q 0.2 ns) and one buffer
// (0.3 ns), a two-flop netlist with the buffer on the data path, and
// SDC files that clock it. They are licence-clean by construction.
// The reg-to-reg path delay is therefore 0.2 + 0.3 + 0.1 = 0.6 ns by
// construction, so a 10 ns clock gives worst slack 9.4 ns and a
// 0.5 ns clock gives worst slack -0.1 ns with total negative slack
// -0.1 ns (one violating endpoint).
//
// Precision provenance: the expected constants below are the values
// reported by the pinned upstream OpenSTA executable (commit
// 0f2529d18218b87aa0e82de444395a830d5058a1, built locally from the
// pristine pin checkout on 2026-10-25) on this identical fixture.
// Its exact output lines were "WORST_SLACK_MAX=9.4000001554312274"
// and "TNS_MAX=0" with the 10 ns SDC, and
// "WORST_SLACK_MAX=-0.10000001110223056" and
// "TNS_MAX=-0.10000001110223056" with the 0.5 ns SDC (the printed
// doubles of the underlying float slacks; report_worst_slack with
// -digits 9 printed 9.400000572 and -0.100000009). The committed
// constants are those values to float precision, and the tests
// compare within 1e-6 relative so only float representation noise
// is tolerated, never model drift. The upstream run scripts and
// logs are kept with the unit 2.2 evidence.
#include "eos/analysis/sta.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

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

// The electrical variant of the fixture library (unit F5): the
// same two cells, with three limits on the buffer's output pin
// and a fanout load on the flip-flop's data pin. By construction
// the buffer output (u1/Y) then violates all three checks by a
// known amount: its transition is 0.05 ns against a 0.04 ns
// limit, its load capacitance is the data pin's 0.01 pF
// against a 0.001 pF limit (a slack of 9 fF, above the upstream
// analyzer's own 1 fF float tolerance for counting a
// capacitance violation), and its fanout load is the data
// pin's 2.0 against a 1.0 limit. No other pin carries a limit,
// so each check's violating-pin count is exactly 1 and its
// worst pin is u1/Y.
const char kLibertyElectrical[] = R"LIB(library (eos_fixture_elec) {
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

// Expected values, as reported by the pinned upstream executable
// (see the provenance note at the top of this file).
constexpr double kWorstSlackNs = 9.4;
constexpr double kWorstSlackTightNs = -0.1;
constexpr double kTnsTightNs = -0.1;

class TempDir {
 public:
  TempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("eos_sta_test_" + std::to_string(counter_++))) {
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

// Loads the fixture into engine with the given SDC text.
void LoadFixture(StaEngine& engine, const TempDir& dir,
                 const std::string& sdc) {
  ASSERT_TRUE(engine.ReadLiberty(dir.Write("cells.lib", kLiberty)).ok());
  ASSERT_TRUE(engine.ReadVerilog(dir.Write("top.v", kVerilog)).ok());
  ASSERT_TRUE(engine.LinkDesign("top").ok());
  ASSERT_TRUE(engine.ReadSdc(dir.Write("top.sdc", sdc)).ok());
}

TEST(StaFacade, WorstSlackMatchesUpstream) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto slack = engine.WorstSlackNs();
  ASSERT_TRUE(slack.ok()) << slack.status().message();
  EXPECT_NEAR(slack.value(), kWorstSlackNs, 1e-6 * std::abs(kWorstSlackNs));
  const auto tns = engine.TotalNegativeSlackNs();
  ASSERT_TRUE(tns.ok()) << tns.status().message();
  EXPECT_NEAR(tns.value(), 0.0, 1e-9);
}

TEST(StaFacade, HoldSlackMatchesUpstream) {
  // Measured with the pinned upstream executable on this identical
  // fixture on 2026-10-25: WORST_SLACK_MIN=0.449999994449 and
  // TNS_MIN=0 with the 10 ns SDC.
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto hold = engine.WorstHoldSlackNs();
  ASSERT_TRUE(hold.ok()) << hold.status().message();
  EXPECT_NEAR(hold.value(), 0.45, 1e-6 * 0.45);
  const auto tns = engine.TotalNegativeHoldSlackNs();
  ASSERT_TRUE(tns.ok()) << tns.status().message();
  EXPECT_NEAR(tns.value(), 0.0, 1e-9);
}

TEST(StaFacade, NegativeSlackAndTnsMatchUpstream) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdcTight);
  const auto slack = engine.WorstSlackNs();
  ASSERT_TRUE(slack.ok()) << slack.status().message();
  EXPECT_NEAR(slack.value(), kWorstSlackTightNs,
              1e-6 * std::abs(kWorstSlackTightNs) + 1e-7);
  const auto tns = engine.TotalNegativeSlackNs();
  ASSERT_TRUE(tns.ok()) << tns.status().message();
  EXPECT_NEAR(tns.value(), kTnsTightNs,
              1e-6 * std::abs(kTnsTightNs) + 1e-7);
}

TEST(StaFacade, ClearDesignReloadsAnotherNetlistInOneSession) {
  // Synthesis unit S3's session-resident STA: one engine
  // evaluates two designs in turn. After a clear the session
  // answers no query until it is linked and constrained again,
  // and the second design loads WITHOUT re-reading the Liberty,
  // which proves the libraries survive the clear: the tight
  // clock then reports the same -0.1 ns the fresh-session test
  // measures, not a missing-library failure.
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto relaxed = engine.WorstSlackNs();
  ASSERT_TRUE(relaxed.ok()) << relaxed.status().message();
  EXPECT_NEAR(relaxed.value(), kWorstSlackNs,
              1e-6 * std::abs(kWorstSlackNs));
  ASSERT_TRUE(engine.ClearDesign().ok());
  const auto cleared = engine.WorstSlackNs();
  EXPECT_FALSE(cleared.ok());
  EXPECT_EQ(cleared.status().code(), ErrorCode::kInvalidState);
  ASSERT_TRUE(engine.ReadVerilog(dir.Write("top2.v", kVerilog)).ok());
  ASSERT_TRUE(engine.LinkDesign("top").ok());
  ASSERT_TRUE(engine.ReadSdc(dir.Write("top2.sdc", kSdcTight)).ok());
  const auto tight = engine.WorstSlackNs();
  ASSERT_TRUE(tight.ok()) << tight.status().message();
  EXPECT_NEAR(tight.value(), kWorstSlackTightNs,
              1e-6 * std::abs(kWorstSlackTightNs) + 1e-7);
  const auto tns = engine.TotalNegativeSlackNs();
  ASSERT_TRUE(tns.ok()) << tns.status().message();
  EXPECT_NEAR(tns.value(), kTnsTightNs,
              1e-6 * std::abs(kTnsTightNs) + 1e-7);
}

TEST(StaFacade, ViolatingEndpointsMatchUpstream) {
  // With the 0.5 ns clock the only violating endpoint is r2's D
  // pin, at the same -0.1 ns the scalar queries report (see the
  // provenance note at the top of this file). Hold has no
  // violating endpoint: its worst slack is positive.
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdcTight);
  const auto setup = engine.ViolatingEndpoints(CheckKind::kSetup);
  ASSERT_TRUE(setup.ok()) << setup.status().message();
  ASSERT_EQ(setup.value().size(), 1u);
  EXPECT_EQ(setup.value()[0].pin, "r2/D");
  EXPECT_NEAR(setup.value()[0].slack_ns, kWorstSlackTightNs,
              1e-6 * std::abs(kWorstSlackTightNs) + 1e-7);
  const auto hold = engine.ViolatingEndpoints(CheckKind::kHold);
  ASSERT_TRUE(hold.ok()) << hold.status().message();
  EXPECT_TRUE(hold.value().empty());
}

TEST(StaFacade, NoViolatingEndpointsAtRelaxedClock) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto setup = engine.ViolatingEndpoints(CheckKind::kSetup);
  ASSERT_TRUE(setup.ok()) << setup.status().message();
  EXPECT_TRUE(setup.value().empty());
  const auto hold = engine.ViolatingEndpoints(CheckKind::kHold);
  ASSERT_TRUE(hold.ok()) << hold.status().message();
  EXPECT_TRUE(hold.value().empty());
}

TEST(StaFacade, WorstSetupPathWalksTheCriticalCone) {
  // The reg-to-reg path runs r1 Q through the buffer into r2 D:
  // the expanded worst path must name those pins in that order,
  // with the owning instances alongside, and end at the endpoint.
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdcTight);
  const auto path = engine.WorstPath(CheckKind::kSetup, "r2/D");
  ASSERT_TRUE(path.ok()) << path.status().message();
  ASSERT_EQ(path.value().size(), 4u);
  EXPECT_EQ(path.value()[0].pin, "r1/Q");
  EXPECT_EQ(path.value()[0].instance, "r1");
  EXPECT_EQ(path.value()[1].pin, "u1/A");
  EXPECT_EQ(path.value()[1].instance, "u1");
  EXPECT_EQ(path.value()[2].pin, "u1/Y");
  EXPECT_EQ(path.value()[2].instance, "u1");
  EXPECT_EQ(path.value()[3].pin, "r2/D");
  EXPECT_EQ(path.value()[3].instance, "r2");
}

TEST(StaFacade, WorstHoldPathUsesTheSameCone) {
  // Worst path queries are not limited to violating endpoints:
  // the hold check on r2/D runs over the same reg-to-reg cone.
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto path = engine.WorstPath(CheckKind::kHold, "r2/D");
  ASSERT_TRUE(path.ok()) << path.status().message();
  ASSERT_FALSE(path.value().empty());
  EXPECT_EQ(path.value().back().pin, "r2/D");
  EXPECT_EQ(path.value().front().pin, "r1/Q");
}

void LoadElectricalFixture(StaEngine& engine, const TempDir& dir) {
  ASSERT_TRUE(
      engine.ReadLiberty(dir.Write("cells_elec.lib", kLibertyElectrical))
          .ok());
  ASSERT_TRUE(engine.ReadVerilog(dir.Write("top.v", kVerilog)).ok());
  ASSERT_TRUE(engine.LinkDesign("top").ok());
  ASSERT_TRUE(engine.ReadSdc(dir.Write("top.sdc", kSdc)).ok());
}

TEST(StaFacade, ElectricalChecksReportTheFixtureViolations) {
  // Expected values by construction (see the fixture note):
  // u1/Y violates all three checks, and it is the only
  // violating pin of each check.
  TempDir dir;
  StaEngine engine;
  LoadElectricalFixture(engine, dir);
  const auto transition = engine.MaxTransitionCheck();
  ASSERT_TRUE(transition.ok()) << transition.status().message();
  EXPECT_TRUE(transition.value().has_limit);
  EXPECT_EQ(transition.value().worst_pin, "u1/Y");
  EXPECT_NEAR(transition.value().worst_value, 0.05, 1e-6);
  EXPECT_NEAR(transition.value().worst_limit, 0.04, 1e-6);
  EXPECT_NEAR(transition.value().worst_slack, -0.01, 1e-6);
  EXPECT_EQ(transition.value().violation_count, 1u);
  const auto capacitance = engine.MaxCapacitanceCheck();
  ASSERT_TRUE(capacitance.ok()) << capacitance.status().message();
  EXPECT_TRUE(capacitance.value().has_limit);
  EXPECT_EQ(capacitance.value().worst_pin, "u1/Y");
  EXPECT_NEAR(capacitance.value().worst_value, 0.01, 1e-9);
  EXPECT_NEAR(capacitance.value().worst_limit, 0.001, 1e-9);
  EXPECT_NEAR(capacitance.value().worst_slack, -0.009, 1e-9);
  EXPECT_EQ(capacitance.value().violation_count, 1u);
  const auto fanout = engine.MaxFanoutCheck();
  ASSERT_TRUE(fanout.ok()) << fanout.status().message();
  EXPECT_TRUE(fanout.value().has_limit);
  EXPECT_EQ(fanout.value().worst_pin, "u1/Y");
  EXPECT_NEAR(fanout.value().worst_value, 2.0, 1e-9);
  EXPECT_NEAR(fanout.value().worst_limit, 1.0, 1e-9);
  EXPECT_NEAR(fanout.value().worst_slack, -1.0, 1e-9);
  EXPECT_EQ(fanout.value().violation_count, 1u);
}

TEST(StaFacade, ElectricalChecksWithoutLimitsReportNoLimit) {
  // The plain fixture library carries no electrical limit on
  // any pin: every check must report has_limit false and a
  // zero count, not a fabricated clean worst value.
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto transition = engine.MaxTransitionCheck();
  ASSERT_TRUE(transition.ok()) << transition.status().message();
  EXPECT_FALSE(transition.value().has_limit);
  EXPECT_TRUE(transition.value().worst_pin.empty());
  EXPECT_EQ(transition.value().violation_count, 0u);
  const auto capacitance = engine.MaxCapacitanceCheck();
  ASSERT_TRUE(capacitance.ok()) << capacitance.status().message();
  EXPECT_FALSE(capacitance.value().has_limit);
  EXPECT_EQ(capacitance.value().violation_count, 0u);
  const auto fanout = engine.MaxFanoutCheck();
  ASSERT_TRUE(fanout.ok()) << fanout.status().message();
  EXPECT_FALSE(fanout.value().has_limit);
  EXPECT_EQ(fanout.value().violation_count, 0u);
}

TEST(StaFacade, ElectricalQueriesBeforeLinkFail) {
  StaEngine engine;
  const auto transition = engine.MaxTransitionCheck();
  EXPECT_FALSE(transition.ok());
  EXPECT_EQ(transition.status().code(), ErrorCode::kInvalidState);
  const auto capacitance = engine.MaxCapacitanceCheck();
  EXPECT_FALSE(capacitance.ok());
  EXPECT_EQ(capacitance.status().code(), ErrorCode::kInvalidState);
  const auto fanout = engine.MaxFanoutCheck();
  EXPECT_FALSE(fanout.ok());
  EXPECT_EQ(fanout.status().code(), ErrorCode::kInvalidState);
}

TEST(StaFacade, WorstPathToUnknownEndpointFails) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdcTight);
  const auto path = engine.WorstPath(CheckKind::kSetup, "r9/D");
  EXPECT_FALSE(path.ok());
  EXPECT_EQ(path.status().code(), ErrorCode::kInvalidArgument);
}

TEST(StaFacade, WorstPathPointsCarryTimingData) {
  // Expected values by construction from the fixture: the DFF
  // clock-to-Q is 0.2 ns, the buffer delay is 0.3 ns, the buffer
  // output transition is 0.05 ns and the flop output transition
  // is 0.1 ns, every load pin carries 0.001 pF, and each net
  // drives exactly one load pin.
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto path = engine.WorstPath(CheckKind::kSetup, "r2/D");
  ASSERT_TRUE(path.ok()) << path.status().message();
  ASSERT_EQ(path.value().size(), 4u);
  const double arrivals[] = {0.2, 0.2, 0.5, 0.5};
  const double incrs[] = {0.2, 0.0, 0.3, 0.0};
  const double slews[] = {0.1, 0.1, 0.05, 0.05};
  const double fanouts[] = {1.0, 0.0, 1.0, 0.0};
  const double caps[] = {0.001, -1.0, 0.001, -1.0};
  const char* cells[] = {"DFF", "BUF", "BUF", "DFF"};
  for (std::size_t i = 0; i < path.value().size(); ++i) {
    const PathPoint& point = path.value()[i];
    EXPECT_NEAR(point.arrival_ns, arrivals[i], 1e-6);
    EXPECT_NEAR(point.incr_ns, incrs[i], 1e-6);
    EXPECT_NEAR(point.slew_ns, slews[i], 1e-6);
    EXPECT_NEAR(point.fanout, fanouts[i], 1e-6);
    EXPECT_NEAR(point.cap_pf, caps[i], 1e-9);
    EXPECT_EQ(point.cell, cells[i]);
    EXPECT_EQ(point.edge, "^");
  }
}

TEST(StaFacade, WorstPathReportHeader) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto report = engine.WorstPathReport(CheckKind::kSetup, "r2/D");
  ASSERT_TRUE(report.ok()) << report.status().message();
  EXPECT_EQ(report.value().endpoint, "r2/D");
  EXPECT_EQ(report.value().startpoint, "r1/Q");
  EXPECT_NEAR(report.value().slack_ns, 9.4, 1e-6);
  EXPECT_NEAR(report.value().arrival_ns, 0.5, 1e-6);
  EXPECT_NEAR(report.value().required_ns, 9.9, 1e-6);
  ASSERT_EQ(report.value().points.size(), 4u);
  EXPECT_EQ(report.value().points.back().pin, "r2/D");
}

TEST(StaFacade, WorstPathReportHeaderOnTightClock) {
  // One engine per test: the upstream analyzer is a singleton,
  // so a second live engine in the same test fails to own it.
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdcTight);
  const auto report = engine.WorstPathReport(CheckKind::kSetup, "r2/D");
  ASSERT_TRUE(report.ok()) << report.status().message();
  EXPECT_NEAR(report.value().slack_ns, -0.1, 1e-6);
  EXPECT_NEAR(report.value().arrival_ns, 0.5, 1e-6);
  EXPECT_NEAR(report.value().required_ns, 0.4, 1e-6);
}

TEST(StaFacade, EndpointsBySlackIncludesMetEndpoints) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto endpoints = engine.EndpointsBySlack(CheckKind::kSetup);
  ASSERT_TRUE(endpoints.ok()) << endpoints.status().message();
  ASSERT_FALSE(endpoints.value().empty());
  // r2/D meets timing on the 10 ns clock, so the violating-only
  // query would omit it; this query includes it, worst first.
  bool found = false;
  for (const EndpointSlack& endpoint : endpoints.value()) {
    if (endpoint.pin == "r2/D") {
      found = true;
      EXPECT_NEAR(endpoint.slack_ns, 9.4, 1e-6);
    }
  }
  EXPECT_TRUE(found);
  for (std::size_t i = 1; i < endpoints.value().size(); ++i) {
    EXPECT_LE(endpoints.value()[i - 1].slack_ns,
              endpoints.value()[i].slack_ns);
  }
}

TEST(StaFacade, WorstPathReportToUnknownEndpointFails) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto report = engine.WorstPathReport(CheckKind::kSetup, "r9/D");
  EXPECT_FALSE(report.ok());
  EXPECT_EQ(report.status().code(), ErrorCode::kInvalidArgument);
}

TEST(StaFacade, WorstPathToUnconstrainedEndpointFails) {
  // The top-level q port is an endpoint with no timing check
  // behind it (no output delay in the SDC), so it has no
  // constrained path end and no worst path.
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdcTight);
  const auto path = engine.WorstPath(CheckKind::kSetup, "q");
  EXPECT_FALSE(path.ok());
  EXPECT_EQ(path.status().code(), ErrorCode::kInvalidArgument);
}

TEST(StaFacade, EndpointQueriesBeforeLinkFail) {
  StaEngine engine;
  const auto endpoints = engine.ViolatingEndpoints(CheckKind::kSetup);
  EXPECT_FALSE(endpoints.ok());
  EXPECT_EQ(endpoints.status().code(), ErrorCode::kInvalidState);
  const auto path = engine.WorstPath(CheckKind::kSetup, "r2/D");
  EXPECT_FALSE(path.ok());
  EXPECT_EQ(path.status().code(), ErrorCode::kInvalidState);
}

TEST(StaFacade, MissingLibertyFails) {
  TempDir dir;
  StaEngine engine;
  const Status status = engine.ReadLiberty(dir.Path("no_such.lib"));
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kIoError);
}

TEST(StaFacade, SlackBeforeLinkFails) {
  StaEngine engine;
  const auto slack = engine.WorstSlackNs();
  EXPECT_FALSE(slack.ok());
  EXPECT_EQ(slack.status().code(), ErrorCode::kInvalidState);
  const auto tns = engine.TotalNegativeSlackNs();
  EXPECT_FALSE(tns.ok());
  EXPECT_EQ(tns.status().code(), ErrorCode::kInvalidState);
}

}  // namespace
}  // namespace eos::analysis
