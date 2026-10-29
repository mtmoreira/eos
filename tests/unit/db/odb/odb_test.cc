// Unit tests: db/odb (the EOS facade over the imported OpenDB).
//
// The fixtures are small synthetic LEF and DEF texts, embedded here
// and written to a temporary directory per test: one site, one
// routing layer, one two-pin macro in LEF; two placed instances of
// it, two top-level pins and three nets in DEF. They are
// licence-clean by construction.
#include "eos/db/odb.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
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

TEST(Odb, NetNamesEnumeratesEveryNet) {
  TempDir dir;
  OdbDatabase db;
  EXPECT_TRUE(db.NetNames().empty());
  LoadFixture(db, dir);
  const std::vector<std::string> names = db.NetNames();
  ASSERT_EQ(names.size(), db.NetCount());
  EXPECT_EQ(names.size(), 3u);
  for (const std::string& name : names) {
    EXPECT_TRUE(db.HasNet(name)) << name;
  }
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

TEST(Odb, WriteDbRoundTripReReadsWithIdenticalCounts) {
  TempDir dir;
  OdbDatabase db;
  LoadFixture(db, dir);
  const std::string out = dir.Path("roundtrip.odb");
  ASSERT_TRUE(db.WriteDb(out).ok());

  OdbDatabase reread;
  ASSERT_TRUE(reread.ReadDb(out).ok());
  EXPECT_EQ(reread.TopBlockName(), "top");
  EXPECT_EQ(reread.InstanceCount(), 2u);
  EXPECT_EQ(reread.NetCount(), 3u);
  EXPECT_EQ(reread.MasterCount(), 1u);
}

TEST(Odb, ReadDbIntoNonEmptyDatabaseIsInvalidState) {
  TempDir dir;
  OdbDatabase db;
  LoadFixture(db, dir);
  const std::string out = dir.Path("roundtrip.odb");
  ASSERT_TRUE(db.WriteDb(out).ok());
  const Status status = db.ReadDb(out);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidState);
}

TEST(Odb, WriteVerilogEmitsTheNetlist) {
  TempDir dir;
  OdbDatabase db;
  LoadFixture(db, dir);
  const std::string out = dir.Path("top.v");
  ASSERT_TRUE(db.WriteVerilog(out).ok());
  std::ifstream in(out);
  std::stringstream text;
  text << in.rdbuf();
  const std::string verilog = text.str();
  EXPECT_NE(verilog.find("module top (in1, out1);"), std::string::npos);
  EXPECT_NE(verilog.find("input in1;"), std::string::npos);
  EXPECT_NE(verilog.find("output out1;"), std::string::npos);
  EXPECT_NE(verilog.find("wire n_mid;"), std::string::npos);
  EXPECT_NE(verilog.find("BUFX2 u1 (.A(n_in), .Y(n_mid));"),
            std::string::npos);
  EXPECT_NE(verilog.find("BUFX2 u2 (.A(n_mid), .Y(n_out));"),
            std::string::npos);
  EXPECT_NE(verilog.find("endmodule"), std::string::npos);
}

TEST(Odb, WriteVerilogNormalizesDefEscapedNames) {
  // A DEF net name with backslash-escaped bus brackets is stored
  // with its escapes; the Verilog writer must emit the name itself,
  // not the stored escape sequence, or SPEF and SDC references to
  // the net will not match it. An internal escaped net is written
  // as one escaped identifier; a port bit whose net carries its
  // name becomes a real bus port (see the bus test below).
  const char kEscapedDef[] = R"DEF(VERSION 5.8 ;
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
- bus\[0\] + NET bus\[0\] + DIRECTION OUTPUT + USE SIGNAL + PLACED ( 10000 5000 ) N ;
END PINS
NETS 3 ;
- n_in ( PIN in1 ) ( u1 A ) ;
- n\[0\] ( u1 Y ) ( u2 A ) ;
- bus\[0\] ( u2 Y ) ( PIN bus\[0\] ) ;
END NETS
END DESIGN
)DEF";
  TempDir dir;
  OdbDatabase db;
  ASSERT_TRUE(db.ReadLef(dir.Write("cells.lef", kLef)).ok());
  ASSERT_TRUE(db.ReadDef(dir.Write("top.def", kEscapedDef)).ok());
  const std::string out = dir.Path("top.v");
  ASSERT_TRUE(db.WriteVerilog(out).ok());
  std::ifstream in(out);
  std::stringstream text;
  text << in.rdbuf();
  const std::string verilog = text.str();
  EXPECT_EQ(verilog.find("bus\\[0\\]"), std::string::npos) << verilog;
  EXPECT_EQ(verilog.find("n\\[0\\]"), std::string::npos) << verilog;
  EXPECT_NE(verilog.find("wire \\n[0] ;"), std::string::npos) << verilog;
  EXPECT_NE(verilog.find(".Y(\\n[0] )"), std::string::npos) << verilog;
  EXPECT_NE(verilog.find("output [0:0] bus;"), std::string::npos)
      << verilog;
  EXPECT_NE(verilog.find(".Y(bus[0])"), std::string::npos) << verilog;
}

