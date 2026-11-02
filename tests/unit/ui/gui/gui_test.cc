// gui: SessionView unit tests (Qt-free; no engines needed).
#include <string>
#include <vector>

#include "eos/ui/gui.h"
#include "eos/ui/console_entry.h"
#include "eos/ui/diagram.h"
#include "eos/ui/hierarchy.h"
#include "eos/ui/panels.h"
#include "eos/ui/registry.h"
#include "eos/ui/schematic.h"
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
using eos::ui::LiveStatsModel;
using eos::ui::PanelsModel;
using eos::ui::SchematicGate;
using eos::ui::SchematicModel;
using eos::ui::SchematicNet;
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

const char kSchematicLeafPayload[] =
    "module=leaf\n"
    "parent port=a direction=in\n"
    "parent port=b direction=in\n"
    "parent port=y direction=out\n"
    "gate instance=g1 type=$and ports=A:in,B:in,Y:out\n"
    "gate instance=g2 type=$not ports=A:in,Y:out\n"
    "net name=a gates=g1:A terminals=a\n"
    "net name=b gates=g1:B terminals=b\n"
    "net name=n gates=g1:Y,g2:A terminals=\n"
    "net name=y gates=g2:Y terminals=y\n";

const char kSchematicAdderPayload[] =
    "module=adder\n"
    "parent port=a direction=in\n"
    "parent port=b direction=in\n"
    "parent port=y direction=out\n"
    "gate instance=add type=$add ports=A:in,B:in,Y:out\n"
    "net name=a gates=add:A terminals=a\n"
    "net name=b gates=add:B terminals=b\n"
    "net name=y gates=add:Y terminals=y\n";

const char kSchematicFlatPayload[] =
    "module=optloop\n"
    "parent port=a direction=in\n"
    "parent port=b direction=in\n"
    "parent port=c direction=in\n"
    "parent port=d direction=in\n"
    "parent port=y1 direction=out\n"
    "parent port=y2 direction=out\n"
    "gate instance=g_ab type=$and ports=A:in,B:in,Y:out\n"
    "gate instance=g_abc type=$and ports=A:in,B:in,Y:out\n"
    "gate instance=g_y1 type=$and ports=A:in,B:in,Y:out\n"
    "net name=a gates=g_ab:A terminals=a\n"
    "net name=b gates=g_ab:B terminals=b\n"
    "net name=ab gates=g_ab:Y,g_abc:A terminals=\n"
    "net name=c gates=g_abc:B terminals=c\n"
    "net name=abc gates=g_abc:Y,g_y1:A terminals=\n"
    "net name=d gates=g_y1:B terminals=d\n"
    "net name=y1 gates=g_y1:Y terminals=y1\n"
    "net name=y2 gates= terminals=y2\n";

TEST(SchematicModel, ParsesLeafPayload) {
  SchematicModel model;
  ASSERT_TRUE(model.LoadPayload(kSchematicLeafPayload));
  EXPECT_FALSE(model.Empty());
  EXPECT_EQ(model.Module(), "leaf");
  ASSERT_EQ(model.ParentPorts().size(), 3u);
  EXPECT_EQ(model.ParentPorts()[2].name, "y");
  EXPECT_EQ(model.ParentPorts()[2].direction, "out");
  ASSERT_EQ(model.Gates().size(), 2u);
  EXPECT_EQ(model.Gates()[0].instance, "g1");
  EXPECT_EQ(model.Gates()[0].type, "$and");
  ASSERT_EQ(model.Gates()[0].ports.size(), 3u);
  EXPECT_EQ(model.Gates()[0].ports[2].name, "Y");
  EXPECT_EQ(model.Gates()[0].ports[2].direction, "out");
  const SchematicGate* g2 = model.FindGate("g2");
  ASSERT_NE(g2, nullptr);
  EXPECT_EQ(g2->type, "$not");
  EXPECT_EQ(model.FindGate("g_nope"), nullptr);
  ASSERT_EQ(model.Nets().size(), 4u);
  const SchematicNet& n = model.Nets()[2];
  EXPECT_EQ(n.name, "n");
  ASSERT_EQ(n.endpoints.size(), 2u);
  EXPECT_EQ(n.endpoints[0].gate, "g1");
  EXPECT_EQ(n.endpoints[0].port, "Y");
  EXPECT_EQ(n.endpoints[1].gate, "g2");
  EXPECT_TRUE(n.terminals.empty());
  const SchematicNet& y = model.Nets()[3];
  EXPECT_EQ(y.name, "y");
  ASSERT_EQ(y.terminals.size(), 1u);
  EXPECT_EQ(y.terminals[0], "y");
}

