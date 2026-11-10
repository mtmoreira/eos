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
// The clock named differently from its port, so -from/-through
// names resolve to the clock itself: "clk" names the top-level
// port pin first (pin, net, instance, then clock), so a
// from-clock test must use a name no pin carries.
const char kSdcSys[] = "create_clock -name sys -period 10 [get_ports clk]\n";

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

// The two-path fixture library: a MUX cell in its own library,
// loaded alongside the base fixture library. The A input is the
// slow arc (0.4 ns) and the B input the fast one (0.1 ns), so the
// two paths to one endpoint have distinct, computable slacks.
const char kLibertyMux[] = R"LIB(library (eos_fixture_mux) {
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

// Loads the fixture into engine with the given SDC text.
void LoadFixture(StaEngine& engine, const TempDir& dir,
                 const std::string& sdc) {
  ASSERT_TRUE(engine.ReadLiberty(dir.Write("cells.lib", kLiberty)).ok());
  ASSERT_TRUE(engine.ReadVerilog(dir.Write("top.v", kVerilog)).ok());
  ASSERT_TRUE(engine.LinkDesign("top").ok());
  ASSERT_TRUE(engine.ReadSdc(dir.Write("top.sdc", sdc)).ok());
}

// Loads the two-path fixture: the base library plus the MUX
// library, then the two-path netlist.
void LoadTwoPathFixture(StaEngine& engine, const TempDir& dir,
                        const std::string& sdc) {
  ASSERT_TRUE(engine.ReadLiberty(dir.Write("cells.lib", kLiberty)).ok());
  ASSERT_TRUE(engine.ReadLiberty(dir.Write("mux.lib", kLibertyMux)).ok());
  ASSERT_TRUE(
      engine.ReadVerilog(dir.Write("top.v", kVerilogTwoPath)).ok());
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

// QoR groups and cell area (Immediate unit 9, review item I9):
// PathGroupSlacks enumerates the groups the timing engine knows,
// with every constrained endpoint in each, and CellArea counts
// the leaf instances by Liberty cell.
TEST(StaFacade, PathGroupSlacksDefaultClockGroup) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto setup = engine.PathGroupSlacks(CheckKind::kSetup);
  ASSERT_TRUE(setup.ok()) << setup.status().message();
  ASSERT_EQ(setup.value().size(), 1u);
  EXPECT_EQ(setup.value()[0].name, "clk");
  ASSERT_EQ(setup.value()[0].endpoints.size(), 1u);
  EXPECT_EQ(setup.value()[0].endpoints[0].pin, "r2/D");
  EXPECT_NEAR(setup.value()[0].endpoints[0].slack_ns, kWorstSlackNs,
              1e-6 * std::abs(kWorstSlackNs));
  const auto hold = engine.PathGroupSlacks(CheckKind::kHold);
  ASSERT_TRUE(hold.ok()) << hold.status().message();
  ASSERT_EQ(hold.value().size(), 1u);
  EXPECT_EQ(hold.value()[0].name, "clk");
  ASSERT_EQ(hold.value()[0].endpoints.size(), 1u);
  EXPECT_EQ(hold.value()[0].endpoints[0].pin, "r2/D");
  EXPECT_NEAR(hold.value()[0].endpoints[0].slack_ns, 0.45, 1e-6);
}

TEST(StaFacade, PathGroupSlacksTightClock) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdcTight);
  const auto setup = engine.PathGroupSlacks(CheckKind::kSetup);
  ASSERT_TRUE(setup.ok()) << setup.status().message();
  ASSERT_EQ(setup.value().size(), 1u);
  EXPECT_EQ(setup.value()[0].name, "clk");
  ASSERT_EQ(setup.value()[0].endpoints.size(), 1u);
  EXPECT_NEAR(setup.value()[0].endpoints[0].slack_ns, kWorstSlackTightNs,
              1e-6 * std::abs(kWorstSlackTightNs) + 1e-7);
}

