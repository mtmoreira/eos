// Unit tests: pv/lvs (the connectivity LVS engine, F13).
//
// Fixture shape mirrors tests/unit/pv/drc/drc_test.cc: one
// site, two routing layers plus a cut layer, macros in LEF;
// placed instances, placed top-level pins, and signal nets in
// DEF. Wires are built programmatically with dbWireEncoder,
// so each seeded case controls its geometry exactly. Every
// broken design is built here in a temp-dir scratch database;
// no committed broken fixture exists.
//
// All fixture numbers are invented for this test, never
// process data. Units: DATABASE MICRONS 1000 (1 DBU = 1 nm).
//   metal1/metal2: WIDTH 0.2. via1: WIDTH 0.06.
//   VIA12 metal 0.3 x 0.3, cut 0.06 x 0.06.
// The design under comparison: u1 and u2 (BUFX2) chained by
// n_in, n_mid, n_out between two top pins; u3 carries the
// single-pin net n_lone (Y) and the POWER net n_pwr (A,
// skipped); ufill is a terminal-less filler master, invisible
// to both sides. Pin rects: u1.A (1050,1050)-(1350,1350),
// u1.Y (1650,1050)-(1950,1350), u2.A (8050,1050)-(8350,1350),
// u2.Y (8650,1050)-(8950,1350), top pins (-150,4850)-
// (150,5150) and (9850,4850)-(10150,5150).
#include "eos/pv/lvs.h"

#include <cstddef>
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
END metal1
LAYER via1
  TYPE CUT ;
  WIDTH 0.06 ;
END via1
LAYER metal2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 0.2 ;
  WIDTH 0.2 ;
END metal2
VIA VIA12
  LAYER metal1 ;
    RECT -0.15 -0.15 0.15 0.15 ;
  LAYER via1 ;
    RECT -0.03 -0.03 0.03 0.03 ;
  LAYER metal2 ;
    RECT -0.15 -0.15 0.15 0.15 ;
END VIA12
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
MACRO FILLX1
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 0.2 BY 2.72 ;
  SITE core ;
END FILLX1
END LIBRARY
)LEF";

const char kDef[] = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 4 ;
- u1 BUFX2 + PLACED ( 1000 1000 ) N ;
- u2 BUFX2 + PLACED ( 8000 1000 ) N ;
- u3 BUFX2 + PLACED ( 8000 6000 ) N ;
- ufill FILLX1 + PLACED ( 4000 1000 ) N ;
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
NETS 4 ;
- n_in ( PIN n_in ) ( u1 A ) ;
- n_mid ( u1 Y ) ( u2 A ) ;
- n_out ( u2 Y ) ( PIN n_out ) ;
- n_lone ( u3 Y ) ;
END NETS
SPECIALNETS 1 ;
- n_pwr ( u3 A ) + USE POWER ;
END SPECIALNETS
END DESIGN
)DEF";

const char kEmptyDef[] = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
END DESIGN
)DEF";

// Variant DEFs for the pin-contact amendment (design section
// 18): u4 added so its A pin rect meets u1.Y's pin rect
// (1650,1050)-(1950,1350) on metal1. The BUFX2 pin rects are
// inset 50 DBU from the cell edge, so abutting pin rects of
// neighboring cells require a 100 DBU cell-footprint overlap
// with this macro; placement legality is not part of the LVS
// comparison, and the overlap is deliberate here.
//   edge:    u4 at (1900,1000): A rect (1950,1050)-
//            (2250,1350) abuts u1.Y along x=1950, no overlap.
//   overlap: u4 at (1800,1000): A rect (1850,1050)-
//            (2150,1350) overlaps u1.Y in x 1850-1950.
const char kPinEdgeDef[] = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 5 ;
- u1 BUFX2 + PLACED ( 1000 1000 ) N ;
- u2 BUFX2 + PLACED ( 8000 1000 ) N ;
- u3 BUFX2 + PLACED ( 8000 6000 ) N ;
- u4 BUFX2 + PLACED ( 1900 1000 ) N ;
- ufill FILLX1 + PLACED ( 4000 1000 ) N ;
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
NETS 5 ;
- n_in ( PIN n_in ) ( u1 A ) ;
- n_mid ( u1 Y ) ( u2 A ) ;
- n_out ( u2 Y ) ( PIN n_out ) ;
- n_lone ( u3 Y ) ;
- n_x ( u4 A ) ( u4 Y ) ;
END NETS
SPECIALNETS 1 ;
- n_pwr ( u3 A ) + USE POWER ;
END SPECIALNETS
END DESIGN
)DEF";

