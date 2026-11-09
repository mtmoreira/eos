// gui: SessionView unit tests (Qt-free except the guarded
// splash timing tests below; no engines needed).
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "eos/ui/gui.h"
#include "eos/ui/console_entry.h"
#include "eos/ui/diagram.h"
#include "eos/ui/floorplan.h"
#include "eos/ui/hierarchy.h"
#include "eos/ui/issues.h"
#include "eos/ui/panels.h"
#include "eos/ui/registry.h"
#include "eos/ui/schematic.h"
#include "eos/ui/timing.h"
#include "eos/analysis/timing.h"
#include "gtest/gtest.h"

#ifdef EOS_UI_GUI_HAVE_QT6
#include "splash_screen.h"
#endif

#ifdef EOS_GUI_HAVE_QT6
#include <cmath>

#include <QApplication>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>

#include "block_diagram.h"
#include "center_view.h"
#include "floorplan_diagram.h"
#include "main_window.h"
#include "panels_view.h"
#include "schematic_diagram.h"
#endif

namespace {

using eos::ui::CommandContext;
using eos::ui::CommandRegistry;
using eos::ui::CommandResult;
using eos::ui::BreadcrumbPaths;
using eos::ui::ComputeSchematicLayout;
using eos::ui::ConeNetPaths;
using eos::ui::ConeNetSpokes;
using eos::ui::DiagramBlock;
using eos::ui::DiagramModel;
using eos::ui::DiagramNet;
using eos::ui::DisplayInstance;
using eos::ui::DisplayType;
using eos::ui::FaninCone;
using eos::ui::FanoutCone;
using eos::ui::FloorplanModel;
using eos::ui::FloorplanRect;
using eos::ui::PlacementModel;
using eos::ui::PlacementRect;
using eos::ui::GateSymbol;
using eos::ui::GuiIssue;
using eos::ui::HierarchyModel;
using eos::ui::HierarchyNode;
using eos::ui::IssuesModel;
using eos::ui::LiveStatsModel;
using eos::ui::MappedSynthModel;
using eos::ui::NetLabel;
using eos::ui::LogicEquation;
using eos::ui::OperatorGlyph;
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
using eos::ui::TimingClock;
using eos::ui::TimingModel;
using eos::ui::TimingScene;
using eos::ui::kSchematicColumnGap;
using eos::ui::kSchematicGateGap;
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

// A floorplan_summary payload for the bridged optloop floorplan:
// one row, six IO pins (four inputs on the left edge, two
// outputs on the right), ten track grids (one per mock routing
// layer). The pin rectangles
// are the small PORT rectangles around the placed points, which
// straddle the die edge.
const char kFloorplanPayload[] =
    "floorplan die=0,0,5000,5000 core=0,0,5000,5000 rows=1 track_grids=10 "
    "io_placed=6 io_total=6\n"
    "row rect=0,0,5000,3200\n"
    "pin name=a dir=in placed=1 rect=-50,950,50,1050\n"
    "pin name=b dir=in placed=1 rect=-50,1950,50,2050\n"
    "pin name=c dir=in placed=1 rect=-50,2950,50,3050\n"
    "pin name=d dir=in placed=1 rect=-50,3950,50,4050\n"
    "pin name=y1 dir=out placed=1 rect=4950,1950,5050,2050\n"
    "pin name=y2 dir=out placed=1 rect=4950,2950,5050,3050\n";

void RegisterFloorplanCommand(CommandRegistry& registry,
                              const std::string& payload, bool ok,
                              int* calls = nullptr) {
  auto status = registry.Register(
      "floorplan_summary", "floorplan_summary",
      [payload, ok, calls](CommandContext&,
                           const std::vector<std::string>&) {
        if (calls != nullptr) ++(*calls);
        if (!ok) {
          return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                     "no floorplan");
        }
        return CommandResult::Ok(payload);
      });
  EXPECT_TRUE(status.ok());
}

// A place_summary payload for the placed optloop design: four
// placed instances (three AND2, one BUF) in a 5000 by 3200
// core, HPWL 12345, no overlaps, and a measured post-place
// slack of 0.05 ns for the func_tt scene. The instance
// rectangles are 460 by 2720, the BUFX2 master size in the
// registry fixture, kept here as the AND2/BUF sizes for the
// demo design.
const char kPlacementPayload[] =
    "placement die=0,0,5000,5000 core=0,0,5000,3200 placed=4 total=4 "
    "hpwl=12345 overlaps=0\n"
    "inst name=u1 master=AND2 x=200 y=200 w=460 h=2720\n"
    "inst name=u2 master=AND2 x=900 y=200 w=460 h=2720\n"
    "inst name=u3 master=AND2 x=1600 y=200 w=460 h=2720\n"
    "inst name=u4 master=BUF x=2300 y=200 w=460 h=2720\n"
    "slack scene=func_tt worst_slack_ns=0.05\n";