TEST(StaFacade, PathGroupSlacksNamedGroupsShareEndpoint) {
  // The I9 worked example: two group_path commands both matching
  // r2/D put the endpoint in both named groups and in neither
  // clock group (group_path exceptions take precedence), so the
  // union TNS is -0.1 over one endpoint while the every-group
  // TNS is -0.2 over two.
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir,
              std::string(kSdcTight) +
                  "group_path -name fast -to [get_pins r2/D]\n"
                  "group_path -name slow -to [get_pins r2/D]\n");
  const auto setup = engine.PathGroupSlacks(CheckKind::kSetup);
  ASSERT_TRUE(setup.ok()) << setup.status().message();
  ASSERT_EQ(setup.value().size(), 2u);
  EXPECT_EQ(setup.value()[0].name, "fast");
  EXPECT_EQ(setup.value()[1].name, "slow");
  for (const PathGroupSlack& group : setup.value()) {
    ASSERT_EQ(group.endpoints.size(), 1u);
    EXPECT_EQ(group.endpoints[0].pin, "r2/D");
    EXPECT_NEAR(group.endpoints[0].slack_ns, kWorstSlackTightNs,
                1e-6 * std::abs(kWorstSlackTightNs) + 1e-7);
  }
}

TEST(StaFacade, CellAreaCountsLeafInstances) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto area = engine.CellArea();
  ASSERT_TRUE(area.ok()) << area.status().message();
  EXPECT_EQ(area.value().total_cells, 3u);
  EXPECT_NEAR(area.value().total_area, 22.0, 1e-9);
  ASSERT_EQ(area.value().rows.size(), 2u);
  EXPECT_EQ(area.value().rows[0].cell, "DFF");
  EXPECT_EQ(area.value().rows[0].count, 2u);
  EXPECT_NEAR(area.value().rows[0].area, 20.0, 1e-9);
  EXPECT_EQ(area.value().rows[1].cell, "BUF");
  EXPECT_EQ(area.value().rows[1].count, 1u);
  EXPECT_NEAR(area.value().rows[1].area, 2.0, 1e-9);
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

TEST(StaFacade, ElectricalViolatorsMatchWorstPinAndCount) {
  // The violator enumeration (Immediate unit 6) must list exactly
  // the pins the bulk check counts: u1/Y is the only violating
  // pin of each check, so each list has one entry, the worst pin,
  // with the check's own worst values, and each list size equals
  // the check's violation count.
  TempDir dir;
  StaEngine engine;
  LoadElectricalFixture(engine, dir);
  const auto transition = engine.MaxTransitionViolators();
  ASSERT_TRUE(transition.ok()) << transition.status().message();
  ASSERT_EQ(transition.value().size(), 1u);
  EXPECT_EQ(transition.value()[0].pin, "u1/Y");
  EXPECT_NEAR(transition.value()[0].value, 0.05, 1e-6);
  EXPECT_NEAR(transition.value()[0].limit, 0.04, 1e-6);
  EXPECT_NEAR(transition.value()[0].slack, -0.01, 1e-6);
  const auto transition_check = engine.MaxTransitionCheck();
  ASSERT_TRUE(transition_check.ok())
      << transition_check.status().message();
  EXPECT_EQ(transition.value().size(),
            transition_check.value().violation_count);
  const auto capacitance = engine.MaxCapacitanceViolators();
  ASSERT_TRUE(capacitance.ok()) << capacitance.status().message();
  ASSERT_EQ(capacitance.value().size(), 1u);
  EXPECT_EQ(capacitance.value()[0].pin, "u1/Y");
  EXPECT_NEAR(capacitance.value()[0].value, 0.01, 1e-9);
  EXPECT_NEAR(capacitance.value()[0].limit, 0.001, 1e-9);
  EXPECT_NEAR(capacitance.value()[0].slack, -0.009, 1e-9);
  const auto capacitance_check = engine.MaxCapacitanceCheck();
  ASSERT_TRUE(capacitance_check.ok())
      << capacitance_check.status().message();
  EXPECT_EQ(capacitance.value().size(),
            capacitance_check.value().violation_count);
  const auto fanout = engine.MaxFanoutViolators();
  ASSERT_TRUE(fanout.ok()) << fanout.status().message();
  ASSERT_EQ(fanout.value().size(), 1u);
  EXPECT_EQ(fanout.value()[0].pin, "u1/Y");
  EXPECT_NEAR(fanout.value()[0].value, 2.0, 1e-9);
  EXPECT_NEAR(fanout.value()[0].limit, 1.0, 1e-9);
  EXPECT_NEAR(fanout.value()[0].slack, -1.0, 1e-9);
  const auto fanout_check = engine.MaxFanoutCheck();
  ASSERT_TRUE(fanout_check.ok()) << fanout_check.status().message();
  EXPECT_EQ(fanout.value().size(), fanout_check.value().violation_count);
}

