// finish: the EOS-owned chip-finish driver (L4 engines).
//
// The runner operates on the live OpenDB database: the caller hands
// it the database the session owns, and finish mutates that database
// in place (filler instances, and with -pdn a power grid on the
// supply nets, plus optional metal fill). It never copies the design
// through a file. The imported engine code lives in the fin module
// (P7 import: Finale metal density fill) and the pdn module (P7
// support import: PdnGen power grid); filler placement reuses the
// in-tree detailed placer's fillerPlacement, which is already in
// tree via the place module. This header is the only surface other
// modules use.
//
// Preconditions, defined precisely: the block must hold rows (a
// floorplanned design, as for place, cts, route, and extract), at
// least one placed instance, and at least one signal net carrying
// a wire. Routed means exactly that last condition, as for
// extraction: a design with guides only, or with no wires at all,
// fails fast as "not routed" with a message saying to run eos::route
// first. Supply nets and special nets are never counted toward
// routed.
//
// Filler cells: the library must hold at least one filler master
// (a master whose type is CORE SPACER in LEF, which OpenDB reports
// through dbMaster::isFiller). A library with no filler master fails
// fast instead of finishing a design with unfilled rows, because
// the report's filler count would otherwise be a silent zero.
//
// Power grid (-pdn): the design must hold one power net and one
// ground net (signal types POWER and GROUND). The runner builds the
// basic grid the plan's Stage 6 names: a core grid over the die
// area, followpins on the lowest routing layer, straps on the next
// routing layer, and vias connecting the two, written back to the
// database as special wires on the supply nets. It is a basic grid,
// not a signoff PDN: no IR analysis, no current-density check, and
// no ring or macro grid. The grid shape count in the report is the
// number of special-wire boxes the grid wrote.
//
// Metal fill (-fill_config <path>): the JSON rules file the fin
// DensityFill engine consumes (the upstream fill.json shape: a
// "layers" map keyed by technology layer name, plus an "outline"
// entry). EOS ships no default rules file, for the same reason
// extraction ships no default model: a default would silently stand
// in for the process data the fill geometry depends on. Without
// -fill_config no metal fill runs, and the report's metal fill
// count is the number of fills already in the block (zero on a
// fresh routed design).
//
// Finished marker: a successful run sets the dbBoolProperty
// "eos_finish" on the block (and "eos_finish_pdn" when the grid
// ran). WriteGds refuses a design without the marker, so
// write_gds before finish fails fast instead of streaming out an
// unfinished layout. A second finish run is idempotent: filler
// placement fills only the gaps that remain (none, after the first
// run), and a grid that already exists is counted, not rebuilt,
// so a repeated run never duplicates shapes.
//
// GDSII stream-out (WriteGds): EOS-owned, in this module
// (src/gds_stream.cc). No OpenDB GDSII writer exists at the ead3236
// pin: the pin tree's OpenDB has no GDS writer, and the io/gds
// module is still a skeleton, so the plan's "OpenDB writer already
// in tree" premise does not hold at this pin and the writer is
// written here as EOS-owned glue instead of imported. It writes
// the top structure (one SREF per placed instance, at the instance
// origin with its orientation), one structure per used master
// (pin rectangles as boundaries), routed wires as PATH elements
// decoded from the database wires, vias as their own layer boxes,
// special-wire boxes (the power grid), and metal fills as
// boundaries. It writes no text labels and no blockages; the
// README states that scope. The file is judged by existence and
// non-trivial size in the tests, and by its record structure
// (HEADER/LIBNAME/UNITS/BGNSTR/ENDLIB) in the fixture evidence run.
//
// This header is a stable surface.
#ifndef EOS_PD_FINISH_H_
#define EOS_PD_FINISH_H_

#include <string>

#include "eos/foundation/errors.h"

namespace odb {
class dbDatabase;
}  // namespace odb

namespace eos::pd {

using foundation::Status;

// Options for one finish run. with_pdn adds the basic power grid.
// fill_config_path names the fin metal-fill JSON rules file; empty
// means no metal fill runs.
struct FinishOptions {
  bool with_pdn = false;
  std::string fill_config_path;
};

// Measured outcome of a run. filler_count counts the instances
// whose master is a filler master after the run (all fillers in
// the block, not just this run's, so a second run reports the same
// count). grid_shape_count counts the special-wire boxes on the
// power and ground nets after the run (zero without -pdn).
// metal_fill_count counts the metal fills in the block after the
// run (zero when no fill config was given and the block held no
// fills). with_pdn echoes whether the grid ran on this run or an
// earlier one (the marker, not just this run's flag).
struct FinishReport {
  int filler_count = 0;
  int grid_shape_count = 0;
  int metal_fill_count = 0;
  bool with_pdn = false;
};

// Runs chip finish (filler insertion, optionally the basic power
// grid and metal fill) on the block of db, and fills report. Fails
// with kInvalidState when the database holds no technology data or
// no mapped design (no top block), when the block holds no rows (an
// un-floorplanned design), when no instance is placed (an unplaced
// design), when no signal net carries a wire (an unrouted design),
// when the library holds no filler master, when -pdn is given but
// the design holds no power or ground net, and when a fill config
// path is given but the file cannot be read. Engine logger errors
// surface as kInvalidState with the engine's message; the database
// is left as the engine left it on such a failure, which the caller
// reports rather than hides.
Status RunFinish(odb::dbDatabase* db, const FinishOptions& options,
                 FinishReport* report);

// Options for one stream-out run. path names the GDSII file the
// run writes; empty fails fast before anything is written.
struct WriteGdsOptions {
  std::string path;
};

// Measured outcome of a stream-out run. path echoes the file the
// run wrote; size_bytes is the file's byte count after the run
// (zero is a runner failure, never a report footnote).
// instance_count counts the SREFs written, structure_count counts
// the structures written (top plus one per used master).
struct WriteGdsReport {
  std::string path;
  long long size_bytes = 0;
  int instance_count = 0;
  int structure_count = 0;
};

// Writes the finished layout of db to options.path as GDSII (see
// the header note for exactly what the writer emits), and fills
// report. Fails with kInvalidState when the database holds no
// technology data or no mapped design, when finish has not run on
// the block (no eos_finish marker; run eos::finish first), when
// the path is empty, and when the file cannot be opened or ends
// up empty. A design that was finished in an earlier session run
// keeps its marker in the live database only: a freshly imported
// database has no marker, so write_gds after a fresh import fails
// until finish runs again on that database.
Status WriteGds(odb::dbDatabase* db, const WriteGdsOptions& options,
                WriteGdsReport* report);

}  // namespace eos::pd

#endif  // EOS_PD_FINISH_H_
