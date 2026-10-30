// Unit tests: ui/tcl shell binding. Built only when the build found a
// Tcl development package (TCL_FOUND), so a pass here always means a
// real interpreter ran.
#include "eos/ui/tcl_shell.h"

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

namespace eos::ui {
namespace {

struct Fixture {
  CommandRegistry registry;
  CommandContext ctx;
  Fixture() { RegisterCoreCommands(registry); }
};

TEST(TclShell, EosCommandsRunInTheEosNamespace) {
  Fixture f;
  auto shell = TclShell::Create(f.registry, f.ctx);
  ASSERT_TRUE(shell.ok());
  const auto made = shell.value()->Eval("eos::create_design top");
  ASSERT_TRUE(made.ok());
  EXPECT_NE(made.value().find("design=top"), std::string::npos);
  const auto net = shell.value()->Eval("eos::create_net n1");
  ASSERT_TRUE(net.ok());
  const auto current = shell.value()->Eval("eos::current_design");
  ASSERT_TRUE(current.ok());
  EXPECT_NE(current.value().find("objects=1"), std::string::npos);
}

TEST(TclShell, PlainTclStillWorksAroundEosCommands) {
  Fixture f;
  auto shell = TclShell::Create(f.registry, f.ctx);
  ASSERT_TRUE(shell.ok());
  const auto value = shell.value()->Eval("expr {6 * 7}");
  ASSERT_TRUE(value.ok());
  EXPECT_EQ(value.value(), "42");
  const auto scripted = shell.value()->Eval(
      "eos::create_design d\n"
      "set nets [eos::get_objects net *]\n"
      "return $nets");
  ASSERT_TRUE(scripted.ok());
  EXPECT_NE(scripted.value().find("collection=eos_coll_1"),
            std::string::npos);
}

TEST(TclShell, CommandFailureIsACatchableTclErrorWithTheDiagnostic) {
  Fixture f;
  auto shell = TclShell::Create(f.registry, f.ctx);
  ASSERT_TRUE(shell.ok());
  const auto failed = shell.value()->Eval("eos::create_net n1");
  ASSERT_FALSE(failed.ok());
  EXPECT_NE(failed.status().message().find("EOS unclosed_design"),
            std::string::npos);
  const auto caught = shell.value()->Eval(
      "catch {eos::create_net n1} msg\n"
      "return $msg");
  ASSERT_TRUE(caught.ok());
  EXPECT_NE(caught.value().find("unclosed_design"), std::string::npos);
}

// S6 backfill: RunFiles executes a script file through the same
// command set, and a missing file is a nonzero exit, not a crash.
TEST(TclShell, RunFilesExecutesScriptFilesAndRejectsMissingFiles) {
  Fixture f;
  auto shell = TclShell::Create(f.registry, f.ctx);
  ASSERT_TRUE(shell.ok());
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "eos_tcl_runfiles_test";
  std::filesystem::create_directories(dir);
  const std::filesystem::path script = dir / "setup.tcl";
  {
    std::ofstream out(script);
    out << "eos::create_design top\n";
    out << "eos::create_net n1\n";
  }
  EXPECT_EQ(shell.value()->RunFiles({script.string()}), 0);
  EXPECT_TRUE(f.ctx.project.HasDesign());
  EXPECT_EQ(shell.value()->RunFiles({(dir / "missing.tcl").string()}), 1);
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

}  // namespace
}  // namespace eos::ui
