// Unit tests: pv/drc (the LEF-driven in-tree DRC engine, F13).
//
// Fixture shape mirrors tests/unit/pd/finish/finish_test.cc: one
// site, two routing layers plus a cut layer, one two-pin macro
// in LEF; two placed instances, two placed top-level pins, and
// three two-pin signal nets in DEF. Wires are built
// programmatically with dbWireEncoder, so each seeded case
// controls its geometry exactly.
//
// All fixture numbers are invented for this test, never process
// data. Units: DATABASE MICRONS 1000 (1 DBU = 1 nm).
//   metal1/metal2: WIDTH 0.2, SPACING 0.2, MINWIDTH 0.18,
//     AREA 0.06, MINSTEP 0.1.
//   via1: WIDTH 0.06, SPACING 0.5, ENCLOSURE 0.05 0.05.
//   VIA12 metal 0.3 x 0.3, cut 0.06 x 0.06.
//   VIA12_BAD metal1 shifted left 0.09, so the cut sticks out
//     0.03 on the right (enclosure 0.03 < 0.05) while the
//     metal stays 0.3 wide, so width and min-area still pass.
// AREA 0.06 sits above the 0.2 x 0.2 minimum shape
// (0.04), so a short 0.25-long stub fails min-area while the
// 0.3 x 0.3 pins and via metal (0.09) pass it. Cut SPACING
// 0.5 sits above metal SPACING 0.2 plus the metal-minus-cut
// width (0.24), so two vias 0.52 apart fail cut spacing
// (gap 0.46) while their metal clears metal spacing (gap
// 0.22). Seeded wires live at y >= 6000, clear of the
// instances (y 1000-3720) and the top pins (y 4850-5150).
#include "eos/pv/drc.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "eos/db/odb.h"
#include "odb/db.h"
#include "odb/dbWireCodec.h"