const char kPinOverlapDef[] = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 5 ;
- u1 BUFX2 + PLACED ( 1000 1000 ) N ;
- u2 BUFX2 + PLACED ( 8000 1000 ) N ;
- u3 BUFX2 + PLACED ( 8000 6000 ) N ;
- u4 BUFX2 + PLACED ( 1800 1000 ) N ;
- ufill FILLX1 + PLACED ( 4000 1000 ) N ;
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
NETS 5 ;
- n_in ( PIN n_in ) ( u1 A ) ;
- n_mid ( u1 Y ) ( u2 A ) ;
- n_out ( u2 Y ) ( PIN n_out ) ;
- n_lone ( u3 Y ) ;
- n_x ( u4 A ) ( u4 Y ) ;
END NETS
SPECIALNETS 1 ;
- n_pwr ( u3 A ) + USE POWER ;
END SPECIALNETS
END DESIGN
)DEF";

class TempDir {
 public:
  TempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("eos_lvs_test_" + std::to_string(counter_++))) {
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

void LoadDesign(db::OdbDatabase& db, const TempDir& dir,
                const char* def = kDef) {
  const std::string lef_path = dir.Write("test.lef", kLef);
  ASSERT_TRUE(db.ReadLef(lef_path).ok());
  const std::string def_path = dir.Write("test.def", def);
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

odb::dbNet* Net(db::OdbDatabase& db, const std::string& name) {
  odb::dbNet* net = TopBlock(db)->findNet(name.c_str());
  EXPECT_NE(net, nullptr) << "missing net " << name;
  return net;
}

// Builds one routed path on a net.
void AddPath(db::OdbDatabase& db, const std::string& net_name,
             const char* layer_name,
             const std::vector<std::pair<int, int>>& points) {
  odb::dbWire* wire = odb::dbWire::create(Net(db, net_name));
  ASSERT_NE(wire, nullptr);
  odb::dbWireEncoder encoder;
  encoder.begin(wire);
  encoder.newPath(Layer(db, layer_name), odb::dbWireType::ROUTED);
  for (const auto& point : points) {
    encoder.addPoint(point.first, point.second);
  }
  encoder.end();
}

// Builds one wire holding two routed paths on one net (a net
// holds a single dbWire; branches and stubs are further paths
// in it).
void AddTwoPaths(db::OdbDatabase& db, const std::string& net_name,
                 const char* layer_name,
                 const std::vector<std::pair<int, int>>& first,
                 const std::vector<std::pair<int, int>>& second) {
  odb::dbWire* wire = odb::dbWire::create(Net(db, net_name));
  ASSERT_NE(wire, nullptr);
  odb::dbWireEncoder encoder;
  encoder.begin(wire);
  for (const auto* points : {&first, &second}) {
    encoder.newPath(Layer(db, layer_name), odb::dbWireType::ROUTED);
    for (const auto& point : *points) {
      encoder.addPoint(point.first, point.second);
    }
  }
  encoder.end();
}

// The clean routed design: three wires joining the pins of
// n_in, n_mid, and n_out, every contact an overlap or a
// positive-length edge.
void AddCleanWires(db::OdbDatabase& db) {
  AddPath(db, "n_in", "metal1",
          {{0, 5000}, {1200, 5000}, {1200, 1200}});
  AddPath(db, "n_mid", "metal1", {{1800, 1200}, {8200, 1200}});
  AddPath(db, "n_out", "metal1",
          {{8800, 1200}, {8800, 5000}, {10000, 5000}});
}

void ExpectCounts(const LvsRecord& record, std::size_t opens,
                  std::size_t shorts, std::size_t floating) {
  EXPECT_EQ(record.CountKind("open"), opens);
  EXPECT_EQ(record.CountKind("short"), shorts);
  EXPECT_EQ(record.CountKind("floating"), floating);
  EXPECT_EQ(record.TotalMismatches(), opens + shorts + floating);
}

// A1: the clean design compares clean. The POWER net is
// skipped and counted; the filler instance is invisible; the
// single-pin net counts and stays silent.
TEST(LvsTest, CleanDesignComparesClean) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  AddCleanWires(db);

  LvsRecord record;
  std::string error;
  ASSERT_TRUE(RunConnectivityLvs(db.RawDatabase(), &record, &error))
      << error;
  EXPECT_EQ(record.source, "live-database");
  EXPECT_EQ(record.nets_compared, 4u);
  EXPECT_EQ(record.pins_compared, 7u);
  EXPECT_EQ(record.single_pin_nets, 1u);
  EXPECT_EQ(record.special_nets_skipped, 1u);
  ExpectCounts(record, 0, 0, 0);
}

// A2: a dropped segment strands exactly one pin: one open
// naming the pin and its rectangle.
TEST(LvsTest, DroppedSegmentReportsOneOpen) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  // n_in keeps only its first segment, which reaches the top
  // pin but not u1.A.
  AddPath(db, "n_in", "metal1", {{0, 5000}, {1200, 5000}});
  AddPath(db, "n_mid", "metal1", {{1800, 1200}, {8200, 1200}});
  AddPath(db, "n_out", "metal1",
          {{8800, 1200}, {8800, 5000}, {10000, 5000}});

  LvsRecord record;
  std::string error;
  ASSERT_TRUE(RunConnectivityLvs(db.RawDatabase(), &record, &error))
      << error;
  ExpectCounts(record, 1, 0, 0);
  ASSERT_EQ(record.mismatches.size(), 1u);
  const LvsMismatch& open = record.mismatches[0];
  EXPECT_EQ(open.kind, "open");
  EXPECT_EQ(open.net, "n_in");
  EXPECT_EQ(open.pin, "u1:A");
  EXPECT_EQ(open.x1, 1050);
  EXPECT_EQ(open.y1, 1050);
  EXPECT_EQ(open.x2, 1350);
  EXPECT_EQ(open.y2, 1350);
}

