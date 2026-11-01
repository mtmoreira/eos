// gui: SessionView unit tests (Qt-free; no engines needed).
#include <string>
#include <vector>

#include "eos/ui/gui.h"
#include "eos/ui/diagram.h"
#include "eos/ui/hierarchy.h"
#include "eos/ui/registry.h"
#include "gtest/gtest.h"

namespace {

using eos::ui::CommandContext;
using eos::ui::CommandRegistry;
using eos::ui::CommandResult;
using eos::ui::BreadcrumbPaths;
using eos::ui::DiagramBlock;
using eos::ui::DiagramModel;
using eos::ui::DiagramNet;
using eos::ui::HierarchyModel;
using eos::ui::HierarchyNode;
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

const char kFlatPayload[] =
    "top=optloop\n"
    "node path=optloop module=optloop ports=a,b,c,d,y1,y2\n";

const char kNestedPayload[] =
    "top=top\n"
    "node path=top module=top ports=a,b,sel,y\n"
    "node path=top/u_ctrl module=ctrl ports=sel,op\n"
    "node path=top/u_mid module=mid ports=a,b,y\n"
    "node path=top/u_mid/u_add module=adder ports=a,b,y\n";

TEST(HierarchyModel, ParsesFlatPayload) {
  HierarchyModel model;
  ASSERT_TRUE(model.LoadPayload(kFlatPayload));
  EXPECT_FALSE(model.Empty());
  const HierarchyNode* root = model.Root();
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(root->path, "optloop");
  EXPECT_EQ(root->module, "optloop");
  EXPECT_EQ(root->ports,
            std::vector<std::string>({"a", "b", "c", "d", "y1", "y2"}));
  EXPECT_TRUE(root->children.empty());
  EXPECT_EQ(model.Find("optloop"), root);
  EXPECT_EQ(model.Find("optloop/nope"), nullptr);
}

TEST(HierarchyModel, ParsesNestedPayload) {
  HierarchyModel model;
  ASSERT_TRUE(model.LoadPayload(kNestedPayload));
  const HierarchyNode* root = model.Root();
  ASSERT_NE(root, nullptr);
  ASSERT_EQ(root->children.size(), 2u);
  EXPECT_EQ(root->children[0].path, "top/u_ctrl");
  EXPECT_EQ(root->children[1].path, "top/u_mid");
  ASSERT_EQ(root->children[1].children.size(), 1u);
  EXPECT_EQ(root->children[1].children[0].path, "top/u_mid/u_add");
  EXPECT_EQ(model.Find("top/u_mid/u_add"), &root->children[1].children[0]);
  EXPECT_EQ(model.Find("top/u_mid/nope"), nullptr);
  EXPECT_EQ(model.Find(""), nullptr);
  EXPECT_EQ(model.Find("other"), nullptr);
}

TEST(HierarchyModel, RejectsMalformedPayloads) {
  const char* bad[] = {
      // No top line.
      "node path=top module=top ports=a\n",
      // Empty top.
      "top=\nnode path=top module=top ports=a\n",
      // Unknown key.
      "top=top\nnode path=top module=top ports=a extra=1\n",
      // Missing ports.
      "top=top\nnode path=top module=top\n",
      // Duplicate path.
      "top=top\nnode path=top module=top ports=a\n"
      "node path=top module=top ports=a\n",
      // Child without parent.
      "top=top\nnode path=top/u_mid module=mid ports=a\n",
      // Empty path component.
      "top=top\nnode path=top/ module=top ports=a\n",
      // Root path does not match top.
      "top=top\nnode path=other module=other ports=a\n",
  };
  for (const char* payload : bad) {
    HierarchyModel model;
    EXPECT_FALSE(model.LoadPayload(payload)) << "payload: " << payload;
    EXPECT_TRUE(model.Empty());
  }
}

TEST(HierarchyModel, SelectionFollowsTheTree) {
  HierarchyModel model;
  ASSERT_TRUE(model.LoadPayload(kNestedPayload));
  EXPECT_FALSE(model.HasSelection());
  EXPECT_FALSE(model.Select("top/nope"));
  EXPECT_FALSE(model.HasSelection());
  ASSERT_TRUE(model.Select("top/u_mid/u_add"));
  EXPECT_EQ(model.SelectedPath(), "top/u_mid/u_add");
  model.ClearSelection();
  EXPECT_FALSE(model.HasSelection());
  // A reload that drops the selected path clears the selection.
  ASSERT_TRUE(model.Select("top/u_ctrl"));
  ASSERT_TRUE(model.LoadPayload(kFlatPayload));
  EXPECT_FALSE(model.HasSelection());
}

void RegisterHierarchyCommand(CommandRegistry& registry,
                              const std::string& payload, bool ok) {
  auto status = registry.Register(
      "synth_hierarchy", "synth_hierarchy",
      [payload, ok](CommandContext&, const std::vector<std::string>&) {
        if (!ok) {
          return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                     "no elaborated design");
        }
        return CommandResult::Ok(payload);
      });
  EXPECT_TRUE(status.ok());
}

