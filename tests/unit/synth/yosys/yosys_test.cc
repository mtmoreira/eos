// Unit tests: synth/yosys, the Yosys facade.
//
// The fixture is a small RTL module with combinational and
// sequential logic (a registered adder). It is the demo Verilog
// for this unit: no demo .v file exists in the EOS tree, so the
// fixture carries it. Every expected value is a fixture
// construction fact: the module name, its ports in declaration
// order, and the wire count the front end reports for it. The
// count is 9, not the 6 a source-level reading suggests: the
// front end adds one expression wire for a + b and one
// process-output wire per always block ($0\y[3:0] and
// $0\sum[3:0]), on top of the 5 ports and sum. Wire names are
// deliberately not asserted: the expression wire's name embeds
// the fixture's file path, so it is path-dependent.
//
// Deliberately not tested here: a malformed file. Upstream Yosys
// terminates the process on a syntax error (log_error calls
// _Exit), so a malformed-input test would kill the test runner.
// That limitation is documented in include/eos/synth/yosys.h and
// is S1's problem to resolve before read_verilog parses.
#include "eos/synth/yosys.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "eos/foundation/errors.h"

namespace eos::synth {
namespace {

const char kDemo[] = R"V(module demo (clk, rst, a, b, y);
  input clk, rst;
  input [3:0] a, b;
  output reg [3:0] y;
  reg [3:0] sum;
  always @(posedge clk) begin
    if (rst) sum <= 4'd0;
    else sum <= a + b;
  end
  always @* y = sum;
endmodule
)V";

const char kOther[] = R"V(module other (x, z);
  input x;
  output z;
  assign z = ~x;
endmodule
)V";

// A three-level hierarchy for the Hierarchy walk: top instantiates
// mid and ctrl, mid instantiates adder. Every expected value is a
// fixture construction fact: instance names, module names, and
// ports in declaration order.
const char kHierarchy[] = R"V(module adder (a, b, y);
  input [3:0] a, b;
  output [3:0] y;
  assign y = a + b;
endmodule
module ctrl (sel, op);
  input sel;
  output op;
  assign op = sel;
endmodule
module mid (a, b, y);
  input [3:0] a, b;
  output [3:0] y;
  adder u_add (.a(a), .b(b), .y(y));
endmodule
module top (a, b, sel, y);
  input [3:0] a, b;
  input sel;
  output [3:0] y;
  wire [3:0] m;
  wire op;
  mid u_mid (.a(a), .b(b), .y(m));
  ctrl u_ctrl (.sel(sel), .op(op));
  assign y = op ? m : a;
endmodule
)V";

class TempDir {
 public:
  TempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("eos_yosys_test_" + std::to_string(counter_++))) {
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
  std::filesystem::path path_;
  static inline int counter_ = 0;
};

const YosysModuleSummary* FindModule(const YosysDesignSummary& summary,
                                     const std::string& name) {
  for (const auto& module : summary.modules) {
    if (module.name == name) return &module;
  }
  return nullptr;
}

TEST(YosysFacade, VersionIsThePinnedRelease) {
  const std::string version = YosysEngine::Version();
  EXPECT_NE(version.find("0.70"), std::string::npos);
  EXPECT_NE(version.find("28ba3cb92"), std::string::npos);
}

TEST(YosysFacade, ParseVerilogFindsTheDemoModule) {
  TempDir dir;
  const std::string demo = dir.Write("demo.v", kDemo);
  YosysEngine engine;
  const auto summary = engine.ParseVerilog(demo);
  ASSERT_TRUE(summary.ok()) << summary.status().message();
  ASSERT_EQ(summary.value().modules.size(), 1u);
  const YosysModuleSummary& module = summary.value().modules[0];
  EXPECT_EQ(module.name, "demo");
  EXPECT_EQ(module.ports,
            (std::vector<std::string>{"clk", "rst", "a", "b", "y"}));
  EXPECT_EQ(module.wire_count, 9);
}