void RegisterPlacementCommand(CommandRegistry& registry,
                              const std::string& payload, bool ok,
                              int* calls = nullptr) {
  auto status = registry.Register(
      "place_summary", "place_summary",
      [payload, ok, calls](CommandContext&,
                           const std::vector<std::string>&) {
        if (calls != nullptr) ++(*calls);
        if (!ok) {
          return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                     "no placement");
        }
        return CommandResult::Ok(payload);
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
  ASSERT_EQ(view.EchoLog().size(), 2u);
  EXPECT_EQ(view.EchoLog()[0], "eos::ping");
  EXPECT_EQ(view.EchoLog()[1], "ok");
  // The output line goes to the log, not the transcript.
  ASSERT_EQ(view.Log().size(), 2u);
  EXPECT_EQ(view.Log()[0].text, "eos::ping");
  EXPECT_EQ(view.Log()[0].severity, "info");
  EXPECT_EQ(view.Log()[1].text, "pong=1");
  EXPECT_EQ(view.Log()[1].severity, "info");
}

TEST(GuiSessionView, DispatchQuotesArgsWithSpaces) {
  CommandRegistry registry;
  RegisterPing(registry);
  CommandContext context;
  SessionView view(registry, context);
  const CommandResult result = view.Dispatch("ping", {"a b", "plain"});
  EXPECT_TRUE(result.ok);
  ASSERT_EQ(view.EchoLog().size(), 2u);
  EXPECT_EQ(view.EchoLog()[0], "eos::ping \"a b\" plain");
  EXPECT_EQ(view.EchoLog()[1], "ok");
}

TEST(GuiSessionView, DispatchUnknownStillEchoed) {
  CommandRegistry registry;
  CommandContext context;
  SessionView view(registry, context);
  const CommandResult result = view.Dispatch("nope", {});
  EXPECT_FALSE(result.ok);
  ASSERT_EQ(view.EchoLog().size(), 2u);
  EXPECT_EQ(view.EchoLog()[0], "eos::nope");
  EXPECT_EQ(view.EchoLog()[1],
            "eos::nope: EOS invalid_command: unknown command: eos::nope");
}

TEST(GuiSessionView, StepStatesFollowSessionFlags) {
  CommandRegistry registry;
  CommandContext context;
  SessionView view(registry, context);
  EXPECT_EQ(view.ReadStep(), StepStatus::kPending);
  EXPECT_EQ(view.ElaborateStep(), StepStatus::kPending);
  EXPECT_EQ(view.GenericStep(), StepStatus::kPending);
  EXPECT_EQ(view.SynthesisStep(), StepStatus::kPending);
  EXPECT_EQ(view.PlacementStep(), StepStatus::kPending);
  context.synth.parsed = true;
  EXPECT_EQ(view.ReadStep(), StepStatus::kDone);
  context.synth.elaborated = true;
  EXPECT_EQ(view.ElaborateStep(), StepStatus::kDone);
  context.synth.generic_optimized = true;
  EXPECT_EQ(view.GenericStep(), StepStatus::kDone);
  context.synth.mapped = true;
  EXPECT_EQ(view.SynthesisStep(), StepStatus::kDone);
  context.place.placed = true;
  EXPECT_EQ(view.PlacementStep(), StepStatus::kDone);
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

void RegisterOkCommand(CommandRegistry& registry,
                       const std::string& name) {
  auto status = registry.Register(
      name, name,
      [](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Ok("");
      });
  EXPECT_TRUE(status.ok());
}

TEST(GuiSessionView, DispatchNavigatesToCommandResults) {
  using Destination = SessionView::ResultDestination;
  CommandRegistry registry;
  for (const std::string& name :
       {"elaborate", "synth_generic", "synth_map", "synth_blocks",
        "synth_issues", "read_verilog", "run_timing", "report_clock",
        "create_scenario"}) {
    RegisterOkCommand(registry, name);
  }
  CommandContext context;
  SessionView view(registry, context);
  const std::pair<const char*, Destination> cases[] = {
      {"elaborate", Destination::kSynthesis},
      {"synth_generic", Destination::kOptLog},
      {"synth_map", Destination::kResults},
      {"synth_blocks", Destination::kCenter},
      {"synth_issues", Destination::kIssues},
      {"read_verilog", Destination::kSources},
      {"run_timing", Destination::kTiming},
      {"report_clock", Destination::kResults},
      {"create_scenario", Destination::kNone},
  };
  for (const auto& [name, want] : cases) {
    ASSERT_TRUE(view.Dispatch(name, {}).ok) << name;
    EXPECT_EQ(view.TakeNavigation(), want) << name;
    // Consumed once: the next take finds nothing pending.
    EXPECT_EQ(view.TakeNavigation(), Destination::kNone) << name;
  }
}

TEST(GuiSessionView, FailedDispatchNavigatesNowhere) {
  using Destination = SessionView::ResultDestination;
  CommandRegistry registry;
  CommandContext context;
  SessionView view(registry, context);
  ASSERT_FALSE(view.Dispatch("nope", {}).ok);
  EXPECT_EQ(view.TakeNavigation(), Destination::kNone);
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

// A cone fixture: u_ctrl drives u_mid through op, u_mid drives
// u_out through y, op also feeds the sibling u_side, and sel
// also feeds u_other, so the cone of u_mid has sibling branches
// that must stay out.
const char kDiagramConePayload[] =
    "module=top\n"
    "parent port=a direction=in\n"
    "parent port=b direction=in\n"
    "parent port=sel direction=in\n"
    "parent port=y direction=out\n"
    "parent port=q direction=out\n"
    "block instance=u_ctrl module=ctrl ports=sel:in,op:out\n"
    "block instance=u_mid module=mid ports=op:in,a:in,y:out\n"
    "block instance=u_out module=out ports=y:in,done:out\n"
    "block instance=u_side module=side ports=op:in,q:out\n"
    "block instance=u_other module=other ports=sel:in,r:out\n"
    "net name=a blocks=u_mid:a terminals=a\n"
    "net name=b blocks= terminals=b\n"
    "net name=sel blocks=u_ctrl:sel,u_other:sel terminals=sel\n"
    "net name=op blocks=u_ctrl:op,u_mid:op,u_side:op terminals=\n"
    "net name=y blocks=u_mid:y,u_out:y terminals=\n"
    "net name=done blocks=u_out:done terminals=y\n"
    "net name=q blocks=u_side:q terminals=q\n"
    "net name=r blocks=u_other:r terminals=\n";

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

TEST(DiagramModel, FaninFanoutCones) {
  DiagramModel model;
  ASSERT_TRUE(model.LoadPayload(kDiagramConePayload));
  EXPECT_EQ(FaninCone(model, "u_mid"), std::set<std::string>({"u_ctrl"}));
  EXPECT_EQ(FanoutCone(model, "u_mid"), std::set<std::string>({"u_out"}));
  EXPECT_TRUE(FaninCone(model, "u_ctrl").empty());
  EXPECT_EQ(FanoutCone(model, "u_ctrl"),
            (std::set<std::string>{"u_mid", "u_out", "u_side"}));
  EXPECT_TRUE(FaninCone(model, "nope").empty());
  EXPECT_TRUE(FanoutCone(model, "nope").empty());
}

TEST(DiagramModel, ConeNetSpokesHighlightsOnlyConeSpokes) {
  DiagramModel model;
  ASSERT_TRUE(model.LoadPayload(kDiagramConePayload));
  // The op spoke to u_mid is in the cone, the sibling spoke to
  // u_side is not; the sel spoke to the sel terminal is in the
  // cone, the sibling spoke to u_other is not.
  EXPECT_EQ(ConeNetSpokes(model, "u_mid"),
            (std::set<std::pair<std::string, std::size_t>>{
                {"a", 1}, {"sel", 2}, {"op", 1}, {"y", 1}, {"done", 1}}));
  EXPECT_EQ(ConeNetSpokes(model, "u_ctrl"),
            (std::set<std::pair<std::string, std::size_t>>{
                {"sel", 2},
                {"op", 1},
                {"op", 2},
                {"y", 1},
                {"done", 1},
                {"q", 1}}));
  EXPECT_TRUE(ConeNetSpokes(model, "nope").empty());
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

// A hier_alu-shaped case: one shared input net feeds the
// selected ADD and three sibling gates, and all four feed one
// PMUX, so clicking ADD must light only the ADD branch of
// each shared net, the ADD branch into the PMUX, and the
// PMUX output, while the sibling branches stay out.
const char kSchematicAluPayload[] =
    "module=alu\n"
    "parent port=op direction=in\n"
    "parent port=x direction=in\n"
    "parent port=y direction=in\n"
    "parent port=z direction=out\n"
    "gate instance=add type=$add ports=A:in,B:in,Y:out\n"
    "gate instance=and type=$and ports=A:in,B:in,Y:out\n"
    "gate instance=sub type=$sub ports=A:in,B:in,Y:out\n"
    "gate instance=xor type=$xor ports=A:in,B:in,Y:out\n"
    "gate instance=mux type=$pmux ports=A:in,B:in,C:in,D:in,S:in,Y:out\n"
    "net name=op gates=mux:S terminals=op\n"
    "net name=x gates=add:A,and:A,sub:A,xor:A terminals=x\n"
    "net name=y gates=add:B,and:B,sub:B,xor:B terminals=y\n"
    "net name=sadd gates=add:Y,mux:A terminals=\n"
    "net name=sand gates=and:Y,mux:B terminals=\n"
    "net name=ssub gates=sub:Y,mux:C terminals=\n"
    "net name=sxor gates=xor:Y,mux:D terminals=\n"
    "net name=z gates=mux:Y terminals=z\n";

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
  EXPECT_EQ(SymbolForType("$mux"), GateSymbol::kMux);
  EXPECT_EQ(SymbolForType("$pmux"), GateSymbol::kMux);
  EXPECT_EQ(SymbolForType("$dff"), GateSymbol::kDff);
  EXPECT_EQ(SymbolForType("$adff"), GateSymbol::kDff);
  EXPECT_EQ(SymbolForType("$aldff"), GateSymbol::kDff);
  EXPECT_EQ(SymbolForType("$sdff"), GateSymbol::kDff);
  EXPECT_EQ(SymbolForType("$dffe"), GateSymbol::kDff);
  EXPECT_EQ(SymbolForType("$dlatch"), GateSymbol::kLatch);
  EXPECT_EQ(SymbolForType("$sr"), GateSymbol::kLatch);
  EXPECT_EQ(SymbolForType("$logic_and"), GateSymbol::kAnd);
  EXPECT_EQ(SymbolForType("$logic_or"), GateSymbol::kOr);
  EXPECT_EQ(SymbolForType("$logic_not"), GateSymbol::kNot);
  EXPECT_EQ(SymbolForType("$reduce_and"), GateSymbol::kAnd);
  EXPECT_EQ(SymbolForType("$reduce_or"), GateSymbol::kOr);
  EXPECT_EQ(SymbolForType("$reduce_xor"), GateSymbol::kXor);
  EXPECT_EQ(SymbolForType("$reduce_xnor"), GateSymbol::kXnor);
  EXPECT_EQ(SymbolForType("$tribuf"), GateSymbol::kBuf);
  EXPECT_EQ(SymbolForType("$pos"), GateSymbol::kBuf);
  // Arithmetic and comparison cells keep the rectangle
  // shape; their operator glyph is a text line, not a shape.
  EXPECT_EQ(SymbolForType("$add"), GateSymbol::kGeneric);
  EXPECT_EQ(SymbolForType("$eq"), GateSymbol::kGeneric);
  EXPECT_EQ(SymbolForType("$mem"), GateSymbol::kGeneric);
  EXPECT_EQ(SymbolForType(""), GateSymbol::kGeneric);
}

TEST(SchematicDisplay, OperatorGlyphs) {
  EXPECT_EQ(OperatorGlyph("$add"), "+");
  EXPECT_EQ(OperatorGlyph("$sub"), "-");
  EXPECT_EQ(OperatorGlyph("$neg"), "-");
  EXPECT_EQ(OperatorGlyph("$mul"), "x");
  EXPECT_EQ(OperatorGlyph("$div"), "/");
  EXPECT_EQ(OperatorGlyph("$mod"), "%");
  EXPECT_EQ(OperatorGlyph("$eq"), "=");
  EXPECT_EQ(OperatorGlyph("$ne"), "!=");
  EXPECT_EQ(OperatorGlyph("$lt"), "<");
  EXPECT_EQ(OperatorGlyph("$gt"), ">");
  EXPECT_EQ(OperatorGlyph("$le"), "<=");
  EXPECT_EQ(OperatorGlyph("$ge"), ">=");
  EXPECT_EQ(OperatorGlyph("$shl"), "<<");
  EXPECT_EQ(OperatorGlyph("$sshl"), "<<");
  EXPECT_EQ(OperatorGlyph("$shr"), ">>");
  EXPECT_EQ(OperatorGlyph("$sshr"), ">>");
  EXPECT_EQ(OperatorGlyph("$ADD"), "+");
  EXPECT_EQ(OperatorGlyph("add"), "+");
  EXPECT_EQ(OperatorGlyph("$and"), "");
  EXPECT_EQ(OperatorGlyph("$mux"), "");
  EXPECT_EQ(OperatorGlyph("$dff"), "");
  EXPECT_EQ(OperatorGlyph("$mem"), "");
  EXPECT_EQ(OperatorGlyph(""), "");
}

TEST(SchematicDisplay, LogicEquations) {
  EXPECT_EQ(LogicEquation("$mem"), "Q = mem[A]");
  EXPECT_EQ(LogicEquation("$memrd"), "Q = mem[A]");
  EXPECT_EQ(LogicEquation("$memwr"), "mem[A] = D");
  EXPECT_EQ(LogicEquation("$fsm"), "Y = f(state, in)");
  EXPECT_EQ(LogicEquation("$lut"), "Y = lut(A)");
  EXPECT_EQ(LogicEquation("$pow"), "Y = A ** B");
  EXPECT_EQ(LogicEquation("$shift"), "Y = shift(A, B)");
  EXPECT_EQ(LogicEquation("$MEM"), "Q = mem[A]");
  EXPECT_EQ(LogicEquation("mem"), "Q = mem[A]");
  EXPECT_EQ(LogicEquation("$and"), "");
  EXPECT_EQ(LogicEquation("$add"), "");
  EXPECT_EQ(LogicEquation("$mux"), "");
  EXPECT_EQ(LogicEquation("$dff"), "");
  EXPECT_EQ(LogicEquation(""), "");
}

TEST(SchematicDisplay, NoTypeHasBothGlyphAndEquation) {
  // The third text line is one or the other, never both.
  const std::vector<std::string> types = {
      "$and", "$nand", "$or", "$nor", "$xor", "$xnor", "$not", "$buf",
      "$mux", "$pmux", "$dff", "$adff", "$aldff", "$sdff", "$dlatch",
      "$sr", "$logic_and", "$logic_or", "$logic_not", "$reduce_and",
      "$reduce_or", "$reduce_xor", "$tribuf", "$pos", "$add", "$sub",
      "$neg", "$mul", "$div", "$mod", "$eq", "$ne", "$lt", "$gt",
      "$le", "$ge", "$shl", "$shr", "$sshl", "$sshr", "$mem",
      "$memrd", "$memwr", "$fsm", "$lut", "$pow", "$shift"};
  for (const std::string& type : types) {
    EXPECT_TRUE(OperatorGlyph(type).empty() ||
                LogicEquation(type).empty())
        << "type: " << type;
  }
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

TEST(SchematicLayout, ConeNetPathsHighlightsOnlyConePaths) {
  SchematicModel model;
  ASSERT_TRUE(model.LoadPayload(kSchematicAluPayload));
  const SchematicLayout layout =
      ComputeSchematicLayout(model, kSchematicMinGateWidth);
  // The layout records one path end per path, in path order.
  const auto* x_route = [&layout]() {
    for (const auto& route : layout.nets) {
      if (route.name == "x") return &route;
    }
    return static_cast<const decltype(&layout.nets[0])>(nullptr);
  }();
  ASSERT_NE(x_route, nullptr);
  ASSERT_EQ(x_route->paths.size(), 4u);
  ASSERT_EQ(x_route->path_ends.size(), 4u);
  EXPECT_EQ(x_route->path_ends[0].driver_terminal, "x");
  EXPECT_EQ(x_route->path_ends[0].load_gate, "add");
  EXPECT_EQ(x_route->path_ends[1].load_gate, "and");
  EXPECT_EQ(x_route->path_ends[2].load_gate, "sub");
  EXPECT_EQ(x_route->path_ends[3].load_gate, "xor");
  // Clicking ADD: only the ADD branch of each shared input
  // net, the ADD branch into the PMUX, and the PMUX output.
  // The sibling branches, the sibling branches into the PMUX,
  // and the op select into the PMUX stay out.
  EXPECT_EQ(ConeNetPaths(model, layout, "add"),
            (std::set<std::pair<std::string, std::size_t>>{
                {"sadd", 0}, {"x", 0}, {"y", 0}, {"z", 0}}));
  // Clicking the PMUX lights its whole fanin: every input
  // branch, including the shared nets' sibling branches.
  std::set<std::pair<std::string, std::size_t>> mux_paths;
  for (const char* net : {"op", "x", "y", "sadd", "sand", "ssub",
                          "sxor", "z"}) {
    const auto* route = [&layout, net]() {
      for (const auto& candidate : layout.nets) {
        if (candidate.name == net) return &candidate;
      }
      return static_cast<const decltype(&layout.nets[0])>(nullptr);
    }();
    ASSERT_NE(route, nullptr) << "net: " << net;
    for (std::size_t i = 0; i < route->paths.size(); ++i) {
      mux_paths.insert({net, i});
    }
  }
  EXPECT_EQ(ConeNetPaths(model, layout, "mux"), mux_paths);
  EXPECT_TRUE(ConeNetPaths(model, layout, "nope").empty());
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

// F1: the full demo chain numbers, as synth_summary reports
// them: mapped 4 cells at -0.35 ns, loop winner classic at
// +0.05 ns, equivalence equivalent.
const char kSummaryPayload[] =
    "mapped target=area liberty=demo/synth/cells.lib "
    "verilog=/tmp/mapped.v cells=4 types=AND2:3,BUF:1 judged=true "
    "worst_slack_ns=-0.35 tns_ns=-0.35\n"
    "opt run=true winner=classic iterations=1 verilog=/tmp/out.v "
    "cells=4 types=AND2:4 worst_slack_ns=0.05 tns_ns=0\n"
    "equiv run=true verdict=equivalent first=/tmp/mapped.v "
    "second=/tmp/out.v\n";

const char kSummaryMappedOnlyPayload[] =
    "mapped target=area liberty=demo/synth/cells.lib "
    "verilog=/tmp/mapped.v cells=4 types=AND2:3,BUF:1 judged=true "
    "worst_slack_ns=-0.35 tns_ns=-0.35\n"
    "opt run=false winner=none iterations=0 verilog=none cells=0 "
    "types=none worst_slack_ns=none tns_ns=none\n"
    "equiv run=false verdict=none first=none second=none\n";

const char kSummaryUnjudgedPayload[] =
    "mapped target=area liberty=demo/synth/cells.lib "
    "verilog=/tmp/mapped.v cells=3 types=AND2:3 judged=false "
    "worst_slack_ns=none tns_ns=none\n"
    "opt run=false winner=none iterations=0 verilog=none cells=0 "
    "types=none worst_slack_ns=none tns_ns=none\n"
    "equiv run=false verdict=none first=none second=none\n";

TEST(MappedSynthModel, ParsesFullPayload) {
  MappedSynthModel model;
  ASSERT_TRUE(model.LoadPayload(kSummaryPayload));
  EXPECT_FALSE(model.Empty());
  EXPECT_EQ(model.Target(), "area");
  EXPECT_EQ(model.Liberty(), "demo/synth/cells.lib");
  EXPECT_EQ(model.Verilog(), "/tmp/mapped.v");
  EXPECT_EQ(model.Cells(), 4);
  ASSERT_EQ(model.Types().size(), 2u);
  EXPECT_EQ(model.Types()[0].type, "AND2");
  EXPECT_EQ(model.Types()[0].count, 3);
  EXPECT_EQ(model.Types()[1].type, "BUF");
  EXPECT_EQ(model.Types()[1].count, 1);
  EXPECT_TRUE(model.Judged());
  EXPECT_NEAR(model.WorstSlackNs(), -0.35, 1e-9);
  EXPECT_NEAR(model.TnsNs(), -0.35, 1e-9);
  EXPECT_TRUE(model.OptRun());
  EXPECT_EQ(model.OptWinner(), "classic");
  EXPECT_EQ(model.OptIterations(), 1);
  EXPECT_EQ(model.OptVerilog(), "/tmp/out.v");
  EXPECT_EQ(model.OptCells(), 4);
  ASSERT_EQ(model.OptTypes().size(), 1u);
  EXPECT_EQ(model.OptTypes()[0].type, "AND2");
  EXPECT_EQ(model.OptTypes()[0].count, 4);
  EXPECT_NEAR(model.OptWorstSlackNs(), 0.05, 1e-9);
  EXPECT_NEAR(model.OptTnsNs(), 0.0, 1e-9);
  EXPECT_TRUE(model.EquivRun());
  EXPECT_EQ(model.EquivVerdict(), "equivalent");
  EXPECT_EQ(model.EquivFirst(), "/tmp/mapped.v");
  EXPECT_EQ(model.EquivSecond(), "/tmp/out.v");
}

TEST(MappedSynthModel, ParsesMappedOnlyPayload) {
  MappedSynthModel model;
  ASSERT_TRUE(model.LoadPayload(kSummaryMappedOnlyPayload));
  EXPECT_FALSE(model.Empty());
  EXPECT_EQ(model.Target(), "area");
  EXPECT_EQ(model.Cells(), 4);
  EXPECT_TRUE(model.Judged());
  EXPECT_FALSE(model.OptRun());
  EXPECT_FALSE(model.EquivRun());
}

TEST(MappedSynthModel, ParsesUnjudgedPayload) {
  MappedSynthModel model;
  ASSERT_TRUE(model.LoadPayload(kSummaryUnjudgedPayload));
  EXPECT_FALSE(model.Judged());
  EXPECT_EQ(model.Cells(), 3);
  EXPECT_NEAR(model.WorstSlackNs(), 0.0, 1e-9);
  EXPECT_NEAR(model.TnsNs(), 0.0, 1e-9);
}

TEST(MappedSynthModel, RejectsMalformedPayloads) {
  const char* bad[] = {
      "",
      "mapped target=area\n",
      "mapped target=area liberty=demo/synth/cells.lib "
      "verilog=/tmp/mapped.v cells=4 types=AND2:3,BUF:1 judged=true "
      "worst_slack_ns=-0.35 tns_ns=-0.35\n"
      "opt run=true winner=classic iterations=1 verilog=/tmp/out.v "
      "cells=4 types=AND2:4 worst_slack_ns=0.05 tns_ns=0\n",
      "map target=area liberty=demo/synth/cells.lib "
      "verilog=/tmp/mapped.v cells=4 types=AND2:3,BUF:1 judged=true "
      "worst_slack_ns=-0.35 tns_ns=-0.35\n"
      "opt run=true winner=classic iterations=1 verilog=/tmp/out.v "
      "cells=4 types=AND2:4 worst_slack_ns=0.05 tns_ns=0\n"
      "equiv run=true verdict=equivalent first=/tmp/mapped.v "
      "second=/tmp/out.v\n",
      "mapped cells=4 target=area liberty=demo/synth/cells.lib "
      "verilog=/tmp/mapped.v types=AND2:3,BUF:1 judged=true "
      "worst_slack_ns=-0.35 tns_ns=-0.35\n"
      "opt run=true winner=classic iterations=1 verilog=/tmp/out.v "
      "cells=4 types=AND2:4 worst_slack_ns=0.05 tns_ns=0\n"
      "equiv run=true verdict=equivalent first=/tmp/mapped.v "
      "second=/tmp/out.v\n",
      "mapped target=area liberty=demo/synth/cells.lib "
      "verilog=/tmp/mapped.v cells=4 types=AND2:3,BUF:1 judged=true "
      "worst_slack_ns=none tns_ns=-0.35\n"
      "opt run=true winner=classic iterations=1 verilog=/tmp/out.v "
      "cells=4 types=AND2:4 worst_slack_ns=0.05 tns_ns=0\n"
      "equiv run=true verdict=equivalent first=/tmp/mapped.v "
      "second=/tmp/out.v\n",
      "mapped target=area liberty=demo/synth/cells.lib "
      "verilog=/tmp/mapped.v cells=4 types=AND2:3,BUF:1 judged=false "
      "worst_slack_ns=-0.35 tns_ns=none\n"
      "opt run=true winner=classic iterations=1 verilog=/tmp/out.v "
      "cells=4 types=AND2:4 worst_slack_ns=0.05 tns_ns=0\n"
      "equiv run=true verdict=equivalent first=/tmp/mapped.v "
      "second=/tmp/out.v\n",
      "mapped target=area liberty=demo/synth/cells.lib "
      "verilog=/tmp/mapped.v cells=4 types=AND2:3,BUF:1 judged=true "
      "worst_slack_ns=-0.35 tns_ns=-0.35\n"
      "opt run=false winner=classic iterations=1 verilog=/tmp/out.v "
      "cells=4 types=AND2:4 worst_slack_ns=0.05 tns_ns=0\n"
      "equiv run=true verdict=equivalent first=/tmp/mapped.v "
      "second=/tmp/out.v\n",
      "mapped target=area liberty=demo/synth/cells.lib "
      "verilog=/tmp/mapped.v cells=4 types=AND2:3,BUF:1 judged=true "
      "worst_slack_ns=-0.35 tns_ns=-0.35\n"
      "opt run=true winner=classic iterations=1 verilog=none "
      "cells=4 types=AND2:4 worst_slack_ns=0.05 tns_ns=0\n"
      "equiv run=true verdict=equivalent first=/tmp/mapped.v "
      "second=/tmp/out.v\n",
      "mapped target=area liberty=demo/synth/cells.lib "
      "verilog=/tmp/mapped.v cells=4 types=AND2:3,BUF:1 judged=true "
      "worst_slack_ns=-0.35 tns_ns=-0.35\n"
      "opt run=true winner=classic iterations=1 verilog=/tmp/out.v "
      "cells=4 types=AND2:4 worst_slack_ns=0.05 tns_ns=0\n"
      "equiv run=true verdict=bogus first=/tmp/mapped.v "
      "second=/tmp/out.v\n",
      "mapped target=area liberty=demo/synth/cells.lib "
      "verilog=/tmp/mapped.v cells=4 types=AND2:3,BUF:1 judged=true "
      "worst_slack_ns=-0.35 tns_ns=-0.35\n"
      "opt run=true winner=classic iterations=1 verilog=/tmp/out.v "
      "cells=4 types=AND2:4 worst_slack_ns=0.05 tns_ns=0\n"
      "equiv run=false verdict=equivalent first=/tmp/mapped.v "
      "second=/tmp/out.v\n",
      "mapped target=area liberty=demo/synth/cells.lib "
      "verilog=/tmp/mapped.v cells=4 types=BUF:1,AND2:3 judged=true "
      "worst_slack_ns=-0.35 tns_ns=-0.35\n"
      "opt run=true winner=classic iterations=1 verilog=/tmp/out.v "
      "cells=4 types=AND2:4 worst_slack_ns=0.05 tns_ns=0\n"
      "equiv run=true verdict=equivalent first=/tmp/mapped.v "
      "second=/tmp/out.v\n",
      "mapped target=area liberty=demo/synth/cells.lib "
      "verilog=/tmp/mapped.v cells=-4 types=AND2:3,BUF:1 judged=true "
      "worst_slack_ns=-0.35 tns_ns=-0.35\n"
      "opt run=true winner=classic iterations=1 verilog=/tmp/out.v "
      "cells=4 types=AND2:4 worst_slack_ns=0.05 tns_ns=0\n"
      "equiv run=true verdict=equivalent first=/tmp/mapped.v "
      "second=/tmp/out.v\n",
      "mapped target=area liberty=demo/synth/cells.lib "
      "verilog=/tmp/mapped.v cells=4 types=AND2:3,BUF:1 judged=true "
      "worst_slack_ns=soon tns_ns=-0.35\n"
      "opt run=true winner=classic iterations=1 verilog=/tmp/out.v "
      "cells=4 types=AND2:4 worst_slack_ns=0.05 tns_ns=0\n"
      "equiv run=true verdict=equivalent first=/tmp/mapped.v "
      "second=/tmp/out.v\n",
      "mapped  target=area liberty=demo/synth/cells.lib "
      "verilog=/tmp/mapped.v cells=4 types=AND2:3,BUF:1 judged=true "
      "worst_slack_ns=-0.35 tns_ns=-0.35\n"
      "opt run=true winner=classic iterations=1 verilog=/tmp/out.v "
      "cells=4 types=AND2:4 worst_slack_ns=0.05 tns_ns=0\n"
      "equiv run=true verdict=equivalent first=/tmp/mapped.v "
      "second=/tmp/out.v\n",
  };
  for (const char* payload : bad) {
    MappedSynthModel model;
    EXPECT_FALSE(model.LoadPayload(payload)) << "payload: " << payload;
    EXPECT_TRUE(model.Empty());
  }
}

TEST(MappedSynthModel, RejectsMalformedKeepsPrevious) {
  MappedSynthModel model;
  ASSERT_TRUE(model.LoadPayload(kSummaryPayload));
  EXPECT_FALSE(model.LoadPayload("garbage\n"));
  EXPECT_EQ(model.Target(), "area");
  EXPECT_EQ(model.Cells(), 4);
  EXPECT_TRUE(model.OptRun());
}

TEST(MappedSynthModel, ReloadReplacesPrevious) {
  MappedSynthModel model;
  ASSERT_TRUE(model.LoadPayload(kSummaryPayload));
  ASSERT_TRUE(model.LoadPayload(kSummaryMappedOnlyPayload));
  EXPECT_EQ(model.Target(), "area");
  EXPECT_EQ(model.Cells(), 4);
  EXPECT_FALSE(model.OptRun());
  EXPECT_FALSE(model.EquivRun());
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

// F1: the synth_summary stub, failing fast when the session
// holds no mapped design, like the real command.
void RegisterSummaryCommand(CommandRegistry& registry,
                            const std::string& payload, bool ok) {
  auto status = registry.Register(
      "synth_summary", "synth_summary",
      [payload, ok](CommandContext& context,
                    const std::vector<std::string>&) {
        if (!ok || !context.synth.mapped) {
          return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                     "no mapped design");
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
  // F1: the mapped record behind kSummaryPayload.
  context.synth.mapped = true;
  context.synth.mapped_target = "area";
  context.synth.mapped_liberty = "demo/synth/cells.lib";
  context.synth.mapped_verilog = "/tmp/mapped.v";
  context.synth.mapped_cells = 4;
  context.synth.mapped_types = "AND2:3,BUF:1";
  context.synth.mapped_timing_judged = true;
  context.synth.mapped_worst_slack_ns = -0.35;
  context.synth.mapped_total_negative_slack_ns = -0.35;
  context.synth.opt_run = true;
  context.synth.opt_winner = "classic";
  context.synth.opt_iterations = 1;
  context.synth.opt_verilog = "/tmp/out.v";
  context.synth.opt_cells = 4;
  context.synth.opt_types = "AND2:4";
  context.synth.opt_worst_slack_ns = 0.05;
  context.synth.opt_total_negative_slack_ns = 0.0;
  context.synth.equiv_run = true;
  context.synth.equiv_verdict = "equivalent";
  context.synth.equiv_first = "/tmp/mapped.v";
  context.synth.equiv_second = "/tmp/out.v";
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
  // No synth_summary command is registered here, so the mapped
  // record stays empty even though the context holds one.
  EXPECT_TRUE(panels.mapped_synth.Empty());
  EXPECT_TRUE(view.EchoLog().empty());
  EXPECT_TRUE(view.Messages().empty());
}

TEST(GuiSessionView, RefreshPanelsPopulatesMappedSynthWithoutEcho) {
  CommandRegistry registry;
  RegisterStatsCommand(registry, kStatsPayload, true);
  RegisterSummaryCommand(registry, kSummaryPayload, true);
  CommandContext context;
  FillPanelsContext(context);
  SessionView view(registry, context);
  view.RefreshPanels();
  const PanelsModel& panels = view.Panels();
  EXPECT_FALSE(panels.mapped_synth.Empty());
  EXPECT_EQ(panels.mapped_synth.Target(), "area");
  EXPECT_EQ(panels.mapped_synth.Cells(), 4);
  EXPECT_TRUE(panels.mapped_synth.Judged());
  EXPECT_NEAR(panels.mapped_synth.WorstSlackNs(), -0.35, 1e-9);
  EXPECT_NEAR(panels.mapped_synth.TnsNs(), -0.35, 1e-9);
  EXPECT_TRUE(panels.mapped_synth.OptRun());
  EXPECT_EQ(panels.mapped_synth.OptWinner(), "classic");
  EXPECT_EQ(panels.mapped_synth.OptIterations(), 1);
  EXPECT_NEAR(panels.mapped_synth.OptWorstSlackNs(), 0.05, 1e-9);
  EXPECT_TRUE(panels.mapped_synth.EquivRun());
  EXPECT_EQ(panels.mapped_synth.EquivVerdict(), "equivalent");
  EXPECT_TRUE(view.EchoLog().empty());
  EXPECT_TRUE(view.Messages().empty());
}

TEST(GuiSessionView, RefreshPanelsClearsMappedSynthWhenNotMapped) {
  CommandRegistry registry;
  RegisterStatsCommand(registry, kStatsPayload, true);
  RegisterSummaryCommand(registry, kSummaryPayload, true);
  CommandContext context;
  FillPanelsContext(context);
  SessionView view(registry, context);
  view.RefreshPanels();
  ASSERT_FALSE(view.Panels().mapped_synth.Empty());
  context.synth.mapped = false;
  view.RefreshPanels();
  EXPECT_TRUE(view.Panels().parsed);
  EXPECT_TRUE(view.Panels().elaborated);
  EXPECT_TRUE(view.Panels().generic_run);
  EXPECT_TRUE(view.Panels().mapped_synth.Empty());
}

TEST(GuiSessionView, RefreshPanelsKeepsRecordClearsMappedSynthOnFailedQuery) {
  CommandRegistry registry;
  RegisterStatsCommand(registry, kStatsPayload, true);
  RegisterSummaryCommand(registry, "", false);
  CommandContext context;
  FillPanelsContext(context);
  SessionView view(registry, context);
  view.RefreshPanels();
  EXPECT_TRUE(view.Panels().parsed);
  EXPECT_TRUE(view.Panels().elaborated);
  EXPECT_TRUE(view.Panels().generic_run);
  EXPECT_TRUE(view.Panels().mapped_synth.Empty());
}

TEST(GuiSessionView, RefreshPanelsKeepsRecordClearsMappedSynthOnMalformedPayload) {
  CommandRegistry registry;
  RegisterStatsCommand(registry, kStatsPayload, true);
  RegisterSummaryCommand(registry, "garbage\n", true);
  CommandContext context;
  FillPanelsContext(context);
  SessionView view(registry, context);
  view.RefreshPanels();
  EXPECT_TRUE(view.Panels().parsed);
  EXPECT_TRUE(view.Panels().elaborated);
  EXPECT_TRUE(view.Panels().generic_run);
  EXPECT_TRUE(view.Panels().mapped_synth.Empty());
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
  ASSERT_EQ(view.EchoLog().size(), 2u);
  EXPECT_EQ(view.EchoLog()[0], "eos::nope");
  EXPECT_EQ(view.EchoLog()[1], view.Messages()[0]);
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
  ASSERT_EQ(view.EchoLog().size(), 2u);
  EXPECT_EQ(view.EchoLog()[0], "eos::ping \"a b\" plain");
  EXPECT_EQ(view.EchoLog()[1], "ok");
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
  ASSERT_EQ(view.EchoLog().size(), 2u);
  EXPECT_EQ(view.EchoLog()[0], "ping \"abc");
  EXPECT_EQ(view.EchoLog()[1],
            "ping \"abc: EOS invalid_command: unterminated quote");
  ASSERT_EQ(view.Messages().size(), 1u);
  EXPECT_EQ(view.Messages()[0],
            "ping \"abc: EOS invalid_command: unterminated quote");
}

TEST(GuiSessionView, DispatchEntryMultiLineRunsEach) {
  CommandRegistry registry;
  RegisterPing(registry);
  CommandContext context;
  SessionView view(registry, context);
  const CommandResult result =
      view.DispatchEntry("eos::ping a\neos::ping b");
  EXPECT_TRUE(result.ok);
  ASSERT_EQ(view.EchoLog().size(), 4u);
  EXPECT_EQ(view.EchoLog()[0], "eos::ping a");
  EXPECT_EQ(view.EchoLog()[1], "ok");
  EXPECT_EQ(view.EchoLog()[2], "eos::ping b");
  EXPECT_EQ(view.EchoLog()[3], "ok");
}

TEST(GuiSessionView, DispatchEntryPastedBlobRunsEach) {
  CommandRegistry registry;
  RegisterPing(registry);
  CommandContext context;
  SessionView view(registry, context);
  const CommandResult result =
      view.DispatchEntry("eos::ping a eos::ping b eos::ping c");
  EXPECT_TRUE(result.ok);
  ASSERT_EQ(view.EchoLog().size(), 6u);
  EXPECT_EQ(view.EchoLog()[0], "eos::ping a");
  EXPECT_EQ(view.EchoLog()[1], "ok");
  EXPECT_EQ(view.EchoLog()[2], "eos::ping b");
  EXPECT_EQ(view.EchoLog()[3], "ok");
  EXPECT_EQ(view.EchoLog()[4], "eos::ping c");
  EXPECT_EQ(view.EchoLog()[5], "ok");
}

TEST(GuiSessionView, DispatchEntryContinuesPastFailure) {
  CommandRegistry registry;
  RegisterPing(registry);
  CommandContext context;
  SessionView view(registry, context);
  const CommandResult result =
      view.DispatchEntry("eos::ping a\neos::nope\neos::ping b");
  EXPECT_FALSE(result.ok);
  ASSERT_EQ(view.EchoLog().size(), 6u);
  EXPECT_EQ(view.EchoLog()[0], "eos::ping a");
  EXPECT_EQ(view.EchoLog()[1], "ok");
  EXPECT_EQ(view.EchoLog()[2], "eos::nope");
  EXPECT_EQ(view.EchoLog()[3],
            "eos::nope: EOS invalid_command: unknown command: eos::nope");
  EXPECT_EQ(view.EchoLog()[4], "eos::ping b");
  EXPECT_EQ(view.EchoLog()[5], "ok");
}

TEST(GuiSessionView, DispatchEntryBareMultiCommandReportsError) {
  CommandRegistry registry;
  auto status = registry.Register(
      "one", "one <arg>",
      [](CommandContext&, const std::vector<std::string>& args) {
        if (args.size() != 1) {
          return CommandResult::Fail(eos::ui::Diag::kInvalidCommand,
                                     "want 1 arg");
        }
        return CommandResult::Ok("got=" + args[0]);
      });
  EXPECT_TRUE(status.ok());
  CommandContext context;
  SessionView view(registry, context);
  // Bare command names never start a segment, so the blob parses
  // as one command with too many arguments: it must fail loudly
  // in the transcript, never silently do nothing.
  const CommandResult result = view.DispatchEntry("one a one b");
  EXPECT_FALSE(result.ok);
  ASSERT_EQ(view.EchoLog().size(), 2u);
  EXPECT_EQ(view.EchoLog()[0], "eos::one a one b");
  EXPECT_EQ(view.EchoLog()[1],
            "eos::one a one b: EOS invalid_command: want 1 arg");
}

TEST(GuiSessionView, DispatchEntrySendsMultiLinePayloadToLog) {
  CommandRegistry registry;
  auto status = registry.Register(
      "multi", "multi",
      [](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Ok("a=1\nb=2\n");
      });
  EXPECT_TRUE(status.ok());
  CommandContext context;
  SessionView view(registry, context);
  EXPECT_TRUE(view.DispatchEntry("multi").ok);
  // The transcript keeps the echo plus one outcome line; the
  // output lines live in the log.
  ASSERT_EQ(view.EchoLog().size(), 2u);
  EXPECT_EQ(view.EchoLog()[0], "eos::multi");
  EXPECT_EQ(view.EchoLog()[1], "ok");
  ASSERT_EQ(view.Log().size(), 3u);
  EXPECT_EQ(view.Log()[0].text, "eos::multi");
  EXPECT_EQ(view.Log()[1].text, "a=1");
  EXPECT_EQ(view.Log()[2].text, "b=2");
}

// The history sink gets every successfully parsed console line in
// normalized eos:: form (the echo line), including a dispatch the
// registry rejects, so the terminal REPL can recall it; a bare
// name is prefixed, since it would fail as Tcl.
TEST(GuiSessionView, DispatchEntryFeedsHistorySinkNormalized) {
  CommandRegistry registry;
  RegisterPing(registry);
  CommandContext context;
  SessionView view(registry, context);
  std::vector<std::string> fed;
  view.SetHistorySink(
      [&fed](const std::string& line) { fed.push_back(line); });
  EXPECT_TRUE(view.DispatchEntry("ping \"a b\" plain").ok);
  EXPECT_TRUE(view.DispatchEntry("eos::ping").ok);
  EXPECT_FALSE(view.DispatchEntry("nope").ok);
  ASSERT_EQ(fed.size(), 3u);
  EXPECT_EQ(fed[0], "eos::ping \"a b\" plain");
  EXPECT_EQ(fed[1], "eos::ping");
  EXPECT_EQ(fed[2], "eos::nope");
}

// Blank, comment, and malformed lines never reach the sink: only
// a line parsed into a command is shared.
TEST(GuiSessionView, DispatchEntrySkipsSinkForBlankCommentAndMalformed) {
  CommandRegistry registry;
  RegisterPing(registry);
  CommandContext context;
  SessionView view(registry, context);
  std::vector<std::string> fed;
  view.SetHistorySink(
      [&fed](const std::string& line) { fed.push_back(line); });
  EXPECT_TRUE(view.DispatchEntry("   ").ok);
  EXPECT_TRUE(view.DispatchEntry("# comment").ok);
  EXPECT_FALSE(view.DispatchEntry("ping \"abc").ok);
  EXPECT_TRUE(fed.empty());
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

TEST(ConsoleEntry, SplitSegmentsOnNewlines) {
  const std::vector<std::string> names = {"ping", "elaborate"};
  const std::vector<std::string> got = eos::ui::SplitConsoleSegments(
      "eos::ping a\neos::elaborate\n", names);
  ASSERT_EQ(got.size(), 2u);
  EXPECT_EQ(got[0], "eos::ping a");
  EXPECT_EQ(got[1], "eos::elaborate");
}

TEST(ConsoleEntry, SplitSegmentsOnEosMarkers) {
  const std::vector<std::string> names = {"ping", "elaborate"};
  const std::vector<std::string> got = eos::ui::SplitConsoleSegments(
      "eos::ping a eos::elaborate", names);
  ASSERT_EQ(got.size(), 2u);
  EXPECT_EQ(got[0], "eos::ping a");
  EXPECT_EQ(got[1], "eos::elaborate");
}

TEST(ConsoleEntry, SplitSegmentsIgnoresQuotedMarkers) {
  const std::vector<std::string> names = {"ping", "elaborate"};
  const std::vector<std::string> got = eos::ui::SplitConsoleSegments(
      "ping \"eos::elaborate\" eos::ping", names);
  ASSERT_EQ(got.size(), 2u);
  EXPECT_EQ(got[0], "ping \"eos::elaborate\"");
  EXPECT_EQ(got[1], "eos::ping");
}

TEST(ConsoleEntry, SplitSegmentsIgnoresUnknownMarkers) {
  const std::vector<std::string> names = {"ping"};
  const std::vector<std::string> got = eos::ui::SplitConsoleSegments(
      "eos::nope eos::ping", names);
  ASSERT_EQ(got.size(), 2u);
  EXPECT_EQ(got[0], "eos::nope");
  EXPECT_EQ(got[1], "eos::ping");
}

TEST(ConsoleEntry, SplitSegmentsIgnoresBareNames) {
  const std::vector<std::string> names = {"ping", "elaborate"};
  const std::vector<std::string> got =
      eos::ui::SplitConsoleSegments("ping a elaborate", names);
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0], "ping a elaborate");
}

TEST(ConsoleEntry, SplitSegmentsDropsBlanks) {
  const std::vector<std::string> names = {"ping"};
  EXPECT_TRUE(eos::ui::SplitConsoleSegments("   \n\n", names).empty());
  const std::vector<std::string> got =
      eos::ui::SplitConsoleSegments("\neos::ping\n\n", names);
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0], "eos::ping");
}

#ifdef EOS_GUI_HAVE_QT6

using eos::ui::BlockDiagram;
using eos::ui::CenterView;
using eos::ui::MainWindow;
using eos::ui::PanelsView;
using eos::ui::FloorplanDiagram;
using eos::ui::PlacementDiagram;
using eos::ui::SchematicDiagram;

// Widget rendering tests: both diagrams sit on a dark
// background, so every shape is a white contour with no fill
// and every label is white text. These tests need a
// QApplication; the offscreen platform keeps them headless in
// CI. They compile out without Qt.
QApplication* OutlineTestApp() {
  static int argc = 1;
  static char arg0[] = "eda_ui_gui_unit_test";
  static char* argv[] = {arg0, nullptr};
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  // Heap-allocated and deliberately never deleted: destroying a
  // static QApplication at exit segfaults while tearing down its
  // QPointingDevice children (Qt 6.4, offscreen). The process
  // reclaims it; no test outlives the process.
  static QApplication* app = new QApplication(argc, argv);
  return app;
}

template <typename T>
std::vector<T*> OutlineSceneItems(const QGraphicsScene* scene) {
  std::vector<T*> found;
  for (QGraphicsItem* item : scene->items()) {
    if (auto* typed = qgraphicsitem_cast<T*>(item)) {
      found.push_back(typed);
    }
  }
  return found;
}

bool OutlineRectNear(const QRectF& a, const QRectF& b) {
  return std::abs(a.left() - b.left()) < 0.01 &&
         std::abs(a.top() - b.top()) < 0.01 &&
         std::abs(a.width() - b.width()) < 0.01 &&
         std::abs(a.height() - b.height()) < 0.01;
}

// A SessionView whose schematic holds the flat optloop payload:
// three AND gates, four input terminals, two output terminals.
struct OutlineSchematicView {
  CommandRegistry registry;
  CommandContext context;
  SessionView view;
  OutlineSchematicView() : view(registry, context) {
    OutlineTestApp();
    RegisterHierarchyCommand(registry, kFlatPayload, true);
    RegisterSchematicCommand(registry, nullptr);
    context.synth.elaborated = true;
    view.RefreshHierarchy();
    view.RefreshSchematic();
  }
};

// A SessionView whose block diagram holds the top payload:
// two hier blocks, three input terminals, one output terminal.
struct OutlineBlockView {
  CommandRegistry registry;
  CommandContext context;
  SessionView view;
  OutlineBlockView() : view(registry, context) {
    OutlineTestApp();
    RegisterHierarchyCommand(registry, kNestedPayload, true);
    RegisterBlocksCommand(registry, nullptr);
    context.synth.elaborated = true;
    view.RefreshHierarchy();
    view.RefreshDiagram();
  }
};

const char kOutlineTwoGateHierarchy[] =
    "top=two\n"
    "node path=two module=two ports=a,b,c,d,y1,y2\n";

// Two disconnected AND gates, so selecting one dims the other.
const char kOutlineTwoGateSchematic[] =
    "module=two\n"
    "parent port=a direction=in\n"
    "parent port=b direction=in\n"
    "parent port=c direction=in\n"
    "parent port=d direction=in\n"
    "parent port=y1 direction=out\n"
    "parent port=y2 direction=out\n"
    "gate instance=g1 type=$and ports=A:in,B:in,Y:out\n"
    "gate instance=g2 type=$and ports=A:in,B:in,Y:out\n"
    "net name=a gates=g1:A terminals=a\n"
    "net name=b gates=g1:B terminals=b\n"
    "net name=c gates=g2:A terminals=c\n"
    "net name=d gates=g2:B terminals=d\n"
    "net name=y1 gates=g1:Y terminals=y1\n"
    "net name=y2 gates=g2:Y terminals=y2\n";

void RegisterTwoGateSchematic(CommandRegistry& registry) {
  auto status = registry.Register(
      "synth_schematic", "synth_schematic <module>",
      [](CommandContext&, const std::vector<std::string>& args) {
        if (args.size() == 1 && args[0] == "two") {
          return CommandResult::Ok(kOutlineTwoGateSchematic);
        }
        return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                   "unknown module");
      });
  EXPECT_TRUE(status.ok());
}

const char kOutlineAluHierarchy[] =
    "top=alu\n"
    "node path=alu module=alu ports=op,x,y,z\n";

void RegisterAluSchematic(CommandRegistry& registry) {
  auto status = registry.Register(
      "synth_schematic", "synth_schematic <module>",
      [](CommandContext&, const std::vector<std::string>& args) {
        if (args.size() == 1 && args[0] == "alu") {
          return CommandResult::Ok(kSchematicAluPayload);
        }
        return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                   "unknown module");
      });
  EXPECT_TRUE(status.ok());
}

// A leaf module exercising the newer symbols: a $mux, a $adff
// flip-flop, a $eq comparator, a $mem box with its equation,
// and a $shl box with its glyph.
const char kSchematicSymsPayload[] =
    "module=syms\n"
    "parent port=a direction=in\n"
    "parent port=b direction=in\n"
    "parent port=clk direction=in\n"
    "parent port=en direction=in\n"
    "parent port=y direction=out\n"
    "parent port=q direction=out\n"
    "parent port=e direction=out\n"
    "parent port=s direction=out\n"
    "gate instance=mux1 type=$mux ports=A:in,B:in,S:in,Y:out\n"
    "gate instance=ff1 type=$adff ports=ARST:in,CLK:in,D:in,Q:out\n"
    "gate instance=eq1 type=$eq ports=A:in,B:in,Y:out\n"
    "gate instance=mem1 type=$mem "
    "ports=RD_ADDR:in,RD_DATA:out,WR_DATA:in,WR_EN:in,CLK:in\n"
    "gate instance=sh1 type=$shl ports=A:in,B:in,Y:out\n"
    "net name=a gates=mux1:A,eq1:A,mem1:RD_ADDR,sh1:A terminals=a\n"
    "net name=b gates=mux1:B,eq1:B,mem1:WR_DATA,sh1:B terminals=b\n"
    "net name=clk gates=ff1:CLK,mem1:CLK terminals=clk\n"
    "net name=en gates=mux1:S,ff1:ARST,mem1:WR_EN terminals=en\n"
    "net name=m gates=mux1:Y,ff1:D terminals=\n"
    "net name=y gates=ff1:Q terminals=y\n"
    "net name=q gates=mem1:RD_DATA terminals=q\n"
    "net name=e gates=eq1:Y terminals=e\n"
    "net name=s gates=sh1:Y terminals=s\n";

const char kOutlineSymsHierarchy[] =
    "top=syms\n"
    "node path=syms module=syms ports=a,b,clk,en,y,q,e,s\n";

void RegisterSymsSchematic(CommandRegistry& registry) {
  auto status = registry.Register(
      "synth_schematic", "synth_schematic <module>",
      [](CommandContext&, const std::vector<std::string>& args) {
        if (args.size() == 1 && args[0] == "syms") {
          return CommandResult::Ok(kSchematicSymsPayload);
        }
        return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                   "unknown module");
      });
  EXPECT_TRUE(status.ok());
}

// Builds the syms schematic in a diagram and returns the
// scene, following the ClickHighlights fixture pattern.
const QGraphicsScene* BuildSymsScene(CommandRegistry& registry,
                                     CommandContext& context,
                                     SessionView& view,
                                     SchematicDiagram& diagram) {
  RegisterHierarchyCommand(registry, kOutlineSymsHierarchy, true);
  RegisterSymsSchematic(registry);
  context.synth.elaborated = true;
  view.RefreshHierarchy();
  view.RefreshSchematic();
  if (view.Schematic().Empty()) return nullptr;
  diagram.show();
  QApplication::processEvents();
  diagram.Refresh();
  QApplication::processEvents();
  return diagram.Scene();
}

// The gate outline holding a type label: generic gates draw
// as outlined rects, shaped gates as paths, both 64 high.
QRectF SymsGateRectForLabel(const QGraphicsScene* scene,
                            const std::string& label) {
  QPointF at;
  bool found = false;
  for (const QGraphicsSimpleTextItem* text :
       OutlineSceneItems<QGraphicsSimpleTextItem>(scene)) {
    if (text->text().toStdString() == label) {
      at = text->pos();
      found = true;
      break;
    }
  }
  if (!found) return QRectF();
  for (const QGraphicsRectItem* rect :
       OutlineSceneItems<QGraphicsRectItem>(scene)) {
    if (rect->pen().style() == Qt::NoPen) continue;
    if (std::abs(rect->rect().height() - 64) > 0.01) continue;
    if (rect->rect().contains(at)) return rect->rect();
  }
  for (const QGraphicsPathItem* path :
       OutlineSceneItems<QGraphicsPathItem>(scene)) {
    const QRectF bounds = path->path().boundingRect();
    if (std::abs(bounds.height() - 64) > 0.01) continue;
    if (bounds.contains(at)) return bounds;
  }
  return QRectF();
}

// The text item with exactly this text, or nullptr.
const QGraphicsSimpleTextItem* SymsTextItem(
    const QGraphicsScene* scene, const std::string& text) {
  for (const QGraphicsSimpleTextItem* item :
       OutlineSceneItems<QGraphicsSimpleTextItem>(scene)) {
    if (item->text().toStdString() == text) return item;
  }
  return nullptr;
}

// Panel rework: the issues model parses a synth_issues
// payload, sorts by severity, and counts per severity. The
// payload lists the issues out of order on purpose: the
// warning comes first, then the error, then the info.
const char kIssuesPayload[] =
    "checked=hierarchy resolution, multiple drivers, undriven "
    "nets, combinational loops, inferred latches, unknown cell "
    "types, unconnected ports, top ports, empty modules\n"
    "netlist=elaborated\n"
    "issue severity=warning code=unconnected_port module=top "
    "path=top/u_mid target=u_add file= line=0 message=Port Y of cell "
    "u_add is unconnected\n"
    "issue severity=error code=unresolved_reference module=broken_top "
    "path=broken_top target=u_missing file=broken.v line=2 "
    "message=Module does_not_exist instantiated at broken_top/u_missing "
    "is not defined\n"
    "issue severity=info code=empty_module module=top path=top "
    "target= file= line=0 message=Module top has no cells\n";

TEST(IssuesModel, ParsesPayloadSortsBySeverityAndCounts) {
  IssuesModel model;
  ASSERT_TRUE(model.LoadPayload(kIssuesPayload));
  EXPECT_TRUE(model.Loaded());
  EXPECT_EQ(model.Netlist(), "elaborated");
  EXPECT_NE(model.Checked().find("multiple drivers"), std::string::npos);
  ASSERT_EQ(model.Issues().size(), 3u);
  EXPECT_EQ(model.ErrorCount(), 1);
  EXPECT_EQ(model.WarningCount(), 1);
  EXPECT_EQ(model.InfoCount(), 1);
  EXPECT_EQ(model.Issues()[0].severity, "error");
  EXPECT_EQ(model.Issues()[0].code, "unresolved_reference");
  EXPECT_EQ(model.Issues()[0].target, "u_missing");
  EXPECT_EQ(model.Issues()[0].file, "broken.v");
  EXPECT_EQ(model.Issues()[0].line, 2);
  EXPECT_EQ(IssuesModel::DisplayPath(model.Issues()[0]),
            "broken_top/u_missing");
  EXPECT_EQ(model.Issues()[1].severity, "warning");
  EXPECT_EQ(model.Issues()[1].code, "unconnected_port");
  EXPECT_TRUE(model.Issues()[1].file.empty());
  EXPECT_EQ(model.Issues()[1].line, 0);
  EXPECT_EQ(IssuesModel::DisplayPath(model.Issues()[1]), "top/u_mid/u_add");
  EXPECT_EQ(model.Issues()[2].severity, "info");
  EXPECT_EQ(model.Issues()[2].code, "empty_module");
  EXPECT_TRUE(model.Issues()[2].target.empty());
  EXPECT_EQ(IssuesModel::DisplayPath(model.Issues()[2]), "top");
}

TEST(IssuesModel, ParsesEmptyPayload) {
  IssuesModel model;
  ASSERT_TRUE(model.LoadPayload(
      "checked=hierarchy resolution, multiple drivers, undriven "
      "nets, combinational loops, inferred latches, unknown cell "
      "types, unconnected ports, top ports, empty modules\n"
      "netlist=generic\n"));
  EXPECT_TRUE(model.Loaded());
  EXPECT_EQ(model.Netlist(), "generic");
  EXPECT_TRUE(model.Issues().empty());
  EXPECT_EQ(model.ErrorCount(), 0);
  EXPECT_EQ(model.WarningCount(), 0);
  EXPECT_EQ(model.InfoCount(), 0);
}

TEST(IssuesModel, RejectsMalformedPayloads) {
  IssuesModel model;
  ASSERT_TRUE(model.LoadPayload(kIssuesPayload));
  const std::string good_checked = model.Checked();
  // Bad netlist value.
  EXPECT_FALSE(model.LoadPayload(
      "checked=hierarchy resolution\nnetlist=bogus\n"));
  // Missing checked line.
  EXPECT_FALSE(model.LoadPayload("netlist=elaborated\n"));
  // Unknown severity.
  EXPECT_FALSE(model.LoadPayload(
      "checked=hierarchy resolution\nnetlist=elaborated\n"
      "issue severity=fatal code=x module=m path=p target= file= "
      "line=0 message=bad\n"));
  // Missing message field.
  EXPECT_FALSE(model.LoadPayload(
      "checked=hierarchy resolution\nnetlist=elaborated\n"
      "issue severity=error code=x module=m path=p target= file= "
      "line=0\n"));
  // Negative line number.
  EXPECT_FALSE(model.LoadPayload(
      "checked=hierarchy resolution\nnetlist=elaborated\n"
      "issue severity=error code=x module=m path=p target= file= "
      "line=-1 message=bad\n"));
  // Empty code.
  EXPECT_FALSE(model.LoadPayload(
      "checked=hierarchy resolution\nnetlist=elaborated\n"
      "issue severity=error code= module=m path=p target= file= "
      "line=0 message=bad\n"));
  // A malformed load keeps the previous issues.
  EXPECT_EQ(model.Checked(), good_checked);
  ASSERT_EQ(model.Issues().size(), 3u);
}

TEST(IssuesModel, ReloadReplacesPreviousIssues) {
  IssuesModel model;
  ASSERT_TRUE(model.LoadPayload(kIssuesPayload));
  ASSERT_EQ(model.Issues().size(), 3u);
  ASSERT_TRUE(model.LoadPayload(
      "checked=hierarchy resolution\nnetlist=elaborated\n"));
  EXPECT_TRUE(model.Issues().empty());
  model.Clear();
  EXPECT_FALSE(model.Loaded());
  EXPECT_TRUE(model.Issues().empty());
}

// A full timing_summary payload for the optloop acceptance
// chain: one scene at worst slack +0.05 ns, the vclk clock
// row, five violator rows, three worst paths, the coverage
// summary, and the QoR rows.
const char kTimingPayload[] =
    "source=verilog scenes=1 revision=3\n"
    "clock name=vclk generated=0 period=1.25 rise=0 fall=0.625 "
    "attrs= skew=none sources={-}\n"
    "scene=func_tt mode=func corner=tt parasitics=estimate "
    "worst_slack_ns=0.05 total_negative_slack_ns=0 "
    "worst_hold_slack_ns=0.45 total_negative_hold_slack_ns=0 "
    "max_transition_ns=0 max_transition_violations=0 "
    "max_capacitance_pf=0 max_capacitance_violations=0 max_fanout=0 "
    "max_fanout_violations=0\n"
    "scene=func_tt class=max_delay violator=- slack=0.05 cost=0 "
    "weight=1 violations=0\n"
    "scene=func_tt class=min_delay violator=- slack=0.45 cost=0 "
    "weight=1 violations=0\n"
    "scene=func_tt class=max_transition violator=- limit=none "
    "weight=1 violations=0\n"
    "scene=func_tt class=max_capacitance violator=- limit=none "
    "weight=1 violations=0\n"
    "scene=func_tt class=max_fanout violator=- limit=none weight=1 "
    "violations=0\n"
    "path scene=func_tt check=setup endpoint=y1 startpoint=a "
    "slack=0.05 arrival=1.2 required=1.25\n"
    "path scene=func_tt check=setup endpoint=y2 startpoint=a "
    "slack=0.05 arrival=1.2 required=1.25\n"
    "path scene=func_tt check=hold endpoint=y1 startpoint=a "
    "slack=0.45 arrival=0.8 required=0.35\n"
    "coverage scene=func_tt checks=4 tested=4 untested=0\n"
    "coverage scene=func_tt type=setup tested=2 untested=0 "
    "percent=100.0\n"
    "coverage scene=func_tt type=hold tested=2 untested=0 "
    "percent=100.0\n"
    "qor scene=func_tt setup_groups=1 hold_groups=1 union_tns=0 "
    "union_endpoints=0 every_group_tns=0 every_group_endpoints=0 "
    "hold_union_tns=0 hold_union_endpoints=0 hold_every_group_tns=0 "
    "hold_every_group_endpoints=0 cells=4 area=8 compile_cpu_s=0.01\n"
    "qor scene=func_tt group=vclk check=setup wns=0.05 tns=0 "
    "violating_paths=0\n"
    "qor scene=func_tt group=vclk check=hold wns=0.45 tns=0 "
    "violating_paths=0\n"
    "qor scene=func_tt cell=AND2 count=4 area=8\n"
    "qor scene=func_tt drc=max_transition violations=0 "
    "worst_slack=none\n"
    "qor scene=func_tt drc=max_capacitance violations=0 "
    "worst_slack=none\n"
    "qor scene=func_tt drc=max_fanout violations=0 worst_slack=none\n";

TEST(TimingModel, ParsesFullPayload) {
  TimingModel model;
  ASSERT_TRUE(model.LoadPayload(kTimingPayload));
  EXPECT_TRUE(model.Loaded());
  EXPECT_EQ(model.Source(), "verilog");
  ASSERT_EQ(model.Scenes().size(), 1u);
  const TimingScene& scene = model.Scenes()[0];
  EXPECT_EQ(scene.scene, "func_tt");
  EXPECT_EQ(scene.mode, "func");
  EXPECT_EQ(scene.corner, "tt");
  EXPECT_EQ(scene.parasitics, "estimate");
  EXPECT_EQ(scene.worst_slack_ns, "0.05");
  EXPECT_EQ(scene.total_negative_slack_ns, "0");
  EXPECT_EQ(scene.worst_hold_slack_ns, "0.45");
  EXPECT_EQ(scene.max_transition_violations, 0u);
  ASSERT_EQ(model.Clocks().size(), 1u);
  const TimingClock& clock = model.Clocks()[0];
  EXPECT_EQ(clock.name, "vclk");
  EXPECT_FALSE(clock.generated);
  EXPECT_EQ(clock.period, "1.25");
  EXPECT_EQ(clock.rise, "0");
  EXPECT_EQ(clock.fall, "0.625");
  EXPECT_EQ(clock.skew, "none");
  EXPECT_EQ(clock.sources, "{-}");
  EXPECT_TRUE(model.GeneratedClocks().empty());
  EXPECT_TRUE(model.BindClocks().empty());
  ASSERT_EQ(model.Violators().size(), 5u);
  EXPECT_EQ(model.Violators()[0].klass, "max_delay");
  EXPECT_EQ(model.Violators()[0].violator, "-");
  EXPECT_EQ(model.Violators()[0].slack, "0.05");
  EXPECT_EQ(model.Violators()[0].cost, "0");
  EXPECT_EQ(model.Violators()[0].violations, 0u);
  EXPECT_TRUE(model.Violators()[0].has_limit);
  EXPECT_EQ(model.Violators()[2].klass, "max_transition");
  EXPECT_FALSE(model.Violators()[2].has_limit);
  ASSERT_EQ(model.Paths().size(), 3u);
  EXPECT_EQ(model.Paths()[0].check, "setup");
  EXPECT_EQ(model.Paths()[0].endpoint, "y1");
  EXPECT_EQ(model.Paths()[0].startpoint, "a");
  EXPECT_EQ(model.Paths()[0].slack, "0.05");
  EXPECT_EQ(model.Paths()[2].check, "hold");
  ASSERT_EQ(model.CoverageTotals().size(), 1u);
  EXPECT_EQ(model.CoverageTotals()[0].checks, 4u);
  EXPECT_EQ(model.CoverageTotals()[0].tested, 4u);
  EXPECT_EQ(model.CoverageTotals()[0].untested, 0u);
  ASSERT_EQ(model.CoverageTypes().size(), 2u);
  EXPECT_EQ(model.CoverageTypes()[0].type, "setup");
  EXPECT_EQ(model.CoverageTypes()[0].percent, "100.0");
  ASSERT_EQ(model.QorScenes().size(), 1u);
  EXPECT_EQ(model.QorScenes()[0].cells, 4u);
  EXPECT_EQ(model.QorScenes()[0].area, "8");
  EXPECT_EQ(model.QorScenes()[0].compile_cpu_s, "0.01");
  ASSERT_EQ(model.QorGroups().size(), 2u);
  EXPECT_EQ(model.QorGroups()[0].group, "vclk");
  EXPECT_EQ(model.QorGroups()[0].wns, "0.05");
  ASSERT_EQ(model.QorCells().size(), 1u);
  EXPECT_EQ(model.QorCells()[0].cell, "AND2");
  EXPECT_EQ(model.QorCells()[0].count, 4u);
  ASSERT_EQ(model.QorDrcs().size(), 3u);
  EXPECT_EQ(model.QorDrcs()[0].drc, "max_transition");
  EXPECT_EQ(model.QorDrcs()[0].worst_slack, "none");
}

TEST(TimingModel, RejectsMalformedPayloads) {
  const char* bad[] = {
      // No header.
      "scene=func_tt mode=func corner=tt parasitics=estimate "
      "worst_slack_ns=0.05 total_negative_slack_ns=0 "
      "worst_hold_slack_ns=0.45 total_negative_hold_slack_ns=0 "
      "max_transition_ns=0 max_transition_violations=0 "
      "max_capacitance_pf=0 max_capacitance_violations=0 max_fanout=0 "
      "max_fanout_violations=0\n",
      // Unknown source.
      "source=odb2 scenes=1 revision=3\n",
      // Scene count mismatch.
      "source=verilog scenes=2 revision=3\n"
      "scene=func_tt mode=func corner=tt parasitics=estimate "
      "worst_slack_ns=0.05 total_negative_slack_ns=0 "
      "worst_hold_slack_ns=0.45 total_negative_hold_slack_ns=0 "
      "max_transition_ns=0 max_transition_violations=0 "
      "max_capacitance_pf=0 max_capacitance_violations=0 max_fanout=0 "
      "max_fanout_violations=0\n",
      // A clock with a skew value: skew is always none.
      "source=verilog scenes=1 revision=3\n"
      "clock name=vclk generated=0 period=1.25 rise=0 fall=0.625 "
      "attrs= skew=0.1 sources={-}\n",
      // A path naming a scene the payload never summarized.
      "source=verilog scenes=1 revision=3\n"
      "scene=func_tt mode=func corner=tt parasitics=estimate "
      "worst_slack_ns=0.05 total_negative_slack_ns=0 "
      "worst_hold_slack_ns=0.45 total_negative_hold_slack_ns=0 "
      "max_transition_ns=0 max_transition_violations=0 "
      "max_capacitance_pf=0 max_capacitance_violations=0 max_fanout=0 "
      "max_fanout_violations=0\n"
      "path scene=func_xx check=setup endpoint=y1 startpoint=a "
      "slack=0.05 arrival=1.2 required=1.25\n",
      // An unknown key on a scene line.
      "source=verilog scenes=1 revision=3\n"
      "scene=func_tt mode=func corner=tt parasitics=estimate "
      "worst_slack_ns=0.05 total_negative_slack_ns=0 "
      "worst_hold_slack_ns=0.45 total_negative_hold_slack_ns=0 "
      "max_transition_ns=0 max_transition_violations=0 "
      "max_capacitance_pf=0 max_capacitance_violations=0 max_fanout=0 "
      "max_fanout_violations=0 extra=1\n",
      // A two-token worst_slack: the value must be one token.
      "source=verilog scenes=1 revision=3\n"
      "scene=func_tt mode=func corner=tt parasitics=estimate "
      "worst_slack_ns=0.05 total_negative_slack_ns=0 "
      "worst_hold_slack_ns=0.45 total_negative_hold_slack_ns=0 "
      "max_transition_ns=0 max_transition_violations=0 "
      "max_capacitance_pf=0 max_capacitance_violations=0 max_fanout=0 "
      "max_fanout_violations=0\n"
      "scene=func_tt class=max_delay violator=- slack=0.05 cost=0 "
      "weight=1 violations=0\n"
      "scene=func_tt class=min_delay violator=- slack=0.45 cost=0 "
      "weight=1 violations=0\n"
      "scene=func_tt class=max_transition violator=- limit=none "
      "weight=1 violations=0\n"
      "scene=func_tt class=max_capacitance violator=- limit=none "
      "weight=1 violations=0\n"
      "scene=func_tt class=max_fanout violator=- limit=none weight=1 "
      "violations=0\n"
      "coverage scene=func_tt checks=0 tested=0 untested=0\n"
      "qor scene=func_tt setup_groups=0 hold_groups=0 union_tns=0 "
      "union_endpoints=0 every_group_tns=0 every_group_endpoints=0 "
      "hold_union_tns=0 hold_union_endpoints=0 hold_every_group_tns=0 "
      "hold_every_group_endpoints=0 cells=0 area=0 compile_cpu_s=n/a\n"
      "qor scene=func_tt drc=max_transition violations=0 "
      "worst_slack=no limit\n",
  };
  for (const char* payload : bad) {
    TimingModel model;
    EXPECT_FALSE(model.LoadPayload(payload)) << "payload: " << payload;
    EXPECT_FALSE(model.Loaded());
  }
  // A malformed load keeps the previous timing, and a payload
  // missing one violator class is malformed.
  TimingModel model;
  ASSERT_TRUE(model.LoadPayload(kTimingPayload));
  EXPECT_FALSE(model.LoadPayload("garbage\n"));
  EXPECT_EQ(model.Scenes().size(), 1u);
  EXPECT_EQ(model.Scenes()[0].worst_slack_ns, "0.05");
  const std::string missing_class =
      std::string(kTimingPayload).substr(
          0, std::string(kTimingPayload).find(
                 "scene=func_tt class=max_fanout"));
  EXPECT_FALSE(model.LoadPayload(missing_class));
  EXPECT_EQ(model.Scenes().size(), 1u);
}

TEST(TimingModel, ReloadReplacesPreviousTiming) {
  TimingModel model;
  ASSERT_TRUE(model.LoadPayload(kTimingPayload));
  ASSERT_EQ(model.Scenes().size(), 1u);
  ASSERT_TRUE(model.LoadPayload(
      "source=verilog scenes=1 revision=4\n"
      "scene=func_tt mode=func corner=tt parasitics=estimate "
      "worst_slack_ns=0.1 total_negative_slack_ns=0 "
      "worst_hold_slack_ns=0.5 total_negative_hold_slack_ns=0 "
      "max_transition_ns=0 max_transition_violations=0 "
      "max_capacitance_pf=0 max_capacitance_violations=0 max_fanout=0 "
      "max_fanout_violations=0\n"
      "scene=func_tt class=max_delay violator=- slack=0.1 cost=0 "
      "weight=1 violations=0\n"
      "scene=func_tt class=min_delay violator=- slack=0.5 cost=0 "
      "weight=1 violations=0\n"
      "scene=func_tt class=max_transition violator=- limit=none "
      "weight=1 violations=0\n"
      "scene=func_tt class=max_capacitance violator=- limit=none "
      "weight=1 violations=0\n"
      "scene=func_tt class=max_fanout violator=- limit=none weight=1 "
      "violations=0\n"
      "coverage scene=func_tt checks=0 tested=0 untested=0\n"
      "qor scene=func_tt setup_groups=0 hold_groups=0 union_tns=0 "
      "union_endpoints=0 every_group_tns=0 every_group_endpoints=0 "
      "hold_union_tns=0 hold_union_endpoints=0 hold_every_group_tns=0 "
      "hold_every_group_endpoints=0 cells=0 area=0 compile_cpu_s=n/a\n"));
  EXPECT_EQ(model.Scenes()[0].worst_slack_ns, "0.1");
  EXPECT_TRUE(model.Paths().empty());
  model.Clear();
  EXPECT_FALSE(model.Loaded());
  EXPECT_TRUE(model.Scenes().empty());
}

TEST(FloorplanModel, ParsesFullPayload) {
  FloorplanModel model;
  ASSERT_TRUE(model.LoadPayload(kFloorplanPayload));
  EXPECT_TRUE(model.Loaded());
  EXPECT_EQ(model.DieArea(), (FloorplanRect{0, 0, 5000, 5000}));
  EXPECT_EQ(model.CoreArea(), (FloorplanRect{0, 0, 5000, 5000}));
  EXPECT_EQ(model.TrackGridCount(), 10u);
  EXPECT_EQ(model.IoPlaced(), 6u);
  EXPECT_EQ(model.IoTotal(), 6u);
  ASSERT_EQ(model.Rows().size(), 1u);
  EXPECT_EQ(model.Rows()[0], (FloorplanRect{0, 0, 5000, 3200}));
  ASSERT_EQ(model.Pins().size(), 6u);
  EXPECT_EQ(model.Pins()[0].name, "a");
  EXPECT_EQ(model.Pins()[0].direction, "in");
  EXPECT_TRUE(model.Pins()[0].placed);
  EXPECT_EQ(model.Pins()[0].rect, (FloorplanRect{-50, 950, 50, 1050}));
  EXPECT_EQ(model.Pins()[4].name, "y1");
  EXPECT_EQ(model.Pins()[4].direction, "out");
  EXPECT_TRUE(model.Pins()[4].placed);
  EXPECT_EQ(model.Pins()[4].rect, (FloorplanRect{4950, 1950, 5050, 2050}));
}

TEST(FloorplanModel, RejectsMalformedPayloads) {
  const char* bad[] = {
      // No header.
      "row rect=0,0,5000,3200\n",
      // Wrong tag.
      "timing die=0,0,5000,5000 core=0,0,5000,5000 rows=1 track_grids=10 "
      "io_placed=6 io_total=6\n",
      // Unknown header key.
      "floorplan die=0,0,5000,5000 core=0,0,5000,5000 rows=1 track_grids=10 "
      "io_placed=6 io_total=6 extra=1\n",
      // Missing header key.
      "floorplan die=0,0,5000,5000 core=0,0,5000,5000 rows=1 track_grids=10 "
      "io_placed=6\n",
      // Row count mismatch.
      "floorplan die=0,0,5000,5000 core=0,0,5000,5000 rows=2 track_grids=10 "
      "io_placed=6 io_total=6\n"
      "row rect=0,0,5000,3200\n"
      "pin name=a dir=in placed=1 rect=-50,950,50,1050\n"
      "pin name=b dir=in placed=1 rect=-50,1950,50,2050\n"
      "pin name=c dir=in placed=1 rect=-50,2950,50,3050\n"
      "pin name=d dir=in placed=1 rect=-50,3950,50,4050\n"
      "pin name=y1 dir=out placed=1 rect=4950,1950,5050,2050\n"
      "pin name=y2 dir=out placed=1 rect=4950,2950,5050,3050\n",
      // IO total mismatch.
      "floorplan die=0,0,5000,5000 core=0,0,5000,5000 rows=1 track_grids=10 "
      "io_placed=6 io_total=5\n"
      "row rect=0,0,5000,3200\n"
      "pin name=a dir=in placed=1 rect=-50,950,50,1050\n"
      "pin name=b dir=in placed=1 rect=-50,1950,50,2050\n"
      "pin name=c dir=in placed=1 rect=-50,2950,50,3050\n"
      "pin name=d dir=in placed=1 rect=-50,3950,50,4050\n"
      "pin name=y1 dir=out placed=1 rect=4950,1950,5050,2050\n"
      "pin name=y2 dir=out placed=1 rect=4950,2950,5050,3050\n",
      // IO placed mismatch.
      "floorplan die=0,0,5000,5000 core=0,0,5000,5000 rows=1 track_grids=10 "
      "io_placed=5 io_total=6\n"
      "row rect=0,0,5000,3200\n"
      "pin name=a dir=in placed=1 rect=-50,950,50,1050\n"
      "pin name=b dir=in placed=1 rect=-50,1950,50,2050\n"
      "pin name=c dir=in placed=1 rect=-50,2950,50,3050\n"
      "pin name=d dir=in placed=1 rect=-50,3950,50,4050\n"
      "pin name=y1 dir=out placed=1 rect=4950,1950,5050,2050\n"
      "pin name=y2 dir=out placed=1 rect=4950,2950,5050,3050\n",
      // Unknown direction.
      "floorplan die=0,0,5000,5000 core=0,0,5000,5000 rows=1 track_grids=10 "
      "io_placed=6 io_total=6\n"
      "row rect=0,0,5000,3200\n"
      "pin name=a dir=sideways placed=1 rect=-50,950,50,1050\n",
      // Duplicate pin name.
      "floorplan die=0,0,5000,5000 core=0,0,5000,5000 rows=1 track_grids=10 "
      "io_placed=2 io_total=2\n"
      "row rect=0,0,5000,3200\n"
      "pin name=a dir=in placed=1 rect=-50,950,50,1050\n"
      "pin name=a dir=in placed=1 rect=-50,1950,50,2050\n",
      // Empty die rectangle.
      "floorplan die=0,0,0,0 core=0,0,5000,5000 rows=1 track_grids=10 "
      "io_placed=6 io_total=6\n",
      // Empty row rectangle.
      "floorplan die=0,0,5000,5000 core=0,0,5000,5000 rows=1 track_grids=10 "
      "io_placed=6 io_total=6\n"
      "row rect=0,0,5000,0\n",
      // Non-integer count.
      "floorplan die=0,0,5000,5000 core=0,0,5000,5000 rows=one "
      "track_grids=10 io_placed=6 io_total=6\n",
      // Short rectangle.
      "floorplan die=0,0,5000 core=0,0,5000,5000 rows=1 track_grids=10 "
      "io_placed=6 io_total=6\n",
      // Unknown line tag.
      "floorplan die=0,0,5000,5000 core=0,0,5000,5000 rows=1 track_grids=10 "
      "io_placed=6 io_total=6\n"
      "net name=n1\n",
      // Empty line in the middle.
      "floorplan die=0,0,5000,5000 core=0,0,5000,5000 rows=1 track_grids=10 "
      "io_placed=6 io_total=6\n"
      "\n"
      "row rect=0,0,5000,3200\n",
  };
  for (const char* payload : bad) {
    FloorplanModel model;
    EXPECT_FALSE(model.LoadPayload(payload)) << payload;
    EXPECT_FALSE(model.Loaded());
  }
}

TEST(FloorplanModel, ReloadReplacesPreviousFloorplan) {
  FloorplanModel model;
  ASSERT_TRUE(model.LoadPayload(kFloorplanPayload));
  ASSERT_EQ(model.Rows().size(), 1u);
  ASSERT_TRUE(model.LoadPayload(
      "floorplan die=0,0,10000,10000 core=0,0,10000,10000 rows=3 "
      "track_grids=2 io_placed=2 io_total=2\n"
      "row rect=0,0,10000,2720\n"
      "row rect=0,2720,10000,5440\n"
      "row rect=0,5440,10000,8160\n"
      "pin name=in1 dir=in placed=1 rect=-50,4950,50,5050\n"
      "pin name=out1 dir=out placed=1 rect=9950,4950,10050,5050\n"));
  EXPECT_EQ(model.DieArea(), (FloorplanRect{0, 0, 10000, 10000}));
  EXPECT_EQ(model.Rows().size(), 3u);
  EXPECT_EQ(model.Pins().size(), 2u);
  EXPECT_EQ(model.Pins()[1].name, "out1");
  // A malformed reload leaves the previous floorplan untouched.
  EXPECT_FALSE(model.LoadPayload("garbage\n"));
  EXPECT_TRUE(model.Loaded());
  EXPECT_EQ(model.Rows().size(), 3u);
  model.Clear();
  EXPECT_FALSE(model.Loaded());
  EXPECT_TRUE(model.Rows().empty());
  EXPECT_TRUE(model.Pins().empty());
}

TEST(PlacementModel, ParsesFullPayload) {
  PlacementModel model;
  ASSERT_TRUE(model.LoadPayload(kPlacementPayload));
  EXPECT_TRUE(model.Loaded());
  EXPECT_EQ(model.DieArea(), (PlacementRect{0, 0, 5000, 5000}));
  EXPECT_EQ(model.CoreArea(), (PlacementRect{0, 0, 5000, 3200}));
  EXPECT_EQ(model.Placed(), 4u);
  EXPECT_EQ(model.Total(), 4u);
  EXPECT_EQ(model.Hpwl(), 12345);
  EXPECT_EQ(model.Overlaps(), 0u);
  ASSERT_EQ(model.Instances().size(), 4u);
  EXPECT_EQ(model.Instances()[0].name, "u1");
  EXPECT_EQ(model.Instances()[0].master, "AND2");
  EXPECT_EQ(model.Instances()[0].rect, (PlacementRect{200, 200, 660, 2920}));
  EXPECT_EQ(model.Instances()[3].name, "u4");
  EXPECT_EQ(model.Instances()[3].master, "BUF");
  EXPECT_EQ(model.Instances()[3].rect,
            (PlacementRect{2300, 200, 2760, 2920}));
  ASSERT_EQ(model.Slacks().size(), 1u);
  EXPECT_EQ(model.Slacks()[0].scene, "func_tt");
  EXPECT_DOUBLE_EQ(model.Slacks()[0].worst_slack_ns, 0.05);
}

TEST(PlacementModel, ParsesPayloadWithoutSlack) {
  PlacementModel model;
  ASSERT_TRUE(model.LoadPayload(
      "placement die=0,0,5000,5000 core=0,0,5000,3200 placed=2 total=2 "
      "hpwl=100 overlaps=0\n"
      "inst name=u1 master=BUFX2 x=1000 y=1000 w=460 h=2720\n"
      "inst name=u2 master=BUFX2 x=2000 y=1000 w=460 h=2720\n"));
  EXPECT_TRUE(model.Loaded());
  EXPECT_EQ(model.Instances().size(), 2u);
  EXPECT_TRUE(model.Slacks().empty());
}

TEST(PlacementModel, RejectsMalformedPayloads) {
  const char* bad[] = {
      // No header.
      "inst name=u1 master=AND2 x=200 y=200 w=460 h=2720\n",
      // Wrong tag.
      "floorplan die=0,0,5000,5000 core=0,0,5000,3200 placed=4 total=4 "
      "hpwl=12345 overlaps=0\n",
      // Unknown header key.
      "placement die=0,0,5000,5000 core=0,0,5000,3200 placed=4 total=4 "
      "hpwl=12345 overlaps=0 extra=1\n",
      // Missing header key.
      "placement die=0,0,5000,5000 core=0,0,5000,3200 placed=4 total=4 "
      "hpwl=12345\n",
      // Placed exceeds total.
      "placement die=0,0,5000,5000 core=0,0,5000,3200 placed=5 total=4 "
      "hpwl=12345 overlaps=0\n",
      // Instance count mismatch.
      "placement die=0,0,5000,5000 core=0,0,5000,3200 placed=4 total=4 "
      "hpwl=12345 overlaps=0\n"
      "inst name=u1 master=AND2 x=200 y=200 w=460 h=2720\n",
      // Duplicate instance name.
      "placement die=0,0,5000,5000 core=0,0,5000,3200 placed=2 total=2 "
      "hpwl=100 overlaps=0\n"
      "inst name=u1 master=AND2 x=200 y=200 w=460 h=2720\n"
      "inst name=u1 master=BUF x=900 y=200 w=460 h=2720\n",
      // Empty die rectangle.
      "placement die=0,0,0,0 core=0,0,5000,3200 placed=4 total=4 "
      "hpwl=12345 overlaps=0\n",
      // Empty instance rectangle.
      "placement die=0,0,5000,5000 core=0,0,5000,3200 placed=1 total=1 "
      "hpwl=100 overlaps=0\n"
      "inst name=u1 master=AND2 x=200 y=200 w=0 h=2720\n",
      // Non-integer HPWL.
      "placement die=0,0,5000,5000 core=0,0,5000,3200 placed=4 total=4 "
      "hpwl=many overlaps=0\n",
      // Short rectangle.
      "placement die=0,0,5000 core=0,0,5000,3200 placed=4 total=4 "
      "hpwl=12345 overlaps=0\n",
      // Unknown line tag.
      "placement die=0,0,5000,5000 core=0,0,5000,3200 placed=4 total=4 "
      "hpwl=12345 overlaps=0\n"
      "net name=n1\n",
      // Duplicate slack scene.
      "placement die=0,0,5000,5000 core=0,0,5000,3200 placed=1 total=1 "
      "hpwl=100 overlaps=0\n"
      "inst name=u1 master=AND2 x=200 y=200 w=460 h=2720\n"
      "slack scene=func_tt worst_slack_ns=0.05\n"
      "slack scene=func_tt worst_slack_ns=0.06\n",
      // Non-numeric slack.
      "placement die=0,0,5000,5000 core=0,0,5000,3200 placed=1 total=1 "
      "hpwl=100 overlaps=0\n"
      "inst name=u1 master=AND2 x=200 y=200 w=460 h=2720\n"
      "slack scene=func_tt worst_slack_ns=fast\n",
      // Empty line in the middle.
      "placement die=0,0,5000,5000 core=0,0,5000,3200 placed=4 total=4 "
      "hpwl=12345 overlaps=0\n"
      "\n"
      "inst name=u1 master=AND2 x=200 y=200 w=460 h=2720\n",
  };
  for (const char* payload : bad) {
    PlacementModel model;
    EXPECT_FALSE(model.LoadPayload(payload)) << payload;
    EXPECT_FALSE(model.Loaded());
  }
}

TEST(PlacementModel, ReloadReplacesPreviousPlacement) {
  PlacementModel model;
  ASSERT_TRUE(model.LoadPayload(kPlacementPayload));
  ASSERT_EQ(model.Instances().size(), 4u);
  ASSERT_TRUE(model.LoadPayload(
      "placement die=0,0,10000,10000 core=0,0,9660,8160 placed=2 total=2 "
      "hpwl=100 overlaps=0\n"
      "inst name=u1 master=BUFX2 x=1000 y=1000 w=460 h=2720\n"
      "inst name=u2 master=BUFX2 x=2000 y=1000 w=460 h=2720\n"));
  EXPECT_EQ(model.DieArea(), (PlacementRect{0, 0, 10000, 10000}));
  EXPECT_EQ(model.Instances().size(), 2u);
  EXPECT_EQ(model.Instances()[1].name, "u2");
  EXPECT_TRUE(model.Slacks().empty());
  // A malformed reload leaves the previous placement untouched.
  EXPECT_FALSE(model.LoadPayload("garbage\n"));
  EXPECT_TRUE(model.Loaded());
  EXPECT_EQ(model.Instances().size(), 2u);
  model.Clear();
  EXPECT_FALSE(model.Loaded());
  EXPECT_TRUE(model.Instances().empty());
  EXPECT_TRUE(model.Slacks().empty());
}

void RegisterIssuesCommand(CommandRegistry& registry,
                           const std::string& payload, bool ok,
                           int* calls = nullptr) {
  auto status = registry.Register(
      "synth_issues", "synth_issues",
      [payload, ok, calls](CommandContext&,
                           const std::vector<std::string>&) {
        if (calls != nullptr) ++(*calls);
        if (!ok) {
          return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                     "no elaborated design");
        }
        return CommandResult::Ok(payload);
      });
  EXPECT_TRUE(status.ok());
}

TEST(SchematicOutline, GatesDrawWhiteOutlineWithoutFill) {
  OutlineSchematicView fixture;
  ASSERT_FALSE(fixture.view.Schematic().Empty());
  SchematicDiagram diagram(fixture.view);
  diagram.Refresh();
  const QGraphicsScene* scene = diagram.Scene();
  ASSERT_NE(scene, nullptr);
  const std::vector<QGraphicsPathItem*> paths =
      OutlineSceneItems<QGraphicsPathItem>(scene);
  // Three AND gates plus six terminal arrowheads.
  ASSERT_EQ(paths.size(), 9u);
  std::size_t gates = 0;
  std::size_t arrows = 0;
  for (const QGraphicsPathItem* path : paths) {
    EXPECT_EQ(path->brush().style(), Qt::NoBrush);
    EXPECT_EQ(path->pen().color(), Qt::white);
    const QRectF bounds = path->path().boundingRect();
    if (std::abs(bounds.height() - 64) < 0.01) ++gates;
    if (OutlineRectNear(bounds,
                        QRectF(bounds.left(), bounds.top(), 16, 16))) {
      ++arrows;
    }
  }
  EXPECT_EQ(gates, 3u);
  EXPECT_EQ(arrows, 6u);
}

TEST(SchematicOutline, GenericGateDrawsWhiteOutlineRect) {
  OutlineTestApp();
  CommandRegistry registry;
  RegisterHierarchyCommand(
      registry,
      "top=adder\nnode path=adder module=adder ports=a,b,y\n", true);
  RegisterSchematicCommand(registry, nullptr);
  CommandContext context;
  context.synth.elaborated = true;
  SessionView view(registry, context);
  view.RefreshHierarchy();
  view.RefreshSchematic();
  ASSERT_FALSE(view.Schematic().Empty());
  SchematicDiagram diagram(view);
  diagram.Refresh();
  const QGraphicsScene* scene = diagram.Scene();
  ASSERT_NE(scene, nullptr);
  // The $add gate draws as a rectangle, not a path; the other
  // rect is the transparent hover rect over the gate.
  const std::vector<QGraphicsRectItem*> rects =
      OutlineSceneItems<QGraphicsRectItem>(scene);
  ASSERT_EQ(rects.size(), 2u);
  std::size_t outlined = 0;
  std::size_t hover = 0;
  for (const QGraphicsRectItem* rect : rects) {
    EXPECT_EQ(rect->brush().style(), Qt::NoBrush);
    if (rect->pen().style() == Qt::NoPen) {
      ++hover;
    } else {
      EXPECT_EQ(rect->pen().color(), Qt::white);
      ++outlined;
    }
  }
  EXPECT_EQ(outlined, 1u);
  EXPECT_EQ(hover, 1u);
  // Three terminal arrowheads, all outline-only.
  const std::vector<QGraphicsPathItem*> paths =
      OutlineSceneItems<QGraphicsPathItem>(scene);
  ASSERT_EQ(paths.size(), 3u);
  for (const QGraphicsPathItem* path : paths) {
    EXPECT_EQ(path->brush().style(), Qt::NoBrush);
    EXPECT_EQ(path->pen().color(), Qt::white);
  }
}

TEST(SchematicOutline, TerminalArrowsPointInSignalDirection) {
  OutlineSchematicView fixture;
  ASSERT_FALSE(fixture.view.Schematic().Empty());
  SchematicDiagram diagram(fixture.view);
  diagram.Refresh();
  const QGraphicsScene* scene = diagram.Scene();
  ASSERT_NE(scene, nullptr);
  const std::vector<QGraphicsPathItem*> paths =
      OutlineSceneItems<QGraphicsPathItem>(scene);
  std::size_t inputs = 0;
  std::size_t outputs = 0;
  for (const QGraphicsPathItem* path : paths) {
    const QRectF bounds = path->path().boundingRect();
    if (std::abs(bounds.height() - 64) < 0.01) continue;
    ASSERT_EQ(path->path().elementCount(), 4); // moveTo, two lineTo, plus the closing line from closeSubpath
    const QPointF center(bounds.center().x(), bounds.center().y());
    const QPointF right(bounds.right(), center.y());
    if (bounds.left() < 100) {
      // Input arrowhead: the path starts at the tip, which
      // meets the wire at the anchor.
      EXPECT_EQ(path->path().elementAt(0).x, right.x());
      EXPECT_EQ(path->path().elementAt(0).y, right.y());
      ++inputs;
    } else {
      // Output arrowhead: the path ends at the tip, which
      // points outward away from the design.
      EXPECT_EQ(path->path().elementAt(2).x, right.x());
      EXPECT_EQ(path->path().elementAt(2).y, right.y());
      ++outputs;
    }
  }
  EXPECT_EQ(inputs, 4u);
  EXPECT_EQ(outputs, 2u);
}

TEST(SchematicOutline, LabelsAreWhite) {
  OutlineSchematicView fixture;
  ASSERT_FALSE(fixture.view.Schematic().Empty());
  SchematicDiagram diagram(fixture.view);
  diagram.Refresh();
  const QGraphicsScene* scene = diagram.Scene();
  ASSERT_NE(scene, nullptr);
  const std::vector<QGraphicsSimpleTextItem*> texts =
      OutlineSceneItems<QGraphicsSimpleTextItem>(scene);
  // Gate labels, terminal labels, and net labels.
  ASSERT_GE(texts.size(), 12u);
  for (const QGraphicsSimpleTextItem* text : texts) {
    EXPECT_EQ(text->brush().color(), Qt::white) << text->text().toStdString();
  }
}

TEST(SchematicOutline, EmptyPlaceholderIsWhite) {
  OutlineTestApp();
  CommandRegistry registry;
  CommandContext context;
  SessionView view(registry, context);
  SchematicDiagram diagram(view);
  diagram.Refresh();
  const std::vector<QGraphicsSimpleTextItem*> texts =
      OutlineSceneItems<QGraphicsSimpleTextItem>(diagram.Scene());
  ASSERT_EQ(texts.size(), 1u);
  EXPECT_EQ(texts[0]->text().toStdString(), "No design loaded.");
  EXPECT_EQ(texts[0]->brush().color(), Qt::white);
}

TEST(SchematicOutline, ClickSelectsGateWithBlueOutline) {
  OutlineTestApp();
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kOutlineTwoGateHierarchy, true);
  RegisterTwoGateSchematic(registry);
  CommandContext context;
  context.synth.elaborated = true;
  SessionView view(registry, context);
  view.RefreshHierarchy();
  view.RefreshSchematic();
  ASSERT_FALSE(view.Schematic().Empty());
  SchematicDiagram diagram(view);
  diagram.show();
  QApplication::processEvents();
  diagram.Refresh();
  QApplication::processEvents();
  const QGraphicsScene* scene = diagram.Scene();
  ASSERT_NE(scene, nullptr);
  auto* graphics_view = diagram.findChild<QGraphicsView*>();
  ASSERT_NE(graphics_view, nullptr);
  const std::vector<QGraphicsPathItem*> before =
      OutlineSceneItems<QGraphicsPathItem>(scene);
  const QGraphicsPathItem* target = nullptr;
  for (const QGraphicsPathItem* path : before) {
    if (std::abs(path->path().boundingRect().height() - 64) < 0.01) {
      target = path;
      break;
    }
  }
  ASSERT_NE(target, nullptr);
  const QPointF center = target->path().boundingRect().center();
  const QPoint viewport_point = graphics_view->mapFromScene(center);
  QMouseEvent press(QEvent::MouseButtonPress, QPointF(viewport_point),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(graphics_view->viewport(), &press);
  // The click rebuilds the scene, so re-collect the items.
  const std::vector<QGraphicsPathItem*> after =
      OutlineSceneItems<QGraphicsPathItem>(scene);
  std::size_t blue = 0;
  std::size_t gray = 0;
  for (const QGraphicsPathItem* path : after) {
    if (std::abs(path->path().boundingRect().height() - 64) > 0.01) {
      continue;
    }
    EXPECT_EQ(path->brush().style(), Qt::NoBrush);
    if (path->pen().color() == Qt::blue) {
      ++blue;
      EXPECT_EQ(path->pen().widthF(), 2);
    } else if (path->pen().color() == Qt::gray) {
      ++gray;
    }
  }
  EXPECT_EQ(blue, 1u);
  EXPECT_EQ(gray, 1u);
}

TEST(SchematicOutline, ClickHighlightsOnlyConeWiresAndPins) {
  OutlineTestApp();
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kOutlineAluHierarchy, true);
  RegisterAluSchematic(registry);
  CommandContext context;
  context.synth.elaborated = true;
  SessionView view(registry, context);
  view.RefreshHierarchy();
  view.RefreshSchematic();
  ASSERT_FALSE(view.Schematic().Empty());
  SchematicDiagram diagram(view);
  diagram.show();
  QApplication::processEvents();
  diagram.Refresh();
  QApplication::processEvents();
  diagram.HighlightGate("add");
  const QGraphicsScene* scene = diagram.Scene();
  ASSERT_NE(scene, nullptr);
  // The gate outline holding a type label: generic gates draw
  // as outlined rects, shaped gates as paths, both 64 high.
  auto gate_rect_for_label = [&](const std::string& label) {
    QPointF at;
    bool found = false;
    for (const QGraphicsSimpleTextItem* text :
         OutlineSceneItems<QGraphicsSimpleTextItem>(scene)) {
      if (text->text().toStdString() == label) {
        at = text->pos();
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found) << "label: " << label;
    if (!found) return QRectF();
    for (const QGraphicsRectItem* rect :
         OutlineSceneItems<QGraphicsRectItem>(scene)) {
      if (rect->pen().style() == Qt::NoPen) continue;
      if (std::abs(rect->rect().height() - 64) > 0.01) continue;
      if (rect->rect().contains(at)) return rect->rect();
    }
    for (const QGraphicsPathItem* path :
         OutlineSceneItems<QGraphicsPathItem>(scene)) {
      const QRectF bounds = path->path().boundingRect();
      if (std::abs(bounds.height() - 64) > 0.01) continue;
      if (bounds.contains(at)) return bounds;
    }
    EXPECT_TRUE(false) << "no gate outline holds label: " << label;
    return QRectF();
  };
  const QRectF add_rect = gate_rect_for_label("ADD");
  const QRectF and_rect = gate_rect_for_label("AND");
  const QRectF sub_rect = gate_rect_for_label("SUB");
  const QRectF xor_rect = gate_rect_for_label("XOR");
  const QRectF mux_rect = gate_rect_for_label("PMUX");
  ASSERT_FALSE(add_rect.isNull());
  ASSERT_FALSE(and_rect.isNull());
  ASSERT_FALSE(sub_rect.isNull());
  ASSERT_FALSE(xor_rect.isNull());
  ASSERT_FALSE(mux_rect.isNull());
  const std::vector<QGraphicsLineItem*> lines =
      OutlineSceneItems<QGraphicsLineItem>(scene);
  std::size_t blue = 0;
  std::size_t grey = 0;
  for (const QGraphicsLineItem* line : lines) {
    if (line->pen().color() == Qt::blue) {
      ++blue;
    } else if (line->pen().color() == Qt::lightGray) {
      ++grey;
    }
  }
  EXPECT_GT(blue, 0u);
  EXPECT_GT(grey, 0u);
  auto touches = [](const QGraphicsLineItem* line, const QRectF& rect) {
    return rect.contains(line->line().p1()) ||
           rect.contains(line->line().p2());
  };
  // No blue wire touches a sibling gate: the shared input nets
  // light only their ADD branch, and only the ADD branch into
  // the PMUX lights.
  for (const QGraphicsLineItem* line : lines) {
    if (line->pen().color() != Qt::blue) continue;
    for (const QRectF& sibling : {and_rect, sub_rect, xor_rect}) {
      EXPECT_FALSE(touches(line, sibling))
          << "blue line touches a sibling gate";
    }
  }
  // A blue wire reaches ADD and the PMUX.
  bool blue_reaches_add = false;
  bool blue_reaches_mux = false;
  for (const QGraphicsLineItem* line : lines) {
    if (line->pen().color() != Qt::blue) continue;
    if (touches(line, add_rect)) blue_reaches_add = true;
    if (touches(line, mux_rect)) blue_reaches_mux = true;
  }
  EXPECT_TRUE(blue_reaches_add);
  EXPECT_TRUE(blue_reaches_mux);
  // Terminal pins follow the same cone: x and y feed ADD, z is
  // driven by the PMUX, op only selects the PMUX from outside
  // the cone. Input arrowheads are 16 high at the top of
  // their row; the output arrowhead is the only one past the
  // gate columns.
  const double row_pitch = kSchematicGateHeight + kSchematicGateGap;
  auto terminal_color = [&](double top, bool output_side) {
    for (const QGraphicsPathItem* path :
         OutlineSceneItems<QGraphicsPathItem>(scene)) {
      const QRectF bounds = path->path().boundingRect();
      if (std::abs(bounds.height() - 16) > 0.01) continue;
      if (output_side) {
        if (bounds.left() < 300) continue;
      } else {
        // Input arrowheads sit at the left edge; the output
        // arrowhead shares the first row's top, so the left
        // edge is what tells them apart.
        if (bounds.left() > 100) continue;
        if (std::abs(bounds.top() - top) > 0.01) continue;
      }
      return path->pen().color();
    }
    return QColor();
  };
  EXPECT_EQ(terminal_color(kSchematicGateTop, false), Qt::gray);
  EXPECT_EQ(terminal_color(kSchematicGateTop + row_pitch, false),
            Qt::blue);
  EXPECT_EQ(terminal_color(kSchematicGateTop + 2 * row_pitch, false),
            Qt::blue);
  EXPECT_EQ(terminal_color(0, true), Qt::blue);
}

// A SessionView whose floorplan holds the bridged optloop
// payload: one row, six IO pins.
struct OutlineFloorplanView {
  CommandRegistry registry;
  CommandContext context;
  SessionView view;
  OutlineFloorplanView() : view(registry, context) {
    OutlineTestApp();
    RegisterFloorplanCommand(registry, kFloorplanPayload, true);
    view.RefreshFloorplan();
  }
};

TEST(FloorplanOutline, DrawsDieCoreRowsAndPins) {
  OutlineFloorplanView fixture;
  ASSERT_TRUE(fixture.view.Floorplan().Loaded());
  FloorplanDiagram diagram(fixture.view);
  diagram.show();
  QApplication::processEvents();
  diagram.Refresh();
  QApplication::processEvents();
  const QGraphicsScene* scene = diagram.Scene();
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(diagram.CountsText(),
            "Die: 0,0,5000,5000\nCore: 0,0,5000,5000\nRows: 1\n"
            "Track grids: 10\nIO placed: 6 / 6");
  const std::vector<QGraphicsRectItem*> rects =
      OutlineSceneItems<QGraphicsRectItem>(scene);
  // Die, core, one row, six pin outlines, six pin hover rects.
  EXPECT_EQ(rects.size(), 15u);
  std::size_t die = 0;
  std::size_t core = 0;
  std::size_t rows = 0;
  std::size_t pins = 0;
  std::size_t hover = 0;
  for (const QGraphicsRectItem* rect : rects) {
    EXPECT_EQ(rect->brush().style(), Qt::NoBrush);
    if (rect->pen().style() == Qt::NoPen) {
      ++hover;
      continue;
    }
    // Rows are grey by design; die, core, and pins are white.
    if (rect->pen().color() == Qt::gray &&
        OutlineRectNear(rect->rect(), QRectF(0, -3200, 5000, 3200))) {
      ++rows;
      continue;
    }
    EXPECT_EQ(rect->pen().color(), Qt::white);
    if (rect->pen().widthF() == 2 &&
        OutlineRectNear(rect->rect(), QRectF(0, -5000, 5000, 5000))) {
      ++die;
    } else if (rect->pen().widthF() == 1 &&
               OutlineRectNear(rect->rect(),
                               QRectF(0, -5000, 5000, 5000))) {
      ++core;
    } else if (rect->pen().widthF() == 1 &&
               std::abs(rect->rect().width() - 100) < 0.01 &&
               std::abs(rect->rect().height() - 100) < 0.01) {
      ++pins;
    }
  }
  EXPECT_EQ(die, 1u);
  EXPECT_EQ(core, 1u);
  EXPECT_EQ(rows, 1u);
  EXPECT_EQ(pins, 6u);
  EXPECT_EQ(hover, 6u);
  // Every pin is labelled by name, in white text.
  const std::vector<QGraphicsSimpleTextItem*> texts =
      OutlineSceneItems<QGraphicsSimpleTextItem>(scene);
  ASSERT_EQ(texts.size(), 6u);
  for (const QGraphicsSimpleTextItem* text : texts) {
    EXPECT_EQ(text->brush().color(), Qt::white) << text->text().toStdString();
  }
  std::vector<std::string> names;
  for (const QGraphicsSimpleTextItem* text : texts) {
    names.push_back(text->text().toStdString());
  }
  std::sort(names.begin(), names.end());
  EXPECT_EQ(names, (std::vector<std::string>{"a", "b", "c", "d", "y1", "y2"}));
}

TEST(FloorplanOutline, EmptyPlaceholderIsWhite) {
  OutlineTestApp();
  CommandRegistry registry;
  CommandContext context;
  SessionView view(registry, context);
  FloorplanDiagram diagram(view);
  diagram.Refresh();
  const std::vector<QGraphicsSimpleTextItem*> texts =
      OutlineSceneItems<QGraphicsSimpleTextItem>(diagram.Scene());
  ASSERT_EQ(texts.size(), 1u);
  EXPECT_EQ(texts[0]->text().toStdString(),
            "(no floorplan yet: run eos::floorplan first)");
  EXPECT_EQ(texts[0]->brush().color(), Qt::white);
  EXPECT_EQ(diagram.CountsText(),
            "(no floorplan yet: run eos::floorplan first)");
}

struct OutlinePlacementView {
  CommandRegistry registry;
  CommandContext context;
  SessionView view;
  OutlinePlacementView() : view(registry, context) {
    OutlineTestApp();
    RegisterPlacementCommand(registry, kPlacementPayload, true);
    view.RefreshPlacement();
  }
};

TEST(PlacementOutline, DrawsDieCoreAndInstances) {
  OutlinePlacementView fixture;
  ASSERT_TRUE(fixture.view.Placement().Loaded());
  PlacementDiagram diagram(fixture.view);
  diagram.show();
  QApplication::processEvents();
  diagram.Refresh();
  QApplication::processEvents();
  const QGraphicsScene* scene = diagram.Scene();
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(diagram.CountsText(),
            "Die: 0,0,5000,5000\nCore: 0,0,5000,3200\nPlaced: 4 / 4\n"
            "HPWL: 12345\nOverlaps: 0\nfunc_tt worst slack: 0.05 ns\n"
            "Masters:\nAND2: 3\nBUF: 1");
  const std::vector<QGraphicsRectItem*> rects =
      OutlineSceneItems<QGraphicsRectItem>(scene);
  // Die, core, four instance outlines, four instance hover rects.
  EXPECT_EQ(rects.size(), 10u);
  std::size_t die = 0;
  std::size_t core = 0;
  std::size_t instances = 0;
  std::size_t hover = 0;
  // Scene items come back in stacking order, not payload
  // order, so instance colors are keyed by the rect's left
  // edge, which identifies the instance: u1 at 200, u2 at 900,
  // u3 at 1600 (all AND2), u4 at 2300 (BUF).
  std::map<qreal, QColor> color_by_left;
  for (const QGraphicsRectItem* rect : rects) {
    if (rect->pen().style() == Qt::NoPen) {
      ++hover;
      continue;
    }
    if (rect->pen().widthF() == 2 &&
        OutlineRectNear(rect->rect(), QRectF(0, -5000, 5000, 5000))) {
      EXPECT_EQ(rect->pen().color(), Qt::white);
      EXPECT_EQ(rect->brush().style(), Qt::NoBrush);
      ++die;
    } else if (rect->pen().widthF() == 1 &&
               OutlineRectNear(rect->rect(),
                               QRectF(0, -3200, 5000, 3200))) {
      EXPECT_EQ(rect->pen().color(), Qt::white);
      EXPECT_EQ(rect->brush().style(), Qt::NoBrush);
      ++core;
    } else if (rect->pen().widthF() == 1 &&
               std::abs(rect->rect().width() - 460) < 0.01 &&
               std::abs(rect->rect().height() - 2720) < 0.01) {
      // Instance rectangles carry their master's color on the
      // outline and a light fill of the same color.
      EXPECT_EQ(rect->brush().color().alpha(), 64);
      EXPECT_EQ(rect->brush().color().rgb(),
                rect->pen().color().rgb());
      color_by_left[rect->rect().left()] = rect->pen().color();
      ++instances;
    }
  }
  EXPECT_EQ(die, 1u);
  EXPECT_EQ(core, 1u);
  EXPECT_EQ(instances, 4u);
  EXPECT_EQ(hover, 4u);
  // Same master, same color; different masters, different
  // colors.
  ASSERT_EQ(color_by_left.size(), 4u);
  EXPECT_EQ(color_by_left[200], color_by_left[900]);
  EXPECT_EQ(color_by_left[200], color_by_left[1600]);
  EXPECT_NE(color_by_left[200], color_by_left[2300]);
  // Every instance is labelled by name, in white text.
  const std::vector<QGraphicsSimpleTextItem*> texts =
      OutlineSceneItems<QGraphicsSimpleTextItem>(scene);
  ASSERT_EQ(texts.size(), 4u);
  for (const QGraphicsSimpleTextItem* text : texts) {
    EXPECT_EQ(text->brush().color(), Qt::white) << text->text().toStdString();
  }
  std::vector<std::string> names;
  for (const QGraphicsSimpleTextItem* text : texts) {
    names.push_back(text->text().toStdString());
  }
  std::sort(names.begin(), names.end());
  EXPECT_EQ(names, (std::vector<std::string>{"u1", "u2", "u3", "u4"}));
}

TEST(PlacementOutline, EmptyPlaceholderIsWhite) {
  OutlineTestApp();
  CommandRegistry registry;
  CommandContext context;
  SessionView view(registry, context);
  PlacementDiagram diagram(view);
  diagram.Refresh();
  const std::vector<QGraphicsSimpleTextItem*> texts =
      OutlineSceneItems<QGraphicsSimpleTextItem>(diagram.Scene());
  ASSERT_EQ(texts.size(), 1u);
  EXPECT_EQ(texts[0]->text().toStdString(),
            "(no placement yet: run eos::place first)");
  EXPECT_EQ(texts[0]->brush().color(), Qt::white);
  EXPECT_EQ(diagram.CountsText(),
            "(no placement yet: run eos::place first)");
}

TEST(SchematicOutline, MuxDrawsTrapezoid) {
  OutlineTestApp();
  CommandRegistry registry;
  CommandContext context;
  SessionView view(registry, context);
  SchematicDiagram diagram(view);
  const QGraphicsScene* scene =
      BuildSymsScene(registry, context, view, diagram);
  ASSERT_NE(scene, nullptr);
  const QRectF mux_rect = SymsGateRectForLabel(scene, "MUX");
  ASSERT_FALSE(mux_rect.isNull());
  // The trapezoid: full height on the input (left) edge, a
  // short centered segment on the output (right) edge. No
  // point of the shape sits at the top or bottom of the
  // right edge, and the left edge spans the full height.
  bool found_shape = false;
  for (const QGraphicsPathItem* path :
       OutlineSceneItems<QGraphicsPathItem>(scene)) {
    const QPainterPath& p = path->path();
    if (std::abs(p.boundingRect().height() - 64) > 0.01) continue;
    if (!p.boundingRect().contains(mux_rect.center())) continue;
    found_shape = true;
    std::set<std::pair<double, double>> points;
    for (int i = 0; i < p.elementCount(); ++i) {
      const QPainterPath::Element e = p.elementAt(i);
      points.insert({e.x, e.y});
    }
    const double left = mux_rect.left();
    const double right = mux_rect.right();
    const double top = mux_rect.top();
    const double bottom = mux_rect.bottom();
    const double middle = mux_rect.center().y();
    EXPECT_TRUE(points.count({left, top}) != 0);
    EXPECT_TRUE(points.count({left, bottom}) != 0);
    EXPECT_TRUE(points.count({right, middle - 12}) != 0);
    EXPECT_TRUE(points.count({right, middle + 12}) != 0);
    EXPECT_TRUE(points.count({right, top}) == 0);
    EXPECT_TRUE(points.count({right, bottom}) == 0);
    EXPECT_EQ(path->brush().style(), Qt::NoBrush);
    EXPECT_EQ(path->pen().color(), Qt::white);
  }
  EXPECT_TRUE(found_shape);
}

TEST(SchematicOutline, DffDrawsClockTriangle) {
  OutlineTestApp();
  CommandRegistry registry;
  CommandContext context;
  SessionView view(registry, context);
  SchematicDiagram diagram(view);
  const QGraphicsScene* scene =
      BuildSymsScene(registry, context, view, diagram);
  ASSERT_NE(scene, nullptr);
  const QRectF ff_rect = SymsGateRectForLabel(scene, "ADFF");
  ASSERT_FALSE(ff_rect.isNull());
  // The flip-flop itself is a rectangle; the clock triangle
  // is the only small triangular path in the scene: 10 wide,
  // 12 high, its base on the left edge of the ADFF rect.
  bool found_triangle = false;
  for (const QGraphicsPathItem* path :
       OutlineSceneItems<QGraphicsPathItem>(scene)) {
    const QRectF bounds = path->path().boundingRect();
    if (std::abs(bounds.width() - 10) > 0.01) continue;
    if (std::abs(bounds.height() - 12) > 0.01) continue;
    EXPECT_EQ(bounds.left(), ff_rect.left());
    EXPECT_GE(bounds.center().y(), ff_rect.top());
    EXPECT_LE(bounds.center().y(), ff_rect.bottom());
    EXPECT_EQ(path->path().elementCount(), 4);
    EXPECT_EQ(path->brush().style(), Qt::NoBrush);
    EXPECT_EQ(path->pen().color(), Qt::white);
    found_triangle = true;
  }
  EXPECT_TRUE(found_triangle);
}

TEST(SchematicOutline, OperatorGlyphAndEquationThirdLines) {
  OutlineTestApp();
  CommandRegistry registry;
  CommandContext context;
  SessionView view(registry, context);
  SchematicDiagram diagram(view);
  const QGraphicsScene* scene =
      BuildSymsScene(registry, context, view, diagram);
  ASSERT_NE(scene, nullptr);
  auto third_line_in_rect = [&](const std::string& text,
                                const QRectF& rect) {
    const QGraphicsSimpleTextItem* item = SymsTextItem(scene, text);
    EXPECT_NE(item, nullptr) << "missing third line: " << text;
    if (item == nullptr) return;
    EXPECT_TRUE(rect.contains(item->pos()))
        << "third line outside its gate: " << text;
    EXPECT_LE(item->sceneBoundingRect().bottom(), rect.bottom() + 0.01)
        << "third line overflows its gate: " << text;
    EXPECT_EQ(item->brush().color(), Qt::white);
  };
  // Comparator glyph, memory equation, shift glyph: each on
  // the third line of its own gate, inside the gate rect.
  const QRectF eq_rect = SymsGateRectForLabel(scene, "EQ");
  const QRectF mem_rect = SymsGateRectForLabel(scene, "MEM");
  const QRectF shl_rect = SymsGateRectForLabel(scene, "SHL");
  ASSERT_FALSE(eq_rect.isNull());
  ASSERT_FALSE(mem_rect.isNull());
  ASSERT_FALSE(shl_rect.isNull());
  third_line_in_rect("=", eq_rect);
  third_line_in_rect("Q = mem[A]", mem_rect);
  third_line_in_rect("<<", shl_rect);
}

TEST(SchematicOutline, ArithmeticGlyphsOnAluBoxes) {
  OutlineTestApp();
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kOutlineAluHierarchy, true);
  RegisterAluSchematic(registry);
  CommandContext context;
  context.synth.elaborated = true;
  SessionView view(registry, context);
  view.RefreshHierarchy();
  view.RefreshSchematic();
  ASSERT_FALSE(view.Schematic().Empty());
  SchematicDiagram diagram(view);
  diagram.show();
  QApplication::processEvents();
  diagram.Refresh();
  QApplication::processEvents();
  const QGraphicsScene* scene = diagram.Scene();
  ASSERT_NE(scene, nullptr);
  const QRectF add_rect = SymsGateRectForLabel(scene, "ADD");
  const QRectF sub_rect = SymsGateRectForLabel(scene, "SUB");
  ASSERT_FALSE(add_rect.isNull());
  ASSERT_FALSE(sub_rect.isNull());
  const QGraphicsSimpleTextItem* plus = SymsTextItem(scene, "+");
  const QGraphicsSimpleTextItem* minus = SymsTextItem(scene, "-");
  ASSERT_NE(plus, nullptr);
  ASSERT_NE(minus, nullptr);
  EXPECT_TRUE(add_rect.contains(plus->pos()));
  EXPECT_TRUE(sub_rect.contains(minus->pos()));
  EXPECT_LE(plus->sceneBoundingRect().bottom(), add_rect.bottom() + 0.01);
  EXPECT_LE(minus->sceneBoundingRect().bottom(), sub_rect.bottom() + 0.01);
}

