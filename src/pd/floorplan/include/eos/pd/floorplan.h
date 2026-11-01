// floorplan: the EOS-owned driver over the imported OpenROAD
// InitFloorplan engine (L4 engines).
//
// The runner operates on the live OpenDB database: the caller
// hands it the database the session owns, and the engine mutates
// that database in place (die area, rows, track grids). It never
// copies the design through a file. The imported engine code
// itself lives in this module's src/ (InitFloorplan.cc, upf.cpp)
// and in the dbsta module (the dbNetwork bridge); this header is
// the only surface other modules use.
//
// Coordinates are database units (DBU), the units the technology
// data in OpenDB uses. A die or core area is four coordinates
// (lx, ly, ux, uy). When no die area is given, the block's
// existing die area is used; when no core area is given, the core
// area is the die area. Tie-cell insertion is not part of this
// runner: it needs a tie MTerm and a Liberty cell, which the
// eos::floorplan command does not take, so insertTiecells is left
// for a later unit.
//
// This header is a stable surface.
#ifndef EOS_PD_FLOORPLAN_H_
#define EOS_PD_FLOORPLAN_H_

#include <array>
#include <string>

#include "eos/foundation/errors.h"

namespace odb {
class dbDatabase;
}  // namespace odb

namespace eos::pd {

using foundation::Status;

// Options for one floorplan run. has_die_area / has_core_area
// select the explicit rectangles; site names an additional site
// whose rows are created alongside the instance sites (empty
// means the sites of the placed instances only, as the engine
// does).
struct FloorplanOptions {
  bool has_die_area = false;
  std::array<int, 4> die_area = {0, 0, 0, 0};
  bool has_core_area = false;
  std::array<int, 4> core_area = {0, 0, 0, 0};
  std::string site;
};

// Measured outcome of a run, in database units unless noted.
struct FloorplanReport {
  std::array<int, 4> die_area = {0, 0, 0, 0};
  std::array<int, 4> core_area = {0, 0, 0, 0};
  int row_count = 0;
  int track_grid_count = 0;
  int io_total_count = 0;
  int io_placed_count = 0;
};

// Runs initFloorplan plus makeTracks on the block of db and
// fills report. Fails with kInvalidState when the database holds
// no technology data or no mapped design (no top block), with
// kNotFound when the named site does not exist, and with
// kInvalidArgument when a requested area is empty or inverted.
// Engine logger errors surface as kInvalidState with the engine's
// message; the database is left as the engine left it on such a
// failure, which the caller reports rather than hides.
Status RunFloorplan(odb::dbDatabase* db, const FloorplanOptions& options,
                    FloorplanReport* report);

}  // namespace eos::pd

#endif  // EOS_PD_FLOORPLAN_H_