namespace eos::pv {
namespace {

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
  PITCH 0.2 ;
  WIDTH 0.2 ;
  SPACING 0.2 ;
  MINWIDTH 0.18 ;
  AREA 0.06 ;
  MINSTEP 0.1 ;
END metal1
LAYER via1
  TYPE CUT ;
  WIDTH 0.06 ;
  SPACING 0.5 ;
  ENCLOSURE 0.05 0.05 ;
END via1
LAYER metal2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 0.2 ;
  WIDTH 0.2 ;
  SPACING 0.2 ;
  MINWIDTH 0.18 ;
  AREA 0.06 ;
  MINSTEP 0.1 ;
END metal2
VIA VIA12
  LAYER metal1 ;
    RECT -0.15 -0.15 0.15 0.15 ;
  LAYER via1 ;
    RECT -0.03 -0.03 0.03 0.03 ;
  LAYER metal2 ;
    RECT -0.15 -0.15 0.15 0.15 ;
END VIA12
VIA VIA12_BAD
  LAYER metal1 ;
    RECT -0.24 -0.15 0.06 0.15 ;
  LAYER via1 ;
    RECT -0.03 -0.03 0.03 0.03 ;
  LAYER metal2 ;
    RECT -0.15 -0.15 0.15 0.15 ;
END VIA12_BAD
MACRO BUFX2
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 1.0 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT 0.05 0.05 0.35 0.35 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT 0.65 0.05 0.95 0.35 ;
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
- n_in + NET n_in + DIRECTION INPUT + USE SIGNAL
  + PORT
  + LAYER metal1 ( -150 -150 ) ( 150 150 )
  + PLACED ( 0 5000 ) N ;
- n_out + NET n_out + DIRECTION OUTPUT + USE SIGNAL
  + PORT
  + LAYER metal1 ( -150 -150 ) ( 150 150 )
  + PLACED ( 10000 5000 ) N ;
END PINS
NETS 3 ;
- n_in ( PIN n_in ) ( u1 A ) ;
- n_mid ( u1 Y ) ( u2 A ) ;
- n_out ( u2 Y ) ( PIN n_out ) ;
END NETS
END DESIGN
)DEF";

class TempDir {
 public:
  TempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("eos_drc_test_" + std::to_string(counter_++))) {
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  std::string Write(const std::string& name,
                    const std::string& text) const {
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

void LoadDesign(db::OdbDatabase& db, const TempDir& dir) {
  const std::string lef_path = dir.Write("test.lef", kLef);
  ASSERT_TRUE(db.ReadLef(lef_path).ok());
  const std::string def_path = dir.Write("test.def", kDef);
  ASSERT_TRUE(db.ReadDef(def_path).ok());
}

odb::dbBlock* TopBlock(db::OdbDatabase& db) {
  return db.RawDatabase()->getChip()->getBlock();
}

odb::dbTechLayer* Layer(db::OdbDatabase& db, const char* name) {
  odb::dbTechLayer* layer =
      db.RawDatabase()->getTech()->findLayer(name);
  EXPECT_NE(layer, nullptr) << "missing layer " << name;
  return layer;
}

// Builds one routed path on a net: a single path through the
// given points on the given layer.
void AddPath(db::OdbDatabase& db, const std::string& net_name,
             const char* layer_name,
             const std::vector<std::pair<int, int>>& points) {
  odb::dbBlock* block = TopBlock(db);
  odb::dbNet* net = block->findNet(net_name.c_str());
  ASSERT_NE(net, nullptr) << "missing net " << net_name;
  odb::dbWire* wire = odb::dbWire::create(net);
  odb::dbWireEncoder encoder;
  encoder.begin(wire);
  encoder.newPath(Layer(db, layer_name), odb::dbWireType::ROUTED);
  for (const auto& point : points) {
    encoder.addPoint(point.first, point.second);
  }
  encoder.end();
}

// Builds one routed path ending in a via at its last point.
void AddPathWithVia(db::OdbDatabase& db, const std::string& net_name,
                    const char* layer_name,
                    const std::vector<std::pair<int, int>>& points,
                    const char* via_name) {
  odb::dbBlock* block = TopBlock(db);
  odb::dbNet* net = block->findNet(net_name.c_str());
  ASSERT_NE(net, nullptr) << "missing net " << net_name;
  odb::dbTechVia* via = db.RawDatabase()->getTech()->findVia(via_name);
  ASSERT_NE(via, nullptr) << "missing via " << via_name;
  odb::dbWire* wire = odb::dbWire::create(net);
  odb::dbWireEncoder encoder;
  encoder.begin(wire);
  encoder.newPath(Layer(db, layer_name), odb::dbWireType::ROUTED);
  for (const auto& point : points) {
    encoder.addPoint(point.first, point.second);
  }
  encoder.addTechVia(via);
  encoder.end();
}

// Builds a patch rect on a net: a single-point path plus a
// rect relative to that point.
void AddPatch(db::OdbDatabase& db, const std::string& net_name,
              const char* layer_name, int x, int y, int dx1, int dy1,
              int dx2, int dy2) {
  odb::dbBlock* block = TopBlock(db);
  odb::dbNet* net = block->findNet(net_name.c_str());
  ASSERT_NE(net, nullptr) << "missing net " << net_name;
  odb::dbWire* wire = odb::dbWire::create(net);
  odb::dbWireEncoder encoder;
  encoder.begin(wire);
  encoder.newPath(Layer(db, layer_name), odb::dbWireType::ROUTED);
  encoder.addPoint(x, y);
  encoder.addRect(dx1, dy1, dx2, dy2);
  encoder.end();
}

const DrcCheckResult* FindCheck(const DrcRecord& record,
                                const std::string& check) {
  for (const DrcCheckResult& result : record.checks) {
    if (result.check == check) {
      return &result;
    }
  }
  return nullptr;
}

void ExpectCheck(const DrcRecord& record, const std::string& check,
                 long long rule_value, std::size_t violations) {
  const DrcCheckResult* result = FindCheck(record, check);
  ASSERT_NE(result, nullptr) << "missing check " << check;
  EXPECT_EQ(result->rule_value, rule_value)
      << "rule value for " << check;
  EXPECT_EQ(result->violations.size(), violations)
      << "violations for " << check;
}

void ExpectAllClean(const DrcRecord& record) {
  for (const DrcCheckResult& result : record.checks) {
    EXPECT_EQ(result.violations.size(), 0u)
        << "unexpected violations for " << result.check;
  }
}

// The clean routed design: three L-shaped and straight wires
// joining the pins, every gap at least the 0.2 spacing, every
// shape at least the 0.06 min-area. Pin and wire rects:
// u1.A (1050,1050)-(1350,1350), u1.Y (1650,1050)-(1950,1350),
// u2.A (8050,1050)-(8350,1350), u2.Y (8650,1050)-(8950,1350),
// top pins (-150,4850)-(150,5150) and (9850,4850)-(10150,5150).
void AddCleanWires(db::OdbDatabase& db) {
  AddPath(db, "n_in", "metal1",
          {{0, 5000}, {1200, 5000}, {1200, 1200}});
  AddPath(db, "n_mid", "metal1", {{1800, 1200}, {8200, 1200}});
  AddPath(db, "n_out", "metal1",
          {{8800, 1200}, {8800, 5000}, {10000, 5000}});
}

TEST(DrcTest, CleanDesignReportsZeroPerCheck) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  AddCleanWires(db);

  DrcRecord record;
  std::string error;
  ASSERT_TRUE(RunDrc(db.RawDatabase(), &record, &error)) << error;
  EXPECT_EQ(record.source, "live-database");
  EXPECT_EQ(record.checks.size(), 10u);
  EXPECT_EQ(record.TotalViolations(), 0u);
  ExpectCheck(record, "metal1.width", 180, 0);
  ExpectCheck(record, "metal1.spacing", 200, 0);
  ExpectCheck(record, "metal1.min-area", 60000, 0);
  ExpectCheck(record, "metal1.min-step", 100, 0);
  ExpectCheck(record, "metal2.width", 180, 0);
  ExpectCheck(record, "metal2.spacing", 200, 0);
  ExpectCheck(record, "metal2.min-area", 60000, 0);
  ExpectCheck(record, "metal2.min-step", 100, 0);
  ExpectCheck(record, "via1.enclosure", 50, 0);
  ExpectCheck(record, "via1.cut-spacing", 500, 0);
}

TEST(DrcTest, NarrowWireCaughtExactlyOnce) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  // 0.1 x 0.7 patch rect: min dimension 100 < MINWIDTH 180,
  // area 70000 >= AREA 60000, clear of every other shape.
  AddPatch(db, "n_in", "metal1", 3000, 7000, -50, -350, 50, 350);

  DrcRecord record;
  std::string error;
  ASSERT_TRUE(RunDrc(db.RawDatabase(), &record, &error)) << error;
  EXPECT_EQ(record.TotalViolations(), 1u);
  const DrcCheckResult* width = FindCheck(record, "metal1.width");
  ASSERT_NE(width, nullptr);
  ASSERT_EQ(width->violations.size(), 1u);
  EXPECT_EQ(width->violations[0].net, "n_in");
  EXPECT_EQ(width->violations[0].detail, "width=100");
  EXPECT_EQ(width->violations[0].x1, 2950);
  EXPECT_EQ(width->violations[0].y1, 6650);
  EXPECT_EQ(width->violations[0].x2, 3050);
  EXPECT_EQ(width->violations[0].y2, 7350);
  ExpectCheck(record, "metal1.spacing", 200, 0);
  ExpectCheck(record, "metal1.min-area", 60000, 0);
}

TEST(DrcTest, CloseWiresCaughtExactlyOnce) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  // Two horizontal wires 100 apart, below the 200 spacing.
  AddPath(db, "n_in", "metal1", {{1000, 7000}, {5000, 7000}});
  AddPath(db, "n_mid", "metal1", {{1000, 7300}, {5000, 7300}});

