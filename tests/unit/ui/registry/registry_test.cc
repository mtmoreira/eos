// Unit tests: ui/registry and the core command set.
#include "eos/ui/registry.h"

#include <cstdio>

#include "eos/db/checkpoint.h"
#include <gtest/gtest.h>

namespace eos::ui {
namespace {

struct Shell {
  CommandRegistry registry;
  CommandContext ctx;
  Shell() { RegisterCoreCommands(registry); }
  CommandResult Run(const std::string& name,
                    std::vector<std::string> args = {}) {
    return registry.Dispatch(ctx, name, args);
  }
};

TEST(Registry, RegisterAndDispatchCustomCommand) {
  CommandRegistry registry;
  const foundation::Status added = registry.Register(
      "ping", "ping",
      [](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Ok("pong=1");
      });
  ASSERT_TRUE(added.ok());
  const foundation::Status dup = registry.Register(
      "ping", "ping",
      [](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Ok("pong=2");
      });
  EXPECT_FALSE(dup.ok());
  CommandContext ctx;
  const CommandResult result = registry.Dispatch(ctx, "ping", {});
  ASSERT_TRUE(result.ok);
  EXPECT_EQ(result.payload, "pong=1");
}

TEST(Registry, UnknownCommandIsAnInvalidCommandDiagnostic) {
  Shell shell;
  const CommandResult result = shell.Run("nope");
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidCommand);
  EXPECT_EQ(std::string(DiagName(result.diag)), "invalid_command");
}

TEST(Registry, CoreSetIsComplete) {
  Shell shell;
  // The count is a contract: adding a command means updating it.
  EXPECT_EQ(shell.registry.Names().size(), 28u);
  EXPECT_NE(shell.registry.Find("create_design"), nullptr);
  EXPECT_NE(shell.registry.Find("restore"), nullptr);
}

TEST(CoreCommands, DesignLifecycleAndUnclosedDesignDiagnostic) {
  Shell shell;
  const CommandResult no_design = shell.Run("create_net", {"n1"});
  ASSERT_FALSE(no_design.ok);
  EXPECT_EQ(no_design.diag, Diag::kUnclosedDesign);

  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  const CommandResult again = shell.Run("create_design", {"other"});
  ASSERT_FALSE(again.ok);
  EXPECT_EQ(again.diag, Diag::kInvalidConfiguration);

  const CommandResult current = shell.Run("current_design");
  ASSERT_TRUE(current.ok);
  EXPECT_NE(current.payload.find("design=top"), std::string::npos);
  EXPECT_NE(current.payload.find("revision=0"), std::string::npos);
  ASSERT_TRUE(shell.Run("close_design").ok);
  EXPECT_EQ(shell.Run("current_design").diag, Diag::kUnclosedDesign);
}

TEST(CoreCommands, BuildADesignThroughCommands) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_cell", {"dff"}).ok);
  ASSERT_TRUE(shell.Run("create_cell", {"pad", "-macro"}).ok);
  ASSERT_TRUE(shell.Run("create_instance", {"u1", "dff"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"clk_net"}).ok);
  const CommandResult pin = shell.Run("connect", {"u1", "CK", "clk_net"});
  ASSERT_TRUE(pin.ok);
  EXPECT_NE(pin.payload.find("name=u1/CK"), std::string::npos);

  const CommandResult bad_cell = shell.Run("create_instance", {"u2", "nope"});
  ASSERT_FALSE(bad_cell.ok);
  EXPECT_EQ(bad_cell.diag, Diag::kInvalidConfiguration);
  const CommandResult bad_dir = shell.Run("create_port", {"x", "sideways"});
  EXPECT_FALSE(bad_dir.ok);
}

TEST(CoreCommands, UnsupportedOptionsAreRejectedNeverIgnored) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  const CommandResult result = shell.Run("create_cell", {"c1", "-bogus"});
  ASSERT_FALSE(result.ok);
  EXPECT_EQ(result.diag, Diag::kInvalidConfiguration);
  EXPECT_NE(result.message.find("unsupported option"),
            std::string::npos);
  // And the rejected command created nothing.
  EXPECT_TRUE(shell.ctx.design->Find(foundation::ObjectKind::kCell, "c1")
                  .is_null());
}

