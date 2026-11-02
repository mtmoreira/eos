// Unit tests: pd/place (the EOS runner over RePlAce + Opendp).
//
// Fixture shape mirrors the floorplan tests: one site, two routing
// layers, one two-pin macro in LEF; two placed instances, two
// placed top-level pins and three nets in DEF. Each DEF pin carries
// a PORT rectangle, because odb creates a placed BPin only from
// port geometry. They are licence-clean by construction. The
// runner operates on the facade's live database through
// RawDatabase, the same path the eos::place command uses. Every
// placement test floorplans first (RunFloorplan with default
// areas), because place fails fast on an un-floorplanned design
// by design; that failure mode has its own test below.
//
// The fixture has no Liberty, so no real STA slack can be measured
// here: unit tests assert placement, overlap, and fail-fast
// behavior. Placed STA slack is measured on the gcd evidence run
// (see the P2 report) and through the registry test asserting the
// STA-unavailable payload when no timing session is configured.
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
- u2 BUFX2 + PLACED ( 2000 1000 ) N ;
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

class TempDir {
 public:
  TempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("eos_place_test_" + std::to_string(counter_++))) {
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

void LoadDesign(db::OdbDatabase& db, const TempDir& dir, bool with_def) {
  const std::string lef = dir.Write("test.lef", kLef);
  ASSERT_TRUE(db.ReadLef(lef).ok());
  if (with_def) {
    const std::string def = dir.Write("test.def", kDef);
    ASSERT_TRUE(db.ReadDef(def).ok());
  }
}

void Floorplan(db::OdbDatabase& db) {
  FloorplanOptions options;
  FloorplanReport report;
  ASSERT_TRUE(RunFloorplan(db.RawDatabase(), options, &report).ok());
  ASSERT_GT(report.row_count, 0);
}

TEST(Place, PlacesAllInstancesWithNoOverlaps) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  Floorplan(db);
  PlaceOptions options;
  PlaceReport report;
  const foundation::Status status =
      RunPlace(db.RawDatabase(), options, &report);
  ASSERT_TRUE(status.ok()) << status.message();
  EXPECT_EQ(report.instance_total_count, 2);
  EXPECT_EQ(report.instance_placed_count, 2);
  EXPECT_EQ(report.overlap_count, 0);
  EXPECT_GT(report.hpwl_dbu, 0);

  // Every instance is placed inside the die area, not just
  // counted as placed.
  odb::dbBlock* block = db.RawDatabase()->getChip()->getBlock();
  for (odb::dbInst* inst : block->getInsts()) {
    EXPECT_TRUE(inst->isPlaced());
    int x, y;
    inst->getLocation(x, y);
    EXPECT_GE(x, 0);
    EXPECT_GE(y, 0);
    EXPECT_LE(x, 10000);
    EXPECT_LE(y, 10000);
  }
}

TEST(Place, SecondRunSucceeds) {
  // A second place in the same process must not dangle on the
  // first run's STA session (the runner clears the Sta singleton
  // before returning).
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  Floorplan(db);
  PlaceOptions options;
  PlaceReport report;
  ASSERT_TRUE(RunPlace(db.RawDatabase(), options, &report).ok());
  ASSERT_TRUE(RunPlace(db.RawDatabase(), options, &report).ok());
  EXPECT_EQ(report.instance_placed_count, 2);
  EXPECT_EQ(report.overlap_count, 0);
}

TEST(Place, NoDatabaseFailsFast) {
  PlaceOptions options;
  PlaceReport report;
  const foundation::Status status = RunPlace(nullptr, options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

TEST(Place, NoTechnologyFailsFast) {
  odb::dbDatabase* raw = odb::dbDatabase::create();
  PlaceOptions options;
  PlaceReport report;
  const foundation::Status status = RunPlace(raw, options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
  odb::dbDatabase::destroy(raw);
}

TEST(Place, NoMappedDesignFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, false);
  PlaceOptions options;
  PlaceReport report;
  const foundation::Status status =
      RunPlace(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

TEST(Place, UnfloorplannedDesignFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  PlaceOptions options;
  PlaceReport report;
  const foundation::Status status =
      RunPlace(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
  EXPECT_NE(status.message().find("floorplan"), std::string::npos);
}

TEST(Place, TimingDrivenWithoutLibertyFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  Floorplan(db);
  PlaceOptions options;
  options.timing_driven = true;
  PlaceReport report;
  const foundation::Status status =
      RunPlace(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
  EXPECT_NE(status.message().find("Liberty"), std::string::npos);
}

}  // namespace
}  // namespace eos::pd