TEST(StaFacade, ElectricalViolatorsAreEmptyWithoutLimits) {
  // The plain fixture library carries no electrical limit on
  // any pin: every violator list is empty, not a fabricated
  // clean worst value.
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto transition = engine.MaxTransitionViolators();
  ASSERT_TRUE(transition.ok()) << transition.status().message();
  EXPECT_TRUE(transition.value().empty());
  const auto capacitance = engine.MaxCapacitanceViolators();
  ASSERT_TRUE(capacitance.ok()) << capacitance.status().message();
  EXPECT_TRUE(capacitance.value().empty());
  const auto fanout = engine.MaxFanoutViolators();
  ASSERT_TRUE(fanout.ok()) << fanout.status().message();
  EXPECT_TRUE(fanout.value().empty());
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
  const auto transition_violators = engine.MaxTransitionViolators();
  EXPECT_FALSE(transition_violators.ok());
  EXPECT_EQ(transition_violators.status().code(),
            ErrorCode::kInvalidState);
  const auto capacitance_violators = engine.MaxCapacitanceViolators();
  EXPECT_FALSE(capacitance_violators.ok());
  EXPECT_EQ(capacitance_violators.status().code(),
            ErrorCode::kInvalidState);
  const auto fanout_violators = engine.MaxFanoutViolators();
  EXPECT_FALSE(fanout_violators.ok());
  EXPECT_EQ(fanout_violators.status().code(), ErrorCode::kInvalidState);
}

TEST(StaFacade, WorstPathToUnknownEndpointFails) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdcTight);
  const auto path = engine.WorstPath(CheckKind::kSetup, "r9/D");
  EXPECT_FALSE(path.ok());
  EXPECT_EQ(path.status().code(), ErrorCode::kNotFound);
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

// WorstPaths (Immediate unit 12, review item I8): the
// per-endpoint enumeration. nworst = 1 is exactly
// WorstPathReport; nworst = 2 on the two-path fixture returns
// the two distinct paths worst first; a larger nworst returns
// what exists; an unknown endpoint fails with the existing
// messages; two consecutive calls return independent data.
TEST(StaFacade, WorstPathsNworstOneEqualsWorstPathReport) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto paths = engine.WorstPaths(CheckKind::kSetup, "r2/D", 1);
  ASSERT_TRUE(paths.ok()) << paths.status().message();
  ASSERT_EQ(paths.value().size(), 1u);
  const auto report = engine.WorstPathReport(CheckKind::kSetup, "r2/D");
  ASSERT_TRUE(report.ok()) << report.status().message();
  const PathReport& one = paths.value()[0];
  EXPECT_EQ(one.endpoint, report.value().endpoint);
  EXPECT_EQ(one.startpoint, report.value().startpoint);
  EXPECT_DOUBLE_EQ(one.slack_ns, report.value().slack_ns);
  EXPECT_DOUBLE_EQ(one.arrival_ns, report.value().arrival_ns);
  EXPECT_DOUBLE_EQ(one.required_ns, report.value().required_ns);
  ASSERT_EQ(one.points.size(), report.value().points.size());
  for (std::size_t i = 0; i < one.points.size(); ++i) {
    EXPECT_EQ(one.points[i].pin, report.value().points[i].pin);
  }
  EXPECT_EQ(one.group, "clk");
}