TEST(BlockOutline, BlocksDrawWhiteOutlineWithoutFill) {
  OutlineBlockView fixture;
  ASSERT_FALSE(fixture.view.Diagram().Empty());
  BlockDiagram diagram(fixture.view);
  diagram.Refresh();
  const QGraphicsScene* scene = diagram.Scene();
  ASSERT_NE(scene, nullptr);
  const std::vector<QGraphicsRectItem*> rects =
      OutlineSceneItems<QGraphicsRectItem>(scene);
  ASSERT_EQ(rects.size(), 2u);
  for (const QGraphicsRectItem* rect : rects) {
    EXPECT_EQ(rect->brush().style(), Qt::NoBrush);
    EXPECT_EQ(rect->pen().color(), Qt::white);
  }
  const std::vector<QGraphicsSimpleTextItem*> texts =
      OutlineSceneItems<QGraphicsSimpleTextItem>(scene);
  ASSERT_FALSE(texts.empty());
  for (const QGraphicsSimpleTextItem* text : texts) {
    EXPECT_EQ(text->brush().color(), Qt::white) << text->text().toStdString();
  }
}

TEST(BlockOutline, HighlightedBlockOutlinesBlueWithoutFill) {
  OutlineBlockView fixture;
  ASSERT_FALSE(fixture.view.Diagram().Empty());
  ASSERT_TRUE(fixture.view.SelectHierarchyPath("top/u_ctrl"));
  BlockDiagram diagram(fixture.view);
  diagram.Refresh();
  const QGraphicsScene* scene = diagram.Scene();
  ASSERT_NE(scene, nullptr);
  const std::vector<QGraphicsRectItem*> rects =
      OutlineSceneItems<QGraphicsRectItem>(scene);
  ASSERT_EQ(rects.size(), 2u);
  for (const QGraphicsRectItem* rect : rects) {
    EXPECT_EQ(rect->brush().style(), Qt::NoBrush);
    // u_ctrl is the first block row (y 40), u_mid the second.
    // u_mid sits outside u_ctrl's cone (the two blocks share
    // no net), so it outlines grey while u_ctrl is traced.
    if (std::abs(rect->rect().top() - 40) < 0.01) {
      EXPECT_EQ(rect->pen().color(), Qt::blue);
      EXPECT_EQ(rect->pen().widthF(), 3);
    } else {
      EXPECT_EQ(rect->pen().color(), Qt::gray);
    }
  }
}

