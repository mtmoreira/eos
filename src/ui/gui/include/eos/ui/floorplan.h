// floorplan: the GUI's floorplan model (L6 interface, Qt-free).
//
// The model the floorplan canvas reads: the live floorplan
// geometry from a floorplan_summary payload. Parsing is
// all-or-nothing like the other models: a malformed payload
// leaves the previous floorplan untouched and reports false.
// Coordinates stay as the payload's integers (database units),
// so the canvas draws exactly what the query measured; counts
// are parsed integers. A payload value is a single token: no
// value in this payload contains a space.
#ifndef EOS_UI_FLOORPLAN_H_
#define EOS_UI_FLOORPLAN_H_

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace eos::ui {

// One rectangle in database units: (lx, ly, ux, uy).
using FloorplanRect = std::array<int, 4>;

// One IO pin of the top block: its name, its direction ("in",
// "out", "inout", or "feedthru"), whether its pin is placed,
// and its pin rectangle (0,0,0,0 when unplaced: an unplaced
// pin has no geometry to draw, and the canvas counts it
// instead).
struct FloorplanPin {
  std::string name;
  std::string direction;
  bool placed = false;
  FloorplanRect rect = {0, 0, 0, 0};
};

class FloorplanModel {
 public:
  // Parses a floorplan_summary payload: a "floorplan"
  // header line with the die and core rectangles and the row,
  // track-grid, and IO counts, then one "row" line per row
  // rectangle and one "pin" line per IO pin, in database
  // order. The header's row count must match the row lines,
  // the IO total must match the pin lines, and the IO placed
  // count must match the pins with placed=1. Returns false
  // (floorplan unchanged) on any malformed line, unknown key,
  // unknown direction, duplicate pin name, empty die or core
  // rectangle, empty row rectangle, or count mismatch. A
  // successful load replaces the previous floorplan.
  bool LoadPayload(const std::string& payload);
  void Clear();

  bool Loaded() const { return loaded_; }
  bool Empty() const { return !loaded_; }
  const FloorplanRect& DieArea() const { return die_; }
  const FloorplanRect& CoreArea() const { return core_; }
  std::size_t TrackGridCount() const { return track_grids_; }
  std::size_t IoPlaced() const { return io_placed_; }
  std::size_t IoTotal() const { return pins_.size(); }
  const std::vector<FloorplanRect>& Rows() const { return rows_; }
  const std::vector<FloorplanPin>& Pins() const { return pins_; }

 private:
  bool loaded_ = false;
  FloorplanRect die_ = {0, 0, 0, 0};
  FloorplanRect core_ = {0, 0, 0, 0};
  std::size_t track_grids_ = 0;
  std::size_t io_placed_ = 0;
  std::vector<FloorplanRect> rows_;
  std::vector<FloorplanPin> pins_;
};

}  // namespace eos::ui

#endif  // EOS_UI_FLOORPLAN_H_