// A2, the limiting case: a net with pins and no wire at all
// reports every pin after the first as open.
TEST(LvsTest, UnwiredNetReportsOpens) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  AddPath(db, "n_in", "metal1",
          {{0, 5000}, {1200, 5000}, {1200, 1200}});
  AddPath(db, "n_out", "metal1",
          {{8800, 1200}, {8800, 5000}, {10000, 5000}});

  LvsRecord record;
  std::string error;
  ASSERT_TRUE(RunConnectivityLvs(db.RawDatabase(), &record, &error))
      << error;
  ExpectCounts(record, 1, 0, 0);
  ASSERT_EQ(record.mismatches.size(), 1u);
  const LvsMismatch& open = record.mismatches[0];
  EXPECT_EQ(open.kind, "open");
  EXPECT_EQ(open.net, "n_mid");
  EXPECT_EQ(open.pin, "u2:A");
  EXPECT_EQ(open.x1, 8050);
  EXPECT_EQ(open.y1, 1050);
  EXPECT_EQ(open.x2, 8350);
  EXPECT_EQ(open.y2, 1350);
}

// A2: a segment of one net reaching into another net's pin
// reports exactly one short naming both nets, the layer, and
// the contact box.
TEST(LvsTest, BridgeReportsOneShort) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  // n_in runs past u1.A and into u1.Y's pin rect, which is
  // n_mid's pin: the two nets share one component.
  AddPath(db, "n_in", "metal1",
          {{0, 5000}, {1200, 5000}, {1200, 1200}, {2000, 1200}});
  AddPath(db, "n_mid", "metal1", {{1800, 1200}, {8200, 1200}});
  AddPath(db, "n_out", "metal1",
          {{8800, 1200}, {8800, 5000}, {10000, 5000}});

  LvsRecord record;
  std::string error;
  ASSERT_TRUE(RunConnectivityLvs(db.RawDatabase(), &record, &error))
      << error;
  ExpectCounts(record, 0, 1, 0);
  ASSERT_EQ(record.mismatches.size(), 1u);
  const LvsMismatch& short_ = record.mismatches[0];
  EXPECT_EQ(short_.kind, "short");
  EXPECT_EQ(short_.net, "n_in");
  EXPECT_EQ(short_.other_net, "n_mid");
  EXPECT_EQ(short_.layer, "metal1");
  // The first contact in shape order: n_in's last segment
  // rect (1100,1100)-(2100,1300) against u1.Y's pin rect
  // (1650,1050)-(1950,1350).
  EXPECT_EQ(short_.x1, 1650);
  EXPECT_EQ(short_.y1, 1100);
  EXPECT_EQ(short_.x2, 1950);
  EXPECT_EQ(short_.y2, 1300);
}