TEST(BlockOutline, TerminalArrowsPointInSignalDirection) {
  OutlineBlockView fixture;
  ASSERT_FALSE(fixture.view.Diagram().Empty());
  BlockDiagram diagram(fixture.view);
  diagram.Refresh();
  const QGraphicsScene* scene = diagram.Scene();
  ASSERT_NE(scene, nullptr);
  const std::vector<QGraphicsPathItem*> paths =
      OutlineSceneItems<QGraphicsPathItem>(scene);
  // Three input terminals (a, b, sel) and one output (y).
  ASSERT_EQ(paths.size(), 4u);
  std::size_t inputs = 0;
  std::size_t outputs = 0;
  for (const QGraphicsPathItem* path : paths) {
    EXPECT_EQ(path->brush().style(), Qt::NoBrush);
    EXPECT_EQ(path->pen().color(), Qt::white);
    ASSERT_EQ(path->path().elementCount(), 4); // moveTo, two lineTo, plus the closing line from closeSubpath
    const QRectF bounds = path->path().boundingRect();
    EXPECT_TRUE(OutlineRectNear(
        bounds, QRectF(bounds.left(), bounds.top(), 16, 16)));
    const QPointF right(bounds.right(), bounds.center().y());
    if (bounds.left() < 100) {
      EXPECT_EQ(path->path().elementAt(0).x, right.x());
      EXPECT_EQ(path->path().elementAt(0).y, right.y());
      ++inputs;
    } else {
      // Block arrowheads use one triangle on both sides, so the
      // tip is element 0 here too; the input/output difference
      // is the attach point, covered by NetLinesAttachAtArrowheadPoints.
      EXPECT_EQ(path->path().elementAt(0).x, right.x());
      EXPECT_EQ(path->path().elementAt(0).y, right.y());
      ++outputs;
    }
  }
  EXPECT_EQ(inputs, 3u);
  EXPECT_EQ(outputs, 1u);
}

