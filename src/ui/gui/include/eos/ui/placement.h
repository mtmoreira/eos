// placement: the GUI's placement model (L6 interface, Qt-free).
//
// The model the placement canvas reads: the live placement
// geometry from a place_summary payload. Parsing is
// all-or-nothing like the other models: a malformed payload
// leaves the previous placement untouched and reports false.
// Coordinates stay as the payload's integers (database units),
// so the canvas draws exactly what the query measured; counts
// are parsed integers, slacks are parsed doubles. A payload
// value is a single token: no value in this payload contains a
// space.
#ifndef EOS_UI_PLACEMENT_H_
#define EOS_UI_PLACEMENT_H_

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace eos::ui {

// One rectangle in database units: (lx, ly, ux, uy).
using PlacementRect = std::array<int, 4>;

// One placed instance: its name, its master's name, and its
// rectangle (x, y, x + width, y + height) in database units.
struct PlacementInstance {
  std::string name;
  std::string master;
  PlacementRect rect = {0, 0, 0, 0};
};

// One scene's post-place worst slack, as the place run
// measured it, in nanoseconds.
struct PlacementSlack {
  std::string scene;
  double worst_slack_ns = 0.0;
};

class PlacementModel {
 public:
  // Parses a place_summary payload: a "placement" header line
  // with the die and core rectangles and the placed, total,
  // HPWL, and overlap counts, then one "inst" line per
  // instance rectangle and one "slack" line per scene the
  // place run measured slack for, in database order. The
  // header's total count must match the inst lines, the
  // placed count must not exceed the total, every instance
  // rectangle must have area, instance names must be unique,
  // and slack scene names must be unique. Returns false
  // (placement unchanged) on any malformed line, unknown key,
  // duplicate instance or scene name, empty die or core
  // rectangle, empty instance rectangle, or count mismatch. A
  // successful load replaces the previous placement.
  bool LoadPayload(const std::string& payload);
  void Clear();

  bool Loaded() const { return loaded_; }
  bool Empty() const { return !loaded_; }
  const PlacementRect& DieArea() const { return die_; }
  const PlacementRect& CoreArea() const { return core_; }
  std::size_t Placed() const { return placed_; }
  std::size_t Total() const { return instances_.size(); }
  long long Hpwl() const { return hpwl_; }
  std::size_t Overlaps() const { return overlaps_; }
  const std::vector<PlacementInstance>& Instances() const {
    return instances_;
  }
  const std::vector<PlacementSlack>& Slacks() const { return slacks_; }

 private:
  bool loaded_ = false;
  PlacementRect die_ = {0, 0, 0, 0};
  PlacementRect core_ = {0, 0, 0, 0};
  std::size_t placed_ = 0;
  long long hpwl_ = 0;
  std::size_t overlaps_ = 0;
  std::vector<PlacementInstance> instances_;
  std::vector<PlacementSlack> slacks_;
};

}  // namespace eos::ui

#endif  // EOS_UI_PLACEMENT_H_
