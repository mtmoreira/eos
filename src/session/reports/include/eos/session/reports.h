// reports: the report registry and the export freshness gate
// (L5 session).
//
// A report is a named result bound to the design revision it was
// computed at and to the exact conditions it was computed under
// (Revised Plan, Sections 4 and 7): the scenario set, the parasitics
// fidelity, the timing model and the tool build. RegisterReport
// creates the ledger artifact and its ReportBinding together, in one
// step, so a report can never exist in the ledger without the binding
// that says what it certifies, or the other way round. The binding is
// immutable afterwards: it is a snapshot, and later changes to the
// scenario set (or anything else) do not rewrite history. Freshness
// and validity are never stored in the binding; they are read from
// the ledger on every query, so a state change that invalidates or
// ages a report is seen by the gate immediately.
//
// The export gate is the Phase 1 form of the Plan Section 17 rule
// that no stale result can certify an export. Evaluate checks every
// required report and the project's constraint coverage, and returns
// every blocker it finds, not just the first: a report that is
// missing, invalid, stale (valid but not bound to the current
// revision), or not proven pass blocks the export, and so does
// incomplete constraint coverage.
//
// Bindings are session state, not project state: they are not part
// of the project checkpoint (increment 5.1), so a restore starts
// with an empty registry and reports must be registered again in the
// new session. The ledger artifacts themselves do survive a restore.
//
// This header is a stable surface.
#ifndef EOS_SESSION_REPORTS_H_
#define EOS_SESSION_REPORTS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "eos/db/scenarios.h"
#include "eos/foundation/errors.h"
#include "eos/session/ledger.h"
#include "eos/session/project.h"

namespace eos::session {

using foundation::Result;
using foundation::Status;

// One scenario as the binding saw it at registration time. The
// triple (mode, corner, parasitics) is the scenario's identity, per
// the L2 scenario model; the id and name are its handles then.
struct ScenarioSnapshot {
  db::ScenarioId id = 0;
  std::string name;
  std::string mode;
  std::string corner;
  db::ParasiticsLevel parasitics = db::ParasiticsLevel::kEstimate;
};

// The immutable record of what a report certifies. Created only by
// ReportRegistry::RegisterReport and never modified afterwards.
struct ReportBinding {
  ArtifactId artifact_id = 0;
  std::string name;
  Revision bound_revision = 0;
  // The active scenario set at registration time, in creation order.
  std::vector<ScenarioSnapshot> scenarios;
  // The single parasitics level covered, when every active scenario
  // ran at the same level. Absent when the active set was empty or
  // mixed levels: levels are never mixed silently inside one report
  // (Plan Section 7), so a mixed binding says so here and the
  // per-scenario levels in `scenarios` carry the exact record.
  std::optional<db::ParasiticsLevel> parasitics;
  std::string timing_model;
  std::string tool_build;
};

// A report's live standing, read from the ledger at query time.
// `found` is false when the registry holds no binding under the
// queried name (or the ledger has lost the artifact, which the
// registry treats the same way for gate purposes).
struct ReportStatus {
  bool found = false;
  ArtifactId artifact_id = 0;
  bool valid = false;
  bool current = false;
  ProofStatus proof = ProofStatus::kUnknown;
  Revision bound_revision = 0;
  Revision current_revision = 0;
};

enum class BlockerKind : std::int32_t {
  kMissing = 0,
  kInvalid = 1,
  kStale = 2,
  kProofNotPass = 3,
  kCoverageIncomplete = 4,
};

const char* BlockerKindName(BlockerKind kind);

struct ExportBlocker {
  BlockerKind kind = BlockerKind::kMissing;
  // The required report this blocker belongs to. Empty for the
  // coverage blocker, which belongs to the project as a whole.
  std::string report;
  std::string detail;
};

struct ExportVerdict {
  std::vector<ExportBlocker> blockers;
  bool allowed() const { return blockers.empty(); }
};

class ReportRegistry {
 public:
  // Registers a report in the project's ledger and binds it, in one
  // step. The binding snapshots the project's active scenario set,
  // the ledger's current revision and the declared timing model and
  // tool build. Fails with kInvalidArgument on an empty name, an
  // empty timing model, or a name this registry already holds (one
  // name certifies one report; register the new run under a new
  // name or in a fresh registry), and leaves the ledger untouched on
  // failure, because the duplicate check runs before registration.
  Result<ArtifactId> RegisterReport(Project& project,
                                    const std::string& name,
                                    ProofStatus proof,
                                    const std::string& timing_model,
                                    const std::string& tool_build);

  // Convenience form with the build tag the command layer uses for
  // its own transactions.
  Result<ArtifactId> RegisterReport(Project& project,
                                    const std::string& name,
                                    ProofStatus proof,
                                    const std::string& timing_model) {
    return RegisterReport(project, name, proof, timing_model,
                          "eos-phase1");
  }

  const ReportBinding* FindByName(const std::string& name) const;
  const ReportBinding* FindByArtifact(ArtifactId id) const;
  const std::vector<ReportBinding>& bindings() const { return bindings_; }

  void Clear() { bindings_.clear(); }

  // The named report's standing, read live from the project's
  // ledger: validity and freshness are the ledger's derived values,
  // proof is the artifact's current proof status.
  ReportStatus StatusFor(const Project& project,
                         const std::string& name) const;

  // The export gate. For each required report name, in order: a
  // missing binding (or a binding whose ledger artifact is gone)
  // blocks as kMissing; an invalid artifact blocks as kInvalid; a
  // valid artifact bound to an older revision blocks as kStale; a
  // proof status other than kPass blocks as kProofNotPass. After the
  // per-report checks, incomplete constraint coverage (the L2
  // accounting: every constraint bound, and at least one constraint)
  // blocks once as kCoverageIncomplete. An empty required list is
  // checked for coverage only.
  ExportVerdict Evaluate(const Project& project,
                         const std::vector<std::string>& required) const;

 private:
  std::vector<ReportBinding> bindings_;
};

}  // namespace eos::session

#endif  // EOS_SESSION_REPORTS_H_