  DrcRecord record;
  std::string error;
  ASSERT_TRUE(RunDrc(db.RawDatabase(), &record, &error)) << error;
  EXPECT_EQ(record.TotalViolations(), 1u);
  const DrcCheckResult* spacing =
      FindCheck(record, "metal1.spacing");
  ASSERT_NE(spacing, nullptr);
  ASSERT_EQ(spacing->violations.size(), 1u);
  EXPECT_EQ(spacing->violations[0].net, "n_in");
  EXPECT_EQ(spacing->violations[0].detail, "gap=100");
  EXPECT_EQ(spacing->violations[0].x1, 900);
  EXPECT_EQ(spacing->violations[0].y1, 6900);
  EXPECT_EQ(spacing->violations[0].x2, 5100);
  EXPECT_EQ(spacing->violations[0].y2, 7400);
  ExpectCheck(record, "metal1.width", 180, 0);
  ExpectCheck(record, "metal1.min-area", 60000, 0);
}

TEST(DrcTest, BadViaEnclosureCaughtExactlyOnce) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  AddPathWithVia(db, "n_in", "metal1",
                 {{1000, 7000}, {3000, 7000}}, "VIA12_BAD");

  DrcRecord record;
  std::string error;
  ASSERT_TRUE(RunDrc(db.RawDatabase(), &record, &error)) << error;
  EXPECT_EQ(record.TotalViolations(), 1u);
  const DrcCheckResult* enclosure =
      FindCheck(record, "via1.enclosure");
  ASSERT_NE(enclosure, nullptr);
  ASSERT_EQ(enclosure->violations.size(), 1u);
  EXPECT_EQ(enclosure->violations[0].net, "n_in");
  EXPECT_EQ(enclosure->violations[0].detail, "metal=metal1");
  EXPECT_EQ(enclosure->violations[0].x1, 2970);
  EXPECT_EQ(enclosure->violations[0].y1, 6970);
  EXPECT_EQ(enclosure->violations[0].x2, 3030);
  EXPECT_EQ(enclosure->violations[0].y2, 7030);
  ExpectCheck(record, "metal1.width", 180, 0);
  ExpectCheck(record, "metal1.min-area", 60000, 0);
  ExpectCheck(record, "via1.cut-spacing", 500, 0);
}

