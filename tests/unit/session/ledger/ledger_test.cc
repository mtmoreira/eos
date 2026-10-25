// Unit tests: session/ledger.
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

TEST(Ledger, StartsAtRevisionZeroWithNoHistory) {
  Ledger ledger;
  EXPECT_EQ(ledger.current_revision(), 0u);
  EXPECT_TRUE(ledger.history().empty());
  EXPECT_TRUE(ledger.artifacts().empty());
  EXPECT_FALSE(ledger.HasPending());
}

TEST(Ledger, CommitRecordsTheFullTransaction) {
  Ledger ledger;
  TransactionSpec spec = Spec(0, "place");
  spec.changed_objects.push_back(
      foundation::ObjectId::Make(foundation::ObjectKind::kInstance, 7));
  spec.before_metrics["wns"] = {-0.25, "ns"};
  spec.after_metrics["wns"] = {0.10, "ns"};

  ASSERT_TRUE(ledger.BeginTransaction(spec).ok());
  const Result<Revision> committed = ledger.CommitPending(ProofStatus::kPass);
  ASSERT_TRUE(committed.ok());
  EXPECT_EQ(committed.value(), 1u);
  EXPECT_EQ(ledger.current_revision(), 1u);
  EXPECT_FALSE(ledger.HasPending());

  ASSERT_EQ(ledger.history().size(), 1u);
  const TransactionRecord& rec = ledger.history()[0];
  EXPECT_EQ(rec.id, 1u);
  EXPECT_EQ(rec.input_revision, 0u);
  EXPECT_EQ(rec.output_revision, 1u);
  EXPECT_EQ(rec.rollback_checkpoint, 0u);
  EXPECT_EQ(rec.label, "place");
  EXPECT_EQ(rec.tool_build, "eos-test-build");
  ASSERT_EQ(rec.changed_objects.size(), 1u);
  EXPECT_EQ(rec.changed_objects[0].index(), 7u);
  EXPECT_EQ(rec.before_metrics.at("wns").value, -0.25);
  EXPECT_EQ(rec.after_metrics.at("wns").value, 0.10);
  EXPECT_EQ(rec.after_metrics.at("wns").unit, "ns");
  EXPECT_EQ(rec.proof, ProofStatus::kPass);
  EXPECT_TRUE(rec.active);
}

TEST(Ledger, BeginRequiresTheCurrentRevision) {
  Ledger ledger;
  const Status s = ledger.BeginTransaction(Spec(3, "future"));
  ASSERT_FALSE(s.ok());
  EXPECT_EQ(s.code(), ErrorCode::kStaleRevision);
  EXPECT_FALSE(ledger.HasPending());
}

TEST(Ledger, OnlyOneTransactionMayBePending) {
  Ledger ledger;
  ASSERT_TRUE(ledger.BeginTransaction(Spec(0, "one")).ok());
  const Status s = ledger.BeginTransaction(Spec(0, "two"));
  ASSERT_FALSE(s.ok());
  EXPECT_EQ(s.code(), ErrorCode::kInvalidState);
}

TEST(Ledger, CommitAndAbortRequireAPendingTransaction) {
  Ledger ledger;
  const Result<Revision> committed = ledger.CommitPending(ProofStatus::kPass);
  ASSERT_FALSE(committed.ok());
  EXPECT_EQ(committed.status().code(), ErrorCode::kInvalidState);
  const Status aborted = ledger.AbortPending();
  ASSERT_FALSE(aborted.ok());
  EXPECT_EQ(aborted.code(), ErrorCode::kInvalidState);
}

TEST(Ledger, AbortDiscardsThePendingTransaction) {
  Ledger ledger;
  ASSERT_TRUE(ledger.BeginTransaction(Spec(0, "doomed")).ok());
  ASSERT_TRUE(ledger.AbortPending().ok());
  EXPECT_EQ(ledger.current_revision(), 0u);
  EXPECT_TRUE(ledger.history().empty());
  // A fresh transaction can begin immediately after an abort: this is
  // the rollback-or-resume decision a restart makes.
  ASSERT_TRUE(ledger.BeginTransaction(Spec(0, "retry")).ok());
  ASSERT_TRUE(ledger.CommitPending(ProofStatus::kPass).ok());
  EXPECT_EQ(ledger.current_revision(), 1u);
}

TEST(Ledger, BeginRejectsInvalidationOfAnUnknownArtifact) {
  Ledger ledger;
  TransactionSpec spec = Spec(0, "bad");
  spec.invalidates.push_back(99);
  const Status s = ledger.BeginTransaction(spec);
  ASSERT_FALSE(s.ok());
  EXPECT_EQ(s.code(), ErrorCode::kNotFound);
}

TEST(Ledger, ArtifactValidityAndCurrencyFollowRevisions) {
  Ledger ledger;
  const ArtifactId report =
      ledger.RegisterArtifact(ArtifactKind::kReport, "timing", ProofStatus::kPass);
  EXPECT_TRUE(ledger.ArtifactValid(report));
  EXPECT_TRUE(ledger.ArtifactCurrent(report));

  // An unrelated commit: the report still describes real state (it was
  // not invalidated) but it no longer certifies the current revision.
  ASSERT_TRUE(ledger.BeginTransaction(Spec(0, "unrelated")).ok());
  ASSERT_TRUE(ledger.CommitPending(ProofStatus::kPass).ok());
  EXPECT_TRUE(ledger.ArtifactValid(report));
  EXPECT_FALSE(ledger.ArtifactCurrent(report));

  // A commit that declares the invalidation: validity ends.
  TransactionSpec spec = Spec(1, "resize");
  spec.invalidates.push_back(report);
  ASSERT_TRUE(ledger.BeginTransaction(spec).ok());
  ASSERT_TRUE(ledger.CommitPending(ProofStatus::kPass).ok());
  EXPECT_FALSE(ledger.ArtifactValid(report));
  EXPECT_FALSE(ledger.ArtifactCurrent(report));
}

