// session: the Project object (L5 session).
//
// A Project is the unit of work every flow runs inside: it owns the
// one open design, the scenario set, the constraint set, the state
// ledger and the report bindings, together. (This module's README
// has always named the Session and Project objects as its charge;
// Phase 1 increment 5 lands the Project half, and Phase 2 unit 2.0
// makes the report bindings project state.)
//
// The project file is the full project checkpoint: a single file,
// magic "EOSPROJ 1", holding the L2 checkpoint text (design,
// scenarios, constraints, from db/checkpoint), the ledger snapshot
// text (from session/ledger) and the report bindings text (the
// session/reports bindings, serialized by this module), each
// length-prefixed so the texts embed without escaping or ambiguity.
// Save is validated and atomic, exactly like the checkpoint save it
// composes. Load is all-or-nothing: the envelope and every embedded
// text are parsed, and every module state is rebuilt and validated,
// before any live member changes; a corrupt file is an error and
// leaves the project exactly as it was. Constraints whose bound
// object is absent from the restored design are marked unresolved
// and counted in the returned report, exactly as the L2 checkpoint
// load reports them. A report binding whose ledger artifact is
// absent from the restored ledger is dropped and counted in the
// returned report, never silently kept: the ledger is the record of
// what exists, and a binding that names a report the ledger does
// not hold certifies nothing. Project files written before report
// bindings became project state carry no reports section; they load
// with an empty binding set.
//
// This header is a stable surface.
#ifndef EOS_SESSION_PROJECT_H_
#define EOS_SESSION_PROJECT_H_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "eos/db/checkpoint.h"
#include "eos/db/constraints.h"
#include "eos/db/design.h"
#include "eos/db/scenarios.h"
#include "eos/foundation/errors.h"
#include "eos/session/ledger.h"

namespace eos::session {

using foundation::Result;
using foundation::Status;

// One scenario as a report binding saw it at registration time. The
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
// ReportRegistry::RegisterReport (session/reports) and never modified
// afterwards: it is a snapshot of the conditions the report was
// computed under, and later changes to the scenario set (or anything
// else) do not rewrite history. Bindings are project state: they are
// serialized into the project file alongside the ledger whose
// artifacts they link to.
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

// What a project load reports, beyond success: the L2 checkpoint
// restore report, and the report bindings outcome. A binding counts
// as restored when its ledger artifact is present (as a report) in
// the restored ledger; every other binding in the file is dropped
// and counted here, never silently kept.
struct ProjectRestoreInfo {
  db::RestoreInfo checkpoint;
  std::size_t bindings_restored = 0;
  std::size_t bindings_dropped = 0;
};

class Project {
 public:
  // The composed state. Public because the command layer and the
  // flows operate on the members directly, as they did when these
  // were the loose members of the registry's CommandContext.
  std::optional<db::Design> design;
  db::ScenarioSet scenarios;
  db::ConstraintSet constraints;
  Ledger ledger;
  // Report bindings, in registration order. Owned here since Phase 2
  // unit 2.0 so they save and restore with the project they
  // describe; session/reports creates and reads them.
  std::vector<ReportBinding> report_bindings;

  bool HasDesign() const { return design.has_value(); }

  // Opens a fresh project state: no design, empty scenario and
  // constraint sets, an empty ledger, no report bindings.
  void Reset();

  // Composes and writes the project file. Fails with kInvalidState
  // when no design is open, with the checkpoint layer's status when
  // the state would not restore, and with kIoError when the file
  // cannot be written.
  Status SaveProject(const std::string& path) const;

  // Reads, parses and validates a whole project file before changing
  // anything, then adopts the restored state in one step. On success
  // returns the restore report (constraints marked unresolved on
  // restore, report bindings restored and dropped). On any failure
  // the project is untouched.
  Result<ProjectRestoreInfo> LoadProject(const std::string& path);
};

}  // namespace eos::session

#endif  // EOS_SESSION_PROJECT_H_
