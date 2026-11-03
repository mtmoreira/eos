// route: the EOS-owned driver over the imported OpenROAD global
// router (L4 engines).
//
// The runner operates on the live OpenDB database: the caller hands
// it the database the session owns, and the engine mutates that
// database in place (guide objects on every routed net, plus the
// block congestion grid). It never copies the design through a
// file. The imported engine code itself lives in the grt module
// (P2 support import); this header is the only surface other
// modules use. This unit imports no new engine: detailed routing,
// rip-up and reroute, and DRC-clean loops are P5, not P4.
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

// Options for one global routing run. A layer value of 0 means
// "engine default" on that end of the range. liberty_path names
// the Liberty library the engine's clock-net detection needs; an
// empty path fails fast.
struct RouteOptions {
  int min_routing_layer = 0;
  int max_routing_layer = 0;
  std::string liberty_path;
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

// Measured outcome of a run.
struct RouteReport {
  int net_total_count = 0;
  int net_routed_count = 0;
  int guide_count = 0;
  long long total_overflow = 0;
  std::vector<LayerCongestion> per_layer;
};

// Runs global routing (GlobalRouter::globalRoute with guides saved
// to the database) on the block of db, and fills report. Fails
// with kInvalidState when the database holds no technology data or
// no mapped design (no top block), when the block holds no rows (an
// un-floorplanned design), when no instance is placed (an
// unplaced design), when the layer range is invalid, when no
// Liberty library path is given or the file cannot be read, and
// when the engine routes zero nets on a design that holds nets. Engine
// logger errors surface as kInvalidState with the engine's
// message; the database is left as the engine left it on such a
// failure, which the caller reports rather than hides.
Status RunRoute(odb::dbDatabase* db, const RouteOptions& options,
                RouteReport* report);

}  // namespace eos::pd

#endif  // EOS_PD_ROUTE_H_