TEST(SchematicModel, ParsesAdderAndFlatPayloads) {
  SchematicModel adder;
  ASSERT_TRUE(adder.LoadPayload(kSchematicAdderPayload));
  EXPECT_EQ(adder.Module(), "adder");
  ASSERT_EQ(adder.Gates().size(), 1u);
  EXPECT_EQ(adder.Gates()[0].type, "$add");
  ASSERT_EQ(adder.Nets().size(), 3u);
  SchematicModel flat;
  ASSERT_TRUE(flat.LoadPayload(kSchematicFlatPayload));
  EXPECT_EQ(flat.Module(), "optloop");
  ASSERT_EQ(flat.Gates().size(), 3u);
  ASSERT_EQ(flat.Nets().size(), 8u);
  // A net with no gate endpoints, only a terminal, is legal:
  // y2 is a pass-through parent port in this payload.
  EXPECT_TRUE(flat.Nets()[7].endpoints.empty());
}

TEST(SchematicModel, ParsesAutoNamedGatesWithColons) {
  // Yosys auto-generates primitive cell names that contain
  // colons (file:line form), so endpoints split at the last
  // colon: the gate keeps its colons, the port is the tail.
  SchematicModel model;
  ASSERT_TRUE(model.LoadPayload(
      "module=optloop\n"
      "parent port=a direction=in\n"
      "parent port=y1 direction=out\n"
      "gate instance=$and$optloop.v:4$1 type=$and "
      "ports=A:in,B:in,Y:out\n"
      "gate instance=$and$optloop.v:4$2 type=$and "
      "ports=A:in,B:in,Y:out\n"
      "net name=a gates=$and$optloop.v:4$1:A terminals=a\n"
      "net name=n gates=$and$optloop.v:4$1:Y,$and$optloop.v:4$2:A "
      "terminals=\n"
      "net name=y1 gates=$and$optloop.v:4$2:Y terminals=y1\n"));
  ASSERT_EQ(model.Gates().size(), 2u);
  EXPECT_EQ(model.Gates()[0].instance, "$and$optloop.v:4$1");
  ASSERT_EQ(model.Nets().size(), 3u);
  const SchematicNet& n = model.Nets()[1];
  EXPECT_EQ(n.name, "n");
  ASSERT_EQ(n.endpoints.size(), 2u);
  EXPECT_EQ(n.endpoints[0].gate, "$and$optloop.v:4$1");
  EXPECT_EQ(n.endpoints[0].port, "Y");
  EXPECT_EQ(n.endpoints[1].gate, "$and$optloop.v:4$2");
  EXPECT_EQ(n.endpoints[1].port, "A");
}

TEST(SchematicModel, RejectsMalformedPayloads) {
  const char* bad[] = {
      // No module line.
      "parent port=a direction=in\n",
      // Empty module.
      "module=\n",
      // Unknown key.
      "module=leaf\n"
      "gate instance=g type=$and ports=A:in extra=1\n",
      // Duplicate gate.
      "module=leaf\n"
      "gate instance=g type=$and ports=A:in,Y:out\n"
      "gate instance=g type=$and ports=A:in,Y:out\n",
      // Duplicate net.
      "module=leaf\n"
      "parent port=a direction=in\n"
      "net name=n gates= terminals=a\n"
      "net name=n gates= terminals=a\n",
      // Empty gate type.
      "module=leaf\n"
      "gate instance=g type= ports=A:in\n",
      // Endpoint on an unknown gate.
      "module=leaf\n"
      "parent port=a direction=in\n"
      "net name=n gates=nope:A terminals=\n",
      // Endpoint on an unknown port.
      "module=leaf\n"
      "gate instance=g type=$and ports=A:in\n"
      "net name=n gates=g:nope terminals=\n",
      // Terminal on an unknown parent port.
      "module=leaf\n"
      "net name=n gates= terminals=nope\n",
      // Unknown direction on a parent port.
      "module=leaf\n"
      "parent port=a direction=sideways\n",
      // Gate ports use in and out only, never inout.
      "module=leaf\n"
      "gate instance=g type=$and ports=A:inout\n",
      // Port without a direction.
      "module=leaf\n"
      "gate instance=g type=$and ports=A\n",
      // Net with neither endpoints nor terminals.
      "module=leaf\n"
      "net name=n gates= terminals=\n",
      // Empty net name.
      "module=leaf\n"
      "net name= gates= terminals=a\n",
  };
  for (const char* payload : bad) {
    SchematicModel model;
    EXPECT_FALSE(model.LoadPayload(payload)) << "payload: " << payload;
    EXPECT_TRUE(model.Empty());
  }
}

