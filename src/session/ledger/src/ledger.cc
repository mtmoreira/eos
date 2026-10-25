// ledger: the state ledger (L5 session).
#include "eos/session/ledger.h"

#include <algorithm>
#include <utility>

namespace eos::session {

Status Ledger::BeginTransaction(const TransactionSpec& spec) {
  if (pending_.has_value()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "a transaction is already pending");
  }
  if (spec.input_revision != current_revision_) {
    return Status::Error(
        ErrorCode::kStaleRevision,
        "transaction input revision is not the current revision");
  }
  for (const ArtifactId id : spec.invalidates) {
    if (FindArtifact(id) == nullptr) {
      return Status::Error(ErrorCode::kNotFound,
                           "transaction invalidates an unknown artifact");
    }
  }
  pending_ = spec;
  return Status::Ok();
}

Result<Revision> Ledger::CommitPending(ProofStatus proof) {
  if (!pending_.has_value()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "no transaction is pending");
  }
  TransactionRecord record;
  record.id = next_txn_++;
  record.input_revision = pending_->input_revision;
  record.output_revision = current_revision_ + 1;
  record.rollback_checkpoint = pending_->input_revision;
  record.label = pending_->label;
  record.tool_build = pending_->tool_build;
  record.changed_objects = pending_->changed_objects;
  record.before_metrics = pending_->before_metrics;
  record.after_metrics = pending_->after_metrics;
  record.invalidated = pending_->invalidates;
  record.proof = proof;
  record.active = true;
  history_.push_back(std::move(record));
  current_revision_ += 1;
  pending_.reset();
  return current_revision_;
}

Status Ledger::AbortPending() {
  if (!pending_.has_value()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "no transaction is pending");
  }
  pending_.reset();
  return Status::Ok();
}

ArtifactId Ledger::RegisterArtifact(ArtifactKind kind, std::string name,
                                    ProofStatus proof) {
  ArtifactRecord record;
  record.id = next_artifact_++;
  record.kind = kind;
  record.name = std::move(name);
  record.bound_revision = current_revision_;
  record.proof = proof;
  artifacts_.push_back(std::move(record));
  return artifacts_.back().id;
}

const ArtifactRecord* Ledger::FindArtifact(ArtifactId id) const {
  for (const ArtifactRecord& record : artifacts_) {
    if (record.id == id) {
      return &record;
    }
  }
  return nullptr;
}

bool Ledger::ArtifactValid(ArtifactId id) const {
  const ArtifactRecord* artifact = FindArtifact(id);
  if (artifact == nullptr || artifact->bound_revision > current_revision_) {
    return false;
  }
  for (const TransactionRecord& txn : history_) {
    if (!txn.active || txn.output_revision > current_revision_) {
      continue;
    }
    if (txn.input_revision < artifact->bound_revision) {
      continue;
    }
    if (std::find(txn.invalidated.begin(), txn.invalidated.end(), id) !=
        txn.invalidated.end()) {
      return false;
    }
  }
  return true;
}

bool Ledger::ArtifactCurrent(ArtifactId id) const {
  const ArtifactRecord* artifact = FindArtifact(id);
  return artifact != nullptr &&
         artifact->bound_revision == current_revision_ &&
         ArtifactValid(id);
}

Status Ledger::SetArtifactProof(ArtifactId id, ProofStatus proof) {
  for (ArtifactRecord& record : artifacts_) {
    if (record.id == id) {
      record.proof = proof;
      return Status::Ok();
    }
  }
  return Status::Error(ErrorCode::kNotFound, "unknown artifact");
}

Status Ledger::RollbackTo(Revision target) {
  if (pending_.has_value()) {
    return Status::Error(
        ErrorCode::kInvalidState,
        "cannot roll back while a transaction is pending");
  }
  if (target > current_revision_) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "rollback target is beyond the current revision");
  }
  for (TransactionRecord& txn : history_) {
    if (txn.output_revision > target) {
      txn.active = false;
    }
  }
  current_revision_ = target;
  return Status::Ok();
}

LedgerSnapshot Ledger::Snapshot() const {
  LedgerSnapshot snapshot;
  snapshot.current_revision = current_revision_;
  snapshot.next_txn = next_txn_;
  snapshot.next_artifact = next_artifact_;
  snapshot.pending = pending_;
  snapshot.history = history_;
  snapshot.artifacts = artifacts_;
  return snapshot;
}

Status Ledger::Restore(const LedgerSnapshot& snapshot) {
  auto reject = [](const char* what) {
    return Status::Error(ErrorCode::kInvalidArgument, what);
  };
  // History: strictly increasing ids, dense revision steps, the
  // rollback checkpoint is the input revision by construction.
  TxnId max_txn = 0;
  Revision max_output = 0;
  Revision last_active_output = 0;
  for (const TransactionRecord& txn : snapshot.history) {
    if (txn.id <= max_txn || txn.output_revision != txn.input_revision + 1 ||
        txn.rollback_checkpoint != txn.input_revision) {
      return reject("ledger snapshot: malformed transaction record");
    }
    max_txn = txn.id;
    if (txn.output_revision > max_output) {
      max_output = txn.output_revision;
    }
    if (txn.active) {
      last_active_output = txn.output_revision;
    }
  }
  if (snapshot.current_revision != last_active_output) {
    return reject(
        "ledger snapshot: current revision is not the last active output");
  }
  if (snapshot.next_txn <= max_txn) {
    return reject("ledger snapshot: txn counter would reuse an id");
  }
  // Artifacts: strictly increasing ids, bound at a revision the
  // history actually reached.
  ArtifactId max_artifact = 0;
  for (const ArtifactRecord& artifact : snapshot.artifacts) {
    if (artifact.id <= max_artifact ||
        artifact.bound_revision > max_output) {
      return reject("ledger snapshot: malformed artifact record");
    }
    max_artifact = artifact.id;
  }
  if (snapshot.next_artifact <= max_artifact) {
    return reject("ledger snapshot: artifact counter would reuse an id");
  }
  auto known = [&snapshot](ArtifactId id) {
    for (const ArtifactRecord& artifact : snapshot.artifacts) {
      if (artifact.id == id) {
        return true;
      }
    }
    return false;
  };
  for (const TransactionRecord& txn : snapshot.history) {
    for (const ArtifactId id : txn.invalidated) {
      if (!known(id)) {
        return reject("ledger snapshot: record invalidates unknown artifact");
      }
    }
  }
  if (snapshot.pending.has_value()) {
    if (snapshot.pending->input_revision != snapshot.current_revision) {
      return reject(
          "ledger snapshot: pending transaction starts at a stale revision");
    }
    for (const ArtifactId id : snapshot.pending->invalidates) {
      if (!known(id)) {
        return reject(
            "ledger snapshot: pending transaction invalidates unknown "
            "artifact");
      }
    }
  }
  current_revision_ = snapshot.current_revision;
  next_txn_ = snapshot.next_txn;
  next_artifact_ = snapshot.next_artifact;
  pending_ = snapshot.pending;
  history_ = snapshot.history;
  artifacts_ = snapshot.artifacts;
  return Status::Ok();
}

}  // namespace eos::session
