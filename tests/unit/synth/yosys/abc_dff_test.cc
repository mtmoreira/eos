// Unit tests: synth/yosys, the `abc -dff` sequential path.
//
// The facade drives ABC in process through the upstream `abc
// -dff` pass (YosysEngine::AbcDff): the pass extracts the
// design's flip-flops, runs ABC on the combinational remainder,
// and re-attaches clock and reset, so the optimized design
// stays sequential. The experiment that settled the integration
// (route (i), in-process ABC) is the shape these tests lock in:
// the counter and hier_top fixtures prove the optimization
// happens and the flops survive, the frame test proves the
// pass coexists with AbcEngine's frame ownership, and the
// failure tests prove the fail-fast rules (unknown script,
// multi-clock, multi-reset-polarity, no flop to extract).
//
// One input-form fact the tests rely on: the pass only handles
// fine flop cells ($_DFF_*), so AigMap (techmap, which converts
// $adff to $_DFF_PN0_) must run before the pass, exactly as the
// synth_generic handler does. Calling the pass on the
// elaborated design directly extracts nothing.

#include "eos/synth/abc.h"
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

// One 4-bit $adff counter: positive-edge clock, active-low async
// reset, enable. After elaborate this is a single $adff cell; after
// AigMap it is four $_DFF_PN0_ cells.
const char kCounter[] = R"V(module cnt (clk, rst_n, en, q);
  input clk, rst_n, en;
  output reg [3:0] q;
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) q <= 4'd0;
    else if (en) q <= q + 4'd1;
  end
endmodule
)V";

// Two 4-bit registers on different clocks: two clock domains in
// one module, which AbcDff must refuse.
const char kTwoClock[] = R"V(module twoclk (clk_a, clk_b, rst_n, d, qa, qb);
  input clk_a, clk_b, rst_n;
  input [3:0] d;
  output reg [3:0] qa, qb;
  always @(posedge clk_a or negedge rst_n) begin
    if (!rst_n) qa <= 4'd0;
    else qa <= d;
  end
  always @(posedge clk_b or negedge rst_n) begin
    if (!rst_n) qb <= 4'd0;
    else qb <= d;
  end
endmodule
)V";

// Two 4-bit registers on one clock with opposite reset
// polarities: two reset polarities in one clock domain, which
// AbcDff must refuse.
const char kTwoReset[] = R"V(module tworeset (clk, rst_n, rst, d, qa, qb);
  input clk, rst_n, rst;
  input [3:0] d;
  output reg [3:0] qa, qb;
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) qa <= 4'd0;
    else qa <= d;
  end
  always @(posedge clk or posedge rst) begin
    if (rst) qb <= 4'd0;
    else qb <= d;
  end
endmodule
)V";

// Combinational only: no flop for the pass to extract.
const char kCombinational[] = R"V(module comb (a, b, c, y);
  input a, b, c;
  output y;
  assign y = (a & b) | c;
endmodule
)V";

// Tiny MAPPED gate netlist for the AbcEngine frame check.
// MapForDelay reads with "read -m", which refuses assign-form
// netlists, so the fixture instantiates genlib cells directly.
const char kTiny[] = R"V(module tiny (a, b, y);
  input a, b;
  output y;
  wire n1, n2;
  and2 u1 (.a(a), .b(b), .O(n1));
  inv u2 (.a(a), .O(n2));
  or2 u3 (.a(n1), .b(n2), .O(y));
endmodule
)V";

// Minimal genlib for the frame check: four gates, unit delay.
const char kTinyGenlib[] =
    "GATE and2 1 O=a*b; PIN * NONINV 1 999 1 0 1 0\n"
    "GATE or2 1 O=a+b; PIN * NONINV 1 999 1 0 1 0\n"
    "GATE inv 1 O=!a; PIN * INV 1 999 1 0 1 0\n"
    "GATE buf 1 O=a; PIN * NONINV 1 999 1 0 1 0\n";