TEST(SchematicModel, RejectsMalformedKeepsPreviousSchematic) {
  SchematicModel model;
  ASSERT_TRUE(model.LoadPayload(kSchematicLeafPayload));
  EXPECT_FALSE(model.LoadPayload("garbage\n"));
  EXPECT_EQ(model.Module(), "leaf");
  EXPECT_EQ(model.Gates().size(), 2u);
}

TEST(SchematicModel, ReloadReplacesPreviousSchematic) {
  SchematicModel model;
  ASSERT_TRUE(model.LoadPayload(kSchematicLeafPayload));
  ASSERT_TRUE(model.LoadPayload(kSchematicAdderPayload));
  EXPECT_EQ(model.Module(), "adder");
  EXPECT_EQ(model.Gates().size(), 1u);
}

void RegisterSchematicCommand(CommandRegistry& registry,
                              std::vector<std::string>* seen_module) {
  auto status = registry.Register(
      "synth_schematic", "synth_schematic <module>",
      [seen_module](CommandContext&,
                    const std::vector<std::string>& args) {
        if (args.size() != 1) {
          return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                     "need a module");
        }
        if (seen_module != nullptr) *seen_module = args;
        if (args[0] == "adder") {
          return CommandResult::Ok(kSchematicAdderPayload);
        }
        if (args[0] == "optloop") {
          return CommandResult::Ok(kSchematicFlatPayload);
        }
        return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                   "unknown module");
      });
  EXPECT_TRUE(status.ok());
}

TEST(GuiSessionView, DiagramLevelIsLeafFollowsTheTree) {
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kNestedPayload, true);
  CommandContext context;
  context.synth.elaborated = true;
  SessionView view(registry, context);
  EXPECT_FALSE(view.DiagramLevelIsLeaf());
  view.RefreshHierarchy();
  ASSERT_TRUE(view.SetDiagramLevel("top"));
  EXPECT_FALSE(view.DiagramLevelIsLeaf());
  ASSERT_TRUE(view.SetDiagramLevel("top/u_mid"));
  EXPECT_FALSE(view.DiagramLevelIsLeaf());
  ASSERT_TRUE(view.SetDiagramLevel("top/u_mid/u_add"));
  EXPECT_TRUE(view.DiagramLevelIsLeaf());
}

TEST(GuiSessionView, RefreshSchematicPopulatesModelWithoutEcho) {
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kNestedPayload, true);
  RegisterBlocksCommand(registry, nullptr);
  std::vector<std::string> seen_module;
  RegisterSchematicCommand(registry, &seen_module);
  CommandContext context;
  context.synth.elaborated = true;
  context.synth.top_cell = "top";
  SessionView view(registry, context);
  view.RefreshHierarchy();
  ASSERT_TRUE(view.SetDiagramLevel("top/u_mid/u_add"));
  view.RefreshDiagram();
  view.RefreshSchematic();
  EXPECT_TRUE(view.DiagramLevelIsLeaf());
  EXPECT_EQ(view.Schematic().Module(), "adder");
  ASSERT_EQ(view.Schematic().Gates().size(), 1u);
  EXPECT_EQ(view.Schematic().Gates()[0].type, "$add");
  ASSERT_EQ(seen_module.size(), 1u);
  EXPECT_EQ(seen_module[0], "adder");
  EXPECT_TRUE(view.EchoLog().empty());
}

TEST(GuiSessionView, RefreshSchematicClearsWhenLevelIsNotLeaf) {
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kNestedPayload, true);
  RegisterBlocksCommand(registry, nullptr);
  RegisterSchematicCommand(registry, nullptr);
  CommandContext context;
  context.synth.elaborated = true;
  SessionView view(registry, context);
  view.RefreshHierarchy();
  ASSERT_TRUE(view.SetDiagramLevel("top/u_mid/u_add"));
  view.RefreshSchematic();
  ASSERT_FALSE(view.Schematic().Empty());
  ASSERT_TRUE(view.SetDiagramLevel("top/u_mid"));
  view.RefreshSchematic();
  EXPECT_TRUE(view.Schematic().Empty());
}

