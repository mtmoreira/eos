// CenterView: the central panel of the main window.
//
// The center is one navigation stack with five pages. For a
// non-leaf hierarchy level it shows the block diagram for that
// level; when the level is a leaf it shows the gate-level
// schematic for the leaf module instead. The top level keeps
// its block view, and the schematic appears exactly when the
// user descends to a leaf, so the block diagram is never lost:
// it is the center view for every level that has children.
// When the user has chosen the floorplan stage (a successful
// floorplan dispatch takes them there, the Floorplan stage
// button takes them there, a hierarchy navigation takes them
// back) and the floorplan model is loaded, the center shows
// the floorplan canvas instead: the die outline, the core
// outline, the rows, and the IO pins, with the payload counts
// beside the canvas. When the user has chosen the placement
// stage (a successful place dispatch takes them there, the
// Place stage button takes them there, a hierarchy navigation
// or the Floorplan stage button takes them back) and the
// placement model is loaded, the center shows the placement
// canvas instead: the die and core outlines and one rectangle
// per placed instance, colored by master, with the payload
// counts and the master legend beside the canvas. Placement
// wins over floorplan while its model is loaded: placement is
// the later stage, so a successful place takes the user past
// the floorplan canvas. When the user has chosen the route
// stage (a successful route dispatch takes them there, the
// Route stage button takes them there, a hierarchy navigation
// or the Floorplan, Place, or CTS stage button takes them
// back) and the route model is loaded, the center shows the
// routing canvas instead: the die and core outlines, the
// placed instance rectangles, and the routed wires with vias,
// with the payload counts, the per-layer congestion table,
// and a layer selector beside the canvas. Route wins over
// placement while its model is loaded: route is the later
// stage, so a successful route takes the user past the
// placement canvas. When the user has chosen the GDS stage
// (a successful finish or write_gds dispatch takes them
// there, a hierarchy navigation or an earlier stage button
// takes them back) and the GDS model is loaded, the center
// shows the GDS canvas instead: the flattened export
// geometry, with the finish counts, the export record, the
// per-layer census, and a layer selector beside the canvas.
// GDS wins over route while its model is loaded: the export
// is the later stage, so a successful finish or export
// takes the user past the routing canvas.
//
// The six pages share the same navigation callbacks (block
// click, breadcrumb) through a single action callback, so the
// main window refreshes the same way whichever page is
// showing.

#pragma once

#include <QStackedWidget>
#include <QWidget>
#include <functional>
#include <string>

#include "eos/ui/gui.h"
#include "block_diagram.h"
#include "floorplan_diagram.h"
#include "gds_diagram.h"
#include "placement_diagram.h"
#include "route_diagram.h"
#include "schematic_diagram.h"

namespace eos::ui {

class CenterView : public QWidget {
 public:
  explicit CenterView(SessionView& view, QWidget* parent = nullptr);

  // Refreshes all six pages from the session and shows the
  // page for the current state: the GDS canvas when the
  // user chose the GDS stage and the GDS model is loaded,
  // the routing canvas when the
  // user chose the route stage and the route model is loaded,
  // the placement canvas when the user chose the placement
  // stage and the placement model is loaded, the floorplan
  // canvas when the user chose the floorplan stage and the
  // floorplan model is loaded, the schematic when the level
  // is a leaf, the block diagram otherwise.
  void Refresh();

  // Forwards a gate highlight to the schematic page when it is
  // showing; a no-op for a non-leaf level.
  void HighlightGate(const std::string& instance);

  // Called when either page navigates (block click, breadcrumb,
  // gate click). The main window refreshes on it.
  void SetActionCallback(std::function<void()> callback);

  bool ShowingSchematic() const { return showing_schematic_; }
  bool ShowingFloorplan() const { return showing_floorplan_; }
  bool ShowingPlacement() const { return showing_placement_; }
  bool ShowingRoute() const { return showing_route_; }
  bool ShowingGds() const { return showing_gds_; }
  BlockDiagram* Diagram() { return diagram_; }
  SchematicDiagram* Schematic() { return schematic_; }
  FloorplanDiagram* Floorplan() { return floorplan_; }
  PlacementDiagram* Placement() { return placement_; }
  RouteDiagram* Route() { return route_; }
  GdsDiagram* Gds() { return gds_; }

 private:
  SessionView& view_;
  QStackedWidget* stack_;
  BlockDiagram* diagram_;
  SchematicDiagram* schematic_;
  FloorplanDiagram* floorplan_;
  PlacementDiagram* placement_;
  RouteDiagram* route_;
  GdsDiagram* gds_;
  bool showing_schematic_ = false;
  bool showing_floorplan_ = false;
  bool showing_placement_ = false;
  bool showing_route_ = false;
  bool showing_gds_ = false;
  std::function<void()> on_action_;
};

}  // namespace eos::ui
