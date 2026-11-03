// route: the EOS-owned driver over the imported OpenROAD global
// router and detailed router (L4 engines).
//
// The runner operates on the live OpenDB database: the caller hands
// it the database the session owns, and the engines mutate that
// database in place (guide objects on every routed net from the
// global stage, plus the block congestion grid, then real wires
// and vias from the detailed stage). It never copies the design
// through a file. The imported engine code itself lives in the grt
// module (P2 support import) and the drt module (P5 import); this
// header is the only surface other modules use.
//
// Preconditions, defined precisely: the block must hold rows (a
// floorplanned design, as for place and cts) and at least one
// placed instance. A design with rows but no placed instance has
// no pin positions for the router, so it fails fast as "not
// placed". A Liberty library is required, as for cts: the engine's
// clock-net detection (findClkNets) builds the STA graph and
// throws STA-2141 when no Liberty library is loaded, so the runner
// fails fast without one instead of letting the engine die inside
// clock detection. The caller passes the library path
// (options.liberty_path); the runner loads it into its private STA
// session. The critical-nets percentage is still pinned to 0,
// because the wire-parasitics builder the critical-nets path needs
// is still deferred (see src/pd/grt/PATCHES.md): a nonzero
// critical percentage would fail loudly inside the engine, never
// silently.
//
// Layer range: min_routing_layer and max_routing_layer are
// 1-based routing layer numbers as OpenDB numbers them
// (findRoutingLayer); 0 on either means "engine default" (first
// routing layer, respectively the highest routing layer). Values
// below 1 other than 0, and a minimum above the maximum, fail fast
// before the engine runs.
//
// Congestion map: after the run the engine has written the block
// GCellGrid (updateDbCongestion runs inside globalRoute). For each
// routing layer in range the runner sums, over all grid tiles, the
// directional capacity and usage (horizontal for horizontal layers,
// vertical for vertical layers) and the per-tile overflow
// max(0, usage - capacity). Those sums are the congestion map in
// the report: total overflow 0 means no tile exceeds capacity on
// any layer in range. The runner reads the map from the database
// the engine wrote, never from FastRoute internals (whose headers
// need the grt module's private include dir).
//
// Routed nets: net_routed_count is the size of the engine route
// map after the run. The engine skips nets it cannot route (for
// example single-terminal nets), so net_routed_count may be below
// net_total_count; the gap is reported, not hidden. Zero routes on
// a design that holds nets is a runner failure, not a report
// footnote. Guide count is the number of dbGuide objects the run
// wrote across all nets.
//
// This header is a stable surface.
#ifndef EOS_PD_ROUTE_H_
#define EOS_PD_ROUTE_H_

#include <string>
#include <vector>

#include "eos/foundation/errors.h"

namespace odb {
class dbDatabase;
}  // namespace odb

namespace eos::pd {

using foundation::Status;

// Options for one routing run: global routing followed by detailed
// routing. A layer value of 0 means "engine default" on that end of
// the range. liberty_path names the Liberty library the engine's
// clock-net detection needs; an empty path fails fast.
// max_detailed_iterations bounds the EOS-owned rip-up and reroute
// loop of the detailed stage (see RunRoute): the loop stops early
// when the violation count reaches zero, and stops at the bound
// otherwise, reporting that it did. Values below 1 fail fast.
// bottom_routing_layer and top_routing_layer name the detailed
// stage's layer range; empty means the engine default on that end.
// A named layer that is not a routing layer in the technology fails
// fast before either engine runs.
struct RouteOptions {
  int min_routing_layer = 0;
  int max_routing_layer = 0;
  std::string liberty_path;
  int max_detailed_iterations = 64;
  std::string bottom_routing_layer;
  std::string top_routing_layer;
};

// Per-layer congestion sums, in the layer's own direction
// (horizontal for horizontal layers, vertical for vertical
// layers). layer is the 1-based OpenDB routing layer number;
// name is the technology layer name.
struct LayerCongestion {
  int layer = 0;
  std::string name;
  long long capacity = 0;
  long long usage = 0;
  long long overflow = 0;
};

// Measured outcome of a run. The global-stage fields (routed nets,
// guides, congestion) are as before; the detailed-stage fields are
// filled by the detailed stage on every successful run:
// violation_count is the DRC marker count the detailed stage leaves
// in the design (zero means DRC-clean on the rules the engine
// checks); detailed_iterations_used counts the rip-up and reroute
// steps actually run; detailed_iteration_bound echoes the bound
// the run was given; detailed_stopped_at_bound is true exactly
// when the loop ran out of iterations with violations remaining
// (it is false when the loop stopped early at zero violations, and
// the report states which case happened through this flag).
struct RouteReport {
  int net_total_count = 0;
  int net_routed_count = 0;
  int guide_count = 0;
  long long total_overflow = 0;
  std::vector<LayerCongestion> per_layer;
  int violation_count = 0;
  int detailed_iterations_used = 0;
  int detailed_iteration_bound = 0;
  bool detailed_stopped_at_bound = false;
};

// Runs global routing (GlobalRouter::globalRoute with guides saved
// to the database) followed by detailed routing (TritonRoute in
// single-step mode, driven by the EOS-owned rip-up and reroute
// loop described above) on the block of db, and fills report. Fails
// with kInvalidState when the database holds no technology data or
// no mapped design (no top block), when the block holds no rows (an
// un-floorplanned design), when no instance is placed (an
// unplaced design), when the layer range is invalid, when the
// detailed iteration bound is below 1, when a named detailed
// layer is not a routing layer in the technology, when no
// Liberty library path is given or the file cannot be read, and
// when the engine routes zero nets on a design that holds nets. Engine
// logger errors surface as kInvalidState with the engine's
// message; the database is left as the engine left it on such a
// failure, which the caller reports rather than hides. Congestion
// is reported, never failed on; a nonzero violation count is
// reported, never failed on: the loop's job is to state the count
// and whether it stopped at the bound.
Status RunRoute(odb::dbDatabase* db, const RouteOptions& options,
                RouteReport* report);

}  // namespace eos::pd

#endif  // EOS_PD_ROUTE_H_
