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

// The generic-script fixture: a six-input AND chain, written as
// continuous assignments, the form the Yosys bridge produces.
// Structural hashing alone leaves five ANDs at five levels; the
// classic script's balance step must cut the depth, and no step
// may grow the gate count. Both numbers are ABC print_stats
// facts for this fixture at this pin.
const char kChain[] = R"V(module chain (a, b, c, d, e, f, y);
  input a, b, c, d, e, f;
  output y;
  assign y = (((((a & b) & c) & d) & e) & f);
endmodule
)V";

TEST(AbcFacade, GenericDefaultScriptBalancesTheChain) {
  TempDir dir;
  const std::string chain = dir.Write("chain.v", kChain);
  const std::string optimized = dir.Path("chain_opt.v");
  AbcEngine engine;
  const auto result = engine.OptimizeGeneric(chain, "default", optimized);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value().before.and_count, 5);
  EXPECT_EQ(result.value().before.levels, 5);
  EXPECT_EQ(result.value().after.and_count, 5);
  EXPECT_LE(result.value().after.levels, 3);
  std::ifstream in(optimized);
  ASSERT_TRUE(in.good());
}

TEST(AbcFacade, GenericNoneScriptIsTheHashedBaseline) {
  TempDir dir;
  const std::string chain = dir.Write("chain.v", kChain);
  const std::string optimized = dir.Path("chain_opt.v");
  AbcEngine engine;
  const auto result = engine.OptimizeGeneric(chain, "none", optimized);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value().before.and_count, result.value().after.and_count);
  EXPECT_EQ(result.value().before.levels, result.value().after.levels);
}

TEST(AbcFacade, GenericUnknownScriptFailsBeforeAbcRuns) {
  TempDir dir;
  const std::string chain = dir.Write("chain.v", kChain);
  const std::string optimized = dir.Path("chain_opt.v");
  AbcEngine engine;
  const auto result = engine.OptimizeGeneric(chain, "sideways", optimized);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), foundation::ErrorCode::kInvalidArgument);
  EXPECT_FALSE(std::filesystem::exists(optimized));
}

TEST(AbcFacade, GenericMissingInputFailsWithIoError) {
  TempDir dir;
  AbcEngine engine;
  const auto result = engine.OptimizeGeneric(dir.Path("missing.v"), "default",
                                             dir.Path("out.v"));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), foundation::ErrorCode::kIoError);
}

// The BLIF-input form: the bridge Yosys hands ABC is BLIF,
// so OptimizeGeneric must accept a .blif input, not only
// Verilog. The fixture is the six-input AND chain from the
// Verilog chain test, in BLIF form: five ANDs at five levels
// before the script, five ANDs at no more than three levels
// after its balance step.
const char kChainBlif[] = R"B(.model chain
.inputs a b c d e f
.outputs y
.names a b n1
11 1
.names n1 c n2
11 1
.names n2 d n3
11 1
.names n3 e n4
11 1
.names n4 f y
11 1
.end
)B";

TEST(AbcFacade, GenericBlifInputOptimizes) {
  TempDir dir;
  const std::string blif = dir.Write("chain.blif", kChainBlif);
  const std::string optimized = dir.Path("chain_opt.v");
  AbcEngine engine;
  const auto result = engine.OptimizeGeneric(blif, "default", optimized);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value().before.and_count, 5);
  EXPECT_EQ(result.value().before.levels, 5);
  EXPECT_EQ(result.value().after.and_count, 5);
  EXPECT_LE(result.value().after.levels, 3);
  std::ifstream in(optimized);
  ASSERT_TRUE(in.good());
}

// The S2 Liberty fixture: three combinational cells with scalar
// delays, one sequential cell, and one table-delay cell. The
// converter must emit exactly the combinational scalar cells,
// in alphabetical order, with each cell's delay as its PIN line.
const char kMapLiberty[] = R"LIB(library (eos_map_fixture) {
  delay_model : "table_lookup";
  time_unit : "1ns";
  cell (AND2) {
    area : 2.0;
    pin (A) { direction : input; capacitance : 0.001; }
    pin (B) { direction : input; capacitance : 0.001; }
    pin (Y) {
      direction : output;
      function : "A*B";
      timing () {
        related_pin : "A";
        cell_rise (scalar) { values ("0.6"); }
        cell_fall (scalar) { values ("0.5"); }
      }
    }
  }
  cell (INV) {
    area : 1.0;
    pin (A) { direction : input; capacitance : 0.001; }
    pin (Y) {
      direction : output;
      function : "!A";
      timing () {
        related_pin : "A";
        cell_rise (scalar) { values ("0.5"); }
        cell_fall (scalar) { values ("0.5"); }
      }
    }
  }
  cell (BUF) {
    area : 1.0;
    pin (A) { direction : input; capacitance : 0.001; }
    pin (Y) {
      direction : output;
      function : "A";
      timing () {
        related_pin : "A";
        cell_rise (scalar) { values ("0.4"); }
        cell_fall (scalar) { values ("0.4"); }
      }
    }
  }
  cell (DFF) {
    area : 10.0;
    ff (IQ, IQN) {
      next_state : "D";
      clocked_on : "CK";
    }
    pin (D) { direction : input; capacitance : 0.001; }
    pin (CK) { direction : input; capacitance : 0.001; }
    pin (Q) { direction : output; function : "IQ"; }
  }
  cell (TABLED) {
    area : 3.0;
    pin (A) { direction : input; capacitance : 0.001; }
    pin (Y) {
      direction : output;
      function : "A";
      timing () {
        related_pin : "A";
        cell_rise (table) { values ("0.9"); }
      }
    }
  }
}
)LIB";

