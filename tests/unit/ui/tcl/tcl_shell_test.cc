// Unit tests: ui/tcl shell binding. Built only when the build found a
// Tcl development package (TCL_FOUND), so a pass here always means a
// real interpreter ran.
#include "eos/ui/tcl_shell.h"

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

}  // namespace
}  // namespace eos::ui
