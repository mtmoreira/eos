// Unit tests: pd/cts (the EOS runner over TritonCTS).
//
// Fixture shape mirrors the place tests: one site, two routing
// layers, two two-pin macros in LEF (BUFX2 buffer, DFFX1 sink);
// six placed sink instances, one placed top-level clock pin, and
// one clock net in DEF. The sinks must not be buffers: upstream
// TritonCTS skips a net that already contains a buffer from the
// buffer list. Each DEF
// pin carries a PORT rectangle, because odb creates a placed BPin
// only from port geometry. The Liberty library is written inline
// here, licence-clean by construction (own cell names and values,
// same shape as the STA facade's fixture library): one buffer
// cell, BUFX2, with NLDM delay tables, so TechChar can
// characterize it. Every CTS test floorplans first (RunFloorplan
// with default areas) and places (RunPlace), because cts fails
// fast on an un-floorplanned or unplaced design by design; those
// failure modes have their own tests below.
//
// The fixture has no SDC: the clock net is named explicitly
// (-clock in command terms, clock_nets in runner terms), which is
// the runner's supported clock selection. Post-CTS STA slack in
// ns is measured on the gcd evidence run (see the P3 report) and
// through the registry test asserting the STA-unavailable payload
// when no timing session is configured; TritonCTS itself reports
// no ns skew or insertion delay, and the runner reports both as
// unavailable with that reason rather than inventing numbers.
#include "eos/pd/cts.h"
#include "eos/pd/floorplan.h"
#include "eos/pd/place.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "eos/db/odb.h"
#include "odb/db.h"

namespace eos::pd {
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
MACRO DFFX1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
  PIN CLK
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END CLK
  PIN Q
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT 0.35 -0.05 0.45 0.05 ;
    END
  END Q
END DFFX1
END LIBRARY
)LEF";

const char kDef[] = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 6 ;
- u1 DFFX1 + PLACED ( 1000 1000 ) N ;
- u2 DFFX1 + PLACED ( 8000 1000 ) N ;
- u3 DFFX1 + PLACED ( 1000 8000 ) N ;
- u4 DFFX1 + PLACED ( 8000 8000 ) N ;
- u5 DFFX1 + PLACED ( 4500 4500 ) N ;
- u6 DFFX1 + PLACED ( 5500 5500 ) N ;
END COMPONENTS
PINS 1 ;
- clk_in + NET clk + DIRECTION INPUT + USE SIGNAL
  + PORT
  + LAYER metal1 ( -50 -50 ) ( 50 50 )
  + PLACED ( 0 5000 ) N ;
END PINS
NETS 1 ;
- clk ( PIN clk_in ) ( u1 CLK ) ( u2 CLK ) ( u3 CLK ) ( u4 CLK ) ( u5 CLK ) ( u6 CLK ) ;
END NETS
END DESIGN
)DEF";

