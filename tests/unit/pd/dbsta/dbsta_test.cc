// Unit tests: pd/dbsta (the merged dbNetwork adapter).
//
// The fixtures mirror the floorplan test's synthetic LEF and DEF
// texts (one site, two routing layers, one two-pin macro in LEF;
// two placed instances, two placed top-level pins and three nets
// in DEF). They are licence-clean by construction. The tests pin
// the R1 merge behavior: Pin and Instance ids are injective and
// null-safe (P-27), and setBlock alone binds the database (M-2).
#include "db_sta/dbNetwork.hh"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include "eos/db/odb.h"
#include "odb/db.h"
#include "utl/Logger.h"

namespace sta {
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
              ("eos_dbsta_test_" + std::to_string(counter_++))) {
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

// Loads the fixture and returns the design block. The dbNetwork
// represents Pin handles as odb dbITerm/dbBTerm pointers and
// Instance handles as dbInst pointers, so the tests reinterpret
// the odb objects the same way STA does.
odb::dbBlock* LoadBlock(eos::db::OdbDatabase& db, const TempDir& dir) {
  const std::string lef = dir.Write("test.lef", kLef);
  EXPECT_TRUE(db.ReadLef(lef).ok());
  const std::string def = dir.Write("test.def", kDef);
  EXPECT_TRUE(db.ReadDef(def).ok());
  odb::dbBlock* block = db.RawDatabase()->getChip()->getBlock();
  EXPECT_NE(block, nullptr);
  return block;
}

TEST(Dbsta, PinIdsAreDistinctAndNullSafe) {
  TempDir dir;
  eos::db::OdbDatabase db;
  odb::dbBlock* block = LoadBlock(db, dir);
  ASSERT_NE(block, nullptr);
  utl::Logger logger;
  dbNetwork network;
  network.init(db.RawDatabase(), &logger);
  network.setBlock(block);

  std::vector<Pin*> pins;
  std::vector<odb::dbITerm*> iterms;
  for (odb::dbInst* inst : block->getInsts()) {
    for (odb::dbITerm* iterm : inst->getITerms()) {
      iterms.push_back(iterm);
      pins.push_back(reinterpret_cast<Pin*>(iterm));
    }
  }
  std::vector<odb::dbBTerm*> bterms;
  for (odb::dbBTerm* bterm : block->getBTerms()) {
    bterms.push_back(bterm);
    pins.push_back(reinterpret_cast<Pin*>(bterm));
  }
  ASSERT_EQ(iterms.size(), 4u);
  ASSERT_EQ(bterms.size(), 2u);

  // Injective: the LoadPinIndexMap collapse (P-27) is exactly
  // distinct pins sharing one id, so distinctness is the property.
  std::set<ObjectId> ids;
  for (Pin* pin : pins) {
    const ObjectId id = network.id(pin);
    EXPECT_NE(id, 0u);
    ids.insert(id);
  }
  EXPECT_EQ(ids.size(), pins.size());
  // Scheme check: iterm ids are raw odb ids, bterm ids carry the
  // high bit so the two spaces cannot collide.
  for (odb::dbITerm* iterm : iterms) {
    const ObjectId id =
        network.id(reinterpret_cast<Pin*>(iterm));
    EXPECT_EQ(id, iterm->getId());
    EXPECT_EQ(id & 0x80000000U, 0u);
  }
  for (odb::dbBTerm* bterm : bterms) {
    const ObjectId id =
        network.id(reinterpret_cast<Pin*>(bterm));
    EXPECT_EQ(id, bterm->getId() | 0x80000000U);
  }
  EXPECT_EQ(network.id(static_cast<Pin*>(nullptr)), 0u);
}

TEST(Dbsta, InstanceIdsUseSentinelForTop) {
  TempDir dir;
  eos::db::OdbDatabase db;
  odb::dbBlock* block = LoadBlock(db, dir);
  ASSERT_NE(block, nullptr);
  utl::Logger logger;
  dbNetwork network;
  network.init(db.RawDatabase(), &logger);
  network.setBlock(block);

  EXPECT_EQ(network.id(network.topInstance()), 0xFFFFFFFFU);
  odb::dbInst* inst = nullptr;
  for (odb::dbInst* candidate : block->getInsts()) {
    inst = candidate;
    break;
  }
  ASSERT_NE(inst, nullptr);
  EXPECT_EQ(network.id(reinterpret_cast<Instance*>(inst)),
            inst->getId());
  EXPECT_EQ(network.id(static_cast<Instance*>(nullptr)), 0u);
}

TEST(Dbsta, NetIdsAreDistinct) {
  TempDir dir;
  eos::db::OdbDatabase db;
  odb::dbBlock* block = LoadBlock(db, dir);
  ASSERT_NE(block, nullptr);
  utl::Logger logger;
  dbNetwork network;
  network.init(db.RawDatabase(), &logger);
  network.setBlock(block);

  ASSERT_EQ(block->getNets().size(), 3u);
  std::set<ObjectId> ids;
  for (odb::dbNet* net : block->getNets()) {
    const ObjectId id =
        network.id(reinterpret_cast<Net*>(net));
    EXPECT_EQ(id, net->getId());
    ids.insert(id);
  }
  EXPECT_EQ(ids.size(), 3u);
}

TEST(Dbsta, SetBlockAloneBindsDatabase) {
  TempDir dir;
  eos::db::OdbDatabase db;
  odb::dbBlock* block = LoadBlock(db, dir);
  ASSERT_NE(block, nullptr);
  // No init call: the scratch-sta path binds by setBlock alone.
  dbNetwork network;
  network.setBlock(block);
  // dbuToMeters reads db_->getTech(); a null database would crash.
  EXPECT_GT(network.dbuToMeters(1000), 0.0);
}

}  // namespace
}  // namespace sta