TEST(YosysFacade, MissingFileFailsWithIoError) {
  TempDir dir;
  YosysEngine engine;
  const auto summary = engine.ParseVerilog(dir.Path("absent.v"));
  ASSERT_FALSE(summary.ok());
  EXPECT_EQ(summary.status().code(), foundation::ErrorCode::kIoError);
}

TEST(YosysFacade, SecondLiveEngineFailsWithInvalidState) {
  TempDir dir;
  const std::string demo = dir.Write("demo.v", kDemo);
  YosysEngine first;
  YosysEngine second;
  const auto summary = second.ParseVerilog(demo);
  ASSERT_FALSE(summary.ok());
  EXPECT_EQ(summary.status().code(), foundation::ErrorCode::kInvalidState);
}

TEST(YosysFacade, ParseTwiceIsFresh) {
  TempDir dir;
  const std::string demo = dir.Write("demo.v", kDemo);
  const std::string other = dir.Write("other.v", kOther);
  YosysEngine engine;
  const auto first = engine.ParseVerilog(demo);
  ASSERT_TRUE(first.ok());
  ASSERT_NE(FindModule(first.value(), "demo"), nullptr);
  const auto second = engine.ParseVerilog(other);
  ASSERT_TRUE(second.ok()) << second.status().message();
  EXPECT_EQ(second.value().modules.size(), 1u);
  EXPECT_NE(FindModule(second.value(), "other"), nullptr);
  EXPECT_EQ(FindModule(second.value(), "demo"), nullptr);
}

// The S1 elaboration fixture: the same registered adder as the
// parse fixture. After hierarchy and process lowering it is three
// cells (an adder, a flip-flop and the reset mux), and its logic
// depth is 2 (adder, then mux, then the register, which breaks
// the chain). The wire count is 8 at the pinned Yosys 0.70: its
// proc_prune pass drops wires the older 0.33 frontend kept, so
// the count is a pin fact, not a version-independent promise.
TEST(YosysFacade, ElaborateDemoReportsUnoptimizedStats) {
  TempDir dir;
  const std::string demo = dir.Write("demo.v", kDemo);
  YosysEngine engine;
  ASSERT_TRUE(engine.ParseVerilog(demo).ok());
  const auto stats = engine.Elaborate("demo");
  ASSERT_TRUE(stats.ok()) << stats.status().message();
  EXPECT_EQ(stats.value().module, "demo");
  EXPECT_EQ(stats.value().cell_count, 3);
  EXPECT_EQ(stats.value().wire_count, 8);
  EXPECT_EQ(stats.value().logic_depth, 2);
  ASSERT_EQ(stats.value().cells_by_type.size(), 3u);
  EXPECT_EQ(stats.value().cells_by_type[0].type, "$add");
  EXPECT_EQ(stats.value().cells_by_type[0].count, 1);
  EXPECT_EQ(stats.value().cells_by_type[1].type, "$dff");
  EXPECT_EQ(stats.value().cells_by_type[1].count, 1);
  EXPECT_EQ(stats.value().cells_by_type[2].type, "$mux");
  EXPECT_EQ(stats.value().cells_by_type[2].count, 1);
  // S2's sequential guard reads this count: the adder's one
  // flip-flop is the only sequential cell of the three.
  EXPECT_EQ(stats.value().sequential_cells, 1);
}

TEST(YosysFacade, ElaborateUnknownTopFailsWithoutAPass) {
  TempDir dir;
  const std::string demo = dir.Write("demo.v", kDemo);
  YosysEngine engine;
  ASSERT_TRUE(engine.ParseVerilog(demo).ok());
  const auto stats = engine.Elaborate("nope");
  ASSERT_FALSE(stats.ok());
  EXPECT_EQ(stats.status().code(), foundation::ErrorCode::kInvalidArgument);
}

TEST(YosysFacade, ElaborateRejectsNonIdentifierTop) {
  TempDir dir;
  const std::string demo = dir.Write("demo.v", kDemo);
  YosysEngine engine;
  ASSERT_TRUE(engine.ParseVerilog(demo).ok());
  // A top name is interpolated into a pass command string, so
  // anything that is not a plain identifier must fail here,
  // before Yosys ever sees it.
  const auto stats = engine.Elaborate("demo; proc");
  ASSERT_FALSE(stats.ok());
  EXPECT_EQ(stats.status().code(), foundation::ErrorCode::kInvalidArgument);
}