TEST(StaFacade, WorstPathsTwoPathsWorstFirst) {
  TempDir dir;
  StaEngine engine;
  LoadTwoPathFixture(engine, dir, kSdc);
  const auto paths = engine.WorstPaths(CheckKind::kSetup, "r2/D", 2);
  ASSERT_TRUE(paths.ok()) << paths.status().message();
  ASSERT_EQ(paths.value().size(), 2u);
  EXPECT_EQ(paths.value()[0].endpoint, "r2/D");
  EXPECT_EQ(paths.value()[0].startpoint, "r1/Q");
  EXPECT_NEAR(paths.value()[0].slack_ns, 9.0, 1e-6);
  EXPECT_NEAR(paths.value()[0].arrival_ns, 0.9, 1e-6);
  EXPECT_EQ(paths.value()[0].group, "clk");
  EXPECT_EQ(paths.value()[1].endpoint, "r2/D");
  EXPECT_EQ(paths.value()[1].startpoint, "r3/Q");
  EXPECT_NEAR(paths.value()[1].slack_ns, 9.3, 1e-6);
  EXPECT_NEAR(paths.value()[1].arrival_ns, 0.6, 1e-6);
  EXPECT_EQ(paths.value()[1].group, "clk");
  // Distinct point sequences: the slow path runs through m1/A,
  // the fast one through m1/B.
  std::vector<std::string> slow;
  for (const PathPoint& point : paths.value()[0].points) {
    slow.push_back(point.pin);
  }
  std::vector<std::string> fast;
  for (const PathPoint& point : paths.value()[1].points) {
    fast.push_back(point.pin);
  }
  EXPECT_NE(slow, fast);
  EXPECT_EQ(slow.back(), "r2/D");
  EXPECT_EQ(fast.back(), "r2/D");
  // A larger nworst returns what exists: no padding, no error.
  const auto more = engine.WorstPaths(CheckKind::kSetup, "r2/D", 5);
  ASSERT_TRUE(more.ok()) << more.status().message();
  EXPECT_EQ(more.value().size(), 2u);
}

TEST(StaFacade, WorstPathsUnknownEndpointFails) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto paths = engine.WorstPaths(CheckKind::kSetup, "r9/D", 2);
  EXPECT_FALSE(paths.ok());
  EXPECT_EQ(paths.status().code(), ErrorCode::kNotFound);
  EXPECT_NE(paths.status().message().find("no endpoint pin named r9/D"),
            std::string::npos);
}

TEST(StaFacade, WorstPathsUnconstrainedEndpointFails) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdcTight);
  const auto paths = engine.WorstPaths(CheckKind::kSetup, "q", 2);
  EXPECT_FALSE(paths.ok());
  EXPECT_EQ(paths.status().code(), ErrorCode::kInvalidArgument);
}

TEST(StaFacade, WorstPathsConsecutiveCallsAreIndependent) {
  TempDir dir;
  StaEngine engine;
  LoadTwoPathFixture(engine, dir, kSdc);
  const auto first = engine.WorstPaths(CheckKind::kSetup, "r2/D", 2);
  ASSERT_TRUE(first.ok()) << first.status().message();
  ASSERT_EQ(first.value().size(), 2u);
  const double first_slack = first.value()[0].slack_ns;
  const std::string first_start = first.value()[0].startpoint;
  // A second enumeration (which frees the first call's upstream
  // ends) and an unrelated worst-path query in between must not
  // disturb the copied-out data.
  const auto second = engine.WorstPaths(CheckKind::kSetup, "r2/D", 2);
  ASSERT_TRUE(second.ok()) << second.status().message();
  ASSERT_EQ(second.value().size(), 2u);
  const auto report = engine.WorstPathReport(CheckKind::kSetup, "r2/D");
  ASSERT_TRUE(report.ok()) << report.status().message();
  EXPECT_DOUBLE_EQ(first.value()[0].slack_ns, first_slack);
  EXPECT_EQ(first.value()[0].startpoint, first_start);
  EXPECT_DOUBLE_EQ(second.value()[0].slack_ns, first_slack);
  EXPECT_EQ(second.value()[0].startpoint, first_start);
}