TEST(BlockOutline, NetLinesAttachAtArrowheadPoints) {
  OutlineBlockView fixture;
  ASSERT_FALSE(fixture.view.Diagram().Empty());
  BlockDiagram diagram(fixture.view);
  diagram.Refresh();
  const QGraphicsScene* scene = diagram.Scene();
  ASSERT_NE(scene, nullptr);
  const std::vector<QGraphicsLineItem*> lines =
      OutlineSceneItems<QGraphicsLineItem>(scene);
  // Net a attaches at the input arrowhead tip (56, 48); the
  // lone output net y stubs out from the output arrowhead tip
  // (656, 48) to (684, 48).
  bool input_attach = false;
  bool output_stub = false;
  for (const QGraphicsLineItem* line : lines) {
    const QLineF segment = line->line();
    if (segment.p1() == QPointF(56, 48) ||
        segment.p2() == QPointF(56, 48)) {
      input_attach = true;
    }
    if ((segment.p1() == QPointF(656, 48) &&
         segment.p2() == QPointF(684, 48)) ||
        (segment.p1() == QPointF(684, 48) &&
         segment.p2() == QPointF(656, 48))) {
      output_stub = true;
    }
  }
  EXPECT_TRUE(input_attach);
  EXPECT_TRUE(output_stub);
  // A lone block anchor stubs from the block edge itself: the
  // m net leaves the right edge of u_mid, with no gap.
  const std::vector<QGraphicsRectItem*> rects =
      OutlineSceneItems<QGraphicsRectItem>(scene);
  const QGraphicsRectItem* umid = nullptr;
  for (const QGraphicsRectItem* rect : rects) {
    if (std::abs(rect->rect().top() - 152) < 0.01) umid = rect;
  }
  ASSERT_NE(umid, nullptr);
  const QPointF edge(umid->rect().right(), umid->rect().top() + 32);
  bool block_stub = false;
  for (const QGraphicsLineItem* line : lines) {
    if (line->line().p1() == edge || line->line().p2() == edge) {
      block_stub = true;
    }
  }
  EXPECT_TRUE(block_stub);
}