TEST(Odb, WriteVerilogGroupsBusBitPortsIntoBuses) {
  // Ports named base[N] (the plain DEF bus form) whose nets carry
  // the same names are one bus: one header entry, one ranged
  // declaration, bit-select connections. Parasitics formats
  // resolve port pins through the bus object.
  const char kBusDef[] = R"DEF(VERSION 5.8 ;
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
- data[0] + NET data[0] + DIRECTION OUTPUT + USE SIGNAL + PLACED ( 10000 5000 ) N ;
- data[1] + NET data[1] + DIRECTION OUTPUT + USE SIGNAL + PLACED ( 10000 6000 ) N ;
END PINS
NETS 3 ;
- n_in ( u1 A ) ;
- data[0] ( u1 Y ) ( PIN data[0] ) ;
- data[1] ( u2 Y ) ( PIN data[1] ) ;
END NETS
END DESIGN
)DEF";
  TempDir dir;
  OdbDatabase db;
  ASSERT_TRUE(db.ReadLef(dir.Write("cells.lef", kLef)).ok());
  ASSERT_TRUE(db.ReadDef(dir.Write("top.def", kBusDef)).ok());
  const std::string out = dir.Path("top.v");
  ASSERT_TRUE(db.WriteVerilog(out).ok());
  std::ifstream in(out);
  std::stringstream text;
  text << in.rdbuf();
  const std::string verilog = text.str();
  EXPECT_NE(verilog.find("module top (data);"), std::string::npos)
      << verilog;
  EXPECT_NE(verilog.find("output [1:0] data;"), std::string::npos)
      << verilog;
  EXPECT_NE(verilog.find(".Y(data[0])"), std::string::npos) << verilog;
  EXPECT_NE(verilog.find(".Y(data[1])"), std::string::npos) << verilog;
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

TEST(Odb, EcoQueriesAndMutations) {
  // The Phase 2 unit 2.4 facade surface the ECO adapters use:
  // master and instance queries, a master swap, instance and net
  // creation, and pin connect and disconnect.
  TempDir dir;
  OdbDatabase db;
  LoadFixture(db, dir);
  EXPECT_EQ(db.MasterNames(), std::vector<std::string>{"BUFX2"});
  const auto pins = db.MasterPins("BUFX2");
  ASSERT_EQ(pins.size(), 2u);
  EXPECT_EQ(pins[0].name, "A");
  EXPECT_TRUE(pins[0].is_input);
  EXPECT_EQ(pins[1].name, "Y");
  EXPECT_TRUE(pins[1].is_output);
  OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance("u1", &info));
  EXPECT_EQ(info.master, "BUFX2");
  EXPECT_EQ(info.x, 1000);
  EXPECT_EQ(info.y, 1000);
  EXPECT_TRUE(db.HasNet("n_mid"));
  const auto connections = db.NetConnections("n_mid");
  ASSERT_EQ(connections.size(), 2u);
  EXPECT_EQ(connections[0].instance, "u1");
  EXPECT_TRUE(connections[0].is_driver);
  EXPECT_EQ(connections[1].instance, "u2");
  EXPECT_FALSE(connections[1].is_driver);

  ASSERT_TRUE(db.CreateNet("n_new").ok());
  EXPECT_EQ(db.CreateNet("n_new").code(), ErrorCode::kInvalidArgument);
  ASSERT_TRUE(db.CreateInstance("u3", "BUFX2", 3000, 1000).ok());
  EXPECT_EQ(db.CreateInstance("u3", "BUFX2", 0, 0).code(),
            ErrorCode::kInvalidArgument);
  EXPECT_EQ(db.CreateInstance("u4", "NOPE", 0, 0).code(),
            ErrorCode::kNotFound);
  ASSERT_TRUE(db.ConnectInstancePin("u3", "A", "n_mid").ok());
  EXPECT_EQ(db.ConnectInstancePin("u3", "A", "n_new").code(),
            ErrorCode::kInvalidState);
  ASSERT_TRUE(db.ConnectInstancePin("u3", "Y", "n_new").ok());
  ASSERT_TRUE(db.DisconnectInstancePin("u3", "A").ok());
  ASSERT_TRUE(db.ConnectInstancePin("u3", "A", "n_new").ok());
  EXPECT_EQ(db.InstanceCount(), 3u);
  EXPECT_EQ(db.NetCount(), 4u);
  // Swap failures: unknown instance and unknown master. Real
  // variant swaps are covered by the timing-eco adapter tests,
  // whose fixture carries several masters.
  EXPECT_EQ(db.SwapInstanceMaster("nope", "BUFX2").code(),
            ErrorCode::kNotFound);
  EXPECT_EQ(db.SwapInstanceMaster("u1", "NOPE").code(),
            ErrorCode::kNotFound);
}

TEST(Odb, ReadDbOrfsV30GcdReportsCountsAndRoundTrips) {
  // The check the ead3236 re-pin exists for (fix track unit REPIN):
  // EOS's facade reads the real ORFS v3.0 sky130hd gcd final
  // database, which the v2.0 pin rejected on its schema version.
  // The evidence file is local-only and never committed, so the
  // test is gated on EOS_ORFS_ODB_PATH and skips (rather than
  // failing) wherever the file is absent, including CI. Run with
  // the variable set, the expected counts are the candidate
  // probe's from docs/odb-repin-decision.md Section 2: block gcd,
  // 1046 instances, 394 nets.
  const char* path = std::getenv("EOS_ORFS_ODB_PATH");
  if (path == nullptr || path[0] == '\0') {
    GTEST_SKIP() << "EOS_ORFS_ODB_PATH not set; the ORFS v3.0 "
                    "evidence database is local-only";
  }
  TempDir dir;
  OdbDatabase db;
  ASSERT_TRUE(db.ReadDb(path).ok());
  EXPECT_EQ(db.TopBlockName(), "gcd");
  EXPECT_EQ(db.InstanceCount(), 1046u);
  EXPECT_EQ(db.NetCount(), 394u);
  const std::string out = dir.Path("roundtrip.odb");
  ASSERT_TRUE(db.WriteDb(out).ok());

  OdbDatabase reread;
  ASSERT_TRUE(reread.ReadDb(out).ok());
  EXPECT_EQ(reread.TopBlockName(), "gcd");
  EXPECT_EQ(reread.InstanceCount(), 1046u);
  EXPECT_EQ(reread.NetCount(), 394u);
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