TEST(StaFacade, WorstPathsFromFilterSelectsStartpoint) {
  TempDir dir;
  StaEngine engine;
  LoadTwoPathFixture(engine, dir, kSdc);
  const auto slow = engine.WorstPaths(CheckKind::kSetup, "r2/D", 2,
                                      PathFilter{{"r1/Q"}, {}});
  ASSERT_TRUE(slow.ok()) << slow.status().message();
  ASSERT_EQ(slow.value().size(), 1u);
  EXPECT_EQ(slow.value()[0].startpoint, "r1/Q");
  EXPECT_NEAR(slow.value()[0].slack_ns, 9.0, 1e-6);
  const auto fast = engine.WorstPaths(CheckKind::kSetup, "r2/D", 2,
                                      PathFilter{{"r3/Q"}, {}});
  ASSERT_TRUE(fast.ok()) << fast.status().message();
  ASSERT_EQ(fast.value().size(), 1u);
  EXPECT_EQ(fast.value()[0].startpoint, "r3/Q");
  EXPECT_NEAR(fast.value()[0].slack_ns, 9.3, 1e-6);
  // An unknown from name fails fast naming it.
  const auto unknown = engine.WorstPaths(CheckKind::kSetup, "r2/D", 2,
                                         PathFilter{{"nope"}, {}});
  EXPECT_FALSE(unknown.ok());
  EXPECT_EQ(unknown.status().code(), ErrorCode::kNotFound);
  EXPECT_NE(unknown.status().message().find("no such object: nope"),
            std::string::npos);
}

TEST(StaFacade, WorstPathsFromClockSelectsLaunchedPaths) {
  // A from-clock selects paths launched by that clock: both
  // paths start at flops clocked by sys. The clock must be
  // named differently from its port: "clk" resolves to the
  // top-level port pin first (pin, net, instance, then clock),
  // so from={"clk"} is the port pin, which launches no path to
  // r2/D, and only the distinctly named clock reaches the
  // from-clock path.
  TempDir dir;
  StaEngine engine;
  LoadTwoPathFixture(engine, dir, kSdcSys);
  const auto clocked = engine.WorstPaths(CheckKind::kSetup, "r2/D", 2,
                                         PathFilter{{"sys"}, {}});
  ASSERT_TRUE(clocked.ok()) << clocked.status().message();
  ASSERT_EQ(clocked.value().size(), 2u);
  EXPECT_EQ(clocked.value()[0].group, "sys");
  const auto port_pin = engine.WorstPaths(CheckKind::kSetup, "r2/D", 2,
                                          PathFilter{{"clk"}, {}});
  ASSERT_TRUE(port_pin.ok()) << port_pin.status().message();
  EXPECT_TRUE(port_pin.value().empty());
}

TEST(StaFacade, WorstPathsThroughClockFails) {
  TempDir dir;
  StaEngine engine;
  LoadTwoPathFixture(engine, dir, kSdcSys);
  const auto failed = engine.WorstPaths(CheckKind::kSetup, "r2/D", 2,
                                        PathFilter{{}, {"sys"}});
  EXPECT_FALSE(failed.ok());
  EXPECT_EQ(failed.status().code(), ErrorCode::kInvalidArgument);
  EXPECT_NE(failed.status().message().find("cannot be a -through object"),
            std::string::npos);
}

