// drc: the LEF-driven in-tree layout DRC engine (F13 DRC unit, L4).
//
// The first in-tree DRC: rules come from the technology LEF through
// OpenDB (dbTechLayer tables), geometry comes from the live ODB
// through RawDatabase(), in the F3 query-command pattern. It checks
// the live database, NOT the GDS file (no GDS reader exists); the
// verdict names its source ("live-database") so the signoff claim
// says which artifact was checked.
//
// Check families in this version, per layer, all LEF-driven (a
// layer with no rule for a family is not checked for that family,
// and a technology with no rule at all fails fast rather than
// reporting a vacuous zero):
//   <layer>.width        wire, pin, and via-metal rects: min
//                        dimension >= LEF MINWIDTH.
//   <layer>.spacing      same-layer rect pairs from different
//                        nets: edge-to-edge distance >= LEF
//                        SPACING (overlap counts as distance 0).
//   <layer>.min-area     merged shape per net per layer (union of
//                        touching/overlapping rects): area >= LEF
//                        AREA. The area is the sum of the rect
//                        areas, which double-counts the overlap
//                        where two rects join; that over-counts
//                        area, so it can only miss a violation,
//                        never invent one.
//   <layer>.min-step     wire-path jogs: a centerline segment
//                        perpendicular to both its neighbors, with
//                        length < LEF MINSTEP. Path jogs only;
//                        notches in merged shapes are not covered
//                        in this version.
//   <cut>.enclosure      every via cut rect enclosed by the metal
//                        rects above and below by >= the LEF
//                        ENCLOSURE overhangs (first = below,
//                        second = above). One violation per (via,
//                        metal layer).
//   <cut>.cut-spacing    cut-rect pairs from different nets on the
//                        same cut layer: edge-to-edge distance >=
//                        the cut layer's LEF SPACING.
// Density and antenna stay out. Metal fills are not checked in
// this version. A wire under a non-default rule is measured at
// the layer default width (no false positive: the default is
// narrower than any widened rule, and MINWIDTH sits below the
// default width); that can miss a spacing violation on a widened
// wire, never invent one.
//
// Pairwise spacing is O(n^2) per layer: fine at mock and fixture
// scale, documented, not a production-scale sweep.
//
// The verdict is per-check violation counts with each violation
// located (layer, rule, bounding box). A clean design reports
// zero violations per check, never a bare PASS with no counts.
#ifndef EOS_PV_DRC_H_
#define EOS_PV_DRC_H_

#include <cstddef>
#include <string>
#include <vector>

namespace odb {
class dbDatabase;
}  // namespace odb

namespace eos::pv {

// One located violation: the check that fired, the layer, the
// net that owns the offending shape (empty when the shape has
// no net), a single-token detail (for example "gap=120" for a
// spacing violation, "metal=metal1" for an enclosure
// violation), and the bounding box in database units.
struct DrcViolation {
  std::string check;
  std::string layer;
  std::string net;
  std::string detail;
  int x1 = 0;
  int y1 = 0;
  int x2 = 0;
  int y2 = 0;
};

// One check's result: the check name (for example
// "em1.spacing"), its layer, the rule value it enforced in
// database units (square database units for min-area), and the
// violations it found, in deterministic order (net, then
// bounding box).
struct DrcCheckResult {
  std::string check;
  std::string layer;
  long long rule_value = 0;
  std::vector<DrcViolation> violations;
};

// The full verdict of one run: the source artifact and the
// per-check results. Checks are ordered by layer, then by
// family (width, spacing, min-area, min-step, enclosure,
// cut-spacing).
struct DrcRecord {
  std::string source;
  std::vector<DrcCheckResult> checks;

  std::size_t TotalViolations() const {
    std::size_t total = 0;
    for (const DrcCheckResult& check : checks) {
      total += check.violations.size();
    }
    return total;
  }
};

// Runs DRC on the live database. Returns false with error
// naming the offending state when the database is null, holds
// no technology, holds no top block, holds no routed design
// (no wires at all), or carries no DRC rule on any layer.
bool RunDrc(odb::dbDatabase* db, DrcRecord* record, std::string* error);

}  // namespace eos::pv

#endif  // EOS_PV_DRC_H_
