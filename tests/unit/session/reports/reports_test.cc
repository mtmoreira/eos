// Unit tests: session/reports, the report registry and the export
// freshness gate.
#include "eos/session/reports.h"

#include <cstdio>
#include <string>

#include <gtest/gtest.h>

namespace eos::session {
namespace {

using db::ConstraintKind;
using db::Direction;
using db::ParasiticsLevel;

// A project one step from export-ready: a design with one bound
// clock constraint and one active scenario.
struct Fixture {
  Project project;
  ReportRegistry reports;

  Fixture() {
    project.design.emplace("top");
    project.design->CreatePort("clk", Direction::kInput);
    project.constraints.Add(ConstraintKind::kClock, "clk_100", "clk",
                            *project.design);
    project.scenarios.Create("func_tt", "func", "tt_025c",
                             ParasiticsLevel::kEstimate);
  }

  // Commits one transaction at the current revision, invalidating
  // the given artifacts (none by default).
  void Commit(std::vector<ArtifactId> invalidates = {}) {
    TransactionSpec spec;
    spec.input_revision = project.ledger.current_revision();
    spec.label = "step";
    spec.tool_build = "eos-test";
    spec.invalidates = invalidates;
    project.ledger.BeginTransaction(spec);
    project.ledger.CommitPending(ProofStatus::kPass);
  }
};

bool HasKind(const ExportVerdict& verdict, BlockerKind kind) {
  for (const ExportBlocker& blocker : verdict.blockers) {
    if (blocker.kind == kind) return true;
  }
  return false;
}

TEST(Reports, RegisterCreatesArtifactAndBindingTogether) {
  Fixture f;
  const Result<ArtifactId> id = f.reports.RegisterReport(
      f.project, "timing", ProofStatus::kPass, "NLDM", "build-42");
  ASSERT_TRUE(id.ok());
  EXPECT_EQ(id.value(), 1u);

  const ArtifactRecord* record = f.project.ledger.FindArtifact(id.value());
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->kind, ArtifactKind::kReport);
  EXPECT_EQ(record->name, "timing");
  EXPECT_EQ(record->proof, ProofStatus::kPass);
  EXPECT_EQ(record->bound_revision, 0u);

