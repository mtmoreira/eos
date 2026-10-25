// Unit tests: session/ledger snapshot text serialization.
#include "eos/session/ledger.h"

#include <string>

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

// A ledger with metrics, changed objects, an invalidated report and
// a live report, exercising every record shape the text form holds.
Ledger Populated() {
  Ledger ledger;
  const ArtifactId early = ledger.RegisterArtifact(
      ArtifactKind::kReport, "early timing", ProofStatus::kPass);
  TransactionSpec first = Spec(0, "synthesize");
  first.changed_objects.push_back(
      foundation::ObjectId::Make(foundation::ObjectKind::kCell, 3));
  first.before_metrics["cell_area_um2"] = MetricValue{10.5, "um2"};
  first.after_metrics["cell_area_um2"] = MetricValue{9.25, "um2"};
  first.after_metrics["worst_slack_ns"] = MetricValue{-0.125, "ns"};
  first.invalidates.push_back(early);
  EXPECT_TRUE(ledger.BeginTransaction(first).ok());
  EXPECT_TRUE(ledger.CommitPending(ProofStatus::kPass).ok());
  EXPECT_TRUE(ledger.BeginTransaction(Spec(1, "place")).ok());
  EXPECT_TRUE(ledger.CommitPending(ProofStatus::kFail).ok());
  ledger.RegisterArtifact(ArtifactKind::kReport, "placed-timing",
                          ProofStatus::kPass);
  return ledger;
}

TEST(LedgerText, RoundTripsThroughRestoreValidation) {
  Ledger source = Populated();
  const Result<std::string> text = SnapshotToText(source.Snapshot());
  ASSERT_TRUE(text.ok());
  const Result<LedgerSnapshot> parsed = SnapshotFromText(text.value());
  ASSERT_TRUE(parsed.ok());

  Ledger restored;
  ASSERT_TRUE(restored.Restore(parsed.value()).ok());
  EXPECT_EQ(restored.current_revision(), 2u);
  ASSERT_EQ(restored.history().size(), 2u);
  EXPECT_EQ(restored.history()[0].label, "synthesize");
  EXPECT_EQ(restored.history()[0].changed_objects.size(), 1u);
  EXPECT_EQ(restored.history()[0].after_metrics.at("worst_slack_ns").value,
            -0.125);
  EXPECT_EQ(restored.history()[0].after_metrics.at("worst_slack_ns").unit,
            "ns");
  EXPECT_EQ(restored.history()[1].proof, ProofStatus::kFail);
  ASSERT_EQ(restored.artifacts().size(), 2u);
  EXPECT_EQ(restored.artifacts()[0].name, "early timing");
  EXPECT_FALSE(restored.ArtifactValid(1));
  EXPECT_TRUE(restored.ArtifactCurrent(2));
}

TEST(LedgerText, PendingTransactionSurvivesTheTextRoundTrip) {
  Ledger ledger;
  const ArtifactId report = ledger.RegisterArtifact(
      ArtifactKind::kReport, "timing", ProofStatus::kPass);
  TransactionSpec pending = Spec(0, "eco resize");
  pending.invalidates.push_back(report);
  pending.after_metrics["total_power_mw"] = MetricValue{42.5, "mw"};
  ASSERT_TRUE(ledger.BeginTransaction(pending).ok());

  const Result<std::string> text = SnapshotToText(ledger.Snapshot());
  ASSERT_TRUE(text.ok());
  const Result<LedgerSnapshot> parsed = SnapshotFromText(text.value());
  ASSERT_TRUE(parsed.ok());
  Ledger restarted;
  ASSERT_TRUE(restarted.Restore(parsed.value()).ok());
  ASSERT_TRUE(restarted.HasPending());
  EXPECT_EQ(restarted.Pending()->label, "eco resize");
  EXPECT_EQ(restarted.Pending()->after_metrics.at("total_power_mw").value,
            42.5);
  const Result<Revision> committed =
      restarted.CommitPending(ProofStatus::kPass);
  ASSERT_TRUE(committed.ok());
  EXPECT_EQ(committed.value(), 1u);
  EXPECT_FALSE(restarted.ArtifactValid(report));
}

TEST(LedgerText, CorruptTextIsRejected) {
  Ledger source = Populated();
  const Result<std::string> text = SnapshotToText(source.Snapshot());
  ASSERT_TRUE(text.ok());

  std::string flipped = text.value();
  flipped[24] = flipped[24] == 'a' ? 'b' : 'a';
  const Result<LedgerSnapshot> bad = SnapshotFromText(flipped);
  ASSERT_FALSE(bad.ok());
  EXPECT_EQ(bad.status().code(), ErrorCode::kInvalidArgument);

  EXPECT_FALSE(SnapshotFromText("hello world\n").ok());
  EXPECT_FALSE(SnapshotFromText(text.value().substr(0, 30)).ok());
  EXPECT_FALSE(SnapshotFromText("").ok());
}

TEST(LedgerText, TextThatParsesCanStillFailRestoreValidation) {
  Ledger source = Populated();
  LedgerSnapshot tampered = source.Snapshot();
  tampered.current_revision = 99;  // inconsistent with the history
  const Result<std::string> text = SnapshotToText(tampered);
  ASSERT_TRUE(text.ok());
  const Result<LedgerSnapshot> parsed = SnapshotFromText(text.value());
  ASSERT_TRUE(parsed.ok());  // syntax is fine...

  Ledger target;
  const ArtifactId marker = target.RegisterArtifact(
      ArtifactKind::kAnalysis, "untouched", ProofStatus::kUnknown);
  const Status status = target.Restore(parsed.value());
  EXPECT_FALSE(status.ok());  // ...semantics are not
  EXPECT_EQ(target.current_revision(), 0u);
  ASSERT_EQ(target.artifacts().size(), 1u);
  EXPECT_EQ(target.FindArtifact(marker)->name, "untouched");
}

TEST(LedgerText, EmptyLedgerRoundTrips) {
  Ledger source;
  const Result<std::string> text = SnapshotToText(source.Snapshot());
  ASSERT_TRUE(text.ok());
  const Result<LedgerSnapshot> parsed = SnapshotFromText(text.value());
  ASSERT_TRUE(parsed.ok());
  Ledger restored;
  ASSERT_TRUE(restored.Restore(parsed.value()).ok());
  EXPECT_EQ(restored.current_revision(), 0u);
  EXPECT_TRUE(restored.history().empty());
  EXPECT_FALSE(restored.HasPending());
}

}  // namespace
}  // namespace eos::session