TEST(DrcTest, SmallShapeCaughtExactlyOnce) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  // A 0.05-long stub: the swept rect is 0.25 x 0.2, area
  // 50000 < AREA 60000, min dimension 200 >= MINWIDTH 180.
  AddPath(db, "n_in", "metal1", {{1000, 7000}, {1050, 7000}});

  DrcRecord record;
  std::string error;
  ASSERT_TRUE(RunDrc(db.RawDatabase(), &record, &error)) << error;
  EXPECT_EQ(record.TotalViolations(), 1u);
  const DrcCheckResult* min_area =
      FindCheck(record, "metal1.min-area");
  ASSERT_NE(min_area, nullptr);
  ASSERT_EQ(min_area->violations.size(), 1u);
  EXPECT_EQ(min_area->violations[0].net, "n_in");
  EXPECT_EQ(min_area->violations[0].detail, "area=50000");
  EXPECT_EQ(min_area->violations[0].x1, 900);
  EXPECT_EQ(min_area->violations[0].y1, 6900);
  EXPECT_EQ(min_area->violations[0].x2, 1150);
  EXPECT_EQ(min_area->violations[0].y2, 7100);
  ExpectCheck(record, "metal1.width", 180, 0);
  ExpectCheck(record, "metal1.spacing", 200, 0);
}

