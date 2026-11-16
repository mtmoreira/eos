// route: the GUI's route model (L6 interface, Qt-free).
//
// The model the routing canvas reads: the recorded route run
// from a route_summary payload, with the live routed geometry
// the query decoded from the database. Parsing is
// all-or-nothing like the other models: a malformed payload
// leaves the previous model untouched and reports false.
// Counts are parsed integers and slacks are parsed doubles,
// so the canvas draws exactly what the query measured. A
// payload value is a single token: no value in this payload
// contains a space, except the sta_note line, whose value is
// the route payload's unavailable statement carried verbatim,
// spaces and all.
#ifndef EOS_UI_ROUTE_H_
#define EOS_UI_ROUTE_H_

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace eos::ui {

// One rectangle in database units: (lx, ly, ux, uy).
using RouteRect = std::array<int, 4>;

// One routing layer's congestion sums, in the layer's own
// direction, as the route run measured them.
struct RouteLayer {
  int number = 0;
  std::string name;
  long long capacity = 0;
  long long usage = 0;
  long long overflow = 0;
};

// One routed path segment: one maximal straight run on one
// layer, with the wire width and its centerline points in
// database units (at least two points).
struct RouteSegment {
  std::string net;
  std::string layer;
  int width = 0;
  std::vector<std::pair<int, int>> points;
};

// One via the wire encoding yielded: the net, the lower and
// upper routing layer names of the via's layer pair, and the
// via's placed point in database units.
struct RouteViaPoint {
  std::string net;
  std::string lower_layer;
  std::string upper_layer;
  int x = 0;
  int y = 0;
};

// One scene's post-route worst slack, as the route run
// measured it, in nanoseconds.
struct RouteSlack {
  std::string scene;
  double worst_slack_ns = 0.0;
};

class RouteModel {
 public:
  // Parses a route_summary payload: a "route" header line
  // with the run's counts, the Liberty corner, and the die
  // and core rectangles, then one "layer" line per routing
  // layer in range, one "wire" line per routed path segment,
  // one "via" line per via, one "unwired" line per signal net
  // with no wire, and one "slack" line per scene the run
  // measured, or one "sta_note" line carrying the run's
  // unavailable statement verbatim when its STA leg could not
  // analyze. Layer numbers must be unique and in range order;
  // wire and via lines must name layers from the layer list;
  // a slack or sta_note line must follow the geometry lines
  // (a run carries slack lines or one note, never both); the
  // points list on a wire line must hold an even count of
  // coordinates. Returns false (model unchanged) on any
  // malformed line, unknown key, duplicate layer number,
  // unknown layer, bad number, odd points count, or count
  // mismatch. A successful load replaces the previous model.
  bool LoadPayload(const std::string& payload);
  void Clear();

  bool Loaded() const { return loaded_; }
  bool Empty() const { return !loaded_; }

  std::size_t Routed() const { return routed_; }
  std::size_t Total() const { return total_; }
  std::size_t Guides() const { return guides_; }
  long long Overflow() const { return overflow_; }
  std::size_t Violations() const { return violations_; }
  std::size_t DetailedIterations() const { return detailed_iterations_; }
  std::size_t DetailedBound() const { return detailed_bound_; }
  bool StoppedAtBound() const { return stopped_at_bound_; }
  const std::string& LibertyCorner() const { return liberty_corner_; }
  const RouteRect& DieArea() const { return die_; }
  const RouteRect& CoreArea() const { return core_; }
  const std::vector<RouteLayer>& Layers() const { return layers_; }
  const std::vector<RouteSegment>& Segments() const { return segments_; }
  const std::vector<RouteViaPoint>& Vias() const { return vias_; }
  const std::vector<std::string>& UnwiredNets() const {
    return unwired_nets_;
  }
  const std::vector<RouteSlack>& Slacks() const { return slacks_; }
  const std::string& StaNote() const { return sta_note_; }

  // The wire segments on one layer, by layer name, for the
  // canvas filter; the vias on one layer, by the layer name
  // of either side of the via's pair.
  std::vector<const RouteSegment*> WiresForLayer(
      const std::string& layer) const;
  std::vector<const RouteViaPoint*> ViasForLayer(
      const std::string& layer) const;

 private:
  bool loaded_ = false;
  std::size_t routed_ = 0;
  std::size_t total_ = 0;
  std::size_t guides_ = 0;
  long long overflow_ = 0;
  std::size_t violations_ = 0;
  std::size_t detailed_iterations_ = 0;
  std::size_t detailed_bound_ = 0;
  bool stopped_at_bound_ = false;
  std::string liberty_corner_;
  RouteRect die_ = {0, 0, 0, 0};
  RouteRect core_ = {0, 0, 0, 0};
  std::vector<RouteLayer> layers_;
  std::vector<RouteSegment> segments_;
  std::vector<RouteViaPoint> vias_;
  std::vector<std::string> unwired_nets_;
  std::vector<RouteSlack> slacks_;
  std::string sta_note_;
};

}  // namespace eos::ui

#endif  // EOS_UI_ROUTE_H_
