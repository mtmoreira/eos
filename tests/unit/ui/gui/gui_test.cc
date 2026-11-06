// gui: SessionView unit tests (Qt-free except the guarded
// splash timing tests below; no engines needed).
#include <algorithm>
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

#ifdef EOS_UI_GUI_HAVE_QT6
#include "splash_screen.h"
#endif

namespace {

using eos::ui::CommandContext;
using eos::ui::CommandRegistry;
using eos::ui::CommandResult;
using eos::ui::BreadcrumbPaths;
using eos::ui::ComputeSchematicLayout;
using eos::ui::DiagramBlock;
using eos::ui::DiagramModel;
using eos::ui::DiagramNet;
using eos::ui::DisplayInstance;
using eos::ui::DisplayType;
using eos::ui::FaninCone;
using eos::ui::FanoutCone;
using eos::ui::GateSymbol;
using eos::ui::HierarchyModel;
using eos::ui::HierarchyNode;
using eos::ui::LiveStatsModel;
using eos::ui::NetLabel;
using eos::ui::PanelsModel;
using eos::ui::SchematicGate;
using eos::ui::SchematicLayout;
using eos::ui::SchematicLevels;
using eos::ui::SchematicModel;
using eos::ui::SchematicNet;
using eos::ui::SchematicPoint;
using eos::ui::SessionView;
using eos::ui::StepStatus;
using eos::ui::SymbolForType;
using eos::ui::kSchematicColumnGap;
using eos::ui::kSchematicGateHeight;
using eos::ui::kSchematicGateTop;
using eos::ui::kSchematicGateX;
using eos::ui::kSchematicMinGateWidth;

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

// A sequential source: a flip-flop whose Q feeds an AND, so
// the flop is level 0 and the AND is level 1 even though the
// flop's own data input comes from a parent terminal.
const char kSchematicSeqPayload[] =
    "module=seq\n"
    "parent port=clk direction=in\n"
    "parent port=d direction=in\n"
    "parent port=q direction=out\n"
    "gate instance=ff type=$adff ports=D:in,CLK:in,Q:out\n"
    "gate instance=g1 type=$and ports=A:in,B:in,Y:out\n"
    "net name=clk gates=ff:CLK terminals=clk\n"
    "net name=d gates=ff:D terminals=d\n"
    "net name=qff gates=ff:Q,g1:A terminals=\n"
    "net name=y gates=g1:Y terminals=q\n";

// A barycenter case: two level-0 gates feed two level-1 gates
// crosswise in model order, so the level-1 order must flip to
// line each gate up with its driver.
const char kSchematicBaryPayload[] =
    "module=bary\n"
    "parent port=a direction=in\n"
    "parent port=b direction=in\n"
    "parent port=y direction=out\n"
    "parent port=z direction=out\n"
    "gate instance=g_top type=$and ports=A:in,B:in,Y:out\n"
    "gate instance=g_bot type=$and ports=A:in,B:in,Y:out\n"
    "gate instance=g_x type=$and ports=A:in,B:in,Y:out\n"
    "gate instance=g_y type=$and ports=A:in,B:in,Y:out\n"
    "net name=a gates=g_top:A terminals=a\n"
    "net name=b gates=g_bot:A terminals=b\n"
    "net name=t gates=g_top:Y,g_y:A terminals=\n"
    "net name=u gates=g_bot:Y,g_x:A terminals=\n"
    "net name=y gates=g_y:Y terminals=y\n"
    "net name=z gates=g_x:Y terminals=z\n";

// A fanout case: one AND drives two NOTs, so its output net
// branches and needs exactly one branch dot.
const char kSchematicFanoutPayload[] =
    "module=fan\n"
    "parent port=a direction=in\n"
    "parent port=b direction=in\n"
    "parent port=y direction=out\n"
    "parent port=z direction=out\n"
    "gate instance=g1 type=$and ports=A:in,B:in,Y:out\n"
    "gate instance=g2 type=$not ports=A:in,Y:out\n"
    "gate instance=g3 type=$not ports=A:in,Y:out\n"
    "net name=a gates=g1:A terminals=a\n"
    "net name=b gates=g1:B terminals=b\n"
    "net name=n gates=g1:Y,g2:A,g3:A terminals=\n"
    "net name=y gates=g2:Y terminals=y\n"
    "net name=z gates=g3:Y terminals=z\n";

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

TEST(SchematicDisplay, SymbolSelectionPerType) {
  EXPECT_EQ(SymbolForType("$and"), GateSymbol::kAnd);
  EXPECT_EQ(SymbolForType("$nand"), GateSymbol::kNand);
  EXPECT_EQ(SymbolForType("$or"), GateSymbol::kOr);
  EXPECT_EQ(SymbolForType("$nor"), GateSymbol::kNor);
  EXPECT_EQ(SymbolForType("$xor"), GateSymbol::kXor);
  EXPECT_EQ(SymbolForType("$xnor"), GateSymbol::kXnor);
  EXPECT_EQ(SymbolForType("$not"), GateSymbol::kNot);
  EXPECT_EQ(SymbolForType("$inv"), GateSymbol::kNot);
  EXPECT_EQ(SymbolForType("$buf"), GateSymbol::kBuf);
  EXPECT_EQ(SymbolForType("$AND"), GateSymbol::kAnd);
  EXPECT_EQ(SymbolForType("and"), GateSymbol::kAnd);
  EXPECT_EQ(SymbolForType("$add"), GateSymbol::kGeneric);
  EXPECT_EQ(SymbolForType("$dff"), GateSymbol::kGeneric);
  EXPECT_EQ(SymbolForType(""), GateSymbol::kGeneric);
}

TEST(SchematicDisplay, TypeLabelsDropDollarPrefix) {
  EXPECT_EQ(DisplayType("$and"), "AND");
  EXPECT_EQ(DisplayType("$not"), "NOT");
  EXPECT_EQ(DisplayType("$add"), "ADD");
  EXPECT_EQ(DisplayType("and"), "AND");
  EXPECT_EQ(DisplayType("$xnor"), "XNOR");
  EXPECT_EQ(DisplayType("$buf"), "BUF");
}

TEST(SchematicDisplay, InstanceLabelsKeepPlainNames) {
  EXPECT_EQ(DisplayInstance("g1"), "g1");
  EXPECT_EQ(DisplayInstance("g_abc"), "g_abc");
  EXPECT_EQ(DisplayInstance("add"), "add");
}

TEST(SchematicDisplay, InstanceLabelsDropAutoNamePaths) {
  // The owner's failing case: a synth temp path as the
  // auto-name file. No path, temp name, or line survives.
  EXPECT_EQ(
      DisplayInstance(
          "$and$/var/folders/kmj_qby98x6hv_n7918dfp7mcw000gn/T/"
          "eos_synth_generic_49994_1v:1085"),
      "");
  EXPECT_EQ(
      DisplayInstance(
          "$and$/var/folders/kmj_qby98x6hv_n7918dfp7mcw000gn/T/"
          "eos_synth_generic_49994_1v:1085$3"),
      "#3");
  // A real source file drops the same way: only the counter.
  EXPECT_EQ(DisplayInstance("$and$optloop.v:4$1"), "#1");
  EXPECT_EQ(DisplayInstance("$and$optloop.v:4"), "");
  // A non-auto path shows its basename without the line.
  EXPECT_EQ(DisplayInstance("a/b/c"), "c");
  EXPECT_EQ(DisplayInstance("a/b/eos_synth_x:12"), "");
  // No display label ever contains a path separator, a temp
  // marker, or a dollar prefix.
  for (const char* raw :
       {"$and$/var/folders/x/eos_synth_generic_1v:9",
        "$or$/tmp/y.v:2$7", "$xor$optloop.v:4$2"}) {
    const std::string label = DisplayInstance(raw);
    EXPECT_EQ(label.find('/'), std::string::npos);
    EXPECT_EQ(label.find("eos_synth"), std::string::npos);
    EXPECT_EQ(label.find('$'), std::string::npos);
  }
}

TEST(SchematicModel, ParsesNetWidth) {
  SchematicModel model;
  ASSERT_TRUE(model.LoadPayload(
      "module=m\n"
      "parent port=a direction=in\n"
      "parent port=y direction=out\n"
      "gate instance=g type=$and ports=A:in,Y:out\n"
      "net name=a gates=g:A terminals=a width=8\n"
      "net name=y gates=g:Y terminals=y\n"));
  ASSERT_EQ(model.Nets().size(), 2u);
  EXPECT_EQ(model.Nets()[0].width, 8);
  EXPECT_EQ(model.Nets()[1].width, 1);
  EXPECT_EQ(NetLabel(model.Nets()[0]), "a [8]");
  EXPECT_EQ(NetLabel(model.Nets()[1]), "y");
}

TEST(SchematicModel, RejectsBadNetWidth) {
  const char* bad[] = {
      // Zero width.
      "module=m\n"
      "parent port=a direction=in\n"
      "gate instance=g type=$and ports=A:in,Y:out\n"
      "net name=a gates=g:A terminals=a width=0\n",
      // Negative width.
      "module=m\n"
      "parent port=a direction=in\n"
      "gate instance=g type=$and ports=A:in,Y:out\n"
      "net name=a gates=g:A terminals=a width=-1\n",
      // Non-numeric width.
      "module=m\n"
      "parent port=a direction=in\n"
      "gate instance=g type=$and ports=A:in,Y:out\n"
      "net name=a gates=g:A terminals=a width=abc\n",
      // Fractional width.
      "module=m\n"
      "parent port=a direction=in\n"
      "gate instance=g type=$and ports=A:in,Y:out\n"
      "net name=a gates=g:A terminals=a width=1.5\n",
      // Empty width.
      "module=m\n"
      "parent port=a direction=in\n"
      "gate instance=g type=$and ports=A:in,Y:out\n"
      "net name=a gates=g:A terminals=a width=\n",
      // Unknown fourth field.
      "module=m\n"
      "parent port=a direction=in\n"
      "gate instance=g type=$and ports=A:in,Y:out\n"
      "net name=a gates=g:A terminals=a foo=1\n",
  };
  for (const char* payload : bad) {
    SchematicModel model;
    EXPECT_FALSE(model.LoadPayload(payload)) << "payload: " << payload;
    EXPECT_TRUE(model.Empty());
  }
}

TEST(SchematicDisplay, NetLabelsCarryWidth) {
  SchematicNet bus;
  bus.name = "acc";
  bus.width = 8;
  EXPECT_EQ(NetLabel(bus), "acc [8]");
  SchematicNet single;
  single.name = "n";
  single.width = 1;
  EXPECT_EQ(NetLabel(single), "n");
}

TEST(SchematicLayout, LevelsChainOnOptloop) {
  SchematicModel model;
  ASSERT_TRUE(model.LoadPayload(kSchematicFlatPayload));
  const std::map<std::string, int> levels = SchematicLevels(model);
  ASSERT_EQ(levels.size(), 3u);
  EXPECT_EQ(levels.at("g_ab"), 0);
  EXPECT_EQ(levels.at("g_abc"), 1);
  EXPECT_EQ(levels.at("g_y1"), 2);
}

TEST(SchematicLayout, SequentialOutputsAreSources) {
  SchematicModel model;
  ASSERT_TRUE(model.LoadPayload(kSchematicSeqPayload));
  const std::map<std::string, int> levels = SchematicLevels(model);
  ASSERT_EQ(levels.size(), 2u);
  EXPECT_EQ(levels.at("ff"), 0);
  EXPECT_EQ(levels.at("g1"), 1);
  EXPECT_EQ(FaninCone(model, "g1"), std::set<std::string>({"ff"}));
  EXPECT_EQ(FanoutCone(model, "ff"), std::set<std::string>({"g1"}));
}

TEST(SchematicLayout, BarycenterOrdersSharedDrivers) {
  SchematicModel model;
  ASSERT_TRUE(model.LoadPayload(kSchematicBaryPayload));
  const SchematicLayout layout =
      ComputeSchematicLayout(model, kSchematicMinGateWidth);
  ASSERT_EQ(layout.gates.size(), 4u);
  const auto* g_y = [&layout]() {
    for (const auto& gate : layout.gates) {
      if (gate.instance == "g_y") return &gate;
    }
    return static_cast<const decltype(&layout.gates[0])>(nullptr);
  }();
  const auto* g_x = [&layout]() {
    for (const auto& gate : layout.gates) {
      if (gate.instance == "g_x") return &gate;
    }
    return static_cast<const decltype(&layout.gates[0])>(nullptr);
  }();
  ASSERT_NE(g_y, nullptr);
  ASSERT_NE(g_x, nullptr);
  EXPECT_EQ(g_y->level, 1);
  EXPECT_EQ(g_x->level, 1);
  // g_y is driven by g_top (level-0 row 0) and g_x by g_bot
  // (level-0 row 1), so g_y takes the upper row even though
  // the payload lists g_x first.
  EXPECT_EQ(g_y->row, 0);
  EXPECT_EQ(g_x->row, 1);
}

TEST(SchematicLayout, ColumnsFollowLevels) {
  SchematicModel model;
  ASSERT_TRUE(model.LoadPayload(kSchematicFlatPayload));
  const SchematicLayout layout =
      ComputeSchematicLayout(model, kSchematicMinGateWidth);
  ASSERT_EQ(layout.gates.size(), 3u);
  // One gate per level, so model order is level order here.
  EXPECT_EQ(layout.gates[0].instance, "g_ab");
  EXPECT_EQ(layout.gates[0].level, 0);
  EXPECT_EQ(layout.gates[0].x, kSchematicGateX);
  EXPECT_EQ(layout.gates[1].instance, "g_abc");
  EXPECT_EQ(layout.gates[1].level, 1);
  EXPECT_EQ(layout.gates[1].x,
            kSchematicGateX + kSchematicMinGateWidth +
                kSchematicColumnGap);
  EXPECT_EQ(layout.gates[2].instance, "g_y1");
  EXPECT_EQ(layout.gates[2].level, 2);
  EXPECT_EQ(layout.gates[2].x,
            kSchematicGateX + 2 * (kSchematicMinGateWidth +
                                   kSchematicColumnGap));
  for (const auto& gate : layout.gates) {
    EXPECT_EQ(gate.row, 0);
    EXPECT_EQ(gate.height, kSchematicGateHeight);
  }
  // Output terminals sit one column gap past the last column.
  EXPECT_EQ(layout.output_x, layout.gates[2].x + kSchematicMinGateWidth +
                                  kSchematicColumnGap);
  // Input terminals face right from the left edge, output
  // terminals face left from the output column.
  ASSERT_EQ(layout.terminals.size(), 6u);
  EXPECT_EQ(layout.terminals[0].name, "a");
  EXPECT_TRUE(layout.terminals[0].input_side);
  EXPECT_EQ(layout.terminals[4].name, "y1");
  EXPECT_FALSE(layout.terminals[4].input_side);
  EXPECT_EQ(layout.terminals[4].x, layout.output_x);
}

namespace {

// Whether two route segments cross: a proper perpendicular
// crossing, or a collinear overlap with positive length.
// Touching at a shared endpoint does not count.
bool SegmentsCross(const SchematicPoint& a, const SchematicPoint& b,
                   const SchematicPoint& c, const SchematicPoint& d) {
  const bool ab_vertical = a.x == b.x;
  const bool cd_vertical = c.x == d.x;
  if (ab_vertical != cd_vertical) {
    const double x = ab_vertical ? a.x : c.x;
    const double y = ab_vertical ? c.y : a.y;
    const double hx0 = std::min(ab_vertical ? c.x : a.x,
                                ab_vertical ? d.x : b.x);
    const double hx1 = std::max(ab_vertical ? c.x : a.x,
                                ab_vertical ? d.x : b.x);
    const double vy0 = std::min(ab_vertical ? a.y : c.y,
                                ab_vertical ? b.y : d.y);
    const double vy1 = std::max(ab_vertical ? a.y : c.y,
                                ab_vertical ? b.y : d.y);
    return x > hx0 && x < hx1 && y > vy0 && y < vy1;
  }
  if (ab_vertical) {
    if (a.x != c.x) return false;
    return std::max(std::min(a.y, b.y), std::min(c.y, d.y)) <
           std::min(std::max(a.y, b.y), std::max(c.y, d.y));
  }
  if (a.y != c.y) return false;
  return std::max(std::min(a.x, b.x), std::min(c.x, d.x)) <
         std::min(std::max(a.x, b.x), std::max(c.x, d.x));
}

}  // namespace

TEST(SchematicLayout, NoCrossingsOnOptloop) {
  SchematicModel model;
  ASSERT_TRUE(model.LoadPayload(kSchematicFlatPayload));
  const SchematicLayout layout =
      ComputeSchematicLayout(model, kSchematicMinGateWidth);
  // Every routed segment is orthogonal, and no segment of one
  // net crosses a segment of another: on the 3-gate optloop
  // design the level layout has no avoidable crossings.
  for (const auto& route : layout.nets) {
    for (const auto& path : route.paths) {
      for (std::size_t i = 1; i < path.size(); ++i) {
        EXPECT_TRUE(path[i - 1].x == path[i].x ||
                    path[i - 1].y == path[i].y)
            << "route " << route.name << " has a diagonal segment";
      }
    }
  }
  for (std::size_t i = 0; i < layout.nets.size(); ++i) {
    for (std::size_t j = i + 1; j < layout.nets.size(); ++j) {
      for (const auto& first : layout.nets[i].paths) {
        for (std::size_t a = 1; a < first.size(); ++a) {
          for (const auto& second : layout.nets[j].paths) {
            for (std::size_t b = 1; b < second.size(); ++b) {
              EXPECT_FALSE(SegmentsCross(first[a - 1], first[a],
                                         second[b - 1], second[b]))
                  << "routes " << layout.nets[i].name << " and "
                  << layout.nets[j].name << " cross";
            }
          }
        }
      }
    }
  }
}

TEST(SchematicLayout, BranchDotsMarkFanout) {
  SchematicModel model;
  ASSERT_TRUE(model.LoadPayload(kSchematicFanoutPayload));
  const SchematicLayout layout =
      ComputeSchematicLayout(model, kSchematicMinGateWidth);
  const auto* fanout = [&layout]() {
    for (const auto& route : layout.nets) {
      if (route.name == "n") return &route;
    }
    return static_cast<const decltype(&layout.nets[0])>(nullptr);
  }();
  ASSERT_NE(fanout, nullptr);
  ASSERT_EQ(fanout->paths.size(), 2u);
  // One branch point: where the shared trunk meets the two
  // load branches, at the trunk x and the driver output y.
  ASSERT_EQ(fanout->branch_dots.size(), 1u);
  EXPECT_EQ(fanout->branch_dots[0].x,
            kSchematicGateX + kSchematicMinGateWidth +
                kSchematicColumnGap / 2);
  EXPECT_EQ(fanout->branch_dots[0].y,
            kSchematicGateTop + kSchematicGateHeight / 2);
  const auto* single = [&layout]() {
    for (const auto& route : layout.nets) {
      if (route.name == "a") return &route;
    }
    return static_cast<const decltype(&layout.nets[0])>(nullptr);
  }();
  ASSERT_NE(single, nullptr);
  EXPECT_TRUE(single->branch_dots.empty());
}

TEST(SchematicLayout, FaninFanoutCones) {
  SchematicModel model;
  ASSERT_TRUE(model.LoadPayload(kSchematicFlatPayload));
  EXPECT_EQ(FaninCone(model, "g_y1"),
            std::set<std::string>({"g_ab", "g_abc"}));
  EXPECT_EQ(FanoutCone(model, "g_ab"),
            std::set<std::string>({"g_abc", "g_y1"}));
  EXPECT_TRUE(FaninCone(model, "g_ab").empty());
  EXPECT_TRUE(FanoutCone(model, "g_y1").empty());
  EXPECT_TRUE(FaninCone(model, "nope").empty());
  EXPECT_TRUE(FanoutCone(model, "nope").empty());
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

TEST(ConsoleHistory, EmptyHistoryNavigatesNowhere) {
  eos::ui::ConsoleHistory history;
  EXPECT_FALSE(history.Up("draft").has_value());
  EXPECT_FALSE(history.Down().has_value());
}

TEST(ConsoleHistory, UpDownCyclesAndRestoresDraft) {
  eos::ui::ConsoleHistory history;
  history.Add("create_scenario func_tt func tt estimate");
  history.Add("elaborate");
  EXPECT_EQ(history.Up("draft").value_or(""), "elaborate");
  EXPECT_EQ(history.Up("draft").value_or(""), "create_scenario func_tt func tt estimate");
  EXPECT_EQ(history.Up("draft").value_or(""), "create_scenario func_tt func tt estimate");
  EXPECT_EQ(history.Down().value_or(""), "elaborate");
  EXPECT_EQ(history.Down().value_or(""), "draft");
  EXPECT_FALSE(history.Down().has_value());
}

TEST(ConsoleHistory, AddIgnoresEmptyAndConsecutiveDuplicates) {
  eos::ui::ConsoleHistory history;
  history.Add("");
  history.Add("elaborate");
  history.Add("elaborate");
  EXPECT_EQ(history.Up("draft").value_or(""), "elaborate");
  EXPECT_EQ(history.Up("draft").value_or(""), "elaborate");
  history.Add("synth_generic");
  EXPECT_EQ(history.Up("draft").value_or(""), "synth_generic");
  EXPECT_EQ(history.Up("draft").value_or(""), "elaborate");
}

TEST(ConsoleCompletion, MatchesBarePrefix) {
  const std::vector<std::string> names = {"create_scenario", "elaborate",
                                          "read_verilog", "synth_generic"};
  const std::vector<std::string> got =
      eos::ui::CompleteConsoleCommand(names, "ela");
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0], "elaborate");
}