class TempDir {
 public:
  TempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("eos_abc_dff_test_" + std::to_string(counter_++))) {
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

std::string ReadFile(const std::string& path) {
  std::ifstream in(path);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Repo root, derived from this file's own path, so the test finds
// demo/hier no matter which directory it runs from.
std::filesystem::path RepoRoot() {
  std::filesystem::path dir =
      std::filesystem::absolute(__FILE__).parent_path();
  // tests/unit/synth/yosys -> repo root is four levels up.
  for (int i = 0; i < 4; ++i) dir = dir.parent_path();
  return dir;
}

TEST(AbcDff, CounterKeepsClockAndReset) {
  TempDir dir;
  const std::string src = dir.Write("cnt.v", kCounter);
  const std::string out = dir.Path("cnt_generic.v");
  std::string verilog;

  {
    YosysEngine yosys;
    ASSERT_TRUE(yosys.ParseVerilog(src).ok());
    auto elab = yosys.Elaborate("cnt");
    ASSERT_TRUE(elab.ok());
    EXPECT_EQ(elab.value().sequential_cells, 1);

    ASSERT_TRUE(yosys.AigMap().ok());
    auto mapped = yosys.Stats("cnt");
    ASSERT_TRUE(mapped.ok());
    EXPECT_EQ(mapped.value().sequential_cells, 4);
    const int cells_before = mapped.value().cell_count;

    const Status dff = yosys.AbcDff("default");
    ASSERT_TRUE(dff.ok()) << dff.message();

    auto after = yosys.Stats("cnt");
    ASSERT_TRUE(after.ok());
    EXPECT_EQ(after.value().sequential_cells, 4);
    // ABC really ran: the incrementer logic shrinks from the
    // unoptimized AIG to a handful of cells, flops untouched.
    EXPECT_LT(after.value().cell_count, cells_before);

    ASSERT_TRUE(yosys.WriteVerilog(out).ok());
    verilog = ReadFile(out);
    EXPECT_NE(verilog.find("rst_n"), std::string::npos);
    EXPECT_NE(verilog.find("clk"), std::string::npos);
    EXPECT_NE(verilog.find("negedge rst_n"), std::string::npos);
  }  // The first engine must be destroyed before a second is built:
     // the Yosys session is process-global and single-owner.

  // The returned netlist re-elaborates with its flops intact.
  YosysEngine yosys2;
  ASSERT_TRUE(yosys2.ParseVerilog(out).ok());
  auto elab2 = yosys2.Elaborate("cnt");
  ASSERT_TRUE(elab2.ok());
  EXPECT_EQ(elab2.value().sequential_cells, 4);
}

TEST(AbcDff, ScriptsPreserveSequential) {
  // Every EOS script name runs the pass and keeps the flops:
  // balance and rewrite run their one step after strash, none
  // runs strash only. Each script gets a fresh design, because
  // the pass mutates the live design.
  for (const char* script : {"balance", "rewrite", "none"}) {
    TempDir dir;
    const std::string src = dir.Write("cnt.v", kCounter);
    YosysEngine yosys;
    ASSERT_TRUE(yosys.ParseVerilog(src).ok());
    ASSERT_TRUE(yosys.Elaborate("cnt").ok());
    ASSERT_TRUE(yosys.AigMap().ok());
    const Status dff = yosys.AbcDff(script);
    ASSERT_TRUE(dff.ok()) << script << ": " << dff.message();
    auto after = yosys.Stats("cnt");
    ASSERT_TRUE(after.ok());
    EXPECT_EQ(after.value().sequential_cells, 4) << script;
  }
}

TEST(AbcDff, UnknownScriptFails) {
  TempDir dir;
  const std::string src = dir.Write("cnt.v", kCounter);
  YosysEngine yosys;
  ASSERT_TRUE(yosys.ParseVerilog(src).ok());
  ASSERT_TRUE(yosys.Elaborate("cnt").ok());
  ASSERT_TRUE(yosys.AigMap().ok());
  const Status dff = yosys.AbcDff("sideways");
  ASSERT_FALSE(dff.ok());
  EXPECT_EQ(dff.code(), foundation::ErrorCode::kInvalidArgument);
}

TEST(AbcDff, HierTop) {
  const std::filesystem::path hier = RepoRoot() / "demo" / "hier";
  std::vector<std::string> files;
  for (const char* rel :
       {"rtl/hier_top.v", "rtl/hier_ctrl.v", "rtl/hier_counter.v",
        "rtl/hier_datapath.v", "rtl/hier_alu.v", "rtl/hier_outreg.v",
        "lib/eos_macro_mac.v"}) {
    files.push_back((hier / rel).string());
  }
  // The top module holds only submodule instances, so the flop
  // counts live on the leaf modules, not on hier_top.
  const std::vector<std::string> flop_modules = {
      "hier_counter", "hier_datapath", "hier_outreg", "eos_macro_mac"};

  TempDir dir;
  YosysEngine yosys;
  ASSERT_TRUE(yosys.ParseVerilogFiles(files, {}, {}).ok());
  ASSERT_TRUE(yosys.Elaborate("hier_top").ok());
  ASSERT_TRUE(yosys.AigMap().ok());

  std::vector<int> seq_after_map;
  for (const auto& mod : flop_modules) {
    auto stats = yosys.Stats(mod);
    ASSERT_TRUE(stats.ok());
    EXPECT_GT(stats.value().sequential_cells, 0);
    seq_after_map.push_back(stats.value().sequential_cells);
  }

  const Status dff = yosys.AbcDff("default");
  ASSERT_TRUE(dff.ok()) << dff.message();

  for (size_t i = 0; i < flop_modules.size(); ++i) {
    auto stats = yosys.Stats(flop_modules[i]);
    ASSERT_TRUE(stats.ok());
    EXPECT_EQ(stats.value().sequential_cells, seq_after_map[i]);
  }

  const std::string out = dir.Path("hier_generic.v");
  ASSERT_TRUE(yosys.WriteVerilog(out).ok());
  const std::string verilog = ReadFile(out);
  EXPECT_NE(verilog.find("rst_n"), std::string::npos);
  EXPECT_NE(verilog.find("clk"), std::string::npos);
}

TEST(AbcDff, FrameSurvivesSequentialUse) {
  TempDir dir;
  const std::string tiny = dir.Write("tiny.v", kTiny);
  const std::string genlib = dir.Write("tiny.genlib", kTinyGenlib);

  // AbcEngine owns the frame, runs a real ABC command, releases it.
  {
    AbcEngine abc;
    const std::string out1 = dir.Path("tiny_mapped1.v");
    ASSERT_TRUE(abc.MapForDelay(tiny, genlib, out1).ok());
    EXPECT_FALSE(ReadFile(out1).empty());
  }

  // The pass drives ABC in process while no AbcEngine is alive.
  const std::string src = dir.Write("cnt.v", kCounter);
  {
    YosysEngine yosys;
    ASSERT_TRUE(yosys.ParseVerilog(src).ok());
    ASSERT_TRUE(yosys.Elaborate("cnt").ok());
    ASSERT_TRUE(yosys.AigMap().ok());
    ASSERT_TRUE(yosys.AbcDff("default").ok());
  }

  // A fresh AbcEngine works after the pass: the frame Abc_RealMain
  // stopped is re-created cleanly on the next use.
  {
    AbcEngine abc;
    const std::string out2 = dir.Path("tiny_mapped2.v");
    ASSERT_TRUE(abc.MapForDelay(tiny, genlib, out2).ok());
    EXPECT_FALSE(ReadFile(out2).empty());
  }
}

TEST(AbcDff, MultiClockFailsFast) {
  TempDir dir;
  const std::string src = dir.Write("twoclk.v", kTwoClock);
  YosysEngine yosys;
  ASSERT_TRUE(yosys.ParseVerilog(src).ok());
  ASSERT_TRUE(yosys.Elaborate("twoclk").ok());
  ASSERT_TRUE(yosys.AigMap().ok());
  auto mapped = yosys.Stats("twoclk");
  ASSERT_TRUE(mapped.ok());
  EXPECT_EQ(mapped.value().sequential_cells, 8);
  const Status dff = yosys.AbcDff("default");
  ASSERT_FALSE(dff.ok());
  EXPECT_EQ(dff.code(), foundation::ErrorCode::kInvalidArgument);
  EXPECT_NE(dff.message().find("clock domain"), std::string::npos);
}

TEST(AbcDff, MultiResetPolarityFailsFast) {
  TempDir dir;
  const std::string src = dir.Write("tworeset.v", kTwoReset);
  YosysEngine yosys;
  ASSERT_TRUE(yosys.ParseVerilog(src).ok());
  ASSERT_TRUE(yosys.Elaborate("tworeset").ok());
  ASSERT_TRUE(yosys.AigMap().ok());
  auto mapped = yosys.Stats("tworeset");
  ASSERT_TRUE(mapped.ok());
  EXPECT_EQ(mapped.value().sequential_cells, 8);
  const Status dff = yosys.AbcDff("default");
  ASSERT_FALSE(dff.ok());
  EXPECT_EQ(dff.code(), foundation::ErrorCode::kInvalidArgument);
  EXPECT_NE(dff.message().find("clock domain"), std::string::npos);
}

TEST(AbcDff, NoFlopFailsFast) {
  // A combinational design has no flop for the pass to extract:
  // the pass would no-op silently, so the facade fails fast
  // instead of reporting a success it did not earn.
  TempDir dir;
  const std::string src = dir.Write("comb.v", kCombinational);
  YosysEngine yosys;
  ASSERT_TRUE(yosys.ParseVerilog(src).ok());
  ASSERT_TRUE(yosys.Elaborate("comb").ok());
  ASSERT_TRUE(yosys.AigMap().ok());
  const Status dff = yosys.AbcDff("default");
  ASSERT_FALSE(dff.ok());
  EXPECT_EQ(dff.code(), foundation::ErrorCode::kInvalidArgument);
}

}  // namespace
}  // namespace eos::synth