TEST(GuiSessionView, RefreshHierarchyPopulatesModelWithoutEcho) {
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kNestedPayload, true);
  CommandContext context;
  context.synth.elaborated = true;
  context.synth.top_cell = "top";
  SessionView view(registry, context);
  view.RefreshHierarchy();
  EXPECT_FALSE(view.Hierarchy().Empty());
  ASSERT_NE(view.Hierarchy().Root(), nullptr);
  EXPECT_EQ(view.Hierarchy().Root()->path, "top");
  ASSERT_TRUE(view.SelectHierarchyPath("top/u_mid"));
  EXPECT_EQ(view.SelectedHierarchyPath(), "top/u_mid");
  EXPECT_FALSE(view.SelectHierarchyPath("top/nope"));
  EXPECT_EQ(view.SelectedHierarchyPath(), "top/u_mid");
  EXPECT_TRUE(view.EchoLog().empty());
}

TEST(GuiSessionView, RefreshHierarchyClearsWhenNotElaborated) {
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kNestedPayload, true);
  CommandContext context;
  context.synth.elaborated = true;
  SessionView view(registry, context);
  view.RefreshHierarchy();
  ASSERT_FALSE(view.Hierarchy().Empty());
  ASSERT_TRUE(view.SelectHierarchyPath("top/u_ctrl"));
  context.synth.elaborated = false;
  view.RefreshHierarchy();
  EXPECT_TRUE(view.Hierarchy().Empty());
  EXPECT_FALSE(view.Hierarchy().HasSelection());
}

TEST(GuiSessionView, RefreshHierarchyClearsOnFailedQuery) {
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kNestedPayload, true);
  CommandContext context;
  context.synth.elaborated = true;
  SessionView view(registry, context);
  view.RefreshHierarchy();
  ASSERT_FALSE(view.Hierarchy().Empty());
  // A failing query clears the model: use a fresh registry whose
  // command fails, since a second registration is rejected.
  CommandRegistry failing;
  RegisterHierarchyCommand(failing, "", false);
  CommandContext failing_ctx;
  failing_ctx.synth.elaborated = true;
  SessionView failing_view(failing, failing_ctx);
  failing_view.RefreshHierarchy();
  EXPECT_TRUE(failing_view.Hierarchy().Empty());
}

TEST(GuiSessionView, RefreshHierarchyClearsOnMalformedPayload) {
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, "garbage\n", true);
  CommandContext context;
  context.synth.elaborated = true;
  SessionView view(registry, context);
  view.RefreshHierarchy();
  EXPECT_TRUE(view.Hierarchy().Empty());
}

const char kDiagramTopPayload[] =
    "module=top\n"
    "parent port=a direction=in\n"
    "parent port=b direction=in\n"
    "parent port=sel direction=in\n"
    "parent port=y direction=out\n"
    "block instance=u_ctrl module=ctrl ports=sel:in,op:out\n"
    "block instance=u_mid module=mid ports=a:in,b:in,y:out\n"
    "net name=a blocks=u_mid:a terminals=a\n"
    "net name=b blocks=u_mid:b terminals=b\n"
    "net name=m blocks=u_mid:y terminals=\n"
    "net name=op blocks=u_ctrl:op terminals=\n"
    "net name=sel blocks=u_ctrl:sel terminals=sel\n"
    "net name=y blocks= terminals=y\n";

const char kDiagramMidPayload[] =
    "module=mid\n"
    "parent port=a direction=in\n"
    "parent port=b direction=in\n"
    "parent port=y direction=out\n"
    "block instance=u_add module=adder ports=a:in,b:in,y:out\n"
    "net name=a blocks=u_add:a terminals=a\n"
    "net name=b blocks=u_add:b terminals=b\n"
    "net name=y blocks=u_add:y terminals=y\n";