TEST(BlockOutline, EmptyPlaceholderIsWhite) {
  OutlineTestApp();
  CommandRegistry registry;
  CommandContext context;
  SessionView view(registry, context);
  BlockDiagram diagram(view);
  diagram.Refresh();
  const std::vector<QGraphicsSimpleTextItem*> texts =
      OutlineSceneItems<QGraphicsSimpleTextItem>(diagram.Scene());
  ASSERT_EQ(texts.size(), 1u);
  EXPECT_EQ(texts[0]->text().toStdString(), "No design loaded.");
  EXPECT_EQ(texts[0]->brush().color(), Qt::white);
}

TEST(BlockOutline, SingleClickSelectsBlockWithoutDescending) {
  OutlineBlockView fixture;
  ASSERT_FALSE(fixture.view.Diagram().Empty());
  ASSERT_EQ(fixture.view.DiagramLevel(), "top");
  BlockDiagram diagram(fixture.view);
  diagram.Refresh();
  const QGraphicsScene* scene = diagram.Scene();
  ASSERT_NE(scene, nullptr);
  // u_ctrl is the first block row (y 40): click its center.
  // The press is dispatched to the scene directly: a synthetic
  // viewport mouse event never reaches the scene on the
  // offscreen platform (the view swallows it unaccepted), while
  // the scene event is exactly what the block item handles.
  const QGraphicsRectItem* target = nullptr;
  for (const QGraphicsRectItem* rect :
       OutlineSceneItems<QGraphicsRectItem>(scene)) {
    if (std::abs(rect->rect().top() - 40) < 0.01) target = rect;
  }
  ASSERT_NE(target, nullptr);
  QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
  press.setScenePos(target->rect().center());
  press.setButton(Qt::LeftButton);
  press.setButtons(Qt::LeftButton);
  QApplication::sendEvent(const_cast<QGraphicsScene*>(scene), &press);
  // The click updates the model only; the refresh rebuilds the
  // scene, as the main window does through the action callback.
  diagram.Refresh();
  EXPECT_EQ(fixture.view.SelectedHierarchyPath(), "top/u_ctrl");
  EXPECT_EQ(fixture.view.DiagramLevel(), "top");
  // The trace lights only u_ctrl's cone: its sel input spoke
  // and its op output stub. The four other lines stay grey,
  // and no blue line touches the sibling u_mid.
  const std::vector<QGraphicsLineItem*> lines =
      OutlineSceneItems<QGraphicsLineItem>(scene);
  ASSERT_EQ(lines.size(), 6u);
  std::size_t blue = 0;
  std::size_t grey = 0;
  for (const QGraphicsLineItem* line : lines) {
    if (line->pen().color() == Qt::blue) {
      ++blue;
      EXPECT_EQ(line->pen().widthF(), 2);
    } else if (line->pen().color() == Qt::lightGray) {
      ++grey;
    }
  }
  EXPECT_EQ(blue, 2u);
  EXPECT_EQ(grey, 4u);
  const QGraphicsRectItem* umid = nullptr;
  for (const QGraphicsRectItem* rect :
       OutlineSceneItems<QGraphicsRectItem>(scene)) {
    if (std::abs(rect->rect().top() - 152) < 0.01) umid = rect;
  }
  ASSERT_NE(umid, nullptr);
  for (const QGraphicsLineItem* line : lines) {
    if (line->pen().color() != Qt::blue) continue;
    EXPECT_FALSE(umid->rect().contains(line->line().p1()));
    EXPECT_FALSE(umid->rect().contains(line->line().p2()));
  }
}