// A2: a stub path touching nothing is one floating mismatch
// charged to its net.
TEST(LvsTest, DanglingStubReportsOneFloating) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  AddPath(db, "n_in", "metal1",
          {{0, 5000}, {1200, 5000}, {1200, 1200}});
  AddPath(db, "n_mid", "metal1", {{1800, 1200}, {8200, 1200}});
  AddTwoPaths(db, "n_out", "metal1",
              {{8800, 1200}, {8800, 5000}, {10000, 5000}},
              {{5000, 7000}, {6000, 7000}});

  LvsRecord record;
  std::string error;
  ASSERT_TRUE(RunConnectivityLvs(db.RawDatabase(), &record, &error))
      << error;
  ExpectCounts(record, 0, 0, 1);
  ASSERT_EQ(record.mismatches.size(), 1u);
  const LvsMismatch& floating = record.mismatches[0];
  EXPECT_EQ(floating.kind, "floating");
  EXPECT_EQ(floating.net, "n_out");
  EXPECT_EQ(floating.layer, "metal1");
  EXPECT_EQ(floating.x1, 4900);
  EXPECT_EQ(floating.y1, 6900);
  EXPECT_EQ(floating.x2, 6100);
  EXPECT_EQ(floating.y2, 7100);
}

// A2: a two-layer net joined by vias at both ends compares
// clean (the via's boxes union together, joining the layers).
TEST(LvsTest, ViaJoinedNetComparesClean) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  AddPath(db, "n_in", "metal1",
          {{0, 5000}, {1200, 5000}, {1200, 1200}});
  // n_mid: a metal1 stub from u1.Y to a via, a metal2 run,
  // and a via down onto u2.A.
  {
    odb::dbWire* wire = odb::dbWire::create(Net(db, "n_mid"));
    ASSERT_NE(wire, nullptr);
    odb::dbTechVia* via =
        db.RawDatabase()->getTech()->findVia("VIA12");
    ASSERT_NE(via, nullptr);
    odb::dbWireEncoder encoder;
    encoder.begin(wire);
    encoder.newPath(Layer(db, "metal1"), odb::dbWireType::ROUTED);
    encoder.addPoint(1800, 1200);
    encoder.addPoint(3000, 1200);
    encoder.addTechVia(via);
    encoder.newPath(Layer(db, "metal2"), odb::dbWireType::ROUTED);
    encoder.addPoint(3000, 1200);
    encoder.addPoint(8200, 1200);
    encoder.addTechVia(via);
    encoder.end();
  }
  AddPath(db, "n_out", "metal1",
          {{8800, 1200}, {8800, 5000}, {10000, 5000}});

  LvsRecord record;
  std::string error;
  ASSERT_TRUE(RunConnectivityLvs(db.RawDatabase(), &record, &error))
      << error;
  ExpectCounts(record, 0, 0, 0);
  EXPECT_EQ(record.nets_compared, 4u);
}

