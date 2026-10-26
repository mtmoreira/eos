// Unit tests: eco/timing-eco, the v1 sizing and buffering
// adapters.
//
// Fixtures are small synthetic LEF/DEF/Liberty texts, embedded
// here and written to a temporary directory per test. The sizing
// fixture is the STA fixture chain (flip-flop, buffer, flip-flop)
// with two buffer drive variants. The fanout fixture models the
// one effect scalar delays cannot show: a flip-flop whose
// clock-to-Q delay is a Liberty table over output capacitance,
// driving eight flip-flop data pins, so splitting the load behind
// a buffer measurably improves the slack. The table values at the
// two capacitances the design actually presents (0.005 pF, the
// buffer input; 0.08 pF, eight 0.01 pF data pins) are the fixture's
// construction, not a measurement of any real cell.
#include "eos/eco/timing_eco.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "eos/analysis/timing.h"

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
              ("eos_timing_eco_test_" + std::to_string(counter_++))) {
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

void LoadFixture(db::OdbDatabase& db, const TempDir& dir,
                 const std::string& def_text) {
  ASSERT_TRUE(db.ReadLef(dir.Write("cells.lef", kLef)).ok());
  ASSERT_TRUE(db.ReadDef(dir.Write("top.def", def_text)).ok());
}

std::string ReadFile(const std::string& path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

bool HasConnection(const std::vector<db::OdbNetConnection>& connections,
                   const std::string& instance, const std::string& pin) {
  for (const db::OdbNetConnection& connection : connections) {
    if (connection.instance == instance && connection.pin == pin) {
      return true;
    }
  }
  return false;
}

double WorstSlack(db::OdbDatabase& db, const TempDir& dir) {
  analysis::SceneAnalysis scene;
  scene.scenario.id = 1;
  scene.scenario.name = "func_tt";
  scene.scenario.mode = "func";
  scene.scenario.corner = "tt";
  scene.scenario.parasitics = db::ParasiticsLevel::kEstimate;
  scene.liberty_path = dir.Write("fanout.lib", kFanoutLiberty);
  analysis::TimingService service;
  const auto results = service.Analyze(
      db, dir.Write("top.sdc",
                    "create_clock -name clk -period 1 [get_ports clk]\n"),
      {scene});
  EXPECT_TRUE(results.ok()) << results.status().message();
  if (!results.ok()) return 0.0;
  return results.value()[0].metrics.at("worst_slack_ns").value;
}

TEST(TimingEco, ResizeSwapsMasterAndPreservesConnectivity) {
  TempDir dir;
  db::OdbDatabase db;
  LoadFixture(db, dir, kDef);
  ASSERT_TRUE(ResizeInstance(db, "u1", "BUF_X2").ok());
  db::OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance("u1", &info));
  EXPECT_EQ(info.master, "BUF_X2");
  EXPECT_EQ(db.InstanceCount(), 3u);
  EXPECT_EQ(db.NetCount(), 5u);
  ASSERT_TRUE(db.WriteVerilog(dir.Path("top.v")).ok());
  EXPECT_NE(ReadFile(dir.Path("top.v")).find("BUF_X2 u1 (.A(n1), .Y(n2));"),
            std::string::npos);
}

TEST(TimingEco, ResizeRejectsUnknownAndSameAndPinMismatchedMasters) {
  TempDir dir;
  db::OdbDatabase db;
  LoadFixture(db, dir, kDef);
  EXPECT_EQ(ResizeInstance(db, "nope", "BUF_X2").code(),
            ErrorCode::kNotFound);
  EXPECT_EQ(ResizeInstance(db, "u1", "NOPE").code(), ErrorCode::kNotFound);
  EXPECT_EQ(ResizeInstance(db, "u1", "BUF_X1").code(),
            ErrorCode::kInvalidArgument);
  // WIDE carries an extra input pin: a swap would strand the
  // connection set, so it is refused and nothing changes.
  EXPECT_EQ(ResizeInstance(db, "u1", "WIDE").code(),
            ErrorCode::kInvalidArgument);
  db::OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance("u1", &info));
  EXPECT_EQ(info.master, "BUF_X1");
}