TEST(YosysFacade, StatsMatchesElaborateWithoutRunningPasses) {
  TempDir dir;
  const std::string demo = dir.Write("demo.v", kDemo);
  YosysEngine engine;
  ASSERT_TRUE(engine.ParseVerilog(demo).ok());
  const auto elaborated = engine.Elaborate("demo");
  ASSERT_TRUE(elaborated.ok());
  const auto stats = engine.Stats("demo");
  ASSERT_TRUE(stats.ok()) << stats.status().message();
  EXPECT_EQ(stats.value().cell_count, elaborated.value().cell_count);
  EXPECT_EQ(stats.value().wire_count, elaborated.value().wire_count);
  EXPECT_EQ(stats.value().logic_depth, elaborated.value().logic_depth);
  const auto missing = engine.Stats("nope");
  ASSERT_FALSE(missing.ok());
  EXPECT_EQ(missing.status().code(), foundation::ErrorCode::kInvalidArgument);
}

// The BLIF bridge: after elaboration and aigmap, the
// sequential demo writes as BLIF with its flip-flop as a
// .latch line (the form ABC reads), not as a vector always
// block (the form ABC's Verilog reader rejects).
TEST(YosysFacade, AigMapAndWriteBlifRoundTrip) {
  TempDir dir;
  const std::string demo = dir.Write("demo.v", kDemo);
  YosysEngine engine;
  ASSERT_TRUE(engine.ParseVerilog(demo).ok());
  ASSERT_TRUE(engine.Elaborate("demo").ok());
  ASSERT_TRUE(engine.AigMap().ok());
  const std::string out = dir.Path("demo.blif");
  ASSERT_TRUE(engine.WriteBlif(out, "demo").ok());
  std::ifstream in(out);
  ASSERT_TRUE(in.good());
  std::stringstream contents;
  contents << in.rdbuf();
  EXPECT_NE(contents.str().find(".model demo"), std::string::npos);
  EXPECT_NE(contents.str().find(".latch"), std::string::npos);
  const auto unwritable = engine.WriteBlif(dir.Path("no_dir/out.blif"), "demo");
  ASSERT_FALSE(unwritable.ok());
  EXPECT_EQ(unwritable.code(), foundation::ErrorCode::kIoError);
  const auto bad_top = engine.WriteBlif(dir.Path("bad.blif"), "demo; proc");
  ASSERT_FALSE(bad_top.ok());
  EXPECT_EQ(bad_top.code(), foundation::ErrorCode::kInvalidArgument);
}

TEST(YosysFacade, AigMapAndWriteVerilogRoundTrip) {
  TempDir dir;
  const std::string demo = dir.Write("demo.v", kDemo);
  YosysEngine engine;
  ASSERT_TRUE(engine.ParseVerilog(demo).ok());
  ASSERT_TRUE(engine.Elaborate("demo").ok());
  ASSERT_TRUE(engine.AigMap().ok());
  const std::string out = dir.Path("demo_aig.v");
  ASSERT_TRUE(engine.WriteVerilog(out).ok());
  std::ifstream in(out);
  ASSERT_TRUE(in.good());
  std::stringstream contents;
  contents << in.rdbuf();
  EXPECT_NE(contents.str().find("module demo"), std::string::npos);
  EXPECT_FALSE(contents.str().empty());
  const auto unwritable = engine.WriteVerilog(dir.Path("no_dir/out.v"));
  ASSERT_FALSE(unwritable.ok());
  EXPECT_EQ(unwritable.code(), foundation::ErrorCode::kIoError);
}

// The S4 file-list fixtures: a leaf module, and a top module
// that includes a header for one wire declaration and selects
// its shape with a -D define. The header lives in a
// subdirectory, so only the -I directory makes the include
// resolve; the top file's own directory holds no defs.vh.
const char kFilelistSub[] = R"V(module sub(a, b, y);
  input a, b;
  output y;
  assign y = a & b;