  const ReportBinding* binding = f.reports.FindByName(f.project, "timing");
  ASSERT_NE(binding, nullptr);
  EXPECT_EQ(binding, f.reports.FindByArtifact(f.project, id.value()));
  EXPECT_EQ(binding->artifact_id, id.value());
  EXPECT_EQ(binding->bound_revision, 0u);
  EXPECT_EQ(binding->timing_model, "NLDM");
  EXPECT_EQ(binding->tool_build, "build-42");
  ASSERT_EQ(binding->scenarios.size(), 1u);
  EXPECT_EQ(binding->scenarios[0].name, "func_tt");
  EXPECT_EQ(binding->scenarios[0].mode, "func");
  EXPECT_EQ(binding->scenarios[0].corner, "tt_025c");
  EXPECT_EQ(binding->scenarios[0].parasitics,
            ParasiticsLevel::kEstimate);
  ASSERT_TRUE(binding->parasitics.has_value());
  EXPECT_EQ(*binding->parasitics, ParasiticsLevel::kEstimate);
}

TEST(Reports, DuplicateOrEmptyRegistrationIsRejectedCleanly) {
  Fixture f;
  ASSERT_TRUE(f.reports.RegisterReport(f.project, "timing",
                                       ProofStatus::kPass, "NLDM")
                  .ok());
  EXPECT_FALSE(f.reports.RegisterReport(f.project, "timing",
                                        ProofStatus::kPass, "NLDM")
                   .ok());
  EXPECT_FALSE(
      f.reports.RegisterReport(f.project, "", ProofStatus::kPass, "NLDM")
          .ok());
  EXPECT_FALSE(f.reports.RegisterReport(f.project, "other",
                                        ProofStatus::kPass, "")
                   .ok());
  // The rejected registrations created no artifact and no binding.
  EXPECT_EQ(f.project.ledger.artifacts().size(), 1u);
  EXPECT_EQ(f.reports.bindings(f.project).size(), 1u);
}

TEST(Reports, BindingIsAnImmutableSnapshot) {
  Fixture f;
  ASSERT_TRUE(f.reports.RegisterReport(f.project, "timing",
                                       ProofStatus::kPass, "NLDM")
                  .ok());
  // Change everything the binding snapshot: deactivate the bound
  // scenario, add another, advance the revision.
  const db::Scenario* bound = f.project.scenarios.FindByName("func_tt");
  ASSERT_NE(bound, nullptr);
  ASSERT_TRUE(f.project.scenarios.SetActive(bound->id, false).ok());
  ASSERT_TRUE(f.project.scenarios
                  .Create("func_ss", "func", "ss_125c",
                          ParasiticsLevel::kExtracted)
                  .ok());
  f.Commit();

  const ReportBinding* binding = f.reports.FindByName(f.project, "timing");
  ASSERT_NE(binding, nullptr);
  EXPECT_EQ(binding->bound_revision, 0u);
  ASSERT_EQ(binding->scenarios.size(), 1u);
  EXPECT_EQ(binding->scenarios[0].name, "func_tt");
  EXPECT_EQ(binding->scenarios[0].corner, "tt_025c");
  EXPECT_EQ(binding->scenarios[0].parasitics,
            ParasiticsLevel::kEstimate);
  ASSERT_TRUE(binding->parasitics.has_value());
  EXPECT_EQ(*binding->parasitics, ParasiticsLevel::kEstimate);
}

TEST(Reports, MixedParasiticsLevelsAreRecordedNeverMerged) {
  Fixture f;
  ASSERT_TRUE(f.project.scenarios
                  .Create("func_ss", "func", "ss_125c",
                          ParasiticsLevel::kExtracted)
                  .ok());
  ASSERT_TRUE(f.reports.RegisterReport(f.project, "timing",
                                       ProofStatus::kPass, "CCS")
                  .ok());
  const ReportBinding* binding = f.reports.FindByName(f.project, "timing");
  ASSERT_NE(binding, nullptr);
  ASSERT_EQ(binding->scenarios.size(), 2u);
  EXPECT_EQ(binding->scenarios[0].parasitics,
            ParasiticsLevel::kEstimate);
  EXPECT_EQ(binding->scenarios[1].parasitics,
            ParasiticsLevel::kExtracted);
  // No single level is claimed for a mixed binding.
  EXPECT_FALSE(binding->parasitics.has_value());
}

TEST(Reports, StatusReadsValidityAndFreshnessFromTheLedger) {
  Fixture f;
  const ArtifactId id = f.reports
                            .RegisterReport(f.project, "timing",
                                            ProofStatus::kPass, "NLDM")
                            .value();
  ReportStatus status = f.reports.StatusFor(f.project, "timing");
  ASSERT_TRUE(status.found);
  EXPECT_TRUE(status.valid);
  EXPECT_TRUE(status.current);
  EXPECT_EQ(status.proof, ProofStatus::kPass);

  // A transaction that does not invalidate: valid, no longer current.
  f.Commit();
  status = f.reports.StatusFor(f.project, "timing");
  EXPECT_TRUE(status.valid);
  EXPECT_FALSE(status.current);
  EXPECT_EQ(status.bound_revision, 0u);
  EXPECT_EQ(status.current_revision, 1u);

  // Rollback restores freshness exactly, because both are derived.
  ASSERT_TRUE(f.project.ledger.RollbackTo(0).ok());
  status = f.reports.StatusFor(f.project, "timing");
  EXPECT_TRUE(status.valid);
  EXPECT_TRUE(status.current);

  EXPECT_FALSE(f.reports.StatusFor(f.project, "nope").found);
  (void)id;
}

TEST(ExportGate, PassesWhenEveryReportIsCurrentProvenAndCovered) {
  Fixture f;
  ASSERT_TRUE(f.reports.RegisterReport(f.project, "timing",
                                       ProofStatus::kPass, "NLDM")
                  .ok());
  const ExportVerdict verdict = f.reports.Evaluate(f.project, {"timing"});
  EXPECT_TRUE(verdict.allowed());
  EXPECT_TRUE(verdict.blockers.empty());
}

TEST(ExportGate, MissingReportBlocks) {
  Fixture f;
  const ExportVerdict verdict = f.reports.Evaluate(f.project, {"nope"});
  ASSERT_FALSE(verdict.allowed());
  ASSERT_EQ(verdict.blockers.size(), 1u);
  EXPECT_EQ(verdict.blockers[0].kind, BlockerKind::kMissing);
  EXPECT_EQ(verdict.blockers[0].report, "nope");
  EXPECT_EQ(std::string(BlockerKindName(verdict.blockers[0].kind)),
            "missing");
}

TEST(ExportGate, InvalidReportBlocks) {
  Fixture f;
  const ArtifactId id = f.reports
                            .RegisterReport(f.project, "timing",
                                            ProofStatus::kPass, "NLDM")
                            .value();
  f.Commit({id});
  const ExportVerdict verdict = f.reports.Evaluate(f.project, {"timing"});
  ASSERT_FALSE(verdict.allowed());
  ASSERT_EQ(verdict.blockers.size(), 1u);
  EXPECT_EQ(verdict.blockers[0].kind, BlockerKind::kInvalid);
}

TEST(ExportGate, StaleReportBlocks) {
  Fixture f;
  ASSERT_TRUE(f.reports.RegisterReport(f.project, "timing",
                                       ProofStatus::kPass, "NLDM")
                  .ok());
  f.Commit();
  const ExportVerdict verdict = f.reports.Evaluate(f.project, {"timing"});
  ASSERT_FALSE(verdict.allowed());
  ASSERT_EQ(verdict.blockers.size(), 1u);
  EXPECT_EQ(verdict.blockers[0].kind, BlockerKind::kStale);
  EXPECT_EQ(verdict.blockers[0].report, "timing");
}

TEST(ExportGate, UnprovenReportBlocks) {
  Fixture f;
  ASSERT_TRUE(f.reports.RegisterReport(f.project, "timing",
                                       ProofStatus::kUnknown, "NLDM")
                  .ok());
  const ExportVerdict verdict = f.reports.Evaluate(f.project, {"timing"});
  ASSERT_FALSE(verdict.allowed());
  ASSERT_EQ(verdict.blockers.size(), 1u);
  EXPECT_EQ(verdict.blockers[0].kind, BlockerKind::kProofNotPass);

  Fixture failed;
  ASSERT_TRUE(failed.reports.RegisterReport(failed.project, "drc",
                                            ProofStatus::kFail, "NLDM")
                  .ok());
  const ExportVerdict fail_verdict =
      failed.reports.Evaluate(failed.project, {"drc"});
  ASSERT_EQ(fail_verdict.blockers.size(), 1u);
  EXPECT_EQ(fail_verdict.blockers[0].kind, BlockerKind::kProofNotPass);
}

TEST(ExportGate, IncompleteCoverageBlocks) {
  Fixture f;
  ASSERT_TRUE(f.reports.RegisterReport(f.project, "timing",
                                       ProofStatus::kPass, "NLDM")
                  .ok());
  f.project.constraints.Add(ConstraintKind::kInputDelay, "din",
                            "missing_port", *f.project.design);
  const ExportVerdict verdict = f.reports.Evaluate(f.project, {"timing"});
  ASSERT_FALSE(verdict.allowed());
  ASSERT_EQ(verdict.blockers.size(), 1u);
  EXPECT_EQ(verdict.blockers[0].kind, BlockerKind::kCoverageIncomplete);
  EXPECT_TRUE(verdict.blockers[0].report.empty());

  // No constraints at all is also incomplete coverage: an empty set
  // covers nothing (the L2 accounting rule).
  Fixture bare;
  bare.project.constraints = db::ConstraintSet();
  ASSERT_TRUE(bare.reports.RegisterReport(bare.project, "timing",
                                          ProofStatus::kPass, "NLDM")
                  .ok());
  const ExportVerdict bare_verdict =
      bare.reports.Evaluate(bare.project, {"timing"});
  ASSERT_EQ(bare_verdict.blockers.size(), 1u);
  EXPECT_EQ(bare_verdict.blockers[0].kind,
            BlockerKind::kCoverageIncomplete);
}

// Phase 2 unit 2.0: bindings are project state. A project saved
// with a passing, current report restores into a fresh project with
// the binding intact, and the export gate gives the identical
// verdict before and after the restore.
TEST(Reports, BindingsSurviveRestoreAndTheGateVerdictIsIdentical) {
  Fixture f;
  const ArtifactId id = f.reports
                            .RegisterReport(f.project, "timing",
                                            ProofStatus::kPass, "NLDM",
                                            "build-42")
                            .value();
  const ExportVerdict before = f.reports.Evaluate(f.project, {"timing"});
  ASSERT_TRUE(before.allowed());

  const std::string path = "reports_test_durable.eosproj";
  std::remove(path.c_str());
  ASSERT_TRUE(f.project.SaveProject(path).ok());

  Project restored;
  ReportRegistry fresh_registry;
  const Result<ProjectRestoreInfo> info = restored.LoadProject(path);
  ASSERT_TRUE(info.ok());
  EXPECT_EQ(info.value().bindings_restored, 1u);
  EXPECT_EQ(info.value().bindings_dropped, 0u);

  const ReportBinding* binding =
      fresh_registry.FindByName(restored, "timing");
  ASSERT_NE(binding, nullptr);
  EXPECT_EQ(binding, fresh_registry.FindByArtifact(restored, id));
  EXPECT_EQ(binding->artifact_id, id);
  EXPECT_EQ(binding->bound_revision, 0u);
  EXPECT_EQ(binding->timing_model, "NLDM");
  EXPECT_EQ(binding->tool_build, "build-42");
  ASSERT_EQ(binding->scenarios.size(), 1u);
  EXPECT_EQ(binding->scenarios[0].name, "func_tt");

  const ReportStatus status = fresh_registry.StatusFor(restored, "timing");
  EXPECT_TRUE(status.found);
  EXPECT_TRUE(status.valid);
  EXPECT_TRUE(status.current);
  const ExportVerdict after = fresh_registry.Evaluate(restored, {"timing"});
  EXPECT_EQ(after.allowed(), before.allowed());
  EXPECT_EQ(after.blockers.size(), before.blockers.size());
  std::remove(path.c_str());
}

// The binding restored from the file is the snapshot taken at
// registration: changes made to the live project after registration
// (a new scenario, an advanced revision) do not rewrite it, before
// or after the restore, and a stale verdict stays stale identically.
TEST(Reports, BindingImmutabilityAcrossRestore) {
  Fixture f;
  ASSERT_TRUE(f.reports.RegisterReport(f.project, "timing",
                                       ProofStatus::kPass, "NLDM")
                  .ok());
  ASSERT_TRUE(f.project.scenarios
                  .Create("func_ss", "func", "ss_125c",
                          ParasiticsLevel::kExtracted)
                  .ok());
  f.Commit();
  const ExportVerdict before = f.reports.Evaluate(f.project, {"timing"});
  ASSERT_EQ(before.blockers.size(), 1u);
  ASSERT_EQ(before.blockers[0].kind, BlockerKind::kStale);

  const std::string path = "reports_test_snapshot.eosproj";
  std::remove(path.c_str());
  ASSERT_TRUE(f.project.SaveProject(path).ok());

  Project restored;
  ReportRegistry fresh_registry;
  ASSERT_TRUE(restored.LoadProject(path).ok());
  const ReportBinding* binding =
      fresh_registry.FindByName(restored, "timing");
  ASSERT_NE(binding, nullptr);
  EXPECT_EQ(binding->bound_revision, 0u);
  ASSERT_EQ(binding->scenarios.size(), 1u);
  EXPECT_EQ(binding->scenarios[0].name, "func_tt");
  EXPECT_EQ(binding->scenarios[0].corner, "tt_025c");

  const ExportVerdict after = fresh_registry.Evaluate(restored, {"timing"});
  ASSERT_EQ(after.blockers.size(), 1u);
  EXPECT_EQ(after.blockers[0].kind, BlockerKind::kStale);
  EXPECT_EQ(after.blockers[0].detail, before.blockers[0].detail);
  std::remove(path.c_str());
}

TEST(ExportGate, EveryBlockerIsReportedNotJustTheFirst) {
  Fixture f;
  ASSERT_TRUE(f.reports.RegisterReport(f.project, "timing",
                                       ProofStatus::kUnknown, "NLDM")
                  .ok());
  f.project.constraints.Add(ConstraintKind::kInputDelay, "din",
                            "missing_port", *f.project.design);
  const ExportVerdict verdict =
      f.reports.Evaluate(f.project, {"timing", "nope"});
  EXPECT_TRUE(HasKind(verdict, BlockerKind::kProofNotPass));
  EXPECT_TRUE(HasKind(verdict, BlockerKind::kMissing));
  EXPECT_TRUE(HasKind(verdict, BlockerKind::kCoverageIncomplete));
  EXPECT_EQ(verdict.blockers.size(), 3u);
}

}  // namespace
}  // namespace eos::session
