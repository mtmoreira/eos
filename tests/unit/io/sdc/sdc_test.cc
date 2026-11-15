// Unit tests: io/sdc, the scoped SDC clock parser.
#include "eos/io/sdc.h"

#include <gtest/gtest.h>

namespace eos::io {
namespace {

// The demo file, verbatim in substance (demo/clocked/
// clk_top.sdc): parsing must reproduce its three statements
// with targets as written, wrappers kept.
TEST(SdcParser, ParsesDemoSdc) {
  const foundation::Result<SdcClockFile> parsed = ParseSdcClocks(
      "# Clock constraints for the clocked demo design.\n"
      "create_clock -name clk -period 5 -waveform {0 2.5} "
      "[get_ports clk]\n"
      "create_generated_clock -name clk_div -source [get_ports clk] "
      "-divide_by 2 [get_pins u_div/q]\n"
      "set_clock_uncertainty -setup 0.1 [all_clocks]\n"
      "set_clock_uncertainty -hold 0.05 [all_clocks]\n");
  ASSERT_TRUE(parsed.ok());
  const SdcClockFile& file = parsed.value();
  ASSERT_EQ(file.clocks.size(), 1u);
  EXPECT_EQ(file.clocks[0].name, "clk");
  EXPECT_DOUBLE_EQ(file.clocks[0].period_ns, 5.0);
  ASSERT_TRUE(file.clocks[0].has_waveform);
  EXPECT_DOUBLE_EQ(file.clocks[0].waveform_rise_ns, 0.0);
  EXPECT_DOUBLE_EQ(file.clocks[0].waveform_fall_ns, 2.5);
  ASSERT_EQ(file.clocks[0].targets.size(), 1u);
  EXPECT_EQ(file.clocks[0].targets[0], "[get_ports clk]");
  ASSERT_EQ(file.generated.size(), 1u);
  EXPECT_EQ(file.generated[0].name, "clk_div");
  EXPECT_EQ(file.generated[0].source, "[get_ports clk]");
  EXPECT_EQ(file.generated[0].divide_by, 2);
  EXPECT_EQ(file.generated[0].multiply_by, 0);
  EXPECT_FALSE(file.generated[0].has_master_clock);
  ASSERT_EQ(file.generated[0].targets.size(), 1u);
  EXPECT_EQ(file.generated[0].targets[0], "[get_pins u_div/q]");
  ASSERT_EQ(file.uncertainties.size(), 2u);
  ASSERT_TRUE(file.uncertainties[0].has_setup);
  EXPECT_DOUBLE_EQ(file.uncertainties[0].setup_ns, 0.1);
  EXPECT_FALSE(file.uncertainties[0].has_hold);
  ASSERT_EQ(file.uncertainties[0].targets.size(), 1u);
  EXPECT_EQ(file.uncertainties[0].targets[0], "[all_clocks]");
  ASSERT_TRUE(file.uncertainties[1].has_hold);
  EXPECT_DOUBLE_EQ(file.uncertainties[1].hold_ns, 0.05);
  EXPECT_TRUE(file.unwired.empty());
}

// Name from the single target, -add, multiply with invert,
// explicit -master_clock, uncertainty by name and by
// [get_clocks ...] list: the grammar mirrors the interactive
// handlers'.
TEST(SdcParser, StatementForms) {
  const foundation::Result<SdcClockFile> parsed = ParseSdcClocks(
      "create_clock -period 10 -add [get_ports clk_b]\n"
      "create_generated_clock -source [get_pins u_gen/clk] "
      "-master_clock clk_b -multiply_by 3 -invert "
      "[get_pins u_gen/q]\n"
      "create_clock -name vclk -period 1.25\n"
      "set_clock_uncertainty -setup 0.2 -hold 0.05 clk_b\n"
      "set_clock_uncertainty -hold 0.01 [get_clocks clk_b vclk]\n");
  ASSERT_TRUE(parsed.ok());
  const SdcClockFile& file = parsed.value();
  ASSERT_EQ(file.clocks.size(), 2u);
  EXPECT_EQ(file.clocks[0].name, "[get_ports clk_b]");
  EXPECT_DOUBLE_EQ(file.clocks[0].period_ns, 10.0);
  EXPECT_TRUE(file.clocks[0].add);
  EXPECT_FALSE(file.clocks[0].has_waveform);
  EXPECT_EQ(file.clocks[1].name, "vclk");
  EXPECT_TRUE(file.clocks[1].targets.empty());
  ASSERT_EQ(file.generated.size(), 1u);
  EXPECT_EQ(file.generated[0].name, "[get_pins u_gen/q]");
  EXPECT_TRUE(file.generated[0].has_master_clock);
  EXPECT_EQ(file.generated[0].master_clock, "clk_b");
  EXPECT_EQ(file.generated[0].multiply_by, 3);
  EXPECT_TRUE(file.generated[0].invert);
  ASSERT_EQ(file.uncertainties.size(), 2u);
  EXPECT_TRUE(file.uncertainties[0].has_setup);
  EXPECT_TRUE(file.uncertainties[0].has_hold);
  ASSERT_EQ(file.uncertainties[0].targets.size(), 1u);
  EXPECT_EQ(file.uncertainties[0].targets[0], "clk_b");
  ASSERT_EQ(file.uncertainties[1].targets.size(), 1u);
  EXPECT_EQ(file.uncertainties[1].targets[0],
            "[get_clocks clk_b vclk]");
  EXPECT_TRUE(file.unwired.empty());
}

// Comments, semicolon-separated statements, a backslash
// continuation, a braced target list, a quoted name, and a
// nested-brace waveform all tokenize as SDC writes them.
TEST(SdcParser, LexicalForms) {
  const foundation::Result<SdcClockFile> parsed = ParseSdcClocks(
      "# leading comment\n"
      "create_clock -period 4 -name a clk_a; "
      "create_clock -period 8 \\\n"
      "  -name b [get_ports clk_b] # trailing comment\n"
      "create_clock -name pair -period 2 {clk_c clk_d}\n"
      "create_clock -name \"quoted\" -period 1 -waveform {0 0.5} "
      "clk_q\n");
  ASSERT_TRUE(parsed.ok());
  const SdcClockFile& file = parsed.value();
  ASSERT_EQ(file.clocks.size(), 4u);
  EXPECT_EQ(file.clocks[0].name, "a");
  ASSERT_EQ(file.clocks[0].targets.size(), 1u);
  EXPECT_EQ(file.clocks[0].targets[0], "clk_a");
  EXPECT_EQ(file.clocks[1].name, "b");
  EXPECT_DOUBLE_EQ(file.clocks[1].period_ns, 8.0);
  ASSERT_EQ(file.clocks[2].targets.size(), 2u);
  EXPECT_EQ(file.clocks[2].targets[0], "clk_c");
  EXPECT_EQ(file.clocks[2].targets[1], "clk_d");
  EXPECT_EQ(file.clocks[3].name, "quoted");
  ASSERT_TRUE(file.clocks[3].has_waveform);
  EXPECT_DOUBLE_EQ(file.clocks[3].waveform_fall_ns, 0.5);
  EXPECT_TRUE(file.unwired.empty());
}

// Every contained failure mode lands in the unwired list with
// its line number and reason, and none of them produces a def.
TEST(SdcParser, UnwiredForms) {
  const foundation::Result<SdcClockFile> parsed = ParseSdcClocks(
      "create_generated_clock -name g -source [get_pins u/q] "
      "-edges {1 3 5} [get_pins u/q]\n"          // line 1
      "set_clock_uncertainty -setup 0.1 -rise [get_clocks a]\n"  // 2
      "set_clock_uncertainty 0.1 -from [get_clocks a] "
      "-to [get_clocks b]\n"                                     // 3
      "set_clock_uncertainty -setup 0.1 [get_pins u/ck]\n"       // 4
      "create_clock -name bad -period fast clk\n"                // 5
      "create_clock -name np clk\n"                              // 6
      "create_clock -name u -period 5 -bogus 1 clk\n"            // 7
      "create_clock -name s -period $period clk\n");             // 8
  ASSERT_TRUE(parsed.ok());
  const SdcClockFile& file = parsed.value();
  EXPECT_TRUE(file.clocks.empty());
  EXPECT_TRUE(file.generated.empty());
  EXPECT_TRUE(file.uncertainties.empty());
  ASSERT_EQ(file.unwired.size(), 8u);
  for (std::size_t k = 0; k < file.unwired.size(); ++k) {
    EXPECT_EQ(file.unwired[k].line, static_cast<int>(k + 1));
    EXPECT_FALSE(file.unwired[k].reason.empty());
  }
  EXPECT_EQ(file.unwired[0].statement, "create_generated_clock");
  EXPECT_NE(file.unwired[0].reason.find("-edges"), std::string::npos);
  EXPECT_EQ(file.unwired[3].statement, "set_clock_uncertainty");
  EXPECT_NE(file.unwired[3].reason.find("pins or ports"),
            std::string::npos);
  EXPECT_NE(file.unwired[4].reason.find("not a number"),
            std::string::npos);
  EXPECT_NE(file.unwired[5].reason.find("no -period"),
            std::string::npos);
  EXPECT_NE(file.unwired[6].reason.find("unknown option"),
            std::string::npos);
  EXPECT_NE(file.unwired[7].reason.find("substitution"),
            std::string::npos);
}

// Everything that is not a clock statement is ignored
// entirely: not parsed, not counted, not reported. A garbage
// file parses to nothing.
TEST(SdcParser, IgnoresNonClockStatements) {
  const foundation::Result<SdcClockFile> parsed = ParseSdcClocks(
      "set_units -time ns\n"
      "create_clock_group -name g -group {[get_clocks a]}\n"
      "set_input_delay 1.0 -clock clk [get_ports d]\n"
      "set_false_path -from [get_clocks a] -to [get_clocks b]\n"
      "set_clock_latency 0.3 [get_clocks a]\n"
      "this is not tcl at all {{{\n");
  ASSERT_TRUE(parsed.ok());
  EXPECT_TRUE(parsed.value().clocks.empty());
  EXPECT_TRUE(parsed.value().generated.empty());
  EXPECT_TRUE(parsed.value().uncertainties.empty());
  EXPECT_TRUE(parsed.value().unwired.empty());
}

}  // namespace
}  // namespace eos::io
