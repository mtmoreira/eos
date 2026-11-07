// CenterView: the central panel of the main window.
//
// The center is one navigation stack with two pages. For a
// non-leaf hierarchy level it shows the block diagram for that
// level; when the level is a leaf it shows the gate-level
// schematic for the leaf module instead. The top level keeps
// its block view, and the schematic appears exactly when the
// user descends to a leaf, so the block diagram is never lost:
// it is the center view for every level that has children.
//
// The two pages share the same navigation callbacks (block
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
#include "schematic_diagram.h"

namespace eos::ui {

class CenterView : public QWidget {
 public:
  explicit CenterView(SessionView& view, QWidget* parent = nullptr);

  // Refreshes both pages from the session and shows the page
  // for the current diagram level: schematic when the level is
  // a leaf, block diagram otherwise.
  void Refresh();

  // Forwards a gate highlight to the schematic page when it is
  // showing; a no-op for a non-leaf level.
  void HighlightGate(const std::string& instance);

  // Called when either page navigates (block click, breadcrumb,
  // gate click). The main window refreshes on it.
  void SetActionCallback(std::function<void()> callback);

  bool ShowingSchematic() const { return showing_schematic_; }
  BlockDiagram* Diagram() { return diagram_; }
  SchematicDiagram* Schematic() { return schematic_; }

 private:
  SessionView& view_;
  QStackedWidget* stack_;
  BlockDiagram* diagram_;
  SchematicDiagram* schematic_;
  bool showing_schematic_ = false;
  std::function<void()> on_action_;
};

}  // namespace eos::ui
