// Unit tests: session/ledger snapshot and restore (restart semantics).
#include "eos/session/ledger.h"

#include <gtest/gtest.h>

namespace eos::session {
namespace {

TransactionSpec Spec(Revision input, const std::string& label) {
  TransactionSpec spec;
  spec.input_revision = input;
  spec.label = label;
  spec.tool_build = "eos-test-build";
  return spec;
}

// A ledger with two committed transactions, one invalidated report,
// and one live report registered at the current revision (2).
Ledger Populated() {
  Ledger ledger;
  const ArtifactId early = ledger.RegisterArtifact(
      ArtifactKind::kReport, "early-timing", ProofStatus::kPass);
  TransactionSpec first = Spec(0, "synthesize");
  first.invalidates.push_back(early);
  EXPECT_TRUE(ledger.BeginTransaction(first).ok());
  EXPECT_TRUE(ledger.CommitPending(ProofStatus::kPass).ok());
  EXPECT_TRUE(ledger.BeginTransaction(Spec(1, "place")).ok());
  EXPECT_TRUE(ledger.CommitPending(ProofStatus::kPass).ok());
  const ArtifactId live = ledger.RegisterArtifact(
      ArtifactKind::kReport, "placed-timing", ProofStatus::kPass);
  EXPECT_EQ(live, 2u);
  return ledger;
}

TEST(LedgerSnapshot, RoundTripsHistoryArtifactsAndValidity) {
  Ledger source = Populated();
  Ledger restored;
  ASSERT_TRUE(restored.Restore(source.Snapshot()).ok());
  EXPECT_EQ(restored.current_revision(), 2u);
  ASSERT_EQ(restored.history().size(), 2u);
  EXPECT_EQ(restored.history()[0].label, "synthesize");
  EXPECT_EQ(restored.history()[1].output_revision, 2u);
  ASSERT_EQ(restored.artifacts().size(), 2u);
  EXPECT_FALSE(restored.ArtifactValid(1));  // invalidated by txn 1
  EXPECT_TRUE(restored.ArtifactValid(2));
  EXPECT_TRUE(restored.ArtifactCurrent(2));
  // Counters continue without reuse.
  const ArtifactId next = restored.RegisterArtifact(
      ArtifactKind::kAnalysis, "post-restore", ProofStatus::kUnknown);
  EXPECT_EQ(next, 3u);
}

TEST(LedgerSnapshot, CrashMidTransactionThenAbortIsTheRollbackDecision) {
  Ledger ledger;
  const ArtifactId report = ledger.RegisterArtifact(
      ArtifactKind::kReport, "timing", ProofStatus::kPass);
  TransactionSpec pending = Spec(0, "eco-resize");
  pending.invalidates.push_back(report);
  ASSERT_TRUE(ledger.BeginTransaction(pending).ok());

  // The snapshot is what survived the crash; the ledger object did
  // not. The pending transaction is still there, undecided.
  const LedgerSnapshot snapshot = ledger.Snapshot();
  Ledger restarted;
  ASSERT_TRUE(restarted.Restore(snapshot).ok());
  ASSERT_TRUE(restarted.HasPending());
  EXPECT_EQ(restarted.Pending()->label, "eco-resize");
  EXPECT_EQ(restarted.current_revision(), 0u);

  // Decision: roll back (abort). State is exactly as before the
  // transaction was begun, and the report was never invalidated.
  ASSERT_TRUE(restarted.AbortPending().ok());
  EXPECT_EQ(restarted.current_revision(), 0u);
  EXPECT_TRUE(restarted.history().empty());
  EXPECT_TRUE(restarted.ArtifactValid(report));
  EXPECT_TRUE(restarted.ArtifactCurrent(report));
}

TEST(LedgerSnapshot, CrashMidTransactionThenCommitIsTheResumeDecision) {
  Ledger ledger;
  const ArtifactId report = ledger.RegisterArtifact(
      ArtifactKind::kReport, "timing", ProofStatus::kPass);
  TransactionSpec pending = Spec(0, "eco-resize");
  pending.invalidates.push_back(report);
  ASSERT_TRUE(ledger.BeginTransaction(pending).ok());
  const LedgerSnapshot snapshot = ledger.Snapshot();

  Ledger restarted;
  ASSERT_TRUE(restarted.Restore(snapshot).ok());
  // Decision: resume (commit). The transaction lands with the same
  // effect it would have had without the interruption.
  const Result<Revision> committed =
      restarted.CommitPending(ProofStatus::kPass);
  ASSERT_TRUE(committed.ok());
  EXPECT_EQ(committed.value(), 1u);
  ASSERT_EQ(restarted.history().size(), 1u);
  EXPECT_EQ(restarted.history()[0].label, "eco-resize");
  EXPECT_FALSE(restarted.ArtifactValid(report));
}

TEST(LedgerSnapshot, RestoreRejectsATamperedSnapshot) {
  Ledger source = Populated();
  Ledger target;
  const ArtifactId marker = target.RegisterArtifact(
      ArtifactKind::kAnalysis, "untouched", ProofStatus::kUnknown);

  LedgerSnapshot tampered = source.Snapshot();
  tampered.current_revision = 99;  // inconsistent with the history
  const Status status = target.Restore(tampered);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidArgument);
  // A rejected restore leaves the target exactly as it was.
  EXPECT_EQ(target.current_revision(), 0u);
  ASSERT_EQ(target.artifacts().size(), 1u);
  EXPECT_EQ(target.FindArtifact(marker)->name, "untouched");

  LedgerSnapshot gapped = source.Snapshot();
  gapped.history[1].output_revision = 7;  // breaks the dense chain
  EXPECT_FALSE(target.Restore(gapped).ok());

  LedgerSnapshot reused = source.Snapshot();
  reused.next_artifact = 2;  // would reuse artifact id 2
  EXPECT_FALSE(target.Restore(reused).ok());
}

TEST(LedgerSnapshot, EmptyLedgerRoundTrips) {
  Ledger source;
  Ledger restored;
  ASSERT_TRUE(restored.Restore(source.Snapshot()).ok());
  EXPECT_EQ(restored.current_revision(), 0u);
  EXPECT_TRUE(restored.history().empty());
  EXPECT_FALSE(restored.HasPending());
}

}  // namespace
}  // namespace eos::session
