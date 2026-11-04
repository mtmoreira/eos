// Unit tests: pd/route (the EOS runner over the in-tree global
// router and detailed router).
//
// Fixture shape mirrors the place tests: one site, two routing
// layers, one two-pin macro in LEF; two placed instances, two
// placed top-level pins, and three two-pin nets in DEF. Each DEF
// pin carries a PORT rectangle, because odb creates a placed BPin
// only from port geometry. Every route test floorplans first
// (RunFloorplan with default areas) and places (RunPlace), because
// route fails fast on an un-floorplanned or unplaced design by
// design; those failure modes have their own tests below.
//
// The fixture Liberty holds one buffer cell, BUFX2, with NLDM
// delay tables, so the engine's clock-net detection has a library
// to build the STA graph from (without one it throws STA-2141).
// The runner still pins the critical-nets percentage to 0 (the
// wire-parasitics builder that path needs is still deferred, see
// grt PATCHES.md), so no timing-driven routing is exercised here.
// The congestion map is asserted from the block GCellGrid the
// engine wrote: capacities and usage are positive on both layers,
// and overflow is zero on this uncongested fixture.
// The LEF pitch is 0.05, not the place tests' 0.19, on purpose:
// the engine's tile size is pitches-per-tile times track spacing,
// so 0.19 gives 2850 DBU tiles and this 10000 DBU die is only 3
// by 3 tiles; after placement every net's pins then snap to a
// single grid point, each route is one via crossing no tile edge,
// and the engine's edge usage is correctly 0. At 0.05 the tile is
// 750 DBU, the die is 13 by 13 tiles, and the nets cross tile
// edges, so the usage assertions exercise real congestion.
#include "eos/pd/route.h"
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
  PITCH 0.05 ;
  WIDTH 0.06 ;
END metal1
LAYER via1
  TYPE CUT ;
  WIDTH 0.06 ;
END via1
LAYER metal2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 0.05 ;
  WIDTH 0.06 ;
END metal2
VIA VIA12
  LAYER metal1 ;
    RECT -0.05 -0.05 0.05 0.05 ;
  LAYER via1 ;
    RECT -0.03 -0.03 0.03 0.03 ;
  LAYER metal2 ;
    RECT -0.05 -0.05 0.05 0.05 ;
END VIA12
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
        RECT 0.05 0.05 0.25 0.25 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT 0.26 0.05 0.46 0.25 ;
    END
  END Y
END BUFX2
END LIBRARY
)LEF";

const char kDef[] = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 2 ;
- u1 BUFX2 + PLACED ( 1000 1000 ) N ;
- u2 BUFX2 + PLACED ( 8000 1000 ) N ;
END COMPONENTS
PINS 2 ;
- in1 + NET n_in + DIRECTION INPUT + USE SIGNAL
  + PORT
  + LAYER metal1 ( -50 -50 ) ( 50 50 )
  + PLACED ( 0 5000 ) N ;
- out1 + NET n_out + DIRECTION OUTPUT + USE SIGNAL
  + PORT
  + LAYER metal1 ( -50 -50 ) ( 50 50 )
  + PLACED ( 10000 5000 ) N ;
END PINS
NETS 3 ;
- n_in ( PIN in1 ) ( u1 A ) ;
- n_mid ( u1 Y ) ( u2 A ) ;
- n_out ( u2 Y ) ( PIN out1 ) ;
END NETS
END DESIGN
)DEF";

const char kLiberty[] = R"LIB(library (route_fixture) {
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
      }
    }
  }
}
)LIB";

class TempDir {
 public:
  TempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("eos_route_test_" + std::to_string(counter_++))) {
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
      << "place must succeed before route";
  ASSERT_EQ(preport.instance_placed_count, 2);
}

TEST(Route, RoutesEveryNetAndReportsCongestion) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  RouteOptions options;
  options.liberty_path = dir.Write("test.lib", kLiberty);
  RouteReport report;
  const foundation::Status status =
      RunRoute(db.RawDatabase(), options, &report);
  ASSERT_TRUE(status.ok()) << status.message();
  EXPECT_EQ(report.net_total_count, 3);
  EXPECT_EQ(report.net_routed_count, 3);
  EXPECT_GT(report.guide_count, 0);
  // Every net carries at least one guide in the database.
  odb::dbBlock* block = db.RawDatabase()->getChip()->getBlock();
  for (odb::dbNet* net : block->getNets()) {
    EXPECT_GT(static_cast<int>(net->getGuides().size()), 0)
        << "net " << net->getConstName() << " has no guides";
  }
  // Congestion map: one entry per routing layer, capacities and
  // usage positive, no tile over capacity on this fixture.
  ASSERT_EQ(report.per_layer.size(), 2u);
  EXPECT_EQ(report.per_layer[0].layer, 1);
  EXPECT_EQ(report.per_layer[1].layer, 2);
  for (const LayerCongestion& layer : report.per_layer) {
    EXPECT_GT(layer.capacity, 0);
    EXPECT_GT(layer.usage, 0);
    EXPECT_EQ(layer.overflow, 0);
  }
  EXPECT_EQ(report.total_overflow, 0);
}