endmodule
)V";

const char kFilelistTop[] = R"V(module filetop(a, b, c, d, y);
  input a, b, c, d;
  output y;
`include "defs.vh"
  sub u_sub(.a(a), .b(b), .y(s));
`ifdef WIDE
  wire t;
  assign t = s & c;
  assign y = t & d;
`else
  assign y = s;
`endif
endmodule
)V";

const char kFilelistDefs[] = "wire s;\n";

TEST(YosysFacade, ParseVerilogFilesAppliesIncludesAndDefines) {
  TempDir dir;
  std::filesystem::create_directories(dir.Path("inc"));
  const std::string sub = dir.Write("sub.v", kFilelistSub);
  const std::string top = dir.Write("filetop.v", kFilelistTop);
  const std::string inc = dir.Write("inc/defs.vh", kFilelistDefs);
  (void)inc;
  YosysEngine engine;
  const auto summary = engine.ParseVerilogFiles(
      {sub, top}, {dir.Path("inc")}, {"WIDE"});
  ASSERT_TRUE(summary.ok()) << summary.status().message();
  EXPECT_NE(FindModule(summary.value(), "sub"), nullptr);
  EXPECT_NE(FindModule(summary.value(), "filetop"), nullptr);
  // The WIDE branch is the two-AND chain on top of the sub
  // instance: with the define, elaboration sees the sub cell
  // plus two AND cells; without it, only the sub cell.
  const auto wide = engine.Elaborate("filetop");
  ASSERT_TRUE(wide.ok()) << wide.status().message();
  EXPECT_EQ(wide.value().cell_count, 3);
}

TEST(YosysFacade, ParseVerilogFilesWithoutDefineTakesOtherBranch) {
  TempDir dir;
  std::filesystem::create_directories(dir.Path("inc"));
  const std::string sub = dir.Write("sub.v", kFilelistSub);
  const std::string top = dir.Write("filetop.v", kFilelistTop);
  dir.Write("inc/defs.vh", kFilelistDefs);
  YosysEngine engine;
  ASSERT_TRUE(engine.ParseVerilogFiles({sub, top}, {dir.Path("inc")}, {})
                  .ok());
  const auto stats = engine.Elaborate("filetop");
  ASSERT_TRUE(stats.ok()) << stats.status().message();
  EXPECT_EQ(stats.value().cell_count, 1);
}

TEST(YosysFacade, ParseVerilogFilesRejectsBadArguments) {
  TempDir dir;
  const std::string sub = dir.Write("sub.v", kFilelistSub);
  YosysEngine engine;
  const auto empty = engine.ParseVerilogFiles({}, {}, {});
  ASSERT_FALSE(empty.ok());
  EXPECT_EQ(empty.status().code(), foundation::ErrorCode::kInvalidArgument);
  const auto missing = engine.ParseVerilogFiles(
      {sub, dir.Path("no_such.v")}, {}, {});
  ASSERT_FALSE(missing.ok());
  EXPECT_EQ(missing.status().code(), foundation::ErrorCode::kIoError);
  EXPECT_NE(missing.status().message().find("no_such.v"),
            std::string::npos);
  const auto empty_dir =
      engine.ParseVerilogFiles({sub}, {""}, {});
  ASSERT_FALSE(empty_dir.ok());
  EXPECT_EQ(empty_dir.status().code(), foundation::ErrorCode::kInvalidArgument);
  const auto bad_define =
      engine.ParseVerilogFiles({sub}, {}, {"1BAD"});
  ASSERT_FALSE(bad_define.ok());
  EXPECT_EQ(bad_define.status().code(), foundation::ErrorCode::kInvalidArgument);
  const auto spaced_define =
      engine.ParseVerilogFiles({sub}, {}, {"WIDE; proc"});
  ASSERT_FALSE(spaced_define.ok());
  EXPECT_EQ(spaced_define.status().code(), foundation::ErrorCode::kInvalidArgument);
  // A failed multi-file parse leaves no stale design behind:
  // the next single-file parse still works.
  ASSERT_TRUE(engine.ParseVerilog(sub).ok());
  EXPECT_NE(FindModule(engine.ParseVerilog(sub).value(), "sub"),
            nullptr);
}