const char kDiagramLeafPayload[] =
    "module=adder\n"
    "parent port=a direction=in\n"
    "parent port=b direction=in\n"
    "parent port=y direction=out\n"
    "net name=a blocks= terminals=a\n"
    "net name=b blocks= terminals=b\n"
    "net name=y blocks= terminals=y\n";

const char kDiagramFlatPayload[] =
    "module=optloop\n"
    "parent port=a direction=in\n"
    "parent port=b direction=in\n"
    "parent port=c direction=in\n"
    "parent port=d direction=in\n"
    "parent port=y1 direction=out\n"
    "parent port=y2 direction=out\n"
    "net name=a blocks= terminals=a\n"
    "net name=b blocks= terminals=b\n"
    "net name=c blocks= terminals=c\n"
    "net name=d blocks= terminals=d\n"
    "net name=y1 blocks= terminals=y1\n"
    "net name=y2 blocks= terminals=y2\n";

TEST(DiagramModel, ParsesTopPayload) {
  DiagramModel model;
  ASSERT_TRUE(model.LoadPayload(kDiagramTopPayload));
  EXPECT_FALSE(model.Empty());
  EXPECT_EQ(model.Module(), "top");
  ASSERT_EQ(model.ParentPorts().size(), 4u);
  EXPECT_EQ(model.ParentPorts()[0].name, "a");
  EXPECT_EQ(model.ParentPorts()[0].direction, "in");
  EXPECT_EQ(model.ParentPorts()[3].name, "y");
  EXPECT_EQ(model.ParentPorts()[3].direction, "out");
  ASSERT_EQ(model.Blocks().size(), 2u);
  EXPECT_EQ(model.Blocks()[0].instance, "u_ctrl");
  EXPECT_EQ(model.Blocks()[0].module, "ctrl");
  ASSERT_EQ(model.Blocks()[0].ports.size(), 2u);
  EXPECT_EQ(model.Blocks()[0].ports[0].name, "sel");
  EXPECT_EQ(model.Blocks()[0].ports[1].direction, "out");
  EXPECT_EQ(model.Blocks()[1].instance, "u_mid");
  const DiagramBlock* mid = model.FindBlock("u_mid");
  ASSERT_NE(mid, nullptr);
  EXPECT_EQ(mid->module, "mid");
  EXPECT_EQ(model.FindBlock("u_nope"), nullptr);
  ASSERT_EQ(model.Nets().size(), 6u);
  const DiagramNet& m = model.Nets()[2];
  EXPECT_EQ(m.name, "m");
  ASSERT_EQ(m.endpoints.size(), 1u);
  EXPECT_EQ(m.endpoints[0].block, "u_mid");
  EXPECT_EQ(m.endpoints[0].port, "y");
  EXPECT_TRUE(m.terminals.empty());
  const DiagramNet& y = model.Nets()[5];
  EXPECT_EQ(y.name, "y");
  EXPECT_TRUE(y.endpoints.empty());
  ASSERT_EQ(y.terminals.size(), 1u);
  EXPECT_EQ(y.terminals[0], "y");
}

TEST(DiagramModel, ParsesLeafAndFlatPayloads) {
  DiagramModel leaf;
  ASSERT_TRUE(leaf.LoadPayload(kDiagramLeafPayload));
  EXPECT_EQ(leaf.Module(), "adder");
  EXPECT_TRUE(leaf.Blocks().empty());
  ASSERT_EQ(leaf.Nets().size(), 3u);
  EXPECT_TRUE(leaf.Nets()[0].endpoints.empty());
  DiagramModel flat;
  ASSERT_TRUE(flat.LoadPayload(kDiagramFlatPayload));
  EXPECT_EQ(flat.Module(), "optloop");
  EXPECT_TRUE(flat.Blocks().empty());
  ASSERT_EQ(flat.ParentPorts().size(), 6u);
  ASSERT_EQ(flat.Nets().size(), 6u);
}

