// lvs: the connectivity LVS engine (F13 unit, L4).
//
// The first in-tree LVS, connectivity only. It compares two
// independent data structures inside the one live database:
// the logical topology (each dbNet's dbITerms and the block's
// dbBTerms, written by the importer) against geometric
// connectivity derived from the routed shapes alone (written
// by the router): wire segment rectangles, via metal boxes at
// their placed positions, special-wire boxes, and the pin
// rectangles themselves, joined by a union-find under the
// same-layer contact rule (all boxes of one via instance
// union together, because a via is one conductor): wire-wire
// and pin-wire contact union on edge-or-overlap, pin-pin
// contact unions on area overlap only (design section 18:
// LEF pin rectangles are access geometry, not poured metal,
// so adjacent instances' pin rects abutting edge to edge do
// not join their nets). The design's closed-box rule (corner abut
// counted) was narrowed to edge-or-overlap as built, under
// design section 11: the suite's diagonal-abut case fired.
// Agreement is evidence, not a tautology: neither side derives
// from the other.
//
// It recognizes no devices, compares no W or L, and folds
// nothing. The verdict vocabulary is the scoping doc's:
// equivalent (zero mismatches), open, short, floating,
// reported as per-kind counts with each mismatch located,
// never as a bare PASS. It must never be presented as
// device-level LVS.
//
// Exclusions, named and counted, never silent: nets whose
// signal type is POWER or GROUND and nets OpenDB marks
// special are skipped and counted in special_nets_skipped;
// instances whose masters carry no terminals (fillers, tap
// cells) contribute no pins and no shapes; metal fills are
// not walked (they are not in any dbWire); a pin with no net
// is on neither side and is not reported in this version.
//
// Pairwise shape contact is O(n^2) per layer: fine at mock
// and fixture scale, documented, not a production-scale sweep.
#ifndef EOS_PV_LVS_H_
#define EOS_PV_LVS_H_

#include <cstddef>
#include <string>
#include <vector>

namespace odb {
class dbDatabase;
}  // namespace odb

namespace eos::pv {

// One located mismatch: the kind ("open", "short", or
// "floating"), the net it is charged to, the second net for a
// short, the pin for an open ("<inst>:<pin>" or the BTerm
// name), the layer of the contact or the shape for a short or
// floating (empty for an open), and the bounding box in
// database units: the pin rectangle for an open, the contact
// intersection for a short, the component's shape union for a
// floating.
struct LvsMismatch {
  std::string kind;
  std::string net;
  std::string other_net;
  std::string pin;
  std::string layer;
  int x1 = 0;
  int y1 = 0;
  int x2 = 0;
  int y2 = 0;
};

// The full verdict of one run: the source artifact, the
// comparison counts, and the mismatches in deterministic
// order (kind: open, short, floating; then net, then pin,
// then bounding box).
struct LvsRecord {
  std::string source;  // "live-database"
  std::size_t nets_compared = 0;
  std::size_t pins_compared = 0;
  std::size_t single_pin_nets = 0;
  std::size_t special_nets_skipped = 0;
  std::vector<LvsMismatch> mismatches;

  std::size_t CountKind(const std::string& kind) const;
  std::size_t TotalMismatches() const { return mismatches.size(); }
};

// Runs connectivity LVS on the live database. Returns false
// with error naming the offending state when the database is
// null, holds no technology, holds no top block, holds no
// routed design (no wires at all), or holds no logical
// netlist (no net carries a pin). No LEF rule is required:
// connectivity reads no rule.
bool RunConnectivityLvs(odb::dbDatabase* db, LvsRecord* record,
                        std::string* error);

}  // namespace eos::pv

#endif  // EOS_PV_LVS_H_