TEST(GuiSessionView, RefreshSchematicFallsBackToRootWhenLevelDrops) {
  CommandRegistry registry;
  std::string hierarchy_payload = kNestedPayload;
  RegisterMutableHierarchyCommand(registry, hierarchy_payload);
  RegisterBlocksCommand(registry, nullptr);
  RegisterSchematicCommand(registry, nullptr);
  CommandContext context;
  context.synth.elaborated = true;
  context.synth.top_cell = "top";
  SessionView view(registry, context);
  view.RefreshHierarchy();
  ASSERT_TRUE(view.SetDiagramLevel("top/u_mid/u_add"));
  view.RefreshSchematic();
  EXPECT_EQ(view.Schematic().Module(), "adder");
  hierarchy_payload = kFlatPayload;
  view.RefreshHierarchy();
  view.RefreshSchematic();
  EXPECT_EQ(view.DiagramLevel(), "optloop");
  EXPECT_EQ(view.Schematic().Module(), "optloop");
  EXPECT_TRUE(view.DiagramLevelIsLeaf());
}

TEST(GuiSessionView, RefreshSchematicClearsWhenNotElaborated) {
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kNestedPayload, true);
  RegisterBlocksCommand(registry, nullptr);
  RegisterSchematicCommand(registry, nullptr);
  CommandContext context;
  context.synth.elaborated = true;
  SessionView view(registry, context);
  view.RefreshHierarchy();
  ASSERT_TRUE(view.SetDiagramLevel("top/u_mid/u_add"));
  view.RefreshSchematic();
  ASSERT_FALSE(view.Schematic().Empty());
  context.synth.elaborated = false;
  view.RefreshSchematic();
  EXPECT_TRUE(view.Schematic().Empty());
  EXPECT_TRUE(view.DiagramLevel().empty());
}

TEST(GuiSessionView, RefreshSchematicClearsOnFailedQuery) {
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kNestedPayload, true);
  RegisterBlocksCommand(registry, nullptr);
  RegisterSchematicCommand(registry, nullptr);
  CommandContext context;
  context.synth.elaborated = true;
  SessionView view(registry, context);
  view.RefreshHierarchy();
  ASSERT_TRUE(view.SetDiagramLevel("top/u_mid/u_add"));
  view.RefreshSchematic();
  ASSERT_FALSE(view.Schematic().Empty());
  CommandRegistry failing;
  RegisterHierarchyCommand(failing, kNestedPayload, true);
  auto status = failing.Register(
      "synth_schematic", "synth_schematic <module>",
      [](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                   "no elaborated design");
      });
  EXPECT_TRUE(status.ok());
  CommandContext failing_ctx;
  failing_ctx.synth.elaborated = true;
  SessionView failing_view(failing, failing_ctx);
  failing_view.RefreshHierarchy();
  ASSERT_TRUE(failing_view.SetDiagramLevel("top/u_mid/u_add"));
  failing_view.RefreshSchematic();
  EXPECT_TRUE(failing_view.Schematic().Empty());
  EXPECT_EQ(failing_view.DiagramLevel(), "top/u_mid/u_add");
}

TEST(GuiSessionView, RefreshSchematicClearsOnMalformedPayload) {
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kNestedPayload, true);
  auto status = registry.Register(
      "synth_schematic", "synth_schematic <module>",
      [](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Ok("garbage\n");
      });
  EXPECT_TRUE(status.ok());
  CommandContext context;
  context.synth.elaborated = true;
  SessionView view(registry, context);
  view.RefreshHierarchy();
  ASSERT_TRUE(view.SetDiagramLevel("top/u_mid/u_add"));
  view.RefreshSchematic();
  EXPECT_TRUE(view.Schematic().Empty());
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

const char kStatsPayload[] =
    "module=optloop\n"
    "cells=6 wires=10 depth=3 sequential=0\n"
    "type $and 6\n";

const char kStatsEmptyPayload[] =
    "module=empty\n"
    "cells=0 wires=0 depth=0 sequential=0\n";

