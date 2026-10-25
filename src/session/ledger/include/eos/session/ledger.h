// ledger: the state ledger (L5 session).
//
// The ledger is the single record of how design state came to be. Every
// mutation of project state is a transaction, committed through this
// module, recording (per the Revised Plan, Section 10): the input design
// revision, the changed objects by persistent ID, before and after
// metrics, the analyses and reports it invalidates, a proof status, the
// tool build, and the rollback checkpoint (its input revision).
//
// Analyses and reports register here as artifacts, bound to the revision
// they describe. Artifact validity is DERIVED, never stored: an artifact
// is valid exactly when it is bound at or before the current revision
// and no committed transaction at or after its binding has invalidated
// it. Deriving validity is what makes rollback exact for free: rolling
// the ledger back to an earlier revision restores the validity every
// artifact had at that revision, and an artifact bound to a revision
// that no longer exists is invalid until the state is rebuilt.
//
// Revisions are dense: the first committed transaction produces
// revision 1, and a transaction may only begin at the current revision.
// After a rollback, new transactions reuse the freed revision numbers;
// rolled-back records stay in the history, flagged inactive, as the
// audit trail.
//
// This header is a stable surface.
#ifndef EOS_SESSION_LEDGER_H_
#define EOS_SESSION_LEDGER_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "eos/foundation/errors.h"
#include "eos/foundation/ids.h"

namespace eos::session {

using foundation::ErrorCode;
using foundation::ObjectId;
using foundation::Result;
using foundation::Status;

using Revision = std::uint64_t;
using TxnId = std::uint64_t;
using ArtifactId = std::uint64_t;

enum class ProofStatus : std::int32_t {
  kUnknown = 0,
  kPass = 1,
  kFail = 2,
};

enum class ArtifactKind : std::int32_t {
  kAnalysis = 0,
  kReport = 1,
};

struct MetricValue {
  double value = 0.0;
  std::string unit;
};

// Metric name -> measured value with its unit. Units are explicit
// because a bare number is how wrong results get certified.
using MetricMap = std::map<std::string, MetricValue>;

// Everything a caller must declare to mutate state. The ledger takes
// the invalidation list as declared: judging what a change invalidates
// is the caller's engineering decision, recording and enforcing it is
// the ledger's.
struct TransactionSpec {
  Revision input_revision = 0;
  std::string label;
  std::string tool_build;
  std::vector<ObjectId> changed_objects;
  MetricMap before_metrics;
  MetricMap after_metrics;
  std::vector<ArtifactId> invalidates;
};

struct TransactionRecord {
  TxnId id = 0;
  Revision input_revision = 0;
  Revision output_revision = 0;
  // The revision a rollback of this transaction returns to; by
  // construction this is the input revision, recorded explicitly
  // because the plan names it as part of the transaction record.
  Revision rollback_checkpoint = 0;
  std::string label;
  std::string tool_build;
  std::vector<ObjectId> changed_objects;
  MetricMap before_metrics;
  MetricMap after_metrics;
  std::vector<ArtifactId> invalidated;
  ProofStatus proof = ProofStatus::kUnknown;
  // False after the ledger has rolled back past this transaction.
  bool active = true;
};

struct ArtifactRecord {
  ArtifactId id = 0;
  ArtifactKind kind = ArtifactKind::kAnalysis;
  std::string name;
  Revision bound_revision = 0;
  ProofStatus proof = ProofStatus::kUnknown;
};

class Ledger {
 public:
  Ledger() = default;

  Revision current_revision() const { return current_revision_; }

  // Two-phase commit: a transaction is begun, inspected (and, by the
  // checkpoint layer, made durable), then committed or aborted. A crash
  // between begin and commit leaves a pending transaction that a
  // restart must decide on: commit it, or abort it (rollback). At most
  // one transaction may be pending.
  //
  // Begin fails with kInvalidState when a transaction is already
  // pending, with kStaleRevision when spec.input_revision is not the
  // current revision, and with kNotFound when spec.invalidates names
  // an artifact that does not exist.
  Status BeginTransaction(const TransactionSpec& spec);

  // Commits the pending transaction with its proof status: appends the
  // record, applies its invalidations, and advances the current
  // revision by one. Returns the new current revision.
  Result<Revision> CommitPending(ProofStatus proof);

  // Discards the pending transaction without recording it.
  Status AbortPending();

  bool HasPending() const { return pending_.has_value(); }
  const TransactionSpec* Pending() const {
    return pending_ ? &*pending_ : nullptr;
  }

  // Registers an analysis or report result against the current
  // revision. Registration is cheap and carries no validity promise
  // beyond the binding: validity is derived on query, below.
  ArtifactId RegisterArtifact(ArtifactKind kind, std::string name,
                              ProofStatus proof);

  const ArtifactRecord* FindArtifact(ArtifactId id) const;

  // Derived validity: bound at or before the current revision, and no
  // active committed transaction, at or after the binding revision,
  // lists this artifact as invalidated.
  bool ArtifactValid(ArtifactId id) const;

  // Current means valid AND bound to exactly the current revision. The
  // export gate's freshness rule is stated in these terms: an artifact
  // from an older revision, however valid, never certifies the current
  // design.
  bool ArtifactCurrent(ArtifactId id) const;

  Status SetArtifactProof(ArtifactId id, ProofStatus proof);

  // Returns the ledger to an earlier revision: transactions committed
  // after the target are flagged inactive (kept as audit history), the
  // current revision becomes the target, and artifact validity follows
  // from the derivation rule. Fails with kInvalidState while a
  // transaction is pending, and with kInvalidArgument for a target
  // beyond the current revision.
  Status RollbackTo(Revision target);

  // The full record, active and rolled-back transactions alike, in
  // commit order. The checkpoint layer serializes this.
  const std::vector<TransactionRecord>& history() const { return history_; }
  const std::vector<ArtifactRecord>& artifacts() const { return artifacts_; }

 private:
  Revision current_revision_ = 0;
  TxnId next_txn_ = 1;
  ArtifactId next_artifact_ = 1;
  std::optional<TransactionSpec> pending_;
  std::vector<TransactionRecord> history_;
  std::vector<ArtifactRecord> artifacts_;
};

}  // namespace eos::session

#endif  // EOS_SESSION_LEDGER_H_