// Design section 11: two nets whose segment rects abut only
// at a corner point. Evidence: under the designed closed-box
// rule this exact fixture reported one short (n_in against
// n_mid, the two segment rects (900,6900)-(2100,7100) and
// (2100,7100)-(3300,7300) sharing exactly the corner point
// (2100,7100)); the case fired, so the union rule was
// narrowed to edge-or-overlap contact as section 11
// prescribes, and a corner-only contact no longer unions.
TEST(LvsTest, DiagonalAbutDoesNotUnion) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  AddPath(db, "n_in", "metal1", {{1000, 7000}, {2000, 7000}});
  AddPath(db, "n_mid", "metal1", {{2200, 7200}, {3200, 7200}});

  LvsRecord record;
  std::string error;
  ASSERT_TRUE(RunConnectivityLvs(db.RawDatabase(), &record, &error))
      << error;
  EXPECT_EQ(record.CountKind("short"), 0u);
  for (const LvsMismatch& mismatch : record.mismatches) {
    EXPECT_NE(mismatch.kind, "short");
  }
}

// Design section 18, pin-contact amendment: two pin
// rectangles of different nets abutting edge to edge do not
// union. u4.A (1950,1050)-(2250,1350) abuts u1.Y
// (1650,1050)-(1950,1350) along x=1950; under the plain
// edge-or-overlap rule this exact pair read as a short (the
// F2 class on the clocked demo). The only wire is n_in's
// clean path, far from both pins, so a short here could
// only come from the pin-pin abutment.
TEST(LvsTest, PinPinEdgeAbutmentDoesNotUnion) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, kPinEdgeDef);
  AddPath(db, "n_in", "metal1",
          {{0, 5000}, {1200, 5000}, {1200, 1200}});

  LvsRecord record;
  std::string error;
  ASSERT_TRUE(RunConnectivityLvs(db.RawDatabase(), &record, &error))
      << error;
  EXPECT_EQ(record.nets_compared, 5u);
  EXPECT_EQ(record.CountKind("short"), 0u);
  for (const LvsMismatch& mismatch : record.mismatches) {
    EXPECT_NE(mismatch.kind, "short");
  }
}

// Design section 18: pin-pin contact still unions on area
// overlap. u4.A (1850,1050)-(2150,1350) overlaps u1.Y
// (1650,1050)-(1950,1350) in x 1850-1950: one short naming
// both nets and the overlap box.
TEST(LvsTest, PinPinOverlapUnions) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, kPinOverlapDef);
  AddPath(db, "n_in", "metal1",
          {{0, 5000}, {1200, 5000}, {1200, 1200}});

  LvsRecord record;
  std::string error;
  ASSERT_TRUE(RunConnectivityLvs(db.RawDatabase(), &record, &error))
      << error;
  EXPECT_EQ(record.nets_compared, 5u);
  EXPECT_EQ(record.CountKind("short"), 1u);
  const LvsMismatch* short_ = nullptr;
  for (const LvsMismatch& mismatch : record.mismatches) {
    if (mismatch.kind == "short") {
      short_ = &mismatch;
    }
  }
  ASSERT_NE(short_, nullptr);
  EXPECT_EQ(short_->net, "n_mid");
  EXPECT_EQ(short_->other_net, "n_x");
  EXPECT_EQ(short_->layer, "metal1");
  EXPECT_EQ(short_->x1, 1850);
  EXPECT_EQ(short_->y1, 1050);
  EXPECT_EQ(short_->x2, 1950);
  EXPECT_EQ(short_->y2, 1350);
}

// Design section 18: wire-wire edge abutment still unions.
// n_in's segment rect (900,6900)-(2100,7100) abuts n_mid's
// (900,7100)-(2100,7300) along y=7100, far from every pin.
TEST(LvsTest, WireWireEdgeAbutmentStillUnions) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  AddPath(db, "n_in", "metal1", {{1000, 7000}, {2000, 7000}});
  AddPath(db, "n_mid", "metal1", {{1000, 7200}, {2000, 7200}});

  LvsRecord record;
  std::string error;
  ASSERT_TRUE(RunConnectivityLvs(db.RawDatabase(), &record, &error))
      << error;
  EXPECT_EQ(record.CountKind("short"), 1u);
  const LvsMismatch* short_ = nullptr;
  for (const LvsMismatch& mismatch : record.mismatches) {
    if (mismatch.kind == "short") {
      short_ = &mismatch;
    }
  }
  ASSERT_NE(short_, nullptr);
  EXPECT_EQ(short_->net, "n_in");
  EXPECT_EQ(short_->other_net, "n_mid");
  EXPECT_EQ(short_->layer, "metal1");
  EXPECT_EQ(short_->x1, 900);
  EXPECT_EQ(short_->y1, 7100);
  EXPECT_EQ(short_->x2, 2100);
  EXPECT_EQ(short_->y2, 7100);
}