TEST(DrcTest, ShortJogCaughtExactlyOnce) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  // An offset jog 50 long, below the 100 MINSTEP, between two
  // horizontal runs.
  AddPath(db, "n_in", "metal1",
          {{1000, 7000}, {3000, 7000}, {3000, 7050}, {5000, 7050}});

  DrcRecord record;
  std::string error;
  ASSERT_TRUE(RunDrc(db.RawDatabase(), &record, &error)) << error;
  EXPECT_EQ(record.TotalViolations(), 1u);
  const DrcCheckResult* min_step =
      FindCheck(record, "metal1.min-step");
  ASSERT_NE(min_step, nullptr);
  ASSERT_EQ(min_step->violations.size(), 1u);
  EXPECT_EQ(min_step->violations[0].net, "n_in");
  EXPECT_EQ(min_step->violations[0].detail, "step=50");
  EXPECT_EQ(min_step->violations[0].x1, 2900);
  EXPECT_EQ(min_step->violations[0].y1, 6900);
  EXPECT_EQ(min_step->violations[0].x2, 3100);
  EXPECT_EQ(min_step->violations[0].y2, 7150);
  ExpectCheck(record, "metal1.width", 180, 0);
  ExpectCheck(record, "metal1.spacing", 200, 0);
  ExpectCheck(record, "metal1.min-area", 60000, 0);
}

TEST(DrcTest, CloseViasCaughtExactlyOnce) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  // Two good vias 520 apart on different nets: the cut gap
  // is 460 < 500, the metal gap is 220 >= 200, the stub gap
  // is 320 >= 200, so only cut spacing fires.
  AddPathWithVia(db, "n_in", "metal1",
                 {{3000, 6800}, {3000, 7200}}, "VIA12");
  AddPathWithVia(db, "n_mid", "metal1",
                 {{3520, 6800}, {3520, 7200}}, "VIA12");

  DrcRecord record;
  std::string error;
  ASSERT_TRUE(RunDrc(db.RawDatabase(), &record, &error)) << error;
  EXPECT_EQ(record.TotalViolations(), 1u);
  const DrcCheckResult* cut_spacing =
      FindCheck(record, "via1.cut-spacing");
  ASSERT_NE(cut_spacing, nullptr);
  ASSERT_EQ(cut_spacing->violations.size(), 1u);
  EXPECT_EQ(cut_spacing->violations[0].net, "n_in");
  EXPECT_EQ(cut_spacing->violations[0].detail, "gap=460");
  EXPECT_EQ(cut_spacing->violations[0].x1, 2970);
  EXPECT_EQ(cut_spacing->violations[0].y1, 7170);
  EXPECT_EQ(cut_spacing->violations[0].x2, 3550);
  EXPECT_EQ(cut_spacing->violations[0].y2, 7230);
  ExpectCheck(record, "metal1.spacing", 200, 0);
  ExpectCheck(record, "metal2.spacing", 200, 0);
  ExpectCheck(record, "via1.enclosure", 50, 0);
  ExpectCheck(record, "metal1.min-area", 60000, 0);
}

TEST(DrcTest, FailsFastWithNoTechnology) {
  db::OdbDatabase db;
  DrcRecord record;
  std::string error;
  EXPECT_FALSE(RunDrc(db.RawDatabase(), &record, &error));
  EXPECT_NE(error.find("no technology data"), std::string::npos)
      << error;
}

TEST(DrcTest, FailsFastWithNoDesign) {
  TempDir dir;
  db::OdbDatabase db;
  const std::string lef_path = dir.Write("test.lef", kLef);
  ASSERT_TRUE(db.ReadLef(lef_path).ok());
  DrcRecord record;
  std::string error;
  EXPECT_FALSE(RunDrc(db.RawDatabase(), &record, &error));
  EXPECT_NE(error.find("no design loaded"), std::string::npos)
      << error;
}

TEST(DrcTest, FailsFastWithNoRoutedDesign) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  DrcRecord record;
  std::string error;
  EXPECT_FALSE(RunDrc(db.RawDatabase(), &record, &error));
  EXPECT_NE(error.find("no routed design"), std::string::npos)
      << error;
}

}  // namespace
}  // namespace eos::pv
