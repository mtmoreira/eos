// Unit tests: db/odb (the EOS facade over the imported OpenDB).
//
// The fixtures are small synthetic LEF and DEF texts, embedded here
// and written to a temporary directory per test: one site, one
// routing layer, one two-pin macro in LEF; two placed instances of
// it, two top-level pins and three nets in DEF. They are
// licence-clean by construction.
#include "eos/db/odb.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace eos::db {
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
- in1 + NET n_in + DIRECTION INPUT + USE SIGNAL + PLACED ( 0 5000 ) N ;
- out1 + NET n_out + DIRECTION OUTPUT + USE SIGNAL + PLACED ( 10000 5000 ) N ;
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
              ("eos_odb_test_" +
               std::to_string(
                   std::filesystem::temp_directory_path().string().size()) +
               "_" + std::to_string(counter_++))) {
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

// Reads the embedded LEF and DEF into db; returns the temp dir so
// the caller keeps the files alive for the test's duration.
void LoadFixture(OdbDatabase& db, const TempDir& dir) {
  ASSERT_TRUE(db.ReadLef(dir.Write("cells.lef", kLef)).ok());
  ASSERT_TRUE(db.ReadDef(dir.Write("top.def", kDef)).ok());
}

TEST(Odb, EmptyDatabaseHasNoTopBlock) {
  OdbDatabase db;
  EXPECT_FALSE(db.has_top_block());
  EXPECT_EQ(db.TopBlockName(), "");
  EXPECT_EQ(db.InstanceCount(), 0u);
  EXPECT_EQ(db.NetCount(), 0u);
  EXPECT_EQ(db.MasterCount(), 0u);
}

TEST(Odb, ReadLefAndDefReportTopBlockAndCounts) {
  TempDir dir;
  OdbDatabase db;
  LoadFixture(db, dir);
  EXPECT_TRUE(db.has_top_block());
  EXPECT_EQ(db.TopBlockName(), "top");
  EXPECT_EQ(db.MasterCount(), 1u);
  EXPECT_EQ(db.InstanceCount(), 2u);
  EXPECT_EQ(db.NetCount(), 3u);
}

TEST(Odb, WriteDefRoundTripReReadsWithIdenticalCounts) {
  TempDir dir;
  OdbDatabase db;
  LoadFixture(db, dir);
  const std::string out = dir.Path("roundtrip.def");
  ASSERT_TRUE(db.WriteDef(out).ok());

  OdbDatabase reread;
  ASSERT_TRUE(reread.ReadLef(dir.Path("cells.lef")).ok());
  ASSERT_TRUE(reread.ReadDef(out).ok());
  EXPECT_EQ(reread.TopBlockName(), db.TopBlockName());
  EXPECT_EQ(reread.InstanceCount(), db.InstanceCount());
  EXPECT_EQ(reread.NetCount(), db.NetCount());
}

TEST(Odb, ReadDefBeforeAnyLefIsInvalidState) {
  TempDir dir;
  OdbDatabase db;
  const Status status = db.ReadDef(dir.Write("top.def", kDef));
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

TEST(Odb, MissingLefFileIsIoError) {
  TempDir dir;
  OdbDatabase db;
  const Status status = db.ReadLef(dir.Path("no_such_file.lef"));
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kIoError);
  EXPECT_EQ(db.MasterCount(), 0u);
}

TEST(Odb, MissingDefFileIsIoErrorAfterLef) {
  TempDir dir;
  OdbDatabase db;
  ASSERT_TRUE(db.ReadLef(dir.Write("cells.lef", kLef)).ok());
  const Status status = db.ReadDef(dir.Path("no_such_file.def"));
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kIoError);
  EXPECT_FALSE(db.has_top_block());
}

TEST(Odb, WriteDefBeforeAnyDefIsInvalidState) {
  TempDir dir;
  OdbDatabase db;
  ASSERT_TRUE(db.ReadLef(dir.Write("cells.lef", kLef)).ok());
  const Status status = db.WriteDef(dir.Path("out.def"));
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

}  // namespace
}  // namespace eos::db
