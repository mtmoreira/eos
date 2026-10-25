// constraints: constraint objects bound to design objects (L2
// design-data), with no silent drops.
//
// The failure this module exists to prevent: a constraint written
// against a target that does not resolve being silently discarded, so
// the tool runs unconstrained while the user believes otherwise
// (Revised Plan, Sections 10 and 14). So a constraint here is never
// dropped. It is stored with its target spec exactly as written, its
// binding status, and, when resolved, the persistent ID it bound to:
//
//   kBound       the target spec resolved to a design object.
//   kUnresolved  the target spec named nothing in the design (yet).
//                The constraint is retained and reported, and
//                RebindAll gives it another chance after the design
//                changes, for example after an import completes.
//   kEmpty       the target spec was empty: the constraint addresses
//                an empty collection. Retained and counted, never
//                treated as covering anything.
//
// Target resolution in this increment: the spec is an object name,
// looked up among ports, then nets, then instances; or a pin path
// "<instance>/<pin>" looked up among pins. Collections as a first
// class type arrive with the interface layer in Phase 1 increment 4,
// and the resolution rule extends there without changing this API.
//
// The Coverage report is the accounting the Phase 1 exit gate uses:
// coverage is complete only when every constraint is kBound.
//
// This header is a stable surface.
#ifndef EOS_DB_CONSTRAINTS_H_
#define EOS_DB_CONSTRAINTS_H_

#include <cstdint>
#include <string>
#include <vector>

#include "eos/db/design.h"
#include "eos/foundation/errors.h"
#include "eos/foundation/ids.h"

namespace eos::db {

using foundation::ObjectId;

enum class ConstraintKind : std::int32_t {
  kClock = 0,
  kInputDelay = 1,
  kOutputDelay = 2,
  kFalsePath = 3,
  kMulticyclePath = 4,
  kMaxDelay = 5,
  kMinDelay = 6,
  kCaseAnalysis = 7,
};

enum class BindingStatus : std::int32_t {
  kBound = 0,
  kUnresolved = 1,
  kEmpty = 2,
};

using ConstraintId = std::uint32_t;

struct Constraint {
  ConstraintId id = 0;
  ConstraintKind kind = ConstraintKind::kClock;
  std::string name;
  std::string target_spec;  // exactly as written by the user
  ObjectId bound_object;    // null unless status is kBound
  BindingStatus status = BindingStatus::kUnresolved;
};

struct CoverageReport {
  std::size_t total = 0;
  std::size_t bound = 0;
  std::size_t unresolved = 0;
  std::size_t empty = 0;
  // Complete means every constraint binds to a real object. An empty
  // constraint set is not complete coverage of anything.
  bool complete() const {
    return total > 0 && unresolved == 0 && empty == 0;
  }
};

// The complete serializable state of a ConstraintSet.
struct ConstraintState {
  std::vector<Constraint> constraints;
  ConstraintId next_id = 1;
};

class ConstraintSet {
 public:
  // Adds a constraint and resolves its target against the design now.
  // Never fails on an unresolvable target: the constraint is stored
  // with its status and returned id either way.
  ConstraintId Add(ConstraintKind kind, const std::string& name,
                   const std::string& target_spec, const Design& design);

  // Re-runs resolution for every constraint, in place. Statuses move
  // in both directions: an unresolved target that now exists binds,
  // and a constraint whose spec was edited to empty reports kEmpty.
  void RebindAll(const Design& design);

  const Constraint* Find(ConstraintId id) const;
  const std::vector<Constraint>& all() const { return constraints_; }

  CoverageReport Coverage() const;
  std::vector<const Constraint*> Unresolved() const;

  // Checkpoint support: ExportState snapshots the set; FromState
  // validates it (nonzero unique ids, bound status consistent with a
  // non-null bound object, next_id ahead of every id) and only then
  // builds. Whether a bound ID still names a live design object is
  // the checkpoint layer's cross-check, not this module's.
  ConstraintState ExportState() const;
  static foundation::Result<ConstraintSet> FromState(
      const ConstraintState& state);

 private:
  static void Resolve(Constraint& constraint, const Design& design);

  ConstraintId next_id_ = 1;
  std::vector<Constraint> constraints_;
};

}  // namespace eos::db

#endif  // EOS_DB_CONSTRAINTS_H_