TEST(TimingEco, InsertBufferSplitsTheNetAtTheDriverLocation) {
  TempDir dir;
  db::OdbDatabase db;
  LoadFixture(db, dir, kDef);
  ASSERT_TRUE(InsertBuffer(db, "n1", "BUF_X4", "u_buf", "n1_buf").ok());
  EXPECT_EQ(db.InstanceCount(), 4u);
  EXPECT_EQ(db.NetCount(), 6u);
  db::OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance("u_buf", &info));
  EXPECT_EQ(info.master, "BUF_X4");
  // Placed at the driver instance r1's location, (1000, 1000).
  EXPECT_EQ(info.x, 1000);
  EXPECT_EQ(info.y, 1000);
  const auto original = db.NetConnections("n1");
  EXPECT_TRUE(HasConnection(original, "r1", "Q"));
  EXPECT_TRUE(HasConnection(original, "u_buf", "A"));
  EXPECT_FALSE(HasConnection(original, "u1", "A"));
  const auto split = db.NetConnections("n1_buf");
  EXPECT_TRUE(HasConnection(split, "u_buf", "Y"));
  EXPECT_TRUE(HasConnection(split, "u1", "A"));
}

TEST(TimingEco, InsertBufferRejectsBadNamesAndPortLoads) {
  TempDir dir;
  db::OdbDatabase db;
  LoadFixture(db, dir, kDef);
  EXPECT_EQ(InsertBuffer(db, "nope", "BUF_X4", "u_buf", "n_buf").code(),
            ErrorCode::kNotFound);
  EXPECT_EQ(InsertBuffer(db, "n1", "NOPE", "u_buf", "n_buf").code(),
            ErrorCode::kNotFound);
  EXPECT_EQ(InsertBuffer(db, "n1", "BUF_X4", "u1", "n_buf").code(),
            ErrorCode::kInvalidArgument);
  EXPECT_EQ(InsertBuffer(db, "n1", "BUF_X4", "u_buf", "n2").code(),
            ErrorCode::kInvalidArgument);
  // Net q is driven by r2 and loaded by the top-level output port
  // q: port loads are out of v1 scope and refused as such.
  EXPECT_EQ(InsertBuffer(db, "q", "BUF_X4", "u_buf", "q_buf").code(),
            ErrorCode::kUnsupported);
  EXPECT_EQ(db.InstanceCount(), 3u);
  EXPECT_EQ(db.NetCount(), 5u);
}

TEST(TimingEco, BufferInsertionImprovesSlackOnAHighFanoutNet) {
  TempDir dir;
  db::OdbDatabase db;
  LoadFixture(db, dir, kFanoutDef);
  // By fixture construction: the driver sees 0.08 pF and its
  // clock-to-Q delay is 1.8 ns against a 1 ns clock, so the setup
  // slack is 1 - 0.1 - 1.8 = -0.9 ns.
  const double before = WorstSlack(db, dir);
  EXPECT_NEAR(before, -0.9, 0.01);
  ASSERT_TRUE(InsertBuffer(db, "wide", "BUF_X4", "u_buf", "wide_buf").ok());
  // After: the driver sees only the buffer input (0.005 pF, 0.2 ns)
  // and the buffer drives the full load (0.25 ns): arrival 0.45 ns,
  // slack 1 - 0.1 - 0.45 = 0.45 ns.
  const double after = WorstSlack(db, dir);
  EXPECT_NEAR(after, 0.45, 0.01);
  EXPECT_GT(after, before);
}

}  // namespace
}  // namespace eos::eco