TEST(Ledger, ArtifactProofIsRecordedAndSettable) {
  Ledger ledger;
  const ArtifactId analysis = ledger.RegisterArtifact(
      ArtifactKind::kAnalysis, "congestion", ProofStatus::kUnknown);
  const ArtifactRecord* rec = ledger.FindArtifact(analysis);
  ASSERT_NE(rec, nullptr);
  EXPECT_EQ(rec->bound_revision, 0u);
  EXPECT_EQ(rec->proof, ProofStatus::kUnknown);
  ASSERT_TRUE(ledger.SetArtifactProof(analysis, ProofStatus::kPass).ok());
  EXPECT_EQ(ledger.FindArtifact(analysis)->proof, ProofStatus::kPass);
  const Status s = ledger.SetArtifactProof(12345, ProofStatus::kPass);
  ASSERT_FALSE(s.ok());
  EXPECT_EQ(s.code(), ErrorCode::kNotFound);
  EXPECT_EQ(ledger.FindArtifact(12345), nullptr);
  EXPECT_FALSE(ledger.ArtifactValid(12345));
}

TEST(Ledger, RollbackRestoresEarlierValidityExactly) {
  Ledger ledger;
  const ArtifactId report =
      ledger.RegisterArtifact(ArtifactKind::kReport, "timing", ProofStatus::kPass);

  TransactionSpec kill = Spec(0, "kill-report");
  kill.invalidates.push_back(report);
  ASSERT_TRUE(ledger.BeginTransaction(kill).ok());
  ASSERT_TRUE(ledger.CommitPending(ProofStatus::kPass).ok());
  ASSERT_TRUE(ledger.BeginTransaction(Spec(1, "more")).ok());
  ASSERT_TRUE(ledger.CommitPending(ProofStatus::kPass).ok());
  const ArtifactId late = ledger.RegisterArtifact(
      ArtifactKind::kAnalysis, "late", ProofStatus::kPass);
  ASSERT_EQ(ledger.current_revision(), 2u);
  EXPECT_FALSE(ledger.ArtifactValid(report));
  EXPECT_TRUE(ledger.ArtifactValid(late));

  // Back to revision 1: the second transaction is undone, so the
  // rev-2 artifact's subject state no longer exists; the first
  // transaction still stands, so the report stays invalidated.
  ASSERT_TRUE(ledger.RollbackTo(1).ok());
  EXPECT_EQ(ledger.current_revision(), 1u);
  EXPECT_FALSE(ledger.ArtifactValid(report));
  EXPECT_FALSE(ledger.ArtifactValid(late));

  // Back to revision 0: the invalidating transaction is undone, so
  // the report is exactly as valid as the day it was registered.
  ASSERT_TRUE(ledger.RollbackTo(0).ok());
  EXPECT_TRUE(ledger.ArtifactValid(report));
  EXPECT_TRUE(ledger.ArtifactCurrent(report));
  EXPECT_FALSE(ledger.history()[0].active);
  EXPECT_FALSE(ledger.history()[1].active);
}

TEST(Ledger, RollbackValidatesItsInputs) {
  Ledger ledger;
  const Status beyond = ledger.RollbackTo(1);
  ASSERT_FALSE(beyond.ok());
  EXPECT_EQ(beyond.code(), ErrorCode::kInvalidArgument);

  ASSERT_TRUE(ledger.BeginTransaction(Spec(0, "pending")).ok());
  const Status during = ledger.RollbackTo(0);
  ASSERT_FALSE(during.ok());
  EXPECT_EQ(during.code(), ErrorCode::kInvalidState);
}

TEST(Ledger, RecommitAfterRollbackReusesTheRevisionCleanly) {
  Ledger ledger;
  const ArtifactId report =
      ledger.RegisterArtifact(ArtifactKind::kReport, "timing", ProofStatus::kPass);
  TransactionSpec kill = Spec(0, "kill-report");
  kill.invalidates.push_back(report);
  ASSERT_TRUE(ledger.BeginTransaction(kill).ok());
  ASSERT_TRUE(ledger.CommitPending(ProofStatus::kPass).ok());
  ASSERT_TRUE(ledger.RollbackTo(0).ok());

  // A different transaction now takes revision 1. It does not
  // invalidate the report, and the rolled-back record must not either:
  // it is audit history, not live state.
  ASSERT_TRUE(ledger.BeginTransaction(Spec(0, "different")).ok());
  ASSERT_TRUE(ledger.CommitPending(ProofStatus::kPass).ok());
  EXPECT_EQ(ledger.current_revision(), 1u);
  ASSERT_EQ(ledger.history().size(), 2u);
  EXPECT_FALSE(ledger.history()[0].active);
  EXPECT_TRUE(ledger.history()[1].active);
  EXPECT_TRUE(ledger.ArtifactValid(report));
  EXPECT_FALSE(ledger.ArtifactCurrent(report));
}

}  // namespace
}  // namespace eos::session
