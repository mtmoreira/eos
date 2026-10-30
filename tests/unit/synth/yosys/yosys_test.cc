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

}  // namespace
}  // namespace eos::synth