TEST(DiagramModel, RejectsMalformedPayloads) {
  const char* bad[] = {
      // No module line.
      "parent port=a direction=in\n",
      // Empty module.
      "module=\n",
      // Unknown key.
      "module=top\nblock instance=x module=m ports=a:in extra=1\n",
      // Duplicate block.
      "module=top\nblock instance=x module=m ports=a:in\n"
      "block instance=x module=m ports=a:in\n",
      // Duplicate net.
      "module=top\nparent port=a direction=in\n"
      "net name=n blocks= terminals=a\n"
      "net name=n blocks= terminals=a\n",
      // Endpoint on an unknown block.
      "module=top\nparent port=a direction=in\n"
      "net name=n blocks=nope:a terminals=\n",
      // Endpoint on an unknown port.
      "module=top\nblock instance=x module=m ports=a:in\n"
      "net name=n blocks=x:nope terminals=\n",
      // Terminal on an unknown parent port.
      "module=top\nnet name=n blocks= terminals=nope\n",
      // Unknown direction.
      "module=top\nparent port=a direction=sideways\n",
      // Port without a direction.
      "module=top\nblock instance=x module=m ports=a\n",
      // Net with neither endpoints nor terminals.
      "module=top\nnet name=n blocks= terminals=\n",
      // Empty net name.
      "module=top\nnet name= blocks= terminals=a\n",
  };
  for (const char* payload : bad) {
    DiagramModel model;
    EXPECT_FALSE(model.LoadPayload(payload)) << "payload: " << payload;
    EXPECT_TRUE(model.Empty());
  }
}

TEST(DiagramModel, RejectsMalformedKeepsPreviousDiagram) {
  DiagramModel model;
  ASSERT_TRUE(model.LoadPayload(kDiagramTopPayload));
  EXPECT_FALSE(model.LoadPayload("garbage\n"));
  EXPECT_EQ(model.Module(), "top");
  EXPECT_EQ(model.Blocks().size(), 2u);
}

TEST(DiagramModel, ReloadReplacesPreviousDiagram) {
  DiagramModel model;
  ASSERT_TRUE(model.LoadPayload(kDiagramTopPayload));
  ASSERT_TRUE(model.LoadPayload(kDiagramLeafPayload));
  EXPECT_EQ(model.Module(), "adder");
  EXPECT_TRUE(model.Blocks().empty());
}

TEST(BreadcrumbPaths, BuildsPrefixes) {
  EXPECT_EQ(BreadcrumbPaths("top/u_mid/u_add"),
            std::vector<std::string>(
                {"top", "top/u_mid", "top/u_mid/u_add"}));
  EXPECT_EQ(BreadcrumbPaths("top"), std::vector<std::string>({"top"}));
  EXPECT_TRUE(BreadcrumbPaths("").empty());
  EXPECT_TRUE(BreadcrumbPaths("top//x").empty());
  EXPECT_TRUE(BreadcrumbPaths("/top").empty());
  // A trailing slash yields no empty component under the
  // getline split, so it is ignored: "top/" crumbs to "top".
  EXPECT_EQ(BreadcrumbPaths("top/"), std::vector<std::string>({"top"}));
}

void RegisterBlocksCommand(CommandRegistry& registry,
                           std::vector<std::string>* seen_module) {
  auto status = registry.Register(
      "synth_blocks", "synth_blocks <module>",
      [seen_module](CommandContext&,
                    const std::vector<std::string>& args) {
        if (args.size() != 1) {
          return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                     "need a module");
        }
        if (seen_module != nullptr) *seen_module = args;
        if (args[0] == "mid") return CommandResult::Ok(kDiagramMidPayload);
        if (args[0] == "top") return CommandResult::Ok(kDiagramTopPayload);
        if (args[0] == "optloop") {
          return CommandResult::Ok(kDiagramFlatPayload);
        }
        return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                   "unknown module");
      });
  EXPECT_TRUE(status.ok());
}

void RegisterMutableHierarchyCommand(CommandRegistry& registry,
                                     std::string& payload) {
  auto status = registry.Register(
      "synth_hierarchy", "synth_hierarchy",
      [&payload](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Ok(payload);
      });
  EXPECT_TRUE(status.ok());
}

TEST(GuiSessionView, RefreshDiagramPopulatesModelWithoutEcho) {
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kNestedPayload, true);
  std::vector<std::string> seen_module;
  RegisterBlocksCommand(registry, &seen_module);
  CommandContext context;
  context.synth.elaborated = true;
  context.synth.top_cell = "top";
  SessionView view(registry, context);
  view.RefreshHierarchy();
  ASSERT_TRUE(view.SetDiagramLevel("top/u_mid"));
  view.RefreshDiagram();
  EXPECT_EQ(view.DiagramLevel(), "top/u_mid");
  EXPECT_EQ(view.Diagram().Module(), "mid");
  ASSERT_EQ(seen_module.size(), 1u);
  EXPECT_EQ(seen_module[0], "mid");
  EXPECT_TRUE(view.EchoLog().empty());
}