// ---------------------------------------------------------------------------
// Resynthesis fixtures (unit 2.6): a chain flip-flop, AND2, INV,
// INV, flip-flop. The double inversion is logic a sizing swap
// cannot remove: the AND's output passes through two inverters
// (0.5 ns each in the Liberty) before the capture flop. Against a
// 1.5 ns clock the setup slack is 1.5 - 0.1 (setup) - 0.2 (clock
// to Q) - 0.6 (AND) - 0.5 - 0.5 (the inversions) = -0.4 ns; with
// the cone remapped to the AND gate alone it is +0.6 ns. The
// Liberty functions are the cells' truth: AND2 is "A & B", INV is
// "!A", and the corruption test's OR2 is "A | B".

namespace eos::eco {
namespace {

const char kResynthLef[] = R"LEF(VERSION 5.8 ;
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
MACRO INV_X1
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
END INV_X1
MACRO AND2_X1
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
END AND2_X1
MACRO OR2_X1
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
END OR2_X1
END LIBRARY
)LEF";

const char kResynthDef[] = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 5 ;
- r1 DFF + PLACED ( 1000 1000 ) N ;
- u1 AND2_X1 + PLACED ( 2000 1000 ) N ;
- u2 INV_X1 + PLACED ( 3000 1000 ) N ;
- u3 INV_X1 + PLACED ( 4000 1000 ) N ;
- r2 DFF + PLACED ( 5000 1000 ) N ;
END COMPONENTS
PINS 4 ;
- clk + NET clk + DIRECTION INPUT + USE SIGNAL + PLACED ( 0 5000 ) N ;
- d + NET d + DIRECTION INPUT + USE SIGNAL + PLACED ( 0 6000 ) N ;
- b + NET nb + DIRECTION INPUT + USE SIGNAL + PLACED ( 0 7000 ) N ;
- q + NET q + DIRECTION OUTPUT + USE SIGNAL + PLACED ( 10000 5000 ) N ;
END PINS
NETS 8 ;
- clk ( PIN clk ) ( r1 CK ) ( r2 CK ) ;
- d ( PIN d ) ( r1 D ) ;
- nb ( PIN b ) ( u1 B ) ;
- n1 ( r1 Q ) ( u1 A ) ;
- n2 ( u1 Y ) ( u2 A ) ;
- n3 ( u2 Y ) ( u3 A ) ;
- n4 ( u3 Y ) ( r2 D ) ;
- q ( r2 Q ) ( PIN q ) ;
END NETS
END DESIGN
)DEF";

const char kResynthLiberty[] = R"LIB(library (eos_resynth_fixture) {
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

  cell (INV_X1) {
    area : 1.0;
    pin (A) {
      direction : input;
      capacitance : 0.001;
    }
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

  cell (AND2_X1) {
    area : 2.0;
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

  cell (OR2_X1) {
    area : 2.0;
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
      function : "A | B";
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
}
)LIB";

const char kCorruptedReplacement[] = R"V(module resynth_cone (n1, nb, n4);
  input n1, nb;
  output n4;
  OR2_X1 u1 (.A(n1), .B(nb), .Y(n4));
endmodule
)V";

void LoadResynthFixture(db::OdbDatabase& db, const TempDir& dir) {
  ASSERT_TRUE(db.ReadLef(dir.Write("resynth.lef", kResynthLef)).ok());
  ASSERT_TRUE(db.ReadDef(dir.Write("resynth.def", kResynthDef)).ok());
}

ResynthesisRequest ResynthRequestFor(const TempDir& dir) {
  ResynthesisRequest request;
  request.target_net = "n4";
  request.sequential_masters = {"DFF"};
  request.liberty_path = dir.Write("resynth.lib", kResynthLiberty);
  request.scratch_dir = dir.Path(".");
  return request;
}