TEST(BlockOutline, DoubleClickDescendsIntoBlock) {
  OutlineBlockView fixture;
  ASSERT_FALSE(fixture.view.Diagram().Empty());
  ASSERT_EQ(fixture.view.DiagramLevel(), "top");
  BlockDiagram diagram(fixture.view);
  diagram.Refresh();
  const QGraphicsScene* scene = diagram.Scene();
  ASSERT_NE(scene, nullptr);
  const QGraphicsRectItem* target = nullptr;
  for (const QGraphicsRectItem* rect :
       OutlineSceneItems<QGraphicsRectItem>(scene)) {
    if (std::abs(rect->rect().top() - 40) < 0.01) target = rect;
  }
  ASSERT_NE(target, nullptr);
  const QPointF center = target->rect().center();
  // A real double-click is a press followed by a double-click
  // event: the press selects, the double-click descends.
  QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
  press.setScenePos(center);
  press.setButton(Qt::LeftButton);
  press.setButtons(Qt::LeftButton);
  QApplication::sendEvent(const_cast<QGraphicsScene*>(scene), &press);
  QGraphicsSceneMouseEvent double_click(
      QEvent::GraphicsSceneMouseDoubleClick);
  double_click.setScenePos(center);
  double_click.setButton(Qt::LeftButton);
  double_click.setButtons(Qt::LeftButton);
  QApplication::sendEvent(const_cast<QGraphicsScene*>(scene),
                          &double_click);
  EXPECT_EQ(fixture.view.SelectedHierarchyPath(), "top/u_ctrl");
  EXPECT_EQ(fixture.view.DiagramLevel(), "top/u_ctrl");
}

// A SessionView whose registry runs fake elaborate and run_timing
// commands, so the main window's auto-navigation can be driven
// without a live design.
struct AutoNavView {
  CommandRegistry registry;
  CommandContext context;
  SessionView view;
  AutoNavView() : view(registry, context) {
    OutlineTestApp();
    auto elaborate = registry.Register(
        "elaborate", "elaborate",
        [](CommandContext& ctx, const std::vector<std::string>&) {
          ctx.synth.elaborated = true;
          return CommandResult::Ok("");
        });
    EXPECT_TRUE(elaborate.ok());
    RegisterOkCommand(registry, "run_timing");
  }
};

QPushButton* AutoNavButtonWithPrefix(MainWindow& window,
                                     const std::string& prefix) {
  for (QPushButton* button : window.findChildren<QPushButton*>()) {
    if (button->text().toStdString().rfind(prefix, 0) == 0) {
      return button;
    }
  }
  return nullptr;
}

TEST(MainWindowAutoNav, ElaborateEnablesAndShowsSynthesisTab) {
  AutoNavView fixture;
  MainWindow window(fixture.view);
  EXPECT_FALSE(window.SynthesisEnabled());
  EXPECT_EQ(window.CurrentTab(), 0);
  ASSERT_TRUE(fixture.view.Dispatch("elaborate", {}).ok);
  EXPECT_EQ(fixture.view.ElaborateStep(), StepStatus::kDone);
  window.Refresh();
  EXPECT_TRUE(window.SynthesisEnabled());
  EXPECT_EQ(window.CurrentTab(), 2);
}

TEST(MainWindowAutoNav, RunTimingShowsTimingTab) {
  AutoNavView fixture;
  MainWindow window(fixture.view);
  ASSERT_TRUE(fixture.view.Dispatch("run_timing", {}).ok);
  window.Refresh();
  EXPECT_EQ(window.CurrentTab(), 3);
  EXPECT_FALSE(window.SynthesisEnabled());
}

TEST(MainWindowAutoNav, SynthesisButtonShowsResultsTab) {
  AutoNavView fixture;
  MainWindow window(fixture.view);
  ASSERT_TRUE(fixture.view.Dispatch("elaborate", {}).ok);
  window.Refresh();
  ASSERT_EQ(window.CurrentTab(), 2);
  // Move away through the stepper's Read step (Sources tab),
  // then take the Synthesis stage button back to Results.
  QPushButton* read = AutoNavButtonWithPrefix(window, "Read");
  ASSERT_NE(read, nullptr);
  read->click();
  EXPECT_EQ(window.CurrentTab(), 1);
  QPushButton* synthesis =
      AutoNavButtonWithPrefix(window, "Synthesis");
  ASSERT_NE(synthesis, nullptr);
  EXPECT_TRUE(synthesis->isEnabled());
  synthesis->click();
  EXPECT_EQ(window.CurrentTab(), 2);
}

// The owner's exact console sequence from the Mac demo
// 2026-11-08: after elaborate the Synthesis button must be
// enabled, the window must end on the Results tab, and
// clicking Synthesis from the Opt log tab must take the user
// back to Results. Drives the console-entry path
// (DispatchEntry), not Dispatch, since that is what the GUI
// console submits.
TEST(MainWindowAutoNav, ConsoleEntrySequenceEndsOnResults) {
  AutoNavView fixture;
  RegisterOkCommand(fixture.registry, "create_scenario");
  RegisterOkCommand(fixture.registry, "read_filelist");
  RegisterOkCommand(fixture.registry, "synth_hierarchy");
  RegisterOkCommand(fixture.registry, "synth_blocks");
  RegisterOkCommand(fixture.registry, "synth_schematic");
  RegisterOkCommand(fixture.registry, "synth_stats");
  MainWindow window(fixture.view);
  const char* entries[] = {
      "eos::create_scenario func_tt func tt estimate",
      "eos::read_filelist design.f hier_top",
      "eos::elaborate",
      "eos::synth_hierarchy",
      "eos::synth_blocks hier_datapath",
      "eos::synth_schematic hier_datapath",
      "eos::synth_stats",
  };
  for (const char* entry : entries) {
    ASSERT_TRUE(fixture.view.DispatchEntry(entry).ok) << entry;
    window.Refresh();
  }
  EXPECT_TRUE(window.SynthesisEnabled());
  EXPECT_EQ(window.CurrentTab(), 2);
  // Move away through the stepper's Generic synth step (Opt
  // log tab), then take the Synthesis stage button back.
  QPushButton* generic = AutoNavButtonWithPrefix(window, "Generic synth");
  ASSERT_NE(generic, nullptr);
  generic->click();
  EXPECT_EQ(window.CurrentTab(), 4);
  QPushButton* synthesis =
      AutoNavButtonWithPrefix(window, "Synthesis");
  ASSERT_NE(synthesis, nullptr);
  EXPECT_TRUE(synthesis->isEnabled());
  synthesis->click();
  EXPECT_EQ(window.CurrentTab(), 2);
}

// A SessionView with the nested hierarchy, so the center can
// switch between the block diagram (non-leaf levels) and the
// schematic (the leaf adder module).
struct CenterViewFixture {
  CommandRegistry registry;
  CommandContext context;
  SessionView view;
  CenterViewFixture() : view(registry, context) {
    OutlineTestApp();
    RegisterHierarchyCommand(registry, kNestedPayload, true);
    RegisterBlocksCommand(registry, nullptr);
    RegisterSchematicCommand(registry, nullptr);
    context.synth.elaborated = true;
    context.synth.top_cell = "top";
    view.RefreshHierarchy();
    view.RefreshDiagram();
    view.RefreshSchematic();
  }
  void RefreshToLevel(const std::string& level) {
    ASSERT_TRUE(view.SetDiagramLevel(level));
    view.RefreshDiagram();
    view.RefreshSchematic();
  }
};

std::string CenterPlaceholderText(const QGraphicsScene* scene) {
  const std::vector<QGraphicsSimpleTextItem*> texts =
      OutlineSceneItems<QGraphicsSimpleTextItem>(scene);
  if (texts.empty()) return "";
  return texts[0]->text().toStdString();
}

TEST(CenterView, ShowsBlockDiagramForNonLeafLevel) {
  CenterViewFixture fixture;
  fixture.RefreshToLevel("top");
  CenterView center(fixture.view);
  center.Refresh();
  EXPECT_FALSE(center.ShowingSchematic());
  // The block diagram page renders the top level's blocks.
  EXPECT_FALSE(OutlineSceneItems<QGraphicsPathItem>(
                   center.Diagram()->Scene())
                   .empty());
  // The schematic page is the non-leaf placeholder.
  EXPECT_EQ(CenterPlaceholderText(center.Schematic()->Scene()),
            "No schematic for this level.");
}

TEST(CenterView, ShowsSchematicForLeafLevel) {
  CenterViewFixture fixture;
  fixture.RefreshToLevel("top/u_mid/u_add");
  CenterView center(fixture.view);
  center.Refresh();
  EXPECT_TRUE(center.ShowingSchematic());
  // The schematic page renders the adder gate: a generic $add
  // draws as an outlined rectangle 64 high (the other rect is
  // the transparent hover rect). The block diagram page has
  // no diagram for the leaf module, so it shows its
  // placeholder.
  std::size_t outlined_gates = 0;
  for (const QGraphicsRectItem* rect :
       OutlineSceneItems<QGraphicsRectItem>(center.Schematic()->Scene())) {
    if (rect->pen().style() == Qt::NoPen) continue;
    if (std::abs(rect->rect().height() - 64) < 0.01) ++outlined_gates;
  }
  EXPECT_EQ(outlined_gates, 1u);
  EXPECT_EQ(CenterPlaceholderText(center.Diagram()->Scene()),
            "No diagram for this level.");
}

TEST(CenterView, SwitchingBackToNonLeafRestoresBlockDiagram) {
  CenterViewFixture fixture;
  fixture.RefreshToLevel("top/u_mid/u_add");
  CenterView center(fixture.view);
  center.Refresh();
  ASSERT_TRUE(center.ShowingSchematic());
  fixture.RefreshToLevel("top/u_mid");
  center.Refresh();
  EXPECT_FALSE(center.ShowingSchematic());
  EXPECT_FALSE(OutlineSceneItems<QGraphicsPathItem>(
                   center.Diagram()->Scene())
                   .empty());
}

TEST(CenterView, ShowsFloorplanWhenFloorplanStageChosen) {
  CenterViewFixture fixture;
  RegisterFloorplanCommand(fixture.registry, kFloorplanPayload, true);
  fixture.view.RefreshFloorplan();
  ASSERT_TRUE(fixture.view.Floorplan().Loaded());
  CenterView center(fixture.view);
  center.Refresh();
  EXPECT_FALSE(center.ShowingFloorplan());
  EXPECT_FALSE(center.ShowingSchematic());
  // A successful floorplan dispatch chooses the floorplan
  // stage: the center shows the canvas, not the schematic.
  fixture.view.ShowFloorplan(true);
  center.Refresh();
  EXPECT_TRUE(center.ShowingFloorplan());
  EXPECT_FALSE(center.ShowingSchematic());
  // The floorplan page renders the die outline: a white 2px
  // rect over the die rectangle.
  bool die_found = false;
  for (const QGraphicsRectItem* rect :
       OutlineSceneItems<QGraphicsRectItem>(center.Floorplan()->Scene())) {
    if (rect->pen().color() == Qt::white &&
        rect->pen().widthF() == 2 &&
        OutlineRectNear(rect->rect(), QRectF(0, -5000, 5000, 5000))) {
      die_found = true;
    }
  }
  EXPECT_TRUE(die_found);
  // Hierarchy navigation clears the stage: the center shows
  // the hierarchy views again.
  fixture.view.ShowFloorplan(false);
  center.Refresh();
  EXPECT_FALSE(center.ShowingFloorplan());
}

TEST(CenterView, HighlightGateForwardsToSchematicOnlyWhenLeaf) {
  CenterViewFixture fixture;
  fixture.RefreshToLevel("top/u_mid/u_add");
  CenterView center(fixture.view);
  center.show();
  QApplication::processEvents();
  center.Refresh();
  QApplication::processEvents();
  center.HighlightGate("add");
  // The adder gate is a generic box, so its highlight is the
  // blue outline on its rectangle, not a path.
  std::size_t blue = 0;
  for (const QGraphicsRectItem* rect :
       OutlineSceneItems<QGraphicsRectItem>(center.Schematic()->Scene())) {
    if (rect->pen().style() == Qt::NoPen) continue;
    if (std::abs(rect->rect().height() - 64) > 0.01) continue;
    if (rect->pen().color() == Qt::blue) {
      ++blue;
      EXPECT_EQ(rect->pen().widthF(), 2);
    }
  }
  EXPECT_EQ(blue, 1u);
  // At a non-leaf level the highlight is a no-op: the
  // schematic stays on its placeholder.
  fixture.RefreshToLevel("top");
  center.Refresh();
  center.HighlightGate("add");
  EXPECT_EQ(CenterPlaceholderText(center.Schematic()->Scene()),
            "No schematic for this level.");
}

TEST(PanelsView, HasSixTabsWithTiming) {
  OutlineTestApp();
  CommandRegistry registry;
  CommandContext context;
  SessionView view(registry, context);
  PanelsView panels(view);
  ASSERT_EQ(panels.count(), 6);
  EXPECT_EQ(panels.tabText(0).toStdString(), "Issues");
  EXPECT_EQ(panels.tabText(1).toStdString(), "Sources");
  EXPECT_EQ(panels.tabText(2).toStdString(), "Results");
  EXPECT_EQ(panels.tabText(3).toStdString(), "Timing");
  EXPECT_EQ(panels.tabText(4).toStdString(), "Opt log");
  EXPECT_EQ(panels.tabText(5).toStdString(), "Messages");
}

// F1: the Results tab carries the mapped synthesis section from
// the synth_summary query, with the demo chain numbers.
TEST(PanelsView, ResultsTabShowsMappedSynthesis) {
  OutlineTestApp();
  CommandRegistry registry;
  RegisterStatsCommand(registry, kStatsPayload, true);
  RegisterSummaryCommand(registry, kSummaryPayload, true);
  CommandContext context;
  FillPanelsContext(context);
  SessionView view(registry, context);
  view.RefreshPanels();
  PanelsView panels(view);
  auto* results = qobject_cast<QPlainTextEdit*>(panels.widget(2));
  ASSERT_NE(results, nullptr);
  const std::string text = results->toPlainText().toStdString();
  EXPECT_NE(text.find("Mapped: target=area cells=4 worst slack=-0.35 ns, "
                      "TNS=-0.35 ns"),
            std::string::npos);
  EXPECT_NE(text.find("AND2: 3"), std::string::npos);
  EXPECT_NE(text.find("BUF: 1"), std::string::npos);
  EXPECT_NE(text.find("Loop: winner=classic iterations=1 cells=4 "
                      "worst slack=0.05 ns, TNS=0 ns"),
            std::string::npos);
  EXPECT_NE(text.find("AND2: 4"), std::string::npos);
  EXPECT_NE(text.find("Equivalence: equivalent"), std::string::npos);
}

// An issue on the leaf adder module, so LocateIssue must
// highlight its gate in the center schematic.
const char kLeafIssuePayload[] =
    "checked=hierarchy resolution\\n"
    "netlist=elaborated\\n"
    "issue severity=warning code=unconnected_port module=adder "
    "path=top/u_mid/u_add target=add file= line=0 message=Port Y "
    "of cell add is unconnected\\n";