TEST(StaFacade, WorstPathsThroughFilterSelectsIntermediate) {
  TempDir dir;
  StaEngine engine;
  LoadTwoPathFixture(engine, dir, kSdc);
  // Through the slow buffer's output net: only the slow path.
  const auto slow = engine.WorstPaths(CheckKind::kSetup, "r2/D", 2,
                                      PathFilter{{}, {"n2"}});
  ASSERT_TRUE(slow.ok()) << slow.status().message();
  ASSERT_EQ(slow.value().size(), 1u);
  EXPECT_EQ(slow.value()[0].startpoint, "r1/Q");
  // Through the slow instance: only the slow path.
  const auto inst = engine.WorstPaths(CheckKind::kSetup, "r2/D", 2,
                                      PathFilter{{}, {"u1"}});
  ASSERT_TRUE(inst.ok()) << inst.status().message();
  ASSERT_EQ(inst.value().size(), 1u);
  EXPECT_EQ(inst.value()[0].startpoint, "r1/Q");
  // Through the mux output: both paths.
  const auto both = engine.WorstPaths(CheckKind::kSetup, "r2/D", 2,
                                      PathFilter{{}, {"m1/Y"}});
  ASSERT_TRUE(both.ok()) << both.status().message();
  EXPECT_EQ(both.value().size(), 2u);
  // Through a pin on no path to this endpoint: empty, not an
  // error.
  const auto none = engine.WorstPaths(CheckKind::kSetup, "r2/D", 2,
                                      PathFilter{{}, {"r1/D"}});
  ASSERT_TRUE(none.ok()) << none.status().message();
  EXPECT_TRUE(none.value().empty());
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
  EXPECT_EQ(report.status().code(), ErrorCode::kNotFound);
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

// ---- analysis coverage (Immediate unit 7, review item I4) --------
// The coverage probe: the facade's universe enumeration and tested
// determination, exercised against the fixture variants. The clean
// fixture has two setup and two hold checks (one per D pin); r1/D
// is fed from the unclocked primary input d, so its checks are
// untested with no clocked startpoint, while r2/D is fed from the
// clocked r1/Q and is tested.
const char kSdcOutputDelay[] =
    "create_clock -name clk -period 10 [get_ports clk]\n"
    "set_output_delay -clock clk -max 1.0 [get_ports q]\n"
    "set_output_delay -clock clk -min 0.5 [get_ports q]\n";

const char kSdcDataCheck[] =
    "create_clock -name clk -period 10 [get_ports clk]\n"
    "set_data_check -from [get_pins r1/Q] -to [get_pins r2/D] -setup "
    "0.2\n"
    "set_data_check -from [get_pins r1/Q] -to [get_pins r2/D] -hold "
    "0.1\n";

const char kSdcMinPulseWidth[] =
    "create_clock -name clk -period 10 [get_ports clk]\n"
    "set_min_pulse_width 1.0 [get_pins r1/CK]\n"
    "set_min_pulse_width 1.0 [get_pins r2/CK]\n";

// The gated-clock fixture: the same two flops, with the second
// flop's clock gated by an AND2 whose enable is the primary input
// e. The gating check lives on the enable pin of the gate itself
// (g1/B), not on the port that drives it. The enable is fed from
// the unclocked primary input, so its gating checks are untested
// with no clocked startpoint.
const char kLibertyGated[] = R"LIB(library (eos_fixture_gated) {
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

const char kVerilogGated[] = R"V(module top (clk, e, d, q);
  input clk, e, d;
  output q;
  wire n1, n2, gclk;

  AND2 g1 (.A(clk), .B(e), .Y(gclk));
  DFF r1 (.D(d), .CK(clk), .Q(n1));
  BUF u1 (.A(n1), .Y(n2));
  DFF r2 (.D(n2), .CK(gclk), .Q(q));
endmodule
)V";

void LoadGatedFixture(StaEngine& engine, const TempDir& dir) {
  ASSERT_TRUE(engine.ReadLiberty(dir.Write("cells.lib", kLibertyGated))
                  .ok());
  ASSERT_TRUE(engine.ReadVerilog(dir.Write("top.v", kVerilogGated))
                  .ok());
  ASSERT_TRUE(engine.LinkDesign("top").ok());
  ASSERT_TRUE(engine.ReadSdc(dir.Write("top.sdc", kSdc)).ok());
}

const CoveragePin* FindCoverageRow(
    const std::vector<CoveragePin>& rows, const std::string& pin,
    const std::string& type) {
  for (const CoveragePin& row : rows) {
    if (row.pin == pin && row.check_type == type) return &row;
  }
  return nullptr;
}

void ExpectUntestedRow(const std::vector<CoveragePin>& rows,
                       const std::string& pin, const std::string& type,
                       const std::string& related,
                       const std::string& reason) {
  const CoveragePin* row = FindCoverageRow(rows, pin, type);
  ASSERT_NE(row, nullptr) << pin << " " << type;
  EXPECT_FALSE(row->tested);
  EXPECT_EQ(row->related_pin, related);
  EXPECT_EQ(row->untested_reason, reason);
}