TEST(CoreCommands, ConstraintsReportTheirBindingHonestly) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  const CommandResult bound = shell.Run("create_clock", {"c1", "clk"});
  ASSERT_TRUE(bound.ok);
  EXPECT_NE(bound.payload.find("status=bound"), std::string::npos);

  const CommandResult unresolved =
      shell.Run("create_clock", {"c2", "missing_clk"});
  ASSERT_TRUE(unresolved.ok);  // the command succeeded...
  EXPECT_NE(unresolved.payload.find("status=unresolved"),
            std::string::npos);  // ...and says so, loudly.

  const CommandResult coverage = shell.Run("constraint_coverage");
  ASSERT_TRUE(coverage.ok);
  EXPECT_NE(coverage.payload.find("total=2"), std::string::npos);
  EXPECT_NE(coverage.payload.find("complete=0"), std::string::npos);

  ASSERT_TRUE(shell.Run("create_port", {"missing_clk", "in"}).ok);
  const CommandResult rebound = shell.Run("rebind_constraints");
  ASSERT_TRUE(rebound.ok);
  EXPECT_NE(rebound.payload.find("bound=2"), std::string::npos);
  EXPECT_NE(rebound.payload.find("complete=1"), std::string::npos);
}

TEST(CoreCommands, CollectionsAreLiveHandlesWithGlobMatching) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"n1"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"n2"}).ok);
  ASSERT_TRUE(shell.Run("create_net", {"clk_net"}).ok);
  const CommandResult got = shell.Run("get_objects", {"net", "n*"});
  ASSERT_TRUE(got.ok);
  EXPECT_NE(got.payload.find("collection=eos_coll_1"), std::string::npos);
  EXPECT_NE(got.payload.find("count=2"), std::string::npos);
  const CommandResult size = shell.Run("sizeof_collection", {"eos_coll_1"});
  ASSERT_TRUE(size.ok);
  EXPECT_EQ(size.payload, "count=2");
  const CommandResult list = shell.Run("list_collection", {"eos_coll_1"});
  ASSERT_TRUE(list.ok);
  EXPECT_EQ(list.payload, "n1\nn2");
  const CommandResult unknown = shell.Run("sizeof_collection", {"nope"});
  ASSERT_FALSE(unknown.ok);
}

TEST(CoreCommands, TransactionsAndReportsThroughCommands) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  const CommandResult report =
      shell.Run("register_report", {"timing", "pass"});
  ASSERT_TRUE(report.ok);
  EXPECT_NE(report.payload.find("artifact=1"), std::string::npos);

  ASSERT_TRUE(shell.Run("begin_transaction", {"place"}).ok);
  const CommandResult twice = shell.Run("begin_transaction", {"again"});
  EXPECT_FALSE(twice.ok);
  const CommandResult committed = shell.Run("commit_transaction", {"pass"});
  ASSERT_TRUE(committed.ok);
  EXPECT_EQ(committed.payload, "revision=1");

  const CommandResult status = shell.Run("report_status", {"1"});
  ASSERT_TRUE(status.ok);
  EXPECT_NE(status.payload.find("valid=1"), std::string::npos);
  EXPECT_NE(status.payload.find("current=0"), std::string::npos);

  ASSERT_TRUE(shell.Run("rollback", {"0"}).ok);
  EXPECT_EQ(shell.Run("current_revision").payload, "revision=0");
  const CommandResult restored = shell.Run("report_status", {"1"});
  EXPECT_NE(restored.payload.find("current=1"), std::string::npos);
}

TEST(CoreCommands, CheckpointWritesARealFileAndRestoreIsHonest) {
  Shell shell;
  ASSERT_TRUE(shell.Run("create_design", {"top"}).ok);
  ASSERT_TRUE(shell.Run("create_port", {"clk", "in"}).ok);
  const std::string path = "registry_test_checkpoint.ckpt";
  std::remove(path.c_str());
  const CommandResult saved = shell.Run("checkpoint", {path});
  ASSERT_TRUE(saved.ok);
  db::RestoreInfo info;
  const foundation::Result<db::CheckpointData> loaded =
      db::Checkpoint::Load(path, &info);
  EXPECT_TRUE(loaded.ok());
  std::remove(path.c_str());

  const CommandResult restore = shell.Run("restore", {path});
  ASSERT_FALSE(restore.ok);
  EXPECT_EQ(restore.diag, Diag::kUnsupportedFeature);
}

TEST(CoreCommands, ScenariosAndListCommands) {
  Shell shell;
  const CommandResult made =
      shell.Run("create_scenario", {"func_tt", "func", "tt", "estimate"});
  ASSERT_TRUE(made.ok);
  const CommandResult dup =
      shell.Run("create_scenario", {"other", "func", "tt", "estimate"});
  ASSERT_FALSE(dup.ok);
  const CommandResult list = shell.Run("list_scenarios");
  EXPECT_NE(list.payload.find("func_tt"), std::string::npos);
  const CommandResult commands = shell.Run("list_commands");
  ASSERT_TRUE(commands.ok);
  EXPECT_NE(commands.payload.find("create_design"), std::string::npos);
  EXPECT_NE(commands.payload.find("checkpoint"), std::string::npos);
}

}  // namespace
}  // namespace eos::ui
