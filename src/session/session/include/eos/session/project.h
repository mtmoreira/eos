// session: the Project object (L5 session).
//
// A Project is the unit of work every flow runs inside: it owns the
// one open design, the scenario set, the constraint set and the state
// ledger, together. (This module's README has always named the
// Session and Project objects as its charge; Phase 1 increment 5
// lands the Project half.)
//
// The project file is the full project checkpoint: a single file,
// magic "EOSPROJ 1", holding the L2 checkpoint text (design,
// scenarios, constraints, from db/checkpoint) and the ledger snapshot
// text (from session/ledger), each length-prefixed so the two texts
// embed without escaping or ambiguity. Save is validated and atomic,
// exactly like the checkpoint save it composes. Load is
// all-or-nothing: the envelope, the checkpoint text and the ledger
// text are all parsed, and every module state is rebuilt and
// validated, before any live member changes; a corrupt file is an
// error and leaves the project exactly as it was. Constraints whose
// bound object is absent from the restored design are marked
// unresolved and counted in the returned RestoreInfo, exactly as the
// L2 checkpoint load reports them.
//
// This header is a stable surface.
#ifndef EOS_SESSION_PROJECT_H_
#define EOS_SESSION_PROJECT_H_

#include <optional>
#include <string>

#include "eos/db/checkpoint.h"
#include "eos/db/constraints.h"
#include "eos/db/design.h"
#include "eos/db/scenarios.h"
#include "eos/foundation/errors.h"
#include "eos/session/ledger.h"

namespace eos::session {

using foundation::Result;
using foundation::Status;

class Project {
 public:
  // The composed state. Public because the command layer and the
  // flows operate on the members directly, as they did when these
  // were the loose members of the registry's CommandContext.
  std::optional<db::Design> design;
  db::ScenarioSet scenarios;
  db::ConstraintSet constraints;
  Ledger ledger;

  bool HasDesign() const { return design.has_value(); }

  // Opens a fresh project state: no design, empty scenario and
  // constraint sets, an empty ledger.
  void Reset();

  // Composes and writes the project file. Fails with kInvalidState
  // when no design is open, with the checkpoint layer's status when
  // the state would not restore, and with kIoError when the file
  // cannot be written.
  Status SaveProject(const std::string& path) const;

  // Reads, parses and validates a whole project file before changing
  // anything, then adopts the restored state in one step. On success
  // returns the checkpoint restore report (constraints marked
  // unresolved on restore). On any failure the project is untouched.
  Result<db::RestoreInfo> LoadProject(const std::string& path);
};

}  // namespace eos::session

#endif  // EOS_SESSION_PROJECT_H_