void ExpectTestedRow(const std::vector<CoveragePin>& rows,
                     const std::string& pin, const std::string& type,
                     const std::string& related) {
  const CoveragePin* row = FindCoverageRow(rows, pin, type);
  ASSERT_NE(row, nullptr) << pin << " " << type;
  EXPECT_TRUE(row->tested);
  EXPECT_EQ(row->related_pin, related);
  EXPECT_TRUE(row->untested_reason.empty());
}

TEST(StaFacade, AnalysisCoverageCleanFixture) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto coverage = engine.AnalysisCoverage();
  ASSERT_TRUE(coverage.ok()) << coverage.status().message();
  const std::vector<CoveragePin>& rows = coverage.value();
  // Only the two D pins carry checks: nothing else in the design
  // defines one, so no other check type appears.
  EXPECT_EQ(rows.size(), 4u);
  ExpectUntestedRow(rows, "r1/D", "setup", "r1/CK",
                    "no_startpoint_clock");
  ExpectUntestedRow(rows, "r1/D", "hold", "r1/CK",
                    "no_startpoint_clock");
  ExpectTestedRow(rows, "r2/D", "setup", "r2/CK");
  ExpectTestedRow(rows, "r2/D", "hold", "r2/CK");
  // Rows arrive sorted by pin then check type.
  ASSERT_EQ(rows.size(), 4u);
  EXPECT_EQ(rows[0].pin, "r1/D");
  EXPECT_EQ(rows[0].check_type, "hold");
  EXPECT_EQ(rows[1].check_type, "setup");
  EXPECT_EQ(rows[2].pin, "r2/D");
}

TEST(StaFacade, AnalysisCoverageOutputDelay) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdcOutputDelay);
  const auto coverage = engine.AnalysisCoverage();
  ASSERT_TRUE(coverage.ok()) << coverage.status().message();
  const std::vector<CoveragePin>& rows = coverage.value();
  EXPECT_EQ(rows.size(), 6u);
  ExpectTestedRow(rows, "q", "out_setup", "clk");
  ExpectTestedRow(rows, "q", "out_hold", "clk");
  ExpectTestedRow(rows, "r2/D", "setup", "r2/CK");
}

TEST(StaFacade, AnalysisCoverageDataCheck) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdcDataCheck);
  const auto coverage = engine.AnalysisCoverage();
  ASSERT_TRUE(coverage.ok()) << coverage.status().message();
  const std::vector<CoveragePin>& rows = coverage.value();
  EXPECT_EQ(rows.size(), 5u);
  ExpectTestedRow(rows, "r2/D", "no-change", "r1/Q");
  ExpectTestedRow(rows, "r2/D", "setup", "r2/CK");
}

TEST(StaFacade, AnalysisCoverageMinPulseWidth) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdcMinPulseWidth);
  const auto coverage = engine.AnalysisCoverage();
  ASSERT_TRUE(coverage.ok()) << coverage.status().message();
  const std::vector<CoveragePin>& rows = coverage.value();
  EXPECT_EQ(rows.size(), 6u);
  ExpectTestedRow(rows, "r1/CK", "min_pulse_width", "-");
  ExpectTestedRow(rows, "r2/CK", "min_pulse_width", "-");
  ExpectTestedRow(rows, "r2/D", "setup", "r2/CK");
}

TEST(StaFacade, AnalysisCoverageClockGating) {
  TempDir dir;
  StaEngine engine;
  LoadGatedFixture(engine, dir);
  const auto coverage = engine.AnalysisCoverage();
  ASSERT_TRUE(coverage.ok()) << coverage.status().message();
  const std::vector<CoveragePin>& rows = coverage.value();
  EXPECT_EQ(rows.size(), 6u);
  ExpectUntestedRow(rows, "g1/B", "clock_gating_setup", "g1/A",
                    "no_startpoint_clock");
  ExpectUntestedRow(rows, "g1/B", "clock_gating_hold", "g1/A",
                    "no_startpoint_clock");
  ExpectTestedRow(rows, "r2/D", "setup", "r2/CK");
  ExpectTestedRow(rows, "r2/D", "hold", "r2/CK");
}