TEST(GuiSessionView, RefreshDiagramFallsBackToRootWhenLevelDrops) {
  CommandRegistry registry;
  std::string hierarchy_payload = kNestedPayload;
  RegisterMutableHierarchyCommand(registry, hierarchy_payload);
  RegisterBlocksCommand(registry, nullptr);
  CommandContext context;
  context.synth.elaborated = true;
  context.synth.top_cell = "top";
  SessionView view(registry, context);
  view.RefreshHierarchy();
  ASSERT_TRUE(view.SetDiagramLevel("top/u_mid"));
  view.RefreshDiagram();
  EXPECT_EQ(view.Diagram().Module(), "mid");
  hierarchy_payload = kFlatPayload;
  view.RefreshHierarchy();
  EXPECT_FALSE(view.Hierarchy().HasSelection());
  view.RefreshDiagram();
  EXPECT_EQ(view.DiagramLevel(), "optloop");
  EXPECT_EQ(view.Diagram().Module(), "optloop");
}

TEST(GuiSessionView, RefreshDiagramClearsWhenNotElaborated) {
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kNestedPayload, true);
  RegisterBlocksCommand(registry, nullptr);
  CommandContext context;
  context.synth.elaborated = true;
  SessionView view(registry, context);
  view.RefreshHierarchy();
  ASSERT_TRUE(view.SetDiagramLevel("top/u_mid"));
  view.RefreshDiagram();
  ASSERT_FALSE(view.Diagram().Empty());
  context.synth.elaborated = false;
  view.RefreshDiagram();
  EXPECT_TRUE(view.Diagram().Empty());
  EXPECT_TRUE(view.DiagramLevel().empty());
}

TEST(GuiSessionView, RefreshDiagramClearsOnFailedQuery) {
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kNestedPayload, true);
  RegisterBlocksCommand(registry, nullptr);
  CommandContext context;
  context.synth.elaborated = true;
  SessionView view(registry, context);
  view.RefreshHierarchy();
  ASSERT_TRUE(view.SetDiagramLevel("top/u_mid"));
  view.RefreshDiagram();
  ASSERT_FALSE(view.Diagram().Empty());
  CommandRegistry failing;
  RegisterHierarchyCommand(failing, kNestedPayload, true);
  auto status = failing.Register(
      "synth_blocks", "synth_blocks <module>",
      [](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                   "no elaborated design");
      });
  EXPECT_TRUE(status.ok());
  CommandContext failing_ctx;
  failing_ctx.synth.elaborated = true;
  SessionView failing_view(failing, failing_ctx);
  failing_view.RefreshHierarchy();
  ASSERT_TRUE(failing_view.SetDiagramLevel("top/u_mid"));
  failing_view.RefreshDiagram();
  EXPECT_TRUE(failing_view.Diagram().Empty());
  EXPECT_EQ(failing_view.DiagramLevel(), "top/u_mid");
}

TEST(GuiSessionView, RefreshDiagramClearsOnMalformedPayload) {
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kNestedPayload, true);
  auto status = registry.Register(
      "synth_blocks", "synth_blocks <module>",
      [](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Ok("garbage\n");
      });
  EXPECT_TRUE(status.ok());
  CommandContext context;
  context.synth.elaborated = true;
  SessionView view(registry, context);
  view.RefreshHierarchy();
  ASSERT_TRUE(view.SetDiagramLevel("top/u_mid"));
  view.RefreshDiagram();
  EXPECT_TRUE(view.Diagram().Empty());
}

TEST(GuiSessionView, SetDiagramLevelValidatesAgainstTheTree) {
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kNestedPayload, true);
  CommandContext context;
  context.synth.elaborated = true;
  SessionView view(registry, context);
  view.RefreshHierarchy();
  EXPECT_TRUE(view.DiagramLevel().empty());
  EXPECT_FALSE(view.SetDiagramLevel("top/nope"));
  EXPECT_TRUE(view.DiagramLevel().empty());
  EXPECT_FALSE(view.SetDiagramLevel(""));
  ASSERT_TRUE(view.SetDiagramLevel("top/u_mid/u_add"));
  EXPECT_EQ(view.DiagramLevel(), "top/u_mid/u_add");
  EXPECT_FALSE(view.SetDiagramLevel("top/nope"));
  EXPECT_EQ(view.DiagramLevel(), "top/u_mid/u_add");
}

}  // namespace