const char kLiberty[] = R"LIB(library (cts_fixture) {
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
  default_max_transition : 0.5;
  nom_process : 1.0;
  nom_temperature : 25.0;
  nom_voltage : 1.0;
  lu_table_template (del_4_4) {
    variable_1 : input_net_transition;
    variable_2 : total_output_net_capacitance;
    index_1 ("0.01, 0.05, 0.2, 0.8");
    index_2 ("0.001, 0.005, 0.02, 0.1");
  }
  cell (BUFX2) {
    area : 2.0;
    pin (A) {
      direction : input;
      capacitance : 0.01;
    }
    pin (Y) {
      direction : output;
      function : "A";
      max_capacitance : 0.13;
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (del_4_4) {
          index_1 ("0.01, 0.05, 0.2, 0.8");
          index_2 ("0.001, 0.005, 0.02, 0.1");
          values ( \
            "0.06, 0.07, 0.08, 0.14", \
            "0.07, 0.08, 0.09, 0.15", \
            "0.12, 0.12, 0.13, 0.20", \
            "0.28, 0.29, 0.30, 0.36" \
          );
        }
        cell_fall (del_4_4) {
          index_1 ("0.01, 0.05, 0.2, 0.8");
          index_2 ("0.001, 0.005, 0.02, 0.1");
          values ( \
            "0.06, 0.07, 0.08, 0.14", \
            "0.07, 0.08, 0.09, 0.15", \
            "0.12, 0.12, 0.13, 0.20", \
            "0.28, 0.29, 0.30, 0.36" \
          );
        }
        rise_transition (del_4_4) {
          index_1 ("0.01, 0.05, 0.2, 0.8");
          index_2 ("0.001, 0.005, 0.02, 0.1");
          values ( \
            "0.02, 0.03, 0.03, 0.08", \
            "0.03, 0.04, 0.04, 0.09", \
            "0.07, 0.07, 0.08, 0.13", \
            "0.22, 0.22, 0.23, 0.28" \
          );
        }
        fall_transition (del_4_4) {
          index_1 ("0.01, 0.05, 0.2, 0.8");
          index_2 ("0.001, 0.005, 0.02, 0.1");
          values ( \
            "0.02, 0.03, 0.03, 0.08", \
            "0.03, 0.04, 0.04, 0.09", \
            "0.07, 0.07, 0.08, 0.13", \
            "0.22, 0.22, 0.23, 0.28" \
          );
        }
      }
    }
  }
  cell (DFFX1) {
    area : 2.0;
    pin (CLK) {
      direction : input;
      capacitance : 0.01;
    }
    pin (Q) {
      direction : output;
      capacitance : 0.01;
    }
  }
}
)LIB";

class TempDir {
 public:
  TempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("eos_cts_test_" + std::to_string(counter_++))) {
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
  std::filesystem::path path_;
  static inline int counter_ = 0;
};

void LoadDesign(db::OdbDatabase& db, const TempDir& dir, bool with_def) {
  const std::string lef = dir.Write("test.lef", kLef);
  ASSERT_TRUE(db.ReadLef(lef).ok());
  if (with_def) {
    const std::string def = dir.Write("test.def", kDef);
    ASSERT_TRUE(db.ReadDef(def).ok());
  }
}

void FloorplanAndPlace(db::OdbDatabase& db) {
  FloorplanOptions foptions;
  FloorplanReport freport;
  ASSERT_TRUE(RunFloorplan(db.RawDatabase(), foptions, &freport).ok());
  ASSERT_GT(freport.row_count, 0);
  PlaceOptions poptions;
  PlaceReport preport;
  ASSERT_TRUE(RunPlace(db.RawDatabase(), poptions, &preport).ok())
      << "place must succeed before cts";
  ASSERT_EQ(preport.instance_placed_count, 6);
}

CtsOptions ClockOptions(const std::string& liberty) {
  CtsOptions options;
  options.clock_nets = {"clk"};
  options.buffer_list = {"BUFX2"};
  options.liberty_path = liberty;
  return options;
}

TEST(Cts, BuildsClockTreeWithBuffers) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  const std::string liberty = dir.Write("test.lib", kLiberty);
  CtsOptions options = ClockOptions(liberty);
  CtsReport report;
  const foundation::Status status =
      RunCts(db.RawDatabase(), options, &report);
  ASSERT_TRUE(status.ok()) << status.message();
  EXPECT_EQ(report.clock_root_count, 1);
  EXPECT_GT(report.buffers_inserted, 0);
  EXPECT_GE(report.sink_count, 6);
  // TritonCTS reports no ns skew or insertion delay; the runner
  // must say so, not invent numbers.
  EXPECT_FALSE(report.skew_ns_available);
  EXPECT_FALSE(report.insertion_delay_ns_available);
  // The tree added buffer instances to the block.
  odb::dbBlock* block = db.RawDatabase()->getChip()->getBlock();
  EXPECT_GT(static_cast<int>(block->getInsts().size()), 6);
}