TEST(LiveStatsModel, ParsesPayload) {
  LiveStatsModel model;
  ASSERT_TRUE(model.LoadPayload(kStatsPayload));
  EXPECT_FALSE(model.Empty());
  EXPECT_EQ(model.Module(), "optloop");
  EXPECT_EQ(model.Cells(), 6);
  EXPECT_EQ(model.Wires(), 10);
  EXPECT_EQ(model.Depth(), 3);
  EXPECT_EQ(model.Sequential(), 0);
  ASSERT_EQ(model.Types().size(), 1u);
  EXPECT_EQ(model.Types()[0].type, "$and");
  EXPECT_EQ(model.Types()[0].count, 6);
}

TEST(LiveStatsModel, ParsesEmptyDesign) {
  LiveStatsModel model;
  ASSERT_TRUE(model.LoadPayload(kStatsEmptyPayload));
  EXPECT_EQ(model.Module(), "empty");
  EXPECT_EQ(model.Cells(), 0);
  EXPECT_TRUE(model.Types().empty());
}

TEST(LiveStatsModel, RejectsMalformedPayloads) {
  const char* bad[] = {
      "",
      "module=optloop\n",
      "cells=6 wires=10 depth=3 sequential=0\n",
      "module=optloop\ncells=6 wires=10 depth=3\ntype $and 6\n",
      "module=optloop\ncells=6 wires=10 depth=3 sequential=x\n",
      "module=optloop\ncells=6 wires=10 depth=-3 sequential=0\n",
      "module=optloop\ncells=6 wires=10 depth=3 sequential=0\n"
      "type $and\n",
      "module=optloop\ncells=6 wires=10 depth=3 sequential=0\n"
      "type $and 6 extra\n",
      "module=optloop\ncells=6 wires=10 depth=3 sequential=0\n"
      "type $and 6\ntype $and 1\n",
      "module=optloop\ncells=6 wires=10 depth=3 sequential=0\n"
      "type $dff 2\ntype $and 6\n",
      "module=optloop\ncells=6 wires=10 depth=3 sequential=0\n"
      "gate $and 6\n",
      "module=optloop\ncells=6 wires=10 depth=3 sequential=0\n\n"
      "type $and 6\n",
  };
  for (const char* payload : bad) {
    LiveStatsModel model;
    EXPECT_FALSE(model.LoadPayload(payload)) << "payload: " << payload;
    EXPECT_TRUE(model.Empty());
  }
}

TEST(LiveStatsModel, RejectsMalformedKeepsPrevious) {
  LiveStatsModel model;
  ASSERT_TRUE(model.LoadPayload(kStatsPayload));
  EXPECT_FALSE(model.LoadPayload("garbage\n"));
  EXPECT_EQ(model.Module(), "optloop");
  EXPECT_EQ(model.Cells(), 6);
  ASSERT_EQ(model.Types().size(), 1u);
  EXPECT_EQ(model.Types()[0].type, "$and");
}

TEST(LiveStatsModel, ReloadReplacesPrevious) {
  LiveStatsModel model;
  ASSERT_TRUE(model.LoadPayload(kStatsPayload));
  ASSERT_TRUE(model.LoadPayload(kStatsEmptyPayload));
  EXPECT_EQ(model.Module(), "empty");
  EXPECT_EQ(model.Cells(), 0);
  EXPECT_TRUE(model.Types().empty());
}

void RegisterStatsCommand(CommandRegistry& registry,
                          const std::string& payload, bool ok) {
  auto status = registry.Register(
      "synth_stats", "synth_stats",
      [payload, ok](CommandContext&, const std::vector<std::string>&) {
        if (!ok) {
          return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                     "no elaborated design");
        }
        return CommandResult::Ok(payload);
      });
  EXPECT_TRUE(status.ok());
}

void FillPanelsContext(CommandContext& context) {
  context.synth.parsed = true;
  context.synth.top_cell = "optloop";
  context.synth.verilog_path = "demo/synth/optloop.v";
  context.synth.elaborated = true;
  context.synth.elaborated_cells = 6;
  context.synth.elaborated_wires = 10;
  context.synth.elaborated_depth = 3;
  context.synth.generic_optimized = true;
  context.synth.generic_script = "default";
  context.synth.generic_before_and = 6;
  context.synth.generic_before_levels = 3;
  context.synth.generic_after_and = 3;
  context.synth.generic_after_levels = 2;
  context.synth.generic_verilog = "demo/synth/optloop_generic.v";
}