TEST(StaFacade, AnalysisCoverageBeforeLinkFails) {
  StaEngine engine;
  const auto coverage = engine.AnalysisCoverage();
  EXPECT_FALSE(coverage.ok());
  EXPECT_EQ(coverage.status().code(), ErrorCode::kInvalidState);
}

// Clock periods (Immediate unit 10, review item T8). The
// facade lists the clocks the engine read from the SDC with
// their periods, so the timing service can derive each
// clock's minimum period as period minus the worst setup
// slack of its group. Note the upstream bulk minimum-period
// check was deliberately not wrapped: it only reports
// Liberty minimum_period constraints and SDF annotations,
// none of which exist in EOS fixtures, so it returns no rows
// on any of them (verified by running it: 0 checks on both
// the clean and the tight fixture).
TEST(StaFacade, ClockPeriodsCleanFixture) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto clocks = engine.ClockPeriods();
  ASSERT_TRUE(clocks.ok()) << clocks.status().message();
  ASSERT_EQ(clocks.value().size(), 1u);
  EXPECT_EQ(clocks.value()[0].name, "clk");
  EXPECT_NEAR(clocks.value()[0].period_ns, 10.0, 1e-6);
}

TEST(StaFacade, ClockPeriodsTightClock) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdcTight);
  const auto clocks = engine.ClockPeriods();
  ASSERT_TRUE(clocks.ok()) << clocks.status().message();
  ASSERT_EQ(clocks.value().size(), 1u);
  EXPECT_EQ(clocks.value()[0].name, "clk");
  EXPECT_NEAR(clocks.value()[0].period_ns, 0.5, 1e-6);
}

TEST(StaFacade, ClockPeriodsBeforeLinkFails) {
  StaEngine engine;
  const auto clocks = engine.ClockPeriods();
  EXPECT_FALSE(clocks.ok());
  EXPECT_EQ(clocks.status().code(), ErrorCode::kInvalidState);
}

// Minimum-pulse-width checks (Immediate unit 10, review item
// T8). The SDC sets a 1.0 ns minimum on both clock pins and
// the 10 ns clock has the default {0 5} waveform, so every
// pulse is 5.0 ns wide with slack 4.0 ns: one check per pin
// per opening edge (the high pulse opens on rise, the low
// pulse on fall).
TEST(StaFacade, MinPulseWidthChecks) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdcMinPulseWidth);
  const auto checks = engine.MinPulseWidthChecks();
  ASSERT_TRUE(checks.ok()) << checks.status().message();
  ASSERT_EQ(checks.value().size(), 4u);
  for (const MinPulseWidthCheck& check : checks.value()) {
    EXPECT_TRUE(check.pin == "r1/CK" || check.pin == "r2/CK")
        << check.pin;
    EXPECT_TRUE(check.transition == "rise" ||
                check.transition == "fall")
        << check.transition;
    EXPECT_NEAR(check.width_ns, 5.0, 1e-6);
    EXPECT_NEAR(check.min_width_ns, 1.0, 1e-6);
    EXPECT_NEAR(check.slack_ns, 4.0, 1e-6);
  }
  // Worst slack first, ties broken by pin then transition.
  for (std::size_t i = 1; i < checks.value().size(); ++i) {
    EXPECT_LE(checks.value()[i - 1].slack_ns,
              checks.value()[i].slack_ns);
  }
}

TEST(StaFacade, MinPulseWidthChecksWithoutConstraintAreEmpty) {
  // No set_min_pulse_width anywhere and no Liberty
  // min-pulse-width arc: the bulk check returns nothing, and
  // the facade reports the empty list rather than a pass.
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdc);
  const auto checks = engine.MinPulseWidthChecks();
  ASSERT_TRUE(checks.ok()) << checks.status().message();
  EXPECT_TRUE(checks.value().empty());
}

TEST(StaFacade, MinPulseWidthChecksBeforeLinkFails) {
  StaEngine engine;
  const auto checks = engine.MinPulseWidthChecks();
  EXPECT_FALSE(checks.ok());
  EXPECT_EQ(checks.status().code(), ErrorCode::kInvalidState);
}

}  // namespace
}  // namespace eos::analysis
