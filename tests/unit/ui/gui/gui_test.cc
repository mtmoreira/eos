// gui: SessionView unit tests (Qt-free; no engines needed).
#include <string>
#include <vector>

#include "eos/ui/gui.h"
#include "eos/ui/registry.h"
#include "gtest/gtest.h"

namespace {

using eos::ui::CommandContext;
using eos::ui::CommandRegistry;
using eos::ui::CommandResult;
using eos::ui::SessionView;
using eos::ui::StepStatus;

void RegisterPing(CommandRegistry& registry) {
  auto status = registry.Register(
      "ping", "ping",
      [](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Ok("pong=1");
      });
  EXPECT_TRUE(status.ok());
}

TEST(GuiSessionView, DispatchEchoesAndRuns) {
  CommandRegistry registry;
  RegisterPing(registry);
  CommandContext context;
  SessionView view(registry, context);
  const CommandResult result = view.Dispatch("ping", {});
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.payload, "pong=1");
  ASSERT_EQ(view.EchoLog().size(), 1u);
  EXPECT_EQ(view.EchoLog()[0], "eos::ping");
}

TEST(GuiSessionView, DispatchQuotesArgsWithSpaces) {
  CommandRegistry registry;
  RegisterPing(registry);
  CommandContext context;
  SessionView view(registry, context);
  const CommandResult result = view.Dispatch("ping", {"a b", "plain"});
  EXPECT_TRUE(result.ok);
  ASSERT_EQ(view.EchoLog().size(), 1u);
  EXPECT_EQ(view.EchoLog()[0], "eos::ping \"a b\" plain");
}

TEST(GuiSessionView, DispatchUnknownStillEchoed) {
  CommandRegistry registry;
  CommandContext context;
  SessionView view(registry, context);
  const CommandResult result = view.Dispatch("nope", {});
  EXPECT_FALSE(result.ok);
  ASSERT_EQ(view.EchoLog().size(), 1u);
  EXPECT_EQ(view.EchoLog()[0], "eos::nope");
}

TEST(GuiSessionView, StepStatesFollowSessionFlags) {
  CommandRegistry registry;
  CommandContext context;
  SessionView view(registry, context);
  EXPECT_EQ(view.ReadStep(), StepStatus::kPending);
  EXPECT_EQ(view.ElaborateStep(), StepStatus::kPending);
  EXPECT_EQ(view.GenericStep(), StepStatus::kPending);
  context.synth.parsed = true;
  EXPECT_EQ(view.ReadStep(), StepStatus::kDone);
  context.synth.elaborated = true;
  EXPECT_EQ(view.ElaborateStep(), StepStatus::kDone);
  context.synth.generic_optimized = true;
  EXPECT_EQ(view.GenericStep(), StepStatus::kDone);
}

TEST(GuiSessionView, DispatchSeesSharedContext) {
  CommandRegistry registry;
  auto status = registry.Register(
      "mark_parsed", "mark_parsed",
      [](CommandContext& ctx, const std::vector<std::string>&) {
        ctx.synth.parsed = true;
        return CommandResult::Ok("");
      });
  EXPECT_TRUE(status.ok());
  CommandContext context;
  SessionView view(registry, context);
  EXPECT_EQ(view.ReadStep(), StepStatus::kPending);
  const CommandResult result = view.Dispatch("mark_parsed", {});
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(view.ReadStep(), StepStatus::kDone);
}

}  // namespace