TEST(GuiSessionView, RefreshPanelsPopulatesFromRecordAndStatsWithoutEcho) {
  CommandRegistry registry;
  RegisterStatsCommand(registry, kStatsPayload, true);
  CommandContext context;
  FillPanelsContext(context);
  SessionView view(registry, context);
  view.RefreshPanels();
  const PanelsModel& panels = view.Panels();
  EXPECT_TRUE(panels.parsed);
  EXPECT_EQ(panels.top, "optloop");
  EXPECT_EQ(panels.verilog_path, "demo/synth/optloop.v");
  EXPECT_TRUE(panels.filelist_path.empty());
  EXPECT_TRUE(panels.filelist_files.empty());
  EXPECT_TRUE(panels.elaborated);
  EXPECT_EQ(panels.elaborated_cells, 6);
  EXPECT_EQ(panels.elaborated_wires, 10);
  EXPECT_EQ(panels.elaborated_depth, 3);
  EXPECT_TRUE(panels.generic_run);
  EXPECT_EQ(panels.generic_script, "default");
  EXPECT_EQ(panels.generic_before_and, 6);
  EXPECT_EQ(panels.generic_before_levels, 3);
  EXPECT_EQ(panels.generic_after_and, 3);
  EXPECT_EQ(panels.generic_after_levels, 2);
  EXPECT_EQ(panels.SavedAnd(), 3);
  EXPECT_EQ(panels.SavedLevels(), 1);
  EXPECT_EQ(panels.generic_verilog, "demo/synth/optloop_generic.v");
  EXPECT_FALSE(panels.live_stats.Empty());
  EXPECT_EQ(panels.live_stats.Module(), "optloop");
  EXPECT_EQ(panels.live_stats.Cells(), 6);
  EXPECT_EQ(panels.live_stats.Wires(), 10);
  EXPECT_EQ(panels.live_stats.Depth(), 3);
  ASSERT_EQ(panels.live_stats.Types().size(), 1u);
  EXPECT_EQ(panels.live_stats.Types()[0].type, "$and");
  EXPECT_EQ(panels.live_stats.Types()[0].count, 6);
  EXPECT_TRUE(view.EchoLog().empty());
  EXPECT_TRUE(view.Messages().empty());
}

TEST(GuiSessionView, RefreshPanelsFilelistShape) {
  CommandRegistry registry;
  RegisterStatsCommand(registry, kStatsPayload, true);
  CommandContext context;
  context.synth.parsed = true;
  context.synth.top_cell = "top";
  context.synth.filelist_path = "demo/synth/design.f";
  context.synth.filelist_files = {"a.v", "b.v"};
  context.synth.elaborated = true;
  SessionView view(registry, context);
  view.RefreshPanels();
  const PanelsModel& panels = view.Panels();
  EXPECT_TRUE(panels.parsed);
  EXPECT_EQ(panels.top, "top");
  EXPECT_TRUE(panels.verilog_path.empty());
  EXPECT_EQ(panels.filelist_path, "demo/synth/design.f");
  ASSERT_EQ(panels.filelist_files.size(), 2u);
  EXPECT_EQ(panels.filelist_files[0], "a.v");
  EXPECT_EQ(panels.filelist_files[1], "b.v");
}

TEST(GuiSessionView, RefreshPanelsClearsWhenNotParsed) {
  CommandRegistry registry;
  RegisterStatsCommand(registry, kStatsPayload, true);
  CommandContext context;
  FillPanelsContext(context);
  SessionView view(registry, context);
  view.RefreshPanels();
  ASSERT_TRUE(view.Panels().parsed);
  context.synth.parsed = false;
  view.RefreshPanels();
  EXPECT_FALSE(view.Panels().parsed);
  EXPECT_FALSE(view.Panels().elaborated);
  EXPECT_FALSE(view.Panels().generic_run);
  EXPECT_TRUE(view.Panels().live_stats.Empty());
}

TEST(GuiSessionView, RefreshPanelsClearsLiveStatsWhenNotElaborated) {
  CommandRegistry registry;
  RegisterStatsCommand(registry, kStatsPayload, true);
  CommandContext context;
  FillPanelsContext(context);
  SessionView view(registry, context);
  view.RefreshPanels();
  ASSERT_FALSE(view.Panels().live_stats.Empty());
  context.synth.elaborated = false;
  view.RefreshPanels();
  EXPECT_TRUE(view.Panels().parsed);
  EXPECT_FALSE(view.Panels().elaborated);
  EXPECT_FALSE(view.Panels().generic_run);
  EXPECT_TRUE(view.Panels().live_stats.Empty());
}