TEST(Route, DetailedStageWritesWiresAndReportsViolations) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  RouteOptions options;
  options.liberty_path = dir.Write("test.lib", kLiberty);
  RouteReport report;
  const foundation::Status status =
      RunRoute(db.RawDatabase(), options, &report);
  ASSERT_TRUE(status.ok()) << status.message();
  EXPECT_EQ(report.net_routed_count, 3);
  // The detailed stage ran: the bound is echoed, at least one
  // rip-up and reroute pass ran, and the violation count is
  // stated (zero on this clean fixture means DRC-clean on the
  // rules the engine checks).
  EXPECT_EQ(report.detailed_iteration_bound, 64);
  EXPECT_GE(report.detailed_iterations_used, 1);
  EXPECT_LE(report.detailed_iterations_used,
            report.detailed_iteration_bound);
  EXPECT_GE(report.violation_count, 0);
  EXPECT_EQ(report.detailed_stopped_at_bound,
            report.violation_count > 0 &&
                report.detailed_iterations_used ==
                    report.detailed_iteration_bound);
  // The detailed stage wrote real wires back into the database.
  odb::dbBlock* block = db.RawDatabase()->getChip()->getBlock();
  for (odb::dbNet* net : block->getNets()) {
    EXPECT_NE(net->getWire(), nullptr)
        << "net " << net->getConstName() << " has no detailed wire";
  }
}

TEST(Route, DetailedBoundOfOneRunsExactlyOnePass) {
  // With the bound at 1 the loop runs exactly one pass whatever
  // the outcome: it stops at the bound exactly when violations
  // remain, and stops early (same single pass, flag false) when
  // the first pass already reaches zero violations.
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  RouteOptions options;
  options.max_detailed_iterations = 1;
  options.liberty_path = dir.Write("test.lib", kLiberty);
  RouteReport report;
  ASSERT_TRUE(RunRoute(db.RawDatabase(), options, &report).ok());
  EXPECT_EQ(report.detailed_iterations_used, 1);
  EXPECT_EQ(report.detailed_iteration_bound, 1);
  EXPECT_EQ(report.detailed_stopped_at_bound,
            report.violation_count > 0);
}

TEST(Route, ExplicitDetailedLayerRangeRoutes) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  RouteOptions options;
  options.bottom_routing_layer = "metal1";
  options.top_routing_layer = "metal2";
  options.liberty_path = dir.Write("test.lib", kLiberty);
  RouteReport report;
  ASSERT_TRUE(RunRoute(db.RawDatabase(), options, &report).ok())
      << "explicit metal1..metal2 detailed range must route";
  EXPECT_EQ(report.net_routed_count, 3);
  EXPECT_GE(report.violation_count, 0);
}

TEST(Route, UnknownDetailedLayerFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  RouteOptions options;
  options.bottom_routing_layer = "metal9";
  RouteReport report;
  const foundation::Status status =
      RunRoute(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
  EXPECT_NE(status.message().find("routing layer"), std::string::npos);
}

TEST(Route, NonRoutingDetailedLayerFailsFast) {
  // via1 is a CUT layer, not a routing layer, so naming it as a
  // detailed layer bound must fail fast before either engine runs.
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  RouteOptions options;
  options.top_routing_layer = "via1";
  RouteReport report;
  const foundation::Status status =
      RunRoute(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

TEST(Route, ZeroDetailedIterationsFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  RouteOptions options;
  options.max_detailed_iterations = 0;
  RouteReport report;
  const foundation::Status status =
      RunRoute(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
  EXPECT_NE(status.message().find("iteration bound"), std::string::npos);
}

TEST(Route, NegativeDetailedIterationsFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  RouteOptions options;
  options.max_detailed_iterations = -1;
  RouteReport report;
  const foundation::Status status =
      RunRoute(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

TEST(Route, SecondRunSucceeds) {
  // A second route in the same process must not dangle on the
  // first run's STA session (the runner clears the Sta singleton
  // before returning), and must not trip on the first run's
  // guides (globalRoute clears them before re-routing).
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  const std::string liberty = dir.Write("test.lib", kLiberty);
  for (int i = 0; i < 2; i++) {
    RouteOptions options;
    options.liberty_path = liberty;
    RouteReport report;
    ASSERT_TRUE(RunRoute(db.RawDatabase(), options, &report).ok());
    EXPECT_EQ(report.net_routed_count, 3);
    EXPECT_GT(report.guide_count, 0);
  }
}

TEST(Route, ExplicitFullLayerRangeRoutes) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  RouteOptions options;
  options.min_routing_layer = 1;
  options.max_routing_layer = 2;
  options.liberty_path = dir.Write("test.lib", kLiberty);
  RouteReport report;
  ASSERT_TRUE(RunRoute(db.RawDatabase(), options, &report).ok())
      << "explicit 1..2 range must behave like the default range";
  EXPECT_EQ(report.net_routed_count, 3);
  ASSERT_EQ(report.per_layer.size(), 2u);
}

// P8: a single-layer global range (min equals max) is accepted
// by the runner's range checks but fails in the global router:
// metal1 is horizontal only, so a net needing a vertical edge
// hits GRT-0229 (vertical edge usage over a zero limit) and the
// runner surfaces it as an engine failure. A routable range must
// span both directions; this test pins the single-direction
// failure mode rather than a success the engine cannot deliver.
TEST(Route, SingleLayerRangeFailsInEngine) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  RouteOptions options;
  options.min_routing_layer = 1;
  options.max_routing_layer = 1;
  options.liberty_path = dir.Write("test.lib", kLiberty);
  RouteReport report;
  const foundation::Status status =
      RunRoute(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

// P8: a minimum layer with the default maximum fails the same
// way in the other direction: with min 2 only the vertical layer
// is available, so a net needing a horizontal edge hits GRT-0228
// (horizontal edge usage over a zero limit).
TEST(Route, MinLayerOnlyFailsInEngine) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  RouteOptions options;
  options.min_routing_layer = 2;
  options.liberty_path = dir.Write("test.lib", kLiberty);
  RouteReport report;
  const foundation::Status status =
      RunRoute(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

// P8: a bottom detailed layer with the default top is a valid
// combination distinct from the explicit both-ends range.
TEST(Route, BottomLayerOnlyRoutes) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  RouteOptions options;
  options.bottom_routing_layer = "metal1";
  options.liberty_path = dir.Write("test.lib", kLiberty);
  RouteReport report;
  ASSERT_TRUE(RunRoute(db.RawDatabase(), options, &report).ok())
      << "bottom-only detailed range must route";
  EXPECT_EQ(report.net_routed_count, 3);
  EXPECT_GE(report.violation_count, 0);
}

// P8: an unreadable Liberty path is a distinct failure from an
// empty path (NoLibertyFailsFast). As in cts, the STA reader
// throws on a missing file, so the run fails fast through the
// runner's engine-failure path; the test pins the fail-fast and
// the code, not a message substring.
TEST(Route, UnreadableLibertyFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  RouteOptions options;
  options.liberty_path = "/nonexistent/eos-route.lib";
  RouteReport report;
  const foundation::Status status =
      RunRoute(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

// P8: a negative maximum layer fails fast on the same range
// branch as a negative minimum (NegativeLayerFailsFast covers
// the minimum side only).
TEST(Route, NegativeMaxLayerFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  RouteOptions options;
  options.max_routing_layer = -1;
  RouteReport report;
  const foundation::Status status =
      RunRoute(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

TEST(Route, NoDatabaseFailsFast) {
  RouteOptions options;
  RouteReport report;
  const foundation::Status status = RunRoute(nullptr, options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

TEST(Route, NoTechnologyFailsFast) {
  odb::dbDatabase* raw = odb::dbDatabase::create();
  RouteOptions options;
  RouteReport report;
  const foundation::Status status = RunRoute(raw, options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
  odb::dbDatabase::destroy(raw);
}

TEST(Route, NoMappedDesignFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, false);
  RouteOptions options;
  RouteReport report;
  const foundation::Status status =
      RunRoute(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

TEST(Route, UnfloorplannedDesignFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  RouteOptions options;
  RouteReport report;
  const foundation::Status status =
      RunRoute(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
  EXPECT_NE(status.message().find("floorplan"), std::string::npos);
}

TEST(Route, UnplacedDesignFailsFast) {
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
  RouteOptions options;
  RouteReport report;
  const foundation::Status status =
      RunRoute(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
  EXPECT_NE(status.message().find("placed"), std::string::npos);
}

TEST(Route, NoLibertyFailsFast) {
  // The engine's clock-net detection builds the STA graph and
  // throws STA-2141 without a Liberty library; the runner fails
  // fast instead, as the cts runner does.
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  RouteOptions options;
  RouteReport report;
  const foundation::Status status =
      RunRoute(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
  EXPECT_NE(status.message().find("Liberty"), std::string::npos);
}

TEST(Route, InvertedLayerRangeFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  RouteOptions options;
  options.min_routing_layer = 2;
  options.max_routing_layer = 1;
  RouteReport report;
  const foundation::Status status =
      RunRoute(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

TEST(Route, NegativeLayerFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanAndPlace(db);
  RouteOptions options;
  options.min_routing_layer = -1;
  RouteReport report;
  const foundation::Status status =
      RunRoute(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

}  // namespace
}  // namespace eos::pd
