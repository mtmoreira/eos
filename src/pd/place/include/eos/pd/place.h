// place: the EOS-owned driver over the imported OpenROAD placement
// engines (L4 engines): RePlAce global placement (gpl) followed by
// OpenROAD detailed placement (dpl), with the global router (grt)
// linked in as the routability engine the global placer calls.
//
// The runner operates on the live OpenDB database: the caller hands
// it the database the session owns, and the engines mutate that
// database in place (instance locations). It never copies the design
// through a file. The imported engine code itself lives in this
// module's src/gpl and src/dpl subdirectories and in the grt, ant,
// stt, rsz and dbsta support modules; this header is the only
// surface other modules use.
//
// Coordinates are database units (DBU). HPWL is the half-perimeter
// wire length of the placed netlist in DBU, as reported by the
// detailed placer after legalization. Overlap count is the number
// of overlapping instance pairs found by a pairwise bounding-box
// check over the placed instances after detailed placement; the
// engine's own checkPlacement run must also pass (it throws on any
// placed, in-rows, overlap, or site-alignment failure), so a
// nonzero overlap count here can only be reported when the engine
// check itself did not already fail the run.
//
// Timing: this runner does not run STA. Timing-driven mode
// (-timing_driven) drives the global placer with resizer slacks,
// which needs a Liberty-loaded STA session; without one it fails
// fast (see RunPlace). Placed STA slack is reported by the session
// timing service (eos::run_timing) on the placed database, the
// same estimated-parasitics path the synthesis loop uses; the
// eos::place command appends that result when the session is
// configured for timing, and states its unavailability otherwise.
// Wire parasitics from routing (the grt wire-parasitics builder)
// are not part of P2: see src/pd/grt/PATCHES.md.
//
// This header is a stable surface.
#ifndef EOS_PD_PLACE_H_
#define EOS_PD_PLACE_H_

#include "eos/foundation/errors.h"

namespace odb {
class dbDatabase;
}  // namespace odb

namespace eos::pd {

using foundation::Status;

// Options for one placement run. timing_driven selects the
// timing-driven global placement path; without it, placement runs
// with routability-driven mode on when the STA session holds a
// Liberty library (the global router needs one to identify clock
// nets) and plain Nesterov plus detailed placement otherwise,
// and no resizer calls.
struct PlaceOptions {
  bool timing_driven = false;
};

// Measured outcome of a run.
struct PlaceReport {
  int instance_total_count = 0;
  int instance_placed_count = 0;
  int overlap_count = 0;
  long long hpwl_dbu = 0;
  bool timing_driven = false;
};

// Runs global placement (doInitialPlace, doNesterovPlace) then
// detailed placement on the block of db, and fills report. Fails
// with kInvalidState when the database holds no technology data or
// no mapped design (no top block), and with kInvalidState naming
// the missing floorplan when the block holds no rows (an
// un-floorplanned design). Fails with kInvalidState when
// timing_driven is set but the STA session holds no Liberty
// library. Without Liberty, routability-driven mode is off (see
// PlaceOptions): the global router's clock-net detection builds
// the STA graph and throws without a library. Engine logger
// errors surface as kInvalidState with the
// engine's message; the database is left as the engine left it on
// such a failure, which the caller reports rather than hides.
Status RunPlace(odb::dbDatabase* db, const PlaceOptions& options,
                PlaceReport* report);

}  // namespace eos::pd

#endif  // EOS_PD_PLACE_H_