TEST(GuiSessionView, RefreshPanelsKeepsRecordClearsLiveStatsOnFailedQuery) {
  CommandRegistry registry;
  RegisterStatsCommand(registry, "", false);
  CommandContext context;
  FillPanelsContext(context);
  SessionView view(registry, context);
  view.RefreshPanels();
  EXPECT_TRUE(view.Panels().parsed);
  EXPECT_TRUE(view.Panels().elaborated);
  EXPECT_EQ(view.Panels().elaborated_cells, 6);
  EXPECT_TRUE(view.Panels().generic_run);
  EXPECT_TRUE(view.Panels().live_stats.Empty());
}

TEST(GuiSessionView, RefreshPanelsKeepsRecordClearsLiveStatsOnMalformedPayload) {
  CommandRegistry registry;
  RegisterStatsCommand(registry, "garbage\n", true);
  CommandContext context;
  FillPanelsContext(context);
  SessionView view(registry, context);
  view.RefreshPanels();
  EXPECT_TRUE(view.Panels().parsed);
  EXPECT_TRUE(view.Panels().elaborated);
  EXPECT_TRUE(view.Panels().generic_run);
  EXPECT_TRUE(view.Panels().live_stats.Empty());
}

TEST(GuiSessionView, DispatchRecordsMessageOnFailure) {
  CommandRegistry registry;
  CommandContext context;
  SessionView view(registry, context);
  const CommandResult result = view.Dispatch("nope", {});
  EXPECT_FALSE(result.ok);
  ASSERT_EQ(view.Messages().size(), 1u);
  EXPECT_EQ(view.Messages()[0],
            "eos::nope: EOS invalid_command: unknown command: eos::nope");
  ASSERT_EQ(view.EchoLog().size(), 1u);
  EXPECT_EQ(view.EchoLog()[0], "eos::nope");
}

TEST(GuiSessionView, DispatchRecordsNoMessageOnSuccess) {
  CommandRegistry registry;
  RegisterPing(registry);
  CommandContext context;
  SessionView view(registry, context);
  const CommandResult result = view.Dispatch("ping", {});
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(view.Messages().empty());
}

TEST(GuiSessionView, MessagesSurviveRefreshPanels) {
  CommandRegistry registry;
  RegisterStatsCommand(registry, kStatsPayload, true);
  CommandContext context;
  FillPanelsContext(context);
  SessionView view(registry, context);
  ASSERT_FALSE(view.Dispatch("nope", {}).ok);
  ASSERT_EQ(view.Messages().size(), 1u);
  view.RefreshPanels();
  ASSERT_EQ(view.Messages().size(), 1u);
  EXPECT_EQ(view.Messages()[0],
            "eos::nope: EOS invalid_command: unknown command: eos::nope");
}

TEST(ConsoleEntry, ParsesPlainAndPrefixedNames) {
  const eos::ui::ConsoleLine plain = eos::ui::ParseConsoleLine("ping a b");
  EXPECT_TRUE(plain.ok);
  EXPECT_EQ(plain.name, "ping");
  ASSERT_EQ(plain.args.size(), 2u);
  EXPECT_EQ(plain.args[0], "a");
  EXPECT_EQ(plain.args[1], "b");
  const eos::ui::ConsoleLine prefixed =
      eos::ui::ParseConsoleLine("eos::ping a");
  EXPECT_TRUE(prefixed.ok);
  EXPECT_EQ(prefixed.name, "ping");
  ASSERT_EQ(prefixed.args.size(), 1u);
  EXPECT_EQ(prefixed.args[0], "a");
}

TEST(ConsoleEntry, ParsesQuotedArgsAndEscapes) {
  const eos::ui::ConsoleLine parsed =
      eos::ui::ParseConsoleLine("ping \"a b\" \"c\\\"d\" \"e\\\\f\"");
  EXPECT_TRUE(parsed.ok);
  EXPECT_EQ(parsed.name, "ping");
  ASSERT_EQ(parsed.args.size(), 3u);
  EXPECT_EQ(parsed.args[0], "a b");
  EXPECT_EQ(parsed.args[1], "c\"d");
  EXPECT_EQ(parsed.args[2], "e\\f");
}