TEST(AbcFacade, LibertyToGenlibEmitsOnlyQualifyingCells) {
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kMapLiberty);
  const std::string genlib = dir.Path("cells.genlib");
  AbcEngine engine;
  const auto cells = engine.LibertyToGenlib(liberty, genlib);
  ASSERT_TRUE(cells.ok()) << cells.status().message();
  ASSERT_EQ(cells.value().size(), 3u);
  EXPECT_EQ(cells.value()[0], "AND2");
  EXPECT_EQ(cells.value()[1], "BUF");
  EXPECT_EQ(cells.value()[2], "INV");
  const std::string text = ReadFile(genlib);
  EXPECT_NE(text.find("GATE AND2 2 Y=A*B;"), std::string::npos);
  EXPECT_NE(text.find("PIN * INV 1 999 0.6 0.0 0.6 0.0;"),
            std::string::npos);
  EXPECT_NE(text.find("GATE INV 1 Y=!A;"), std::string::npos);
  EXPECT_EQ(text.find("DFF"), std::string::npos);
  EXPECT_EQ(text.find("TABLED"), std::string::npos);
}

TEST(AbcFacade, LibertyToGenlibAcceptsPlusAsOr) {
  TempDir dir;
  const std::string liberty = dir.Write(
      "or.lib",
      "library (x) { cell (OR2) { area : 2.0; "
      "pin (A) { direction : input; capacitance : 0.001; } "
      "pin (B) { direction : input; capacitance : 0.001; } "
      "pin (Y) { direction : output; function : \"A+B\"; "
      "timing () { related_pin : \"A\"; "
      "cell_rise (scalar) { values (\"0.6\"); } "
      "cell_fall (scalar) { values (\"0.6\"); } } } } }\n");
  const std::string genlib = dir.Path("or.genlib");
  AbcEngine engine;
  const auto cells = engine.LibertyToGenlib(liberty, genlib);
  ASSERT_TRUE(cells.ok()) << cells.status().message();
  ASSERT_EQ(cells.value().size(), 1u);
  EXPECT_EQ(cells.value()[0], "OR2");
  EXPECT_NE(ReadFile(genlib).find("GATE OR2 2 Y=A+B;"),
            std::string::npos);
}

TEST(AbcFacade, LibertyToGenlibFailures) {
  TempDir dir;
  AbcEngine engine;
  const auto missing =
      engine.LibertyToGenlib(dir.Path("no_such.lib"), dir.Path("out.genlib"));
  ASSERT_FALSE(missing.ok());
  EXPECT_EQ(missing.status().code(), foundation::ErrorCode::kIoError);
  const std::string sequential_only = dir.Write(
      "seq.lib",
      "library (x) { cell (DFF) { area : 1.0; ff (IQ, IQN) { "
      "next_state : \"D\"; clocked_on : \"CK\"; } "
      "pin (Q) { direction : output; function : \"IQ\"; } } }\n");
  const auto none = engine.LibertyToGenlib(sequential_only,
                                           dir.Path("seq.genlib"));
  ASSERT_FALSE(none.ok());
  EXPECT_EQ(none.status().code(), foundation::ErrorCode::kUnsupported);
  EXPECT_FALSE(std::filesystem::exists(dir.Path("seq.genlib")));
}

TEST(AbcFacade, MapTechnologyDelayAndAreaMapTheChain) {
  TempDir dir;
  const std::string liberty = dir.Write("cells.lib", kMapLiberty);
  const std::string genlib = dir.Path("cells.genlib");
  const std::string chain = dir.Write("chain.blif", kChainBlif);
  AbcEngine engine;
  const auto cells = engine.LibertyToGenlib(liberty, genlib);
  ASSERT_TRUE(cells.ok()) << cells.status().message();
  const std::string delay_mapped = dir.Path("delay.v");
  ASSERT_TRUE(engine.MapTechnology(chain, genlib, delay_mapped,
                                   MapTarget::kDelay)
                  .ok());
  const std::string delay_text = ReadFile(delay_mapped);
  EXPECT_NE(delay_text.find("AND2"), std::string::npos);
  const std::string area_mapped = dir.Path("area.v");
  ASSERT_TRUE(engine.MapTechnology(chain, genlib, area_mapped,
                                   MapTarget::kArea)
                  .ok());
  const std::string area_text = ReadFile(area_mapped);
  EXPECT_NE(area_text.find("AND2"), std::string::npos);
  const auto missing_input = engine.MapTechnology(
      dir.Path("missing.blif"), genlib, dir.Path("out.v"),
      MapTarget::kDelay);
  ASSERT_FALSE(missing_input.ok());
  EXPECT_EQ(missing_input.code(), foundation::ErrorCode::kIoError);
  const auto missing_lib = engine.MapTechnology(
      chain, dir.Path("missing.genlib"), dir.Path("out.v"),
      MapTarget::kDelay);
  ASSERT_FALSE(missing_lib.ok());
  EXPECT_EQ(missing_lib.code(), foundation::ErrorCode::kIoError);
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
