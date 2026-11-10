// waivers: waiver records (L2 design-data), with no silent drops.
//
// A waiver is a named approval for a named violation: a reporting
// disposition only, applied at report time over violations the
// engine just computed. It is not a constraint, not a false path,
// and not requirement coverage. It never changes what the engine
// checks, what slack is computed, or what TNS is: a waived
// violation stays in every listing, marked with its waiver name,
// and totals always show both numbers (unwaived and waived).
//
// Matching is exact on the violation identity (kind, class,
// target, scene). No wildcards in v1: a glob could silently waive
// more than intended, which is a silent-drop failure in the exact
// sense the constraint module forbids. One waiver covers one
// identity; covering N endpoints means N waivers, each with its
// own reason and approver.
//
// A waiver that matches nothing is retained and reports
// unmatched, never dropped: it is a standing approval waiting for
// its violation to appear. An expired waiver matches nothing and
// reports expired; expiry is a disposition change, not a
// deletion. Status is derived, never stored, so a waiver moves
// between active and unmatched as the design changes, with no
// stored state to go stale.
//
// The class vocabularies live here, so the store and the command
// layer validate against one list: the five report_constraint
// classes for timing, the check_timing check names for check, and
// the ten analysis-coverage check types for coverage. The check
// names mirror the check_timing table in the registry; the
// registry test pins the agreement.
//
// This header is a stable surface.
#ifndef EOS_DB_WAIVERS_H_
#define EOS_DB_WAIVERS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "eos/foundation/errors.h"

namespace eos::db {

using foundation::Result;
using foundation::Status;

enum class WaiverKind : std::int32_t {
  kTiming = 0,
  kCheck = 1,
  kCoverage = 2,
};

// Parses a kind word ("timing", "check", "coverage"); nullopt for
// anything else.
std::optional<WaiverKind> ParseWaiverKind(const std::string& word);
std::string WaiverKindWord(WaiverKind kind);

// Whether this class is a known class for the kind: the five
// report_constraint classes for timing, the check_timing check
// names for check, the ten analysis-coverage check types for
// coverage. The `path` kind does not exist in v1.
bool IsKnownWaiverClass(WaiverKind kind, const std::string& klass);

// ISO calendar date helpers. ParseIsoDate validates YYYY-MM-DD as
// a real calendar date (month lengths, leap years). TodayUtc reads
// the system date in UTC. Validated dates compare
// lexicographically, so expiry is a string comparison.
bool ParseIsoDate(const std::string& text);
std::string TodayUtc();

using WaiverId = std::uint32_t;

// One violation identity, as a report computes it: the violation
// source, the class within it, the violating endpoint, pin or
// finding object by name, and the scene it was computed under.
// A check_timing finding carries no scene; its identity uses an
// empty scene, so only a scene-less check waiver matches it.
struct WaiverViolation {
  WaiverKind kind;
  std::string klass;
  std::string target;
  std::string scene;
};

struct Waiver {
  WaiverId id = 0;
  std::string name;
  WaiverKind kind = WaiverKind::kTiming;
  std::string klass;
  std::string target;   // exactly as written, matched by name
  std::string scene;    // empty means all scenes
  std::string reason;
  std::string approved_by;
  std::string expires;  // YYYY-MM-DD, empty means never
  std::uint64_t created_revision = 0;

  // Past its date the waiver matches nothing. A waiver expiring
  // today is not expired: expiry is strictly past the date.
  bool IsExpired(const std::string& today) const {
    return !expires.empty() && expires < today;
  }

  // Exact match on kind, class, target, and scene. An empty
  // waiver scene matches every violation scene; a named scene
  // must equal the violation's scene. An expired waiver matches
  // nothing.
  bool Matches(const WaiverViolation& violation,
               const std::string& today) const;
};

enum class WaiverStatus {
  kActive,     // matches at least one current violation
  kUnmatched,  // matches nothing right now
  kExpired,    // past its date
};

std::string WaiverStatusWord(WaiverStatus status);

struct WaiverCoverage {
  std::size_t total = 0;
  std::size_t active = 0;
  std::size_t unmatched = 0;
  std::size_t expired = 0;
};

// The complete serializable state of a WaiverSet.
struct WaiverState {
  std::vector<Waiver> waivers;
  WaiverId next_id = 1;
};

class WaiverSet {
 public:
  // Adds a waiver. Validates the kind value, the class for the
  // kind, the scene against known_scenes (a named scene must be
  // one of them; an empty scene means all scenes), the expiry
  // date, the reason and approver (both required, non-empty),
  // and the name (duplicates fail). Any failure returns
  // kInvalidArgument naming the reason; nothing is stored on
  // failure. The id comes from next_id and is never reused.
  Result<WaiverId> Add(WaiverKind kind, const std::string& name,
                       const std::string& klass, const std::string& target,
                       const std::string& scene, const std::string& reason,
                       const std::string& approved_by,
                       const std::string& expires,
                       std::uint64_t created_revision,
                       const std::vector<std::string>& known_scenes);

  // Removes the waiver with this name, if present.
  bool Remove(const std::string& name);

  const Waiver* Find(const std::string& name) const;
  const Waiver* Find(WaiverId id) const;
  const std::vector<Waiver>& all() const { return waivers_; }

  // The first waiver, in creation order, matching this
  // violation, or nullptr. Expired waivers never match.
  const Waiver* Match(const WaiverViolation& violation,
                      const std::string& today) const;

  // How many of these violations this waiver matches.
  std::size_t MatchesFor(const Waiver& waiver,
                         const std::vector<WaiverViolation>& violations,
                         const std::string& today) const;

  // The derived status of one waiver against these violations:
  // expired when past its date, active when it matches at least
  // one, unmatched otherwise.
  static WaiverStatus StatusOf(const Waiver& waiver, std::size_t matches,
                               const std::string& today);

  // The counts over the set against these violations.
  WaiverCoverage Coverage(const std::vector<WaiverViolation>& violations,
                          const std::string& today) const;

  // State support: ExportState snapshots the set; FromState
  // validates it (nonzero unique ids, unique names, valid kinds,
  // classes, expiry dates, and next_id ahead of every id) and
  // only then builds.
  WaiverState ExportState() const;
  static Result<WaiverSet> FromState(const WaiverState& state);

 private:
  WaiverId next_id_ = 1;
  std::vector<Waiver> waivers_;
};

}  // namespace eos::db

#endif  // EOS_DB_WAIVERS_H_
