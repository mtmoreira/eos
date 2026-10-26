// Unit tests: synth/abc, the ABC facade.
//
// The fixtures are a hand-written genlib library (ABC's mapping
// format, with the delays of the timing-eco synthetic cells) and
// a small cone netlist: an AND gate behind a double inversion.
// The double inversion is the canonical local-redundancy case:
// sizing and buffering cannot remove it, AIG structural hashing
// folds it away, and the mapped cone must come back as the AND
// gate alone. The equivalence tests use ABC's own cec through the
// facade: the mapped cone must prove equivalent to the original,
// and a cone with OR in place of AND must prove NOT equivalent.
// Every expected value is a fixture construction fact.
#include "eos/synth/abc.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace eos::synth {
namespace {

const char kGenlib[] = R"GEN(GATE INV_X1 1 Y=!A;
PIN * INV 1 999 0.5 0.0 0.5 0.0;
GATE AND2_X1 2 Y=A*B;
PIN * INV 1 999 0.6 0.0 0.6 0.0;
GATE OR2_X1 2 Y=A+B;
PIN * INV 1 999 0.6 0.0 0.6 0.0;
GATE BUF_X1 1 Y=A;
PIN * INV 1 999 0.4 0.0 0.4 0.0;
)GEN";

const char kCone[] = R"V(module cone (a, b, y);
  input a, b;
  output y;
  wire n1, n2;
  AND2_X1 u1 (.A(a), .B(b), .Y(n1));
  INV_X1 u2 (.A(n1), .Y(n2));
  INV_X1 u3 (.A(n2), .Y(y));
endmodule
)V";

const char kCorruptedCone[] = R"V(module cone (a, b, y);
  input a, b;
  output y;
  wire n1, n2;
  OR2_X1 u1 (.A(a), .B(b), .Y(n1));
  INV_X1 u2 (.A(n1), .Y(n2));
  INV_X1 u3 (.A(n2), .Y(y));
endmodule
)V";

class TempDir {
 public:
  TempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("eos_abc_test_" + std::to_string(counter_++))) {
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

std::string ReadFile(const std::string& path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

TEST(AbcFacade, MapForDelayRemovesTheDoubleInversion) {
  TempDir dir;
  const std::string genlib = dir.Write("cells.genlib", kGenlib);
  const std::string cone = dir.Write("cone.v", kCone);
  const std::string mapped = dir.Path("mapped.v");
  AbcEngine engine;
  ASSERT_TRUE(engine.MapForDelay(cone, genlib, mapped).ok());
  const std::string text = ReadFile(mapped);
  EXPECT_NE(text.find("AND2_X1"), std::string::npos);
  EXPECT_EQ(text.find("INV_X1"), std::string::npos);
}

TEST(AbcFacade, ProveEquivalentAcceptsTheMappedCone) {
  TempDir dir;
  const std::string genlib = dir.Write("cells.genlib", kGenlib);
  const std::string cone = dir.Write("cone.v", kCone);
  const std::string mapped = dir.Path("mapped.v");
  AbcEngine engine;
  ASSERT_TRUE(engine.MapForDelay(cone, genlib, mapped).ok());
  const auto verdict = engine.ProveEquivalent(cone, mapped, genlib);
  ASSERT_TRUE(verdict.ok()) << verdict.status().message();
  EXPECT_EQ(verdict.value(), EquivalenceVerdict::kEquivalent);
}

TEST(AbcFacade, ProveEquivalentRejectsACorruptedCone) {
  TempDir dir;
  const std::string genlib = dir.Write("cells.genlib", kGenlib);
  const std::string cone = dir.Write("cone.v", kCone);
  const std::string corrupted = dir.Write("corrupted.v", kCorruptedCone);
  AbcEngine engine;
  const auto verdict = engine.ProveEquivalent(cone, corrupted, genlib);
  ASSERT_TRUE(verdict.ok()) << verdict.status().message();
  EXPECT_EQ(verdict.value(), EquivalenceVerdict::kNotEquivalent);
}

TEST(AbcFacade, ProveEquivalentIsUnknownWhenTheProofCannotRun) {
  TempDir dir;
  const std::string genlib = dir.Write("cells.genlib", kGenlib);
  const std::string cone = dir.Write("cone.v", kCone);
  AbcEngine engine;
  // A missing second netlist: the proof cannot conclude, and the
  // facade must say unknown, never equivalent.
  const auto verdict =
      engine.ProveEquivalent(cone, dir.Path("missing.v"), genlib);
  ASSERT_TRUE(verdict.ok()) << verdict.status().message();
  EXPECT_EQ(verdict.value(), EquivalenceVerdict::kUnknown);
}

}  // namespace
}  // namespace eos::synth