TEST(Cts, SecondRunSucceeds) {
  // A second cts in the same process must not dangle on the
  // first run's STA session (the runner clears the Sta singleton
  // before returning). Each run uses a fresh design: re-running
  // on an already-buffered tree is a skip by upstream design
  // (the net already contains a clock buffer), not a second
  // tree build, so the second run must not reuse run one's
  // buffered design.
  for (int i = 0; i < 2; i++) {
    TempDir dir;
    db::OdbDatabase db;
    LoadDesign(db, dir, true);
    FloorplanAndPlace(db);
    const std::string liberty = dir.Write("test.lib", kLiberty);
    CtsOptions options = ClockOptions(liberty);
    CtsReport report;
    ASSERT_TRUE(RunCts(db.RawDatabase(), options, &report).ok());
    EXPECT_EQ(report.clock_root_count, 1);
  }
}

TEST(Cts, NoDatabaseFailsFast) {
  CtsOptions options;
  CtsReport report;
  const foundation::Status status = RunCts(nullptr, options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

TEST(Cts, NoTechnologyFailsFast) {
  odb::dbDatabase* raw = odb::dbDatabase::create();
  CtsOptions options;
  CtsReport report;
  const foundation::Status status = RunCts(raw, options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
  odb::dbDatabase::destroy(raw);
}

TEST(Cts, NoMappedDesignFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, false);
  CtsOptions options;
  CtsReport report;
  const foundation::Status status =
      RunCts(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

TEST(Cts, UnfloorplannedDesignFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  CtsOptions options = ClockOptions(dir.Write("test.lib", kLiberty));
  CtsReport report;
  const foundation::Status status =
      RunCts(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
  EXPECT_NE(status.message().find("floorplan"), std::string::npos);
}

TEST(Cts, UnplacedDesignFailsFast) {
  // Floorplanned but not placed: DEF components are PLACED, so
  // unplace them explicitly to prove the placed precondition.
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanOptions foptions;
  FloorplanReport freport;
  ASSERT_TRUE(RunFloorplan(db.RawDatabase(), foptions, &freport).ok());
  odb::dbBlock* block = db.RawDatabase()->getChip()->getBlock();
  for (odb::dbInst* inst : block->getInsts()) {
    inst->setPlacementStatus(odb::dbPlacementStatus::UNPLACED);
  }
  CtsOptions options = ClockOptions(dir.Write("test.lib", kLiberty));
  CtsReport report;
  const foundation::Status status =
      RunCts(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
  EXPECT_NE(status.message().find("placed"), std::string::npos);
}

TEST(Cts, NoLibertyFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  CtsOptions options;
  options.clock_nets = {"clk"};
  options.buffer_list = {"BUFX2"};
  CtsReport report;
  const foundation::Status status =
      RunCts(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
  EXPECT_NE(status.message().find("Liberty"), std::string::npos);
}

TEST(Cts, NoClockFailsFast) {
  // The optloop shape: a placed design with no clock named and
  // no SDC clock in the session. The engine would only warn;
  // the runner fails fast instead.
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  CtsOptions options;
  options.buffer_list = {"BUFX2"};
  options.liberty_path = dir.Write("test.lib", kLiberty);
  CtsReport report;
  const foundation::Status status =
      RunCts(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
  EXPECT_NE(status.message().find("clock"), std::string::npos);
}

// P8: an empty buffer list is a valid option combination, not a
// missing option: the engine infers the buffer list from Liberty
// (BUFX2 matches the engine's BUF name pattern), so the run must
// succeed and build the same single-root tree as the explicit
// buffer list case.
TEST(Cts, EmptyBufferListInfersFromLiberty) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  CtsOptions options;
  options.clock_nets = {"clk"};
  options.liberty_path = dir.Write("test.lib", kLiberty);
  CtsReport report;
  const foundation::Status status =
      RunCts(db.RawDatabase(), options, &report);
  ASSERT_TRUE(status.ok()) << status.message();
  EXPECT_EQ(report.clock_root_count, 1);
  EXPECT_GT(report.buffers_inserted, 0);
  EXPECT_GE(report.sink_count, 6);
}

// P8: explicitly pinned root and sink buffers are a valid option
// combination; the run must succeed with the pinned cells.
TEST(Cts, ExplicitRootAndSinkBuffers) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  CtsOptions options = ClockOptions(dir.Write("test.lib", kLiberty));
  options.root_buffer = "BUFX2";
  options.sink_buffer = "BUFX2";
  CtsReport report;
  const foundation::Status status =
      RunCts(db.RawDatabase(), options, &report);
  ASSERT_TRUE(status.ok()) << status.message();
  EXPECT_EQ(report.clock_root_count, 1);
  EXPECT_GT(report.buffers_inserted, 0);
}

// P8: a clock named by instance pin (u1/CLK, the DEF divider
// form) resolves to the same net as the net name, per the
// runner's clock selection contract.
TEST(Cts, ClockByInstancePinName) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  CtsOptions options = ClockOptions(dir.Write("test.lib", kLiberty));
  options.clock_nets = {"u1/CLK"};
  CtsReport report;
  const foundation::Status status =
      RunCts(db.RawDatabase(), options, &report);
  ASSERT_TRUE(status.ok()) << status.message();
  EXPECT_EQ(report.clock_root_count, 1);
}

// P8: a clock net with no sinks builds no tree, so the runner's
// post-run zero-roots check must fail fast instead of reporting
// an empty tree. The variant design keeps the placed instances
// (the placed precondition) but connects the clock net to the
// top-level pin only.
const char kDefNoClockSinks[] = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 6 ;
- u1 DFFX1 + PLACED ( 1000 1000 ) N ;
- u2 DFFX1 + PLACED ( 8000 1000 ) N ;
- u3 DFFX1 + PLACED ( 1000 8000 ) N ;
- u4 DFFX1 + PLACED ( 8000 8000 ) N ;
- u5 DFFX1 + PLACED ( 4500 4500 ) N ;
- u6 DFFX1 + PLACED ( 5500 5500 ) N ;
END COMPONENTS
PINS 1 ;
- clk_in + NET clk + DIRECTION INPUT + USE SIGNAL
  + PORT
  + LAYER metal1 ( -50 -50 ) ( 50 50 )
  + PLACED ( 0 5000 ) N ;
END PINS
NETS 1 ;
- clk ( PIN clk_in ) ;
END NETS
END DESIGN
)DEF";

TEST(Cts, ClockNetWithoutSinksFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  const std::string lef = dir.Write("test.lef", kLef);
  ASSERT_TRUE(db.ReadLef(lef).ok());
  const std::string def = dir.Write("test.def", kDefNoClockSinks);
  ASSERT_TRUE(db.ReadDef(def).ok());
  FloorplanAndPlace(db);
  CtsOptions options = ClockOptions(dir.Write("test.lib", kLiberty));
  CtsReport report;
  const foundation::Status status =
      RunCts(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
  EXPECT_NE(status.message().find("clock"), std::string::npos);
}

// P8: an unreadable Liberty path is a distinct failure from an
// empty path (NoLibertyFailsFast). The STA reader throws on a
// missing file, so the run fails fast through the runner's
// engine-failure path rather than its null-return message; the
// test pins the fail-fast and the code, not a message substring.
TEST(Cts, UnreadableLibertyFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  CtsOptions options;
  options.clock_nets = {"clk"};
  options.buffer_list = {"BUFX2"};
  options.liberty_path = "/nonexistent/eos-cts.lib";
  CtsReport report;
  const foundation::Status status =
      RunCts(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

TEST(Cts, UnknownClockNetFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  CtsOptions options = ClockOptions(dir.Write("test.lib", kLiberty));
  options.clock_nets = {"no_such_net"};
  CtsReport report;
  const foundation::Status status =
      RunCts(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
  EXPECT_NE(status.message().find("clock"), std::string::npos);
}

}  // namespace
}  // namespace eos::pd