TEST(PanelsView, LocateIssueHighlightsGateInCenterSchematic) {
  OutlineTestApp();
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kNestedPayload, true);
  RegisterBlocksCommand(registry, nullptr);
  RegisterSchematicCommand(registry, nullptr);
  RegisterIssuesCommand(registry, kLeafIssuePayload, true);
  CommandContext context;
  context.synth.elaborated = true;
  context.synth.top_cell = "top";
  SessionView view(registry, context);
  view.RefreshHierarchy();
  view.RefreshIssues();
  PanelsView panels(view);
  std::string highlighted;
  panels.SetHighlightGateCallback(
      [&highlighted](const std::string& target) { highlighted = target; });
  bool refreshed = false;
  panels.SetActionCallback([&refreshed]() { refreshed = true; });
  panels.LocateIssue("top/u_mid/u_add", "add");
  EXPECT_EQ(view.DiagramLevel(), "top/u_mid/u_add");
  EXPECT_TRUE(view.DiagramLevelIsLeaf());
  EXPECT_EQ(highlighted, "add");
  EXPECT_TRUE(refreshed);
}

TEST(PanelsView, LocateIssueSkipsHighlightForNonLeafLevel) {
  OutlineTestApp();
  CommandRegistry registry;
  RegisterHierarchyCommand(registry, kNestedPayload, true);
  RegisterBlocksCommand(registry, nullptr);
  RegisterSchematicCommand(registry, nullptr);
  RegisterIssuesCommand(registry, kIssuesPayload, true);
  CommandContext context;
  context.synth.elaborated = true;
  context.synth.top_cell = "top";
  SessionView view(registry, context);
  view.RefreshHierarchy();
  view.RefreshIssues();
  PanelsView panels(view);
  std::string highlighted;
  panels.SetHighlightGateCallback(
      [&highlighted](const std::string& target) { highlighted = target; });
  bool refreshed = false;
  panels.SetActionCallback([&refreshed]() { refreshed = true; });
  panels.LocateIssue("top/u_mid", "u_add");
  EXPECT_EQ(view.DiagramLevel(), "top/u_mid");
  EXPECT_FALSE(view.DiagramLevelIsLeaf());
  EXPECT_TRUE(highlighted.empty());
  EXPECT_TRUE(refreshed);
}

#endif  // EOS_GUI_HAVE_QT6

TEST(GuiSessionView, RefreshIssuesPopulatesModelWithoutEcho) {
  CommandRegistry registry;
  RegisterIssuesCommand(registry, kIssuesPayload, true);
  CommandContext context;
  context.synth.elaborated = true;
  context.synth.top_cell = "top";
  SessionView view(registry, context);
  view.RefreshIssues();
  EXPECT_TRUE(view.Issues().Loaded());
  ASSERT_EQ(view.Issues().Issues().size(), 3u);
  EXPECT_EQ(view.Issues().ErrorCount(), 1);
  EXPECT_TRUE(view.EchoLog().empty());
}

TEST(GuiSessionView, RefreshIssuesRunsCheckOncePerDesign) {
  CommandRegistry registry;
  int calls = 0;
  RegisterIssuesCommand(registry, kIssuesPayload, true, &calls);
  CommandContext context;
  context.synth.elaborated = true;
  context.synth.top_cell = "top";
  SessionView view(registry, context);
  view.RefreshIssues();
  EXPECT_EQ(calls, 1);
  view.RefreshIssues();
  EXPECT_EQ(calls, 1);
  view.RefreshIssues();
  EXPECT_EQ(calls, 1);
}

TEST(GuiSessionView, RefreshIssuesRerunsAfterDesignChangingDispatch) {
  CommandRegistry registry;
  int calls = 0;
  RegisterIssuesCommand(registry, kIssuesPayload, true, &calls);
  auto status = registry.Register(
      "elaborate", "elaborate",
      [](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Ok("");
      });
  ASSERT_TRUE(status.ok());
  CommandContext context;
  context.synth.elaborated = true;
  context.synth.top_cell = "top";
  SessionView view(registry, context);
  view.RefreshIssues();
  EXPECT_EQ(calls, 1);
  view.RefreshIssues();
  EXPECT_EQ(calls, 1);
  ASSERT_TRUE(view.Dispatch("elaborate", {}).ok);
  view.RefreshIssues();
  EXPECT_EQ(calls, 2);
}

TEST(GuiSessionView, RefreshIssuesClearsWhenNotElaborated) {
  CommandRegistry registry;
  RegisterIssuesCommand(registry, kIssuesPayload, true);
  CommandContext context;
  context.synth.elaborated = true;
  context.synth.top_cell = "top";
  SessionView view(registry, context);
  view.RefreshIssues();
  ASSERT_TRUE(view.Issues().Loaded());
  context.synth.elaborated = false;
  view.RefreshIssues();
  EXPECT_FALSE(view.Issues().Loaded());
  EXPECT_TRUE(view.Issues().Issues().empty());
}

TEST(GuiSessionView, RefreshIssuesClearsOnFailedQuery) {
  CommandRegistry registry;
  RegisterIssuesCommand(registry, "", false);
  CommandContext context;
  context.synth.elaborated = true;
  context.synth.top_cell = "top";
  SessionView view(registry, context);
  view.RefreshIssues();
  EXPECT_FALSE(view.Issues().Loaded());
  EXPECT_TRUE(view.Issues().Issues().empty());
}

TEST(GuiSessionView, RefreshIssuesClearsOnMalformedPayload) {
  CommandRegistry registry;
  RegisterIssuesCommand(registry, "garbage\n", true);
  CommandContext context;
  context.synth.elaborated = true;
  context.synth.top_cell = "top";
  SessionView view(registry, context);
  view.RefreshIssues();
  EXPECT_FALSE(view.Issues().Loaded());
}

void RegisterTimingCommand(CommandRegistry& registry,
                           const std::string& payload, bool ok,
                           int* calls = nullptr) {
  auto status = registry.Register(
      "timing_summary", "timing_summary",
      [payload, ok, calls](CommandContext&,
                           const std::vector<std::string>&) {
        if (calls != nullptr) ++(*calls);
        if (!ok) {
          return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                     "no timing results");
        }
        return CommandResult::Ok(payload);
      });
  EXPECT_TRUE(status.ok());
}

// One fabricated last run: the func_tt scene at worst slack
// +0.05 ns, so RefreshTiming has timing inputs to key on.
eos::analysis::SceneResult FakeTimingResult(double worst_slack_ns) {
  eos::analysis::SceneResult result;
  result.scenario.name = "func_tt";
  result.scenario.mode = "func";
  result.scenario.corner = "tt";
  result.scenario.parasitics = eos::db::ParasiticsLevel::kEstimate;
  result.metrics["worst_slack_ns"] =
      eos::analysis::TimingMetric{worst_slack_ns, "ns"};
  result.metrics["total_negative_slack_ns"] =
      eos::analysis::TimingMetric{0.0, "ns"};
  result.metrics["worst_hold_slack_ns"] =
      eos::analysis::TimingMetric{0.45, "ns"};
  result.metrics["total_negative_hold_slack_ns"] =
      eos::analysis::TimingMetric{0.0, "ns"};
  return result;
}

TEST(GuiSessionView, RefreshTimingPopulatesModelWithoutEcho) {
  CommandRegistry registry;
  RegisterTimingCommand(registry, kTimingPayload, true);
  CommandContext context;
  context.timing.last_results.push_back(FakeTimingResult(0.05));
  SessionView view(registry, context);
  view.RefreshTiming();
  EXPECT_TRUE(view.Timing().Loaded());
  ASSERT_EQ(view.Timing().Scenes().size(), 1u);
  EXPECT_EQ(view.Timing().Scenes()[0].worst_slack_ns, "0.05");
  EXPECT_TRUE(view.EchoLog().empty());
}

TEST(GuiSessionView, RefreshTimingRunsQueryOncePerInputs) {
  CommandRegistry registry;
  int calls = 0;
  RegisterTimingCommand(registry, kTimingPayload, true, &calls);
  CommandContext context;
  context.timing.last_results.push_back(FakeTimingResult(0.05));
  SessionView view(registry, context);
  view.RefreshTiming();
  EXPECT_EQ(calls, 1);
  view.RefreshTiming();
  EXPECT_EQ(calls, 1);
  // A new run with a different worst slack changes the key, so
  // the query runs again.
  context.timing.last_results[0] = FakeTimingResult(0.1);
  view.RefreshTiming();
  EXPECT_EQ(calls, 2);
}

TEST(GuiSessionView, RefreshTimingClearsWhenNoResults) {
  CommandRegistry registry;
  RegisterTimingCommand(registry, kTimingPayload, true);
  CommandContext context;
  context.timing.last_results.push_back(FakeTimingResult(0.05));
  SessionView view(registry, context);
  view.RefreshTiming();
  ASSERT_TRUE(view.Timing().Loaded());
  context.timing.last_results.clear();
  view.RefreshTiming();
  EXPECT_FALSE(view.Timing().Loaded());
  EXPECT_TRUE(view.Timing().Scenes().empty());
}

TEST(GuiSessionView, RefreshTimingClearsOnFailedQuery) {
  CommandRegistry registry;
  RegisterTimingCommand(registry, "", false);
  CommandContext context;
  context.timing.last_results.push_back(FakeTimingResult(0.05));
  SessionView view(registry, context);
  view.RefreshTiming();
  EXPECT_FALSE(view.Timing().Loaded());
}

TEST(GuiSessionView, RefreshTimingClearsOnMalformedPayload) {
  CommandRegistry registry;
  RegisterTimingCommand(registry, "garbage\n", true);
  CommandContext context;
  context.timing.last_results.push_back(FakeTimingResult(0.05));
  SessionView view(registry, context);
  view.RefreshTiming();
  EXPECT_FALSE(view.Timing().Loaded());
}

TEST(GuiSessionView, RefreshFloorplanPopulatesModelWithoutEcho) {
  CommandRegistry registry;
  RegisterFloorplanCommand(registry, kFloorplanPayload, true);
  CommandContext context;
  SessionView view(registry, context);
  view.RefreshFloorplan();
  EXPECT_TRUE(view.Floorplan().Loaded());
  ASSERT_EQ(view.Floorplan().Rows().size(), 1u);
  EXPECT_EQ(view.Floorplan().Rows()[0], (FloorplanRect{0, 0, 5000, 3200}));
  ASSERT_EQ(view.Floorplan().Pins().size(), 6u);
  EXPECT_EQ(view.Floorplan().Pins()[0].name, "a");
  EXPECT_TRUE(view.EchoLog().empty());
}

TEST(GuiSessionView, RefreshFloorplanRunsQueryOnEveryRefresh) {
  CommandRegistry registry;
  int calls = 0;
  RegisterFloorplanCommand(registry, kFloorplanPayload, true, &calls);
  CommandContext context;
  SessionView view(registry, context);
  view.RefreshFloorplan();
  EXPECT_EQ(calls, 1);
  // No timing-key guard here: the query is a cheap database
  // walk, so it runs on every refresh, like the hierarchy
  // refresh.
  view.RefreshFloorplan();
  EXPECT_EQ(calls, 2);
}

TEST(GuiSessionView, RefreshFloorplanClearsOnFailedQuery) {
  CommandRegistry registry;
  RegisterFloorplanCommand(registry, "", false);
  CommandContext context;
  SessionView view(registry, context);
  view.RefreshFloorplan();
  EXPECT_FALSE(view.Floorplan().Loaded());
}

TEST(GuiSessionView, RefreshFloorplanClearsOnMalformedPayload) {
  CommandRegistry registry;
  RegisterFloorplanCommand(registry, "garbage\n", true);
  CommandContext context;
  SessionView view(registry, context);
  view.RefreshFloorplan();
  EXPECT_FALSE(view.Floorplan().Loaded());
}

TEST(GuiSessionView, RefreshFloorplanClearsWhenQueryStartsFailing) {
  CommandRegistry registry;
  std::string payload = kFloorplanPayload;
  bool ok = true;
  auto status = registry.Register(
      "floorplan_summary", "floorplan_summary",
      [&payload, &ok](CommandContext&,
                      const std::vector<std::string>&) {
        if (!ok) {
          return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                     "no floorplan");
        }
        return CommandResult::Ok(payload);
      });
  ASSERT_TRUE(status.ok());
  CommandContext context;
  SessionView view(registry, context);
  view.RefreshFloorplan();
  ASSERT_TRUE(view.Floorplan().Loaded());
  ok = false;
  view.RefreshFloorplan();
  EXPECT_FALSE(view.Floorplan().Loaded());
}

TEST(GuiSessionView, RefreshPlacementPopulatesModelWithoutEcho) {
  CommandRegistry registry;
  RegisterPlacementCommand(registry, kPlacementPayload, true);
  CommandContext context;
  SessionView view(registry, context);
  view.RefreshPlacement();
  EXPECT_TRUE(view.Placement().Loaded());
  ASSERT_EQ(view.Placement().Instances().size(), 4u);
  EXPECT_EQ(view.Placement().Instances()[0].name, "u1");
  ASSERT_EQ(view.Placement().Slacks().size(), 1u);
  EXPECT_EQ(view.Placement().Slacks()[0].scene, "func_tt");
  EXPECT_TRUE(view.EchoLog().empty());
}

TEST(GuiSessionView, RefreshPlacementRunsQueryOnEveryRefresh) {
  CommandRegistry registry;
  int calls = 0;
  RegisterPlacementCommand(registry, kPlacementPayload, true, &calls);
  CommandContext context;
  SessionView view(registry, context);
  view.RefreshPlacement();
  EXPECT_EQ(calls, 1);
  // No timing-key guard here: the query is a cheap database
  // walk plus the stored place record, so it runs on every
  // refresh, like the floorplan refresh.
  view.RefreshPlacement();
  EXPECT_EQ(calls, 2);
}

TEST(GuiSessionView, RefreshPlacementClearsOnFailedQuery) {
  CommandRegistry registry;
  RegisterPlacementCommand(registry, "", false);
  CommandContext context;
  SessionView view(registry, context);
  view.RefreshPlacement();
  EXPECT_FALSE(view.Placement().Loaded());
}

TEST(GuiSessionView, RefreshPlacementClearsOnMalformedPayload) {
  CommandRegistry registry;
  RegisterPlacementCommand(registry, "garbage\n", true);
  CommandContext context;
  SessionView view(registry, context);
  view.RefreshPlacement();
  EXPECT_FALSE(view.Placement().Loaded());
}

TEST(GuiSessionView, RefreshPlacementClearsWhenQueryStartsFailing) {
  CommandRegistry registry;
  std::string payload = kPlacementPayload;
  bool ok = true;
  auto status = registry.Register(
      "place_summary", "place_summary",
      [&payload, &ok](CommandContext&,
                      const std::vector<std::string>&) {
        if (!ok) {
          return CommandResult::Fail(eos::ui::Diag::kInvalidConfiguration,
                                     "no placement");
        }
        return CommandResult::Ok(payload);
      });
  ASSERT_TRUE(status.ok());
  CommandContext context;
  SessionView view(registry, context);
  view.RefreshPlacement();
  ASSERT_TRUE(view.Placement().Loaded());
  ok = false;
  view.RefreshPlacement();
  EXPECT_FALSE(view.Placement().Loaded());
}

TEST(GuiSessionView, DispatchPlaceShowsPlacementCenter) {
  CommandRegistry registry;
  auto place_status = registry.Register(
      "place", "place",
      [](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Ok("placed=4 total=4 hpwl=12345 overlaps=0");
      });
  ASSERT_TRUE(place_status.ok());
  auto close_status = registry.Register(
      "close_design", "close_design",
      [](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Ok("");
      });
  ASSERT_TRUE(close_status.ok());
  CommandContext context;
  SessionView view(registry, context);
  EXPECT_FALSE(view.ShowingPlacement());
  ASSERT_TRUE(view.Dispatch("place", {}).ok);
  EXPECT_TRUE(view.ShowingPlacement());
  ASSERT_TRUE(view.Dispatch("close_design", {}).ok);
  EXPECT_FALSE(view.ShowingPlacement());
}

TEST(GuiSessionView, DispatchFloorplanShowsFloorplanCenter) {
  CommandRegistry registry;
  auto floorplan_status = registry.Register(
      "floorplan", "floorplan",
      [](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Ok("die_area=0,0,5000,5000");
      });
  ASSERT_TRUE(floorplan_status.ok());
  auto close_status = registry.Register(
      "close_design", "close_design",
      [](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Ok("");
      });
  ASSERT_TRUE(close_status.ok());
  CommandContext context;
  SessionView view(registry, context);
  EXPECT_FALSE(view.ShowingFloorplan());
  ASSERT_TRUE(view.Dispatch("floorplan", {}).ok);
  EXPECT_TRUE(view.ShowingFloorplan());
  ASSERT_TRUE(view.Dispatch("close_design", {}).ok);
  EXPECT_FALSE(view.ShowingFloorplan());
}

// Panel rework: the log window's entries. A dispatch logs its
// command line as info plus its output lines as info; a failed
// dispatch logs the diagnostic as an error as well.
TEST(GuiSessionView, DispatchLogsCommandAndFailure) {
  CommandRegistry registry;
  RegisterPing(registry);
  CommandContext context;
  SessionView view(registry, context);
  ASSERT_TRUE(view.Dispatch("ping", {}).ok);
  ASSERT_EQ(view.Log().size(), 2u);
  EXPECT_EQ(view.Log()[0].severity, "info");
  EXPECT_EQ(view.Log()[0].text, "eos::ping");
  ASSERT_EQ(view.Log()[0].timestamp.size(), 8u);
  EXPECT_EQ(view.Log()[1].severity, "info");
  EXPECT_EQ(view.Log()[1].text, "pong=1");
  EXPECT_FALSE(view.Dispatch("nope", {}).ok);
  ASSERT_EQ(view.Log().size(), 4u);
  EXPECT_EQ(view.Log()[2].severity, "info");
  EXPECT_EQ(view.Log()[2].text, "eos::nope");
  EXPECT_EQ(view.Log()[3].severity, "error");
  EXPECT_NE(view.Log()[3].text.find("EOS invalid_command"),
            std::string::npos);
}

// Console/log routing: a command that prints output leaves
// the transcript with only the echo plus one outcome line,
// while the log keeps every output line in order.
TEST(GuiSessionView, DispatchPayloadGoesToLogNotTranscript) {
  CommandRegistry registry;
  auto status = registry.Register(
      "report", "report",
      [](CommandContext&, const std::vector<std::string>&) {
        return CommandResult::Ok("line1\nline2\nline3\n");
      });
  EXPECT_TRUE(status.ok());
  CommandContext context;
  SessionView view(registry, context);
  ASSERT_TRUE(view.Dispatch("report", {}).ok);
  ASSERT_EQ(view.EchoLog().size(), 2u);
  EXPECT_EQ(view.EchoLog()[0], "eos::report");
  EXPECT_EQ(view.EchoLog()[1], "ok");
  for (const std::string& line : view.EchoLog()) {
    EXPECT_EQ(line.find("line"), std::string::npos) << line;
  }
  ASSERT_EQ(view.Log().size(), 4u);
  EXPECT_EQ(view.Log()[0].text, "eos::report");
  EXPECT_EQ(view.Log()[1].text, "line1");
  EXPECT_EQ(view.Log()[2].text, "line2");
  EXPECT_EQ(view.Log()[3].text, "line3");
}

// Console/log routing: a failing command shows its one-line
// diagnostic in the transcript, the same line in Messages,
// and the diagnostic as an error in the log.
TEST(GuiSessionView, DispatchFailureShowsOneLineOutcomeInTranscript) {
  CommandRegistry registry;
  CommandContext context;
  SessionView view(registry, context);
  EXPECT_FALSE(view.Dispatch("nope", {}).ok);
  ASSERT_EQ(view.EchoLog().size(), 2u);
  EXPECT_EQ(view.EchoLog()[0], "eos::nope");
  EXPECT_EQ(view.EchoLog()[1],
            "eos::nope: EOS invalid_command: unknown command: eos::nope");
  ASSERT_EQ(view.Messages().size(), 1u);
  EXPECT_EQ(view.Messages()[0], view.EchoLog()[1]);
  ASSERT_EQ(view.Log().size(), 2u);
  EXPECT_EQ(view.Log()[0].text, "eos::nope");
  EXPECT_EQ(view.Log()[1].severity, "error");
  EXPECT_EQ(view.Log()[1].text, view.EchoLog()[1]);
}

// The engine-log drain: RefreshLog moves the context's engine
// lines into log entries, classifying severity by the Yosys
// prefix, and clears the context field so a second refresh
// adds nothing.
TEST(GuiSessionView, RefreshLogDrainsEngineLinesWithSeverity) {
  CommandRegistry registry;
  CommandContext context;
  context.engine_log_lines = {
      "1. Executing HIERARCHY pass (hierarchy, ...).",
      "Warning: Cell top.u_x has unknown internal type $foo.",
      "ERROR: something failed."};
  SessionView view(registry, context);
  view.RefreshLog();
  ASSERT_EQ(view.Log().size(), 3u);
  EXPECT_EQ(view.Log()[0].severity, "info");
  EXPECT_EQ(view.Log()[0].text,
            "1. Executing HIERARCHY pass (hierarchy, ...).");
  EXPECT_EQ(view.Log()[1].severity, "warning");
  EXPECT_EQ(view.Log()[2].severity, "error");
  EXPECT_TRUE(context.engine_log_lines.empty());
  view.RefreshLog();
  EXPECT_EQ(view.Log().size(), 3u);
}

}  // namespace
