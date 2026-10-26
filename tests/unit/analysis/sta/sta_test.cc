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

TEST(StaFacade, WorstPathToUnknownEndpointFails) {
  TempDir dir;
  StaEngine engine;
  LoadFixture(engine, dir, kSdcTight);
  const auto path = engine.WorstPath(CheckKind::kSetup, "r9/D");
  EXPECT_FALSE(path.ok());
  EXPECT_EQ(path.status().code(), ErrorCode::kInvalidArgument);
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
