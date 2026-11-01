// Unit tests: pd/floorplan (the EOS runner over InitFloorplan).
//
// The fixtures are small synthetic LEF and DEF texts, embedded here
// and written to a temporary directory per test, in the same shape
// as the db/odb facade tests: one site, two routing layers, one
// two-pin macro in LEF; two placed instances, two placed
// top-level pins and three nets in DEF. Each DEF pin carries a
// PORT rectangle: odb creates a placed BPin only from port
// geometry, so a pin without a PORT would report as unplaced.
// They are licence-clean by
// construction. The runner operates on the facade's live database
// through RawDatabase, the same path the eos::floorplan command
// uses.
#include "eos/pd/floorplan.h"

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
              ("eos_floorplan_test_" + std::to_string(counter_++))) {
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

void LoadDesign(db::OdbDatabase& db, const TempDir& dir, bool with_def) {
  const std::string lef = dir.Write("test.lef", kLef);
  ASSERT_TRUE(db.ReadLef(lef).ok());
  if (with_def) {
    const std::string def = dir.Write("test.def", kDef);
    ASSERT_TRUE(db.ReadDef(def).ok());
  }
}

int CountRows(odb::dbBlock* block) {
  int rows = 0;
  for (odb::dbRow* row : block->getRows()) {
    (void)row;
    ++rows;
  }
  return rows;
}

TEST(Floorplan, ExplicitAreasCreateRowsTracksAndPlacedIo) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanOptions options;
  options.has_die_area = true;
  options.die_area = {0, 0, 10000, 10000};
  options.has_core_area = true;
  options.core_area = {0, 0, 10000, 10000};
  FloorplanReport report;
  ASSERT_TRUE(RunFloorplan(db.RawDatabase(), options, &report).ok());
  EXPECT_EQ(report.die_area, (std::array<int, 4>{0, 0, 10000, 10000}));
  EXPECT_EQ(report.core_area, (std::array<int, 4>{0, 0, 10000, 10000}));
  EXPECT_GT(report.row_count, 0);
  EXPECT_GE(report.track_grid_count, 1);
  EXPECT_EQ(report.io_total_count, 2);
  EXPECT_EQ(report.io_placed_count, 2);

  // The floorplanned design reloads in OpenDB with its rows intact.
  const std::string odb_path = dir.Path("floorplanned.odb");
  ASSERT_TRUE(db.WriteDb(odb_path).ok());
  db::OdbDatabase reloaded;
  ASSERT_TRUE(reloaded.ReadDb(odb_path).ok());
  ASSERT_TRUE(reloaded.has_top_block());
  odb::dbBlock* block = reloaded.RawDatabase()->getChip()->getBlock();
  EXPECT_EQ(CountRows(block), report.row_count);
  EXPECT_EQ(block->getDieArea().xMax(), 10000);
}

TEST(Floorplan, DefaultAreasComeFromTheDesign) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanOptions options;
  FloorplanReport report;
  ASSERT_TRUE(RunFloorplan(db.RawDatabase(), options, &report).ok());
  EXPECT_EQ(report.die_area, (std::array<int, 4>{0, 0, 10000, 10000}));
  EXPECT_GT(report.row_count, 0);
}

TEST(Floorplan, NamedSiteIsAccepted) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanOptions options;
  options.site = "core";
  FloorplanReport report;
  ASSERT_TRUE(RunFloorplan(db.RawDatabase(), options, &report).ok());
  EXPECT_GT(report.row_count, 0);
}

TEST(Floorplan, NoDatabaseFailsFast) {
  FloorplanOptions options;
  FloorplanReport report;
  const foundation::Status status = RunFloorplan(nullptr, options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

TEST(Floorplan, NoTechnologyFailsFast) {
  // A fresh database holds no technology and no chip.
  odb::dbDatabase* raw = odb::dbDatabase::create();
  FloorplanOptions options;
  FloorplanReport report;
  const foundation::Status status = RunFloorplan(raw, options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
  odb::dbDatabase::destroy(raw);
}

TEST(Floorplan, NoMappedDesignFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, false);
  FloorplanOptions options;
  FloorplanReport report;
  const foundation::Status status =
      RunFloorplan(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

TEST(Floorplan, UnknownSiteFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanOptions options;
  options.site = "no_such_site";
  FloorplanReport report;
  const foundation::Status status =
      RunFloorplan(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kNotFound);
}

TEST(Floorplan, EmptyDieAreaFailsFast) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, true);
  FloorplanOptions options;
  options.has_die_area = true;
  options.die_area = {0, 0, 0, 0};
  FloorplanReport report;
  const foundation::Status status =
      RunFloorplan(db.RawDatabase(), options, &report);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidArgument);
}

}  // namespace
}  // namespace eos::pd