TEST(ConsoleCompletion, KeepsEosPrefix) {
  const std::vector<std::string> names = {"create_scenario", "elaborate",
                                          "read_verilog", "synth_generic"};
  const std::vector<std::string> got =
      eos::ui::CompleteConsoleCommand(names, "eos::syn");
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0], "eos::synth_generic");
}

TEST(ConsoleCompletion, EmptyWordMatchesAllAndArgumentsMatchNone) {
  const std::vector<std::string> names = {"elaborate", "read_verilog"};
  EXPECT_EQ(eos::ui::CompleteConsoleCommand(names, "").size(), 2u);
  EXPECT_TRUE(eos::ui::CompleteConsoleCommand(names, "elaborate foo").empty());
  EXPECT_TRUE(eos::ui::CompleteConsoleCommand(names, "zzz").empty());
}

#ifdef EOS_UI_GUI_HAVE_QT6
// The splash minimum-visible seam is pure arithmetic on the
// SplashScreen class, so it is testable without constructing
// a widget (no QApplication needed here). The full close path
// (timer fires, splash finishes and deletes itself) runs only
// inside RunGui on the real event loop.
TEST(SplashMinimumVisible, WaitsTheFullMinimumWhenJustShown) {
  EXPECT_EQ(eos::ui::SplashScreen::kMinimumVisibleMs, 2000);
  EXPECT_EQ(eos::ui::SplashScreen::RemainingVisibleMs(0), 2000);
  EXPECT_EQ(eos::ui::SplashScreen::RemainingVisibleMs(1), 1999);
  EXPECT_EQ(eos::ui::SplashScreen::RemainingVisibleMs(500), 1500);
}

TEST(SplashMinimumVisible, ClosesAtOnceAfterTheMinimum) {
  EXPECT_EQ(eos::ui::SplashScreen::RemainingVisibleMs(1999), 1);
  EXPECT_EQ(eos::ui::SplashScreen::RemainingVisibleMs(2000), 0);
  EXPECT_EQ(eos::ui::SplashScreen::RemainingVisibleMs(2001), 0);
  EXPECT_EQ(eos::ui::SplashScreen::RemainingVisibleMs(60000), 0);
}

TEST(SplashMinimumVisible, NegativeElapsedCountsAsZero) {
  EXPECT_EQ(eos::ui::SplashScreen::RemainingVisibleMs(-5), 2000);
}
#endif

}  // namespace
