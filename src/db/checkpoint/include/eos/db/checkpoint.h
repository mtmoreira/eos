// checkpoint: saves and restores design-data checkpoints (L2).
//
// A checkpoint file holds the complete L2 state of a project: the
// design graph with its ID allocator state, the scenario set, and the
// constraint set. (The state ledger is L5 state; the session layer
// composes it with this file into the full project checkpoint. The
// other Revised Plan Section 10 components, synthesis state, Liberty
// corner mapping, RC extraction state and the technology package
// revision, join the format as their owning modules land; the format
// is versioned precisely so they can.)
//
// Format, version 1: a line-oriented text format. First line is the
// magic "EOSCKPT 1"; sections follow in fixed order (design,
// scenarios, constraints); the last line carries an FNV-1a 64 checksum
// over every preceding byte. String fields are percent-escaped, so
// names may contain any byte except newline-adjacent ambiguity.
//
// Guarantees:
// - Atomic save: the file is written to "<path>.tmp" and renamed over
//   the target, so a crash mid-save never leaves a torn checkpoint.
// - Validated save: state that FromState would reject is never
//   written in the first place.
// - All-or-nothing load: any defect (bad magic, unsupported version,
//   checksum mismatch, truncation, malformed record, inconsistent
//   state) is an error, and no partial state is produced.
// - Honest restore: a constraint whose bound object is absent from
//   the restored design is not dropped and not left falsely bound; it
//   is marked unresolved and counted in RestoreInfo.
//
// This header is a stable surface.
#ifndef EOS_DB_CHECKPOINT_H_
#define EOS_DB_CHECKPOINT_H_

#include <cstddef>
#include <string>

#include "eos/db/constraints.h"
#include "eos/db/design.h"
#include "eos/db/scenarios.h"
#include "eos/foundation/errors.h"

namespace eos::db {

using foundation::Result;
using foundation::Status;

// Everything a checkpoint file holds, as module state snapshots.
struct CheckpointData {
  DesignState design;
  ScenarioState scenarios;
  ConstraintState constraints;
};

struct RestoreInfo {
  // Constraints whose bound object was missing from the restored
  // design and were therefore marked unresolved on load.
  std::size_t constraints_marked_unresolved = 0;
};

class Checkpoint {
 public:
  static constexpr int kFormatVersion = 1;

  // Validates the state, then writes it atomically. Fails with
  // kInvalidArgument on inconsistent state (including a constraint
  // bound to an object the design does not contain) and with
  // kIoError when the file cannot be written or renamed.
  static Status Save(const std::string& path, const CheckpointData& data);

  // Reads and fully validates a checkpoint. On success, returns the
  // restored state and, when info is non-null, fills RestoreInfo.
  static Result<CheckpointData> Load(const std::string& path,
                                     RestoreInfo* info);
};

}  // namespace eos::db

#endif  // EOS_DB_CHECKPOINT_H_