TEST(ConsoleEntry, BlankAndCommentLinesAreNoOps) {
  const eos::ui::ConsoleLine blank = eos::ui::ParseConsoleLine("   ");
  EXPECT_TRUE(blank.ok);
  EXPECT_TRUE(blank.name.empty());
  const eos::ui::ConsoleLine comment = eos::ui::ParseConsoleLine("  # hello");
  EXPECT_TRUE(comment.ok);
  EXPECT_TRUE(comment.name.empty());
}

TEST(ConsoleEntry, UnterminatedQuoteFails) {
  const eos::ui::ConsoleLine parsed = eos::ui::ParseConsoleLine("ping \"abc");
  EXPECT_FALSE(parsed.ok);
  EXPECT_EQ(parsed.error, "unterminated quote");
}

TEST(GuiSessionView, DispatchEntryRunsAndEchoes) {
  CommandRegistry registry;
  RegisterPing(registry);
  CommandContext context;
  SessionView view(registry, context);
  const CommandResult result =
      view.DispatchEntry("eos::ping \"a b\" plain");
  EXPECT_TRUE(result.ok);
  ASSERT_EQ(view.EchoLog().size(), 1u);
  EXPECT_EQ(view.EchoLog()[0], "eos::ping \"a b\" plain");
}

TEST(GuiSessionView, DispatchEntryBlankRecordsNothing) {
  CommandRegistry registry;
  RegisterPing(registry);
  CommandContext context;
  SessionView view(registry, context);
  EXPECT_TRUE(view.DispatchEntry("   ").ok);
  EXPECT_TRUE(view.EchoLog().empty());
  EXPECT_TRUE(view.Messages().empty());
}

TEST(GuiSessionView, DispatchEntryMalformedEchoesAndMessages) {
  CommandRegistry registry;
  CommandContext context;
  SessionView view(registry, context);
  const CommandResult result = view.DispatchEntry("ping \"abc");
  EXPECT_FALSE(result.ok);
  ASSERT_EQ(view.EchoLog().size(), 1u);
  EXPECT_EQ(view.EchoLog()[0], "ping \"abc");
  ASSERT_EQ(view.Messages().size(), 1u);
  EXPECT_EQ(view.Messages()[0],
            "ping \"abc: EOS invalid_command: unterminated quote");
}

void RegisterCheckpointStubs(CommandRegistry& registry) {
  auto one_arg = [](CommandContext&, const std::vector<std::string>& args) {
    if (args.size() != 1) {
      return CommandResult::Fail(eos::ui::Diag::kInvalidCommand, "want 1 arg");
    }
    return CommandResult::Ok("ok=1");
  };
  auto no_arg = [](CommandContext&, const std::vector<std::string>& args) {
    if (!args.empty()) {
      return CommandResult::Fail(eos::ui::Diag::kInvalidCommand, "want 0 args");
    }
    return CommandResult::Ok("ok=1");
  };
  EXPECT_TRUE(registry.Register("checkpoint", "checkpoint <path>", one_arg)
                  .ok());
  EXPECT_TRUE(
      registry.Register("restore", "restore <path>", one_arg).ok());
  EXPECT_TRUE(
      registry.Register("close_design", "close_design", no_arg).ok());
}

TEST(GuiSessionView, CheckpointNameTracksSuccessfulDispatches) {
  CommandRegistry registry;
  RegisterCheckpointStubs(registry);
  CommandContext context;
  SessionView view(registry, context);
  EXPECT_TRUE(view.CheckpointName().empty());
  EXPECT_TRUE(view.Dispatch("checkpoint", {"/tmp/a.ckpt"}).ok);
  EXPECT_EQ(view.CheckpointName(), "/tmp/a.ckpt");
  EXPECT_TRUE(view.Dispatch("restore", {"/tmp/b.ckpt"}).ok);
  EXPECT_EQ(view.CheckpointName(), "/tmp/b.ckpt");
  EXPECT_TRUE(view.Dispatch("close_design", {}).ok);
  EXPECT_TRUE(view.CheckpointName().empty());
}

TEST(GuiSessionView, CheckpointNameIgnoresFailures) {
  CommandRegistry registry;
  RegisterCheckpointStubs(registry);
  CommandContext context;
  SessionView view(registry, context);
  EXPECT_TRUE(view.Dispatch("checkpoint", {"/tmp/a.ckpt"}).ok);
  EXPECT_FALSE(view.Dispatch("checkpoint", {}).ok);
  EXPECT_EQ(view.CheckpointName(), "/tmp/a.ckpt");
}

}  // namespace