double ResynthWorstSlack(db::OdbDatabase& db, const TempDir& dir) {
  analysis::SceneAnalysis scene;
  scene.scenario.id = 1;
  scene.scenario.name = "func_tt";
  scene.scenario.mode = "func";
  scene.scenario.corner = "tt";
  scene.scenario.parasitics = db::ParasiticsLevel::kEstimate;
  scene.liberty_path = dir.Write("resynth.lib", kResynthLiberty);
  analysis::TimingService service;
  const auto results = service.Analyze(
      db, dir.Write("resynth.sdc",
                    "create_clock -name clk -period 1.5 [get_ports clk]\n"),
      {scene});
  EXPECT_TRUE(results.ok()) << results.status().message();
  if (!results.ok()) return 0.0;
  return results.value()[0].metrics.at("worst_slack_ns").value;
}

TEST(TimingEcoResynthesis, RemapAppliesOnlyWithTheProofPassed) {
  TempDir dir;
  db::OdbDatabase db;
  LoadResynthFixture(db, dir);
  EXPECT_NEAR(ResynthWorstSlack(db, dir), -0.4, 0.01);
  const auto outcome = ResynthesizeCone(db, ResynthRequestFor(dir));
  ASSERT_TRUE(outcome.ok()) << outcome.status().message();
  EXPECT_TRUE(outcome.value().applied) << outcome.value().reason;
  EXPECT_EQ(outcome.value().proof, ProofVerdict::kEquivalent);
  EXPECT_EQ(outcome.value().cone_instances,
            std::vector<std::string>({"u1", "u2", "u3"}));
  ASSERT_EQ(outcome.value().created_instances.size(), 1u);
  // The design is the two flops and the one remapped AND gate;
  // the cone instances are gone and the slack is the fixture's
  // post-remap construction value.
  EXPECT_EQ(db.InstanceCount(), 3u);
  db::OdbInstanceInfo info;
  EXPECT_FALSE(db.FindInstance("u1", &info));
  EXPECT_FALSE(db.FindInstance("u3", &info));
  ASSERT_TRUE(db.FindInstance(outcome.value().created_instances[0], &info));
  EXPECT_EQ(info.master, "AND2_X1");
  EXPECT_NEAR(ResynthWorstSlack(db, dir), 0.6, 0.01);
}

TEST(TimingEcoResynthesis, CorruptedSpliceIsCaughtAndRolledBack) {
  TempDir dir;
  db::OdbDatabase db;
  LoadResynthFixture(db, dir);
  const std::string replacement =
      dir.Write("corrupted_cone.v", kCorruptedReplacement);
  const auto outcome =
      SpliceReplacementCone(db, ResynthRequestFor(dir), replacement);
  ASSERT_TRUE(outcome.ok()) << outcome.status().message();
  // OR is not AND: the proof must say so, the splice must not
  // stand, and the design must be exactly as it was.
  EXPECT_FALSE(outcome.value().applied);
  EXPECT_EQ(outcome.value().proof, ProofVerdict::kNotEquivalent);
  EXPECT_TRUE(outcome.value().created_instances.empty());
  EXPECT_EQ(db.InstanceCount(), 5u);
  db::OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance("u1", &info));
  EXPECT_EQ(info.master, "AND2_X1");
  ASSERT_TRUE(db.FindInstance("u3", &info));
  EXPECT_EQ(info.master, "INV_X1");
  EXPECT_NEAR(ResynthWorstSlack(db, dir), -0.4, 0.01);
}

TEST(TimingEcoResynthesis, RegisterOutputHasNoCone) {
  TempDir dir;
  db::OdbDatabase db;
  LoadResynthFixture(db, dir);
  ResynthesisRequest request = ResynthRequestFor(dir);
  request.target_net = "n1";  // driven by r1, a DFF
  const auto outcome = ResynthesizeCone(db, request);
  ASSERT_FALSE(outcome.ok());
  EXPECT_EQ(outcome.status().code(), ErrorCode::kInvalidArgument);
  EXPECT_EQ(db.InstanceCount(), 5u);
}

}  // namespace
}  // namespace eos::eco