TEST(YosysFacade, HierarchyFlatReportsRootOnly) {
  TempDir dir;
  YosysEngine engine;
  ASSERT_TRUE(engine.ParseVerilog(dir.Write("demo.v", kDemo)).ok());
  ASSERT_TRUE(engine.Elaborate("demo").ok());
  const auto hierarchy = engine.Hierarchy("demo");
  ASSERT_TRUE(hierarchy.ok()) << hierarchy.status().message();
  EXPECT_EQ(hierarchy.value().path, "demo");
  EXPECT_EQ(hierarchy.value().module, "demo");
  EXPECT_TRUE(hierarchy.value().children.empty());
}

TEST(YosysFacade, HierarchyMultiModuleReportsNestedTree) {
  TempDir dir;
  YosysEngine engine;
  ASSERT_TRUE(engine.ParseVerilog(dir.Write("top.v", kHierarchy)).ok());
  ASSERT_TRUE(engine.Elaborate("top").ok());
  const auto hierarchy = engine.Hierarchy("top");
  ASSERT_TRUE(hierarchy.ok()) << hierarchy.status().message();
  const YosysEngine::HierarchyNode& root = hierarchy.value();
  EXPECT_EQ(root.path, "top");
  EXPECT_EQ(root.module, "top");
  ASSERT_EQ(root.children.size(), 2u);
  // Children are sorted by path, so ctrl precedes mid.
  const YosysEngine::HierarchyNode& ctrl = root.children[0];
  EXPECT_EQ(ctrl.path, "top/u_ctrl");
  EXPECT_EQ(ctrl.module, "ctrl");
  EXPECT_EQ(ctrl.ports, std::vector<std::string>({"sel", "op"}));
  EXPECT_TRUE(ctrl.children.empty());
  const YosysEngine::HierarchyNode& mid = root.children[1];
  EXPECT_EQ(mid.path, "top/u_mid");
  EXPECT_EQ(mid.module, "mid");
  EXPECT_EQ(mid.ports, std::vector<std::string>({"a", "b", "y"}));
  ASSERT_EQ(mid.children.size(), 1u);
  EXPECT_EQ(mid.children[0].path, "top/u_mid/u_add");
  EXPECT_EQ(mid.children[0].module, "adder");
  EXPECT_EQ(mid.children[0].ports, std::vector<std::string>({"a", "b", "y"}));
  EXPECT_TRUE(mid.children[0].children.empty());
}

TEST(YosysFacade, HierarchyUnknownTopFails) {
  TempDir dir;
  YosysEngine engine;
  ASSERT_TRUE(engine.ParseVerilog(dir.Write("top.v", kHierarchy)).ok());
  ASSERT_TRUE(engine.Elaborate("top").ok());
  const auto hierarchy = engine.Hierarchy("nope");
  ASSERT_FALSE(hierarchy.ok());
  EXPECT_EQ(hierarchy.status().code(), foundation::ErrorCode::kInvalidArgument);
}

TEST(YosysFacade, HierarchyRejectsNonIdentifierTop) {
  TempDir dir;
  YosysEngine engine;
  ASSERT_TRUE(engine.ParseVerilog(dir.Write("top.v", kHierarchy)).ok());
  const auto hierarchy = engine.Hierarchy("top; proc");
  ASSERT_FALSE(hierarchy.ok());
  EXPECT_EQ(hierarchy.status().code(), foundation::ErrorCode::kInvalidArgument);
}

TEST(YosysFacade, HierarchyWithoutOwnershipFails) {
  TempDir dir;
  YosysEngine first;
  ASSERT_TRUE(first.ParseVerilog(dir.Write("top.v", kHierarchy)).ok());
  YosysEngine second;
  const auto hierarchy = second.Hierarchy("top");
  ASSERT_FALSE(hierarchy.ok());
  EXPECT_EQ(hierarchy.status().code(), foundation::ErrorCode::kInvalidState);
}

}  // namespace
}  // namespace eos::synth