// Design section 18: pin-wire edge abutment still unions.
// n_in's segment rect (1600,1350)-(2000,1550) abuts u1.Y's
// pin rect (1650,1050)-(1950,1350) along y=1350.
TEST(LvsTest, PinWireEdgeAbutmentStillUnions) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  AddPath(db, "n_in", "metal1", {{1700, 1450}, {1900, 1450}});

  LvsRecord record;
  std::string error;
  ASSERT_TRUE(RunConnectivityLvs(db.RawDatabase(), &record, &error))
      << error;
  EXPECT_EQ(record.CountKind("short"), 1u);
  const LvsMismatch* short_ = nullptr;
  for (const LvsMismatch& mismatch : record.mismatches) {
    if (mismatch.kind == "short") {
      short_ = &mismatch;
    }
  }
  ASSERT_NE(short_, nullptr);
  EXPECT_EQ(short_->net, "n_in");
  EXPECT_EQ(short_->other_net, "n_mid");
  EXPECT_EQ(short_->layer, "metal1");
  EXPECT_EQ(short_->x1, 1650);
  EXPECT_EQ(short_->y1, 1350);
  EXPECT_EQ(short_->x2, 1950);
  EXPECT_EQ(short_->y2, 1350);
}

// A3: the failure states each fail with the state named and
// write no record verdict.
TEST(LvsTest, FailsFastWithNullDatabase) {
  LvsRecord record;
  std::string error;
  EXPECT_FALSE(RunConnectivityLvs(nullptr, &record, &error));
  EXPECT_NE(error.find("no database"), std::string::npos) << error;
}

TEST(LvsTest, FailsFastWithNoTechnology) {
  db::OdbDatabase db;
  LvsRecord record;
  std::string error;
  EXPECT_FALSE(RunConnectivityLvs(db.RawDatabase(), &record, &error));
  EXPECT_NE(error.find("no technology data"), std::string::npos)
      << error;
}

TEST(LvsTest, FailsFastWithNoDesign) {
  TempDir dir;
  db::OdbDatabase db;
  const std::string lef_path = dir.Write("test.lef", kLef);
  ASSERT_TRUE(db.ReadLef(lef_path).ok());
  LvsRecord record;
  std::string error;
  EXPECT_FALSE(RunConnectivityLvs(db.RawDatabase(), &record, &error));
  EXPECT_NE(error.find("no design loaded"), std::string::npos)
      << error;
}

TEST(LvsTest, FailsFastWithNoRoutedDesign) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir);
  LvsRecord record;
  std::string error;
  EXPECT_FALSE(RunConnectivityLvs(db.RawDatabase(), &record, &error));
  EXPECT_NE(error.find("no routed design"), std::string::npos)
      << error;
}

TEST(LvsTest, FailsFastWithNoLogicalNetlist) {
  TempDir dir;
  db::OdbDatabase db;
  LoadDesign(db, dir, kEmptyDef);
  // A routed wire on a net that carries no pin at all.
  odb::dbNet* net = odb::dbNet::create(TopBlock(db), "n_free");
  ASSERT_NE(net, nullptr);
  odb::dbWire* wire = odb::dbWire::create(net);
  ASSERT_NE(wire, nullptr);
  odb::dbWireEncoder encoder;
  encoder.begin(wire);
  encoder.newPath(Layer(db, "metal1"), odb::dbWireType::ROUTED);
  encoder.addPoint(1000, 1000);
  encoder.addPoint(2000, 1000);
  encoder.end();

  LvsRecord record;
  std::string error;
  EXPECT_FALSE(RunConnectivityLvs(db.RawDatabase(), &record, &error));
  EXPECT_NE(error.find("no logical netlist"), std::string::npos)
      << error;
}

}  // namespace
}  // namespace eos::pv
