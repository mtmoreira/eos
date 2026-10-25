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

}  // namespace eos::session
