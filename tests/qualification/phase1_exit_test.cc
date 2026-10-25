// Qualification tests: the Phase 1 exit conditions (Revised Plan,
// Section 17), end to end at the project level.
//
// Phase 1 promised a coherent session:
//
//   "State invalidation and constraint coverage tests pass; restart
//    and rollback tests pass; no stale result can certify an export"
//
// The unit suites prove each mechanism in isolation. This suite
// proves them composed, in one project, through the same surfaces a
// real session uses: a Project holding a design, scenarios,
// constraints and the ledger together, the report registry and its
// export gate, a project file as the restart boundary, and the
// command registry's eos::export_check as the user-facing gate.
//
// Test map, one test per exit condition in the queue for unit 5.3:
//   - state invalidation demotes reports
//   - constraint coverage accounting is exact
//   - restart reproduces state including the ledger
//   - rollback after restart restores artifact validity exactly
//   - crash mid-transaction then restart, abort variant
//   - crash mid-transaction then restart, resume variant
//   - a stale or unproven report makes export_check refuse
#include "eos/session/project.h"
#include "eos/session/reports.h"
#include "eos/ui/registry.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace eos {
namespace {

using db::BindingStatus;
using db::ConstraintKind;
using db::Direction;
using db::ParasiticsLevel;
using session::ArtifactId;
using session::ArtifactKind;
using session::BlockerKind;
using session::ExportVerdict;
using session::Project;
using session::ProofStatus;
using session::ReportRegistry;
using session::Revision;
using session::TransactionSpec;

std::string TempPath(const std::string& tag) {
  return "phase1_qualification_" + tag + ".eosproj";
}

void Remove(const std::string& path) {
  std::remove(path.c_str());
  std::remove((path + ".tmp").c_str());
}

std::string ReadAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// A populated project at revision 0: a small design, two scenarios
// (one inactive), and no constraints or transactions yet, so each
// test adds exactly the state it means to examine.
void PopulateDesignAndScenarios(Project& project) {
  project.design.emplace("top");
  db::Design& design = *project.design;
  const foundation::ObjectId cell = design.CreateCell("dff").value();
  design.CreatePort("clk", Direction::kInput);
  design.CreatePort("din", Direction::kInput);
  const foundation::ObjectId u1 = design.CreateInstance("u1", cell).value();
  const foundation::ObjectId net = design.CreateNet("clk_net").value();
  design.Connect(u1, "CK", net);
  project.scenarios.Create("func_tt", "func", "tt_025c",
                            ParasiticsLevel::kEstimate);
  const db::ScenarioId slow =
      project.scenarios
          .Create("func_ss", "func", "ss_125c", ParasiticsLevel::kExtracted)
          .value();
  project.scenarios.SetActive(slow, false);
}

// Two constraints, both bound, so coverage is complete.
void AddBoundConstraints(Project& project) {
  project.constraints.Add(ConstraintKind::kClock, "clk_100", "clk",
                          *project.design);
  project.constraints.Add(ConstraintKind::kInputDelay, "din_delay", "din",
                          *project.design);
}

// Commits one transaction at the project's current revision.
void Commit(Project& project, const std::string& label,
            std::vector<ArtifactId> invalidates = {}) {
  TransactionSpec spec;
  spec.input_revision = project.ledger.current_revision();
  spec.label = label;
  spec.tool_build = "eos-qualification";
  spec.invalidates = invalidates;
  ASSERT_TRUE(project.ledger.BeginTransaction(spec).ok());
  ASSERT_TRUE(project.ledger.CommitPending(ProofStatus::kPass).ok());
}

bool HasKind(const ExportVerdict& verdict, BlockerKind kind) {
  for (const session::ExportBlocker& blocker : verdict.blockers) {
    if (blocker.kind == kind) return true;
  }
  return false;
}

// The populated project carried to revision 2: an early report
// invalidated by the first transaction, a live report bound at
// revision 2, complete constraint coverage.
struct ExportReadyProject {
  Project project;
  ReportRegistry reports;
  ArtifactId early_report = 0;
  ArtifactId live_report = 0;

  ExportReadyProject() {
    PopulateDesignAndScenarios(project);
    AddBoundConstraints(project);
    early_report = reports
                       .RegisterReport(project, "synth-timing",
                                       ProofStatus::kPass, "NLDM")
                       .value();
    Commit(project, "synthesize", {early_report});
    Commit(project, "place");
    live_report = reports
                      .RegisterReport(project, "placed-timing",
                                      ProofStatus::kPass, "NLDM")
                      .value();
  }
};

TEST(Phase1Qualification, StateInvalidationDemotesReports) {
  ExportReadyProject f;

  // At revision 2 the live report is valid and current, the early
  // report is already invalid, and the gate passes for the live one.
  session::ReportStatus status =
      f.reports.StatusFor(f.project, "placed-timing");
  ASSERT_TRUE(status.found);
  EXPECT_TRUE(status.valid);
  EXPECT_TRUE(status.current);
  EXPECT_TRUE(f.reports.Evaluate(f.project, {"placed-timing"}).allowed());
  EXPECT_FALSE(f.project.ledger.ArtifactValid(f.early_report));

  // A state change that invalidates the live report demotes it all
  // the way: not valid, not current, and the gate names the
  // invalidation as its only blocker.
  Commit(f.project, "eco-resize", {f.live_report});
  status = f.reports.StatusFor(f.project, "placed-timing");
  EXPECT_FALSE(status.valid);
  EXPECT_FALSE(status.current);
  const ExportVerdict verdict =
      f.reports.Evaluate(f.project, {"placed-timing"});
  ASSERT_FALSE(verdict.allowed());
  ASSERT_EQ(verdict.blockers.size(), 1u);
  EXPECT_EQ(verdict.blockers[0].kind, BlockerKind::kInvalid);
  EXPECT_EQ(verdict.blockers[0].report, "placed-timing");

  // Rolling the change back restores the report exactly, because
  // validity is derived from the ledger, never stored.
  ASSERT_TRUE(f.project.ledger.RollbackTo(2).ok());
  status = f.reports.StatusFor(f.project, "placed-timing");
  EXPECT_TRUE(status.valid);
  EXPECT_TRUE(status.current);
  EXPECT_TRUE(f.reports.Evaluate(f.project, {"placed-timing"}).allowed());
}

TEST(Phase1Qualification, ConstraintCoverageAccountingIsExact) {
  Project project;
  PopulateDesignAndScenarios(project);
  project.constraints.Add(ConstraintKind::kClock, "clk_100", "clk",
                          *project.design);
  project.constraints.Add(ConstraintKind::kInputDelay, "din_delay", "din",
                          *project.design);
  project.constraints.Add(ConstraintKind::kInputDelay, "late_delay",
                          "missing_port", *project.design);
  project.constraints.Add(ConstraintKind::kFalsePath, "empty_path", "",
                          *project.design);

  // Every constraint is retained and counted in its own class:
  // nothing is dropped, merged or rounded.
  const db::CoverageReport coverage = project.constraints.Coverage();
  EXPECT_EQ(coverage.total, 4u);
  EXPECT_EQ(coverage.bound, 2u);
  EXPECT_EQ(coverage.unresolved, 1u);
  EXPECT_EQ(coverage.empty, 1u);
  EXPECT_FALSE(coverage.complete());
  const std::vector<const db::Constraint*> unresolved =
      project.constraints.Unresolved();
  ASSERT_EQ(unresolved.size(), 1u);
  EXPECT_EQ(unresolved[0]->name, "late_delay");
  EXPECT_EQ(unresolved[0]->target_spec, "missing_port");
  EXPECT_EQ(unresolved[0]->status, BindingStatus::kUnresolved);

  // The export gate reports the same accounting, digit for digit,
  // as its coverage blocker.
  ReportRegistry reports;
  ASSERT_TRUE(reports.RegisterReport(project, "timing", ProofStatus::kPass,
                                     "NLDM")
                  .ok());
  const ExportVerdict verdict = reports.Evaluate(project, {"timing"});
  ASSERT_EQ(verdict.blockers.size(), 1u);
  ASSERT_EQ(verdict.blockers[0].kind, BlockerKind::kCoverageIncomplete);
  EXPECT_NE(verdict.blockers[0].detail.find("total=4"), std::string::npos);
  EXPECT_NE(verdict.blockers[0].detail.find("bound=2"), std::string::npos);
  EXPECT_NE(verdict.blockers[0].detail.find("unresolved=1"),
            std::string::npos);
  EXPECT_NE(verdict.blockers[0].detail.find("empty=1"), std::string::npos);

  // Rebinding after the missing object appears moves the count, it
  // does not hide it: bound grows by one, unresolved falls to zero,
  // and the empty constraint still blocks completion on its own.
  project.design->CreatePort("missing_port", Direction::kInput);
  project.constraints.RebindAll(*project.design);
  const db::CoverageReport rebound = project.constraints.Coverage();
  EXPECT_EQ(rebound.total, 4u);
  EXPECT_EQ(rebound.bound, 3u);
  EXPECT_EQ(rebound.unresolved, 0u);
  EXPECT_EQ(rebound.empty, 1u);
  EXPECT_FALSE(rebound.complete());

  // A set with no empty or unresolved constraint is complete, and
  // an empty set is complete for nothing.
  Project clean;
  PopulateDesignAndScenarios(clean);
  EXPECT_FALSE(clean.constraints.Coverage().complete());
  AddBoundConstraints(clean);
  const db::CoverageReport done = clean.constraints.Coverage();
  EXPECT_EQ(done.total, 2u);
  EXPECT_EQ(done.bound, 2u);
  EXPECT_TRUE(done.complete());
}

TEST(Phase1Qualification, RestartReproducesStateIncludingTheLedger) {
  ExportReadyProject f;
  const std::string path = TempPath("restart");
  Remove(path);
  ASSERT_TRUE(f.project.SaveProject(path).ok());

  Project restored;
  const foundation::Result<session::ProjectRestoreInfo> info =
      restored.LoadProject(path);
  ASSERT_TRUE(info.ok());
  EXPECT_EQ(info.value().checkpoint.constraints_marked_unresolved, 0u);
  EXPECT_EQ(info.value().bindings_restored, 2u);
  EXPECT_EQ(info.value().bindings_dropped, 0u);

  // Design, scenarios and constraints reproduce exactly.
  ASSERT_TRUE(restored.HasDesign());
  EXPECT_EQ(restored.design->name(), "top");
  EXPECT_EQ(restored.design->Count(foundation::ObjectKind::kCell), 1u);
  EXPECT_EQ(restored.design->Count(foundation::ObjectKind::kInstance), 1u);
  EXPECT_EQ(restored.design->Count(foundation::ObjectKind::kPin), 1u);
  EXPECT_FALSE(restored.design
                   ->Find(foundation::ObjectKind::kNet, "clk_net")
                   .is_null());
  EXPECT_EQ(restored.scenarios.size(), 2u);
  ASSERT_EQ(restored.scenarios.ActiveScenarios().size(), 1u);
  EXPECT_EQ(restored.scenarios.ActiveScenarios()[0]->name, "func_tt");
  const db::CoverageReport coverage = restored.constraints.Coverage();
  EXPECT_EQ(coverage.total, 2u);
  EXPECT_EQ(coverage.bound, 2u);
  EXPECT_TRUE(coverage.complete());

  // The ledger reproduces: revision, the full history with its
  // labels, proofs and invalidation lists, and derived validity.
  EXPECT_EQ(restored.ledger.current_revision(), 2u);
  ASSERT_EQ(restored.ledger.history().size(), 2u);
  EXPECT_EQ(restored.ledger.history()[0].label, "synthesize");
  EXPECT_EQ(restored.ledger.history()[0].proof, ProofStatus::kPass);
  ASSERT_EQ(restored.ledger.history()[0].invalidated.size(), 1u);
  EXPECT_EQ(restored.ledger.history()[0].invalidated[0], f.early_report);
  EXPECT_EQ(restored.ledger.history()[1].label, "place");
  EXPECT_EQ(restored.ledger.history()[1].output_revision, 2u);
  ASSERT_EQ(restored.ledger.artifacts().size(), 2u);
  EXPECT_FALSE(restored.ledger.ArtifactValid(f.early_report));
  EXPECT_TRUE(restored.ledger.ArtifactValid(f.live_report));
  EXPECT_TRUE(restored.ledger.ArtifactCurrent(f.live_report));

  // The report bindings reproduce with the project (Phase 2 unit
  // 2.0, closing limitation Q1): both bindings are back, linked to
  // their ledger artifacts, and the gate on the restored project
  // passes the live report and refuses the invalidated one.
  ASSERT_EQ(restored.report_bindings.size(), 2u);
  ReportRegistry restored_reports;
  const session::ReportBinding* live =
      restored_reports.FindByName(restored, "placed-timing");
  ASSERT_NE(live, nullptr);
  EXPECT_EQ(live->artifact_id, f.live_report);
  EXPECT_EQ(live->bound_revision, 2u);
  const ExportVerdict live_verdict =
      restored_reports.Evaluate(restored, {"placed-timing"});
  EXPECT_TRUE(live_verdict.allowed());
  const ExportVerdict early_verdict =
      restored_reports.Evaluate(restored, {"synth-timing"});
  ASSERT_EQ(early_verdict.blockers.size(), 1u);
  EXPECT_EQ(early_verdict.blockers[0].kind, BlockerKind::kInvalid);

  // Re-saving the restored project reproduces the project file
  // byte for byte: restart is a reproduction, not an approximation.
  const std::string path2 = TempPath("restart_resaved");
  Remove(path2);
  ASSERT_TRUE(restored.SaveProject(path2).ok());
  EXPECT_EQ(ReadAll(path2), ReadAll(path));

  // Counters continue without reuse after the restart.
  const ArtifactId next = restored.ledger.RegisterArtifact(
      session::ArtifactKind::kAnalysis, "post-restore",
      ProofStatus::kUnknown);
  EXPECT_EQ(next, 3u);
  Remove(path);
  Remove(path2);
}

TEST(Phase1Qualification, RollbackAfterRestartRestoresValidityExactly) {
  ExportReadyProject f;
  const std::string path = TempPath("rollback_restart");
  Remove(path);
  ASSERT_TRUE(f.project.SaveProject(path).ok());

  Project restored;
  ASSERT_TRUE(restored.LoadProject(path).ok());
  Remove(path);

  // Validity at the restart point, revision 2.
  EXPECT_FALSE(restored.ledger.ArtifactValid(f.early_report));
  EXPECT_TRUE(restored.ledger.ArtifactValid(f.live_report));
  EXPECT_TRUE(restored.ledger.ArtifactCurrent(f.live_report));

  // A new change invalidates the live report; rolling back to the
  // restart revision restores its validity and freshness exactly,
  // and the earlier invalidation stays in force.
  Commit(restored, "eco-resize", {f.live_report});
  EXPECT_FALSE(restored.ledger.ArtifactValid(f.live_report));
  ASSERT_TRUE(restored.ledger.RollbackTo(2).ok());
  EXPECT_TRUE(restored.ledger.ArtifactValid(f.live_report));
  EXPECT_TRUE(restored.ledger.ArtifactCurrent(f.live_report));
  EXPECT_FALSE(restored.ledger.ArtifactValid(f.early_report));

  // Rolling back to revision 0 restores the state at that revision
  // exactly: the early report, invalidated by transaction 1, is
  // valid and current again, and the live report, bound at
  // revision 2, cannot certify a revision that predates it.
  ASSERT_TRUE(restored.ledger.RollbackTo(0).ok());
  EXPECT_EQ(restored.ledger.current_revision(), 0u);
  EXPECT_TRUE(restored.ledger.ArtifactValid(f.early_report));
  EXPECT_TRUE(restored.ledger.ArtifactCurrent(f.early_report));
  EXPECT_FALSE(restored.ledger.ArtifactValid(f.live_report));
  // The audit history is retained, flagged inactive, not erased.
  ASSERT_EQ(restored.ledger.history().size(), 3u);
  EXPECT_FALSE(restored.ledger.history()[0].active);
  EXPECT_FALSE(restored.ledger.history()[2].active);
}

// Builds a project at revision 1 with one live report, begins the
// "eco-resize" transaction against it, and saves the project file
// with the transaction still pending. The save is the crash: only
// the file survives into the two crash tests below.
struct CrashFixture {
  Project project;
  ArtifactId report = 0;
  foundation::ObjectId changed;

  explicit CrashFixture(const std::string& path) {
    PopulateDesignAndScenarios(project);
    AddBoundConstraints(project);
    Commit(project, "place");
    report = project.ledger.RegisterArtifact(session::ArtifactKind::kReport,
                                             "placed-timing",
                                             ProofStatus::kPass);
    changed = project.design->Find(foundation::ObjectKind::kNet, "clk_net");
    TransactionSpec pending;
    pending.input_revision = 1;
    pending.label = "eco-resize";
    pending.tool_build = "eos-qualification";
    pending.changed_objects.push_back(changed);
    pending.before_metrics["worst_slack_ns"] = {-0.25, "ns"};
    pending.after_metrics["worst_slack_ns"] = {0.10, "ns"};
    pending.invalidates.push_back(report);
    EXPECT_TRUE(project.ledger.BeginTransaction(pending).ok());
    Remove(path);
    EXPECT_TRUE(project.SaveProject(path).ok());
  }
};

TEST(Phase1Qualification, CrashMidTransactionRestartAbort) {
  const std::string path = TempPath("crash_abort");
  CrashFixture f(path);

  Project restarted;
  ASSERT_TRUE(restarted.LoadProject(path).ok());
  Remove(path);
  // The undecided transaction survived the restart, intact.
  ASSERT_TRUE(restarted.ledger.HasPending());
  EXPECT_EQ(restarted.ledger.Pending()->label, "eco-resize");
  EXPECT_EQ(restarted.ledger.current_revision(), 1u);

  // Decision: abort. The project ends in exactly the
  // pre-transaction state: same revision, same history, and the
  // report the transaction would have invalidated is untouched.
  ASSERT_TRUE(restarted.ledger.AbortPending().ok());
  EXPECT_FALSE(restarted.ledger.HasPending());
  EXPECT_EQ(restarted.ledger.current_revision(), 1u);
  ASSERT_EQ(restarted.ledger.history().size(), 1u);
  EXPECT_EQ(restarted.ledger.history()[0].label, "place");
  EXPECT_TRUE(restarted.ledger.ArtifactValid(f.report));
  EXPECT_TRUE(restarted.ledger.ArtifactCurrent(f.report));
  EXPECT_TRUE(restarted.constraints.Coverage().complete());
}

TEST(Phase1Qualification, CrashMidTransactionRestartResume) {
  const std::string path = TempPath("crash_resume");
  CrashFixture f(path);

  Project restarted;
  ASSERT_TRUE(restarted.LoadProject(path).ok());
  Remove(path);
  ASSERT_TRUE(restarted.ledger.HasPending());

  // Decision: resume. The transaction commits with the full effect
  // it would have had without the interruption: revision, record,
  // changed objects, metrics and invalidation all land.
  const foundation::Result<Revision> committed =
      restarted.ledger.CommitPending(ProofStatus::kPass);
  ASSERT_TRUE(committed.ok());
  EXPECT_EQ(committed.value(), 2u);
  EXPECT_FALSE(restarted.ledger.HasPending());
  ASSERT_EQ(restarted.ledger.history().size(), 2u);
  const session::TransactionRecord& record = restarted.ledger.history()[1];
  EXPECT_EQ(record.label, "eco-resize");
  EXPECT_EQ(record.input_revision, 1u);
  EXPECT_EQ(record.output_revision, 2u);
  EXPECT_EQ(record.rollback_checkpoint, 1u);
  EXPECT_EQ(record.proof, ProofStatus::kPass);
  ASSERT_EQ(record.changed_objects.size(), 1u);
  EXPECT_EQ(record.changed_objects[0], f.changed);
  ASSERT_EQ(record.invalidated.size(), 1u);
  EXPECT_EQ(record.invalidated[0], f.report);
  ASSERT_EQ(record.before_metrics.size(), 1u);
  EXPECT_DOUBLE_EQ(record.before_metrics.at("worst_slack_ns").value, -0.25);
  EXPECT_EQ(record.before_metrics.at("worst_slack_ns").unit, "ns");
  ASSERT_EQ(record.after_metrics.size(), 1u);
  EXPECT_DOUBLE_EQ(record.after_metrics.at("worst_slack_ns").value, 0.10);
  EXPECT_FALSE(restarted.ledger.ArtifactValid(f.report));
}

TEST(Phase1Qualification, StaleOrUnprovenReportMakesExportCheckRefuse) {
  ui::CommandRegistry registry;
  ui::RegisterCoreCommands(registry);
  ui::CommandContext ctx;
  auto run = [&registry, &ctx](const std::string& name,
                               std::vector<std::string> args = {}) {
    return registry.Dispatch(ctx, name, args);
  };

  // A coherent session through the command layer: design, a bound
  // clock constraint, a scenario, and a proven report.
  ASSERT_TRUE(run("create_design", {"top"}).ok);
  ASSERT_TRUE(run("create_port", {"clk", "in"}).ok);
  ASSERT_TRUE(run("create_clock", {"c1", "clk"}).ok);
  ASSERT_TRUE(
      run("create_scenario", {"func_tt", "func", "tt", "estimate"}).ok);
  ASSERT_TRUE(run("register_report", {"timing", "pass"}).ok);

  // Current and proven: export_check carries verified_pass.
  const ui::CommandResult pass = run("export_check", {"timing"});
  ASSERT_TRUE(pass.ok);
  EXPECT_EQ(pass.diag, ui::Diag::kVerifiedPass);

  // A later commit ages the report without invalidating it. The
  // report is still valid, but stale, and no stale result can
  // certify an export: export_check refuses with proof_unknown.
  ASSERT_TRUE(run("begin_transaction", {"place"}).ok);
  ASSERT_TRUE(run("commit_transaction", {"pass"}).ok);
  const ui::CommandResult stale = run("export_check", {"timing"});
  ASSERT_FALSE(stale.ok);
  EXPECT_EQ(stale.diag, ui::Diag::kProofUnknown);
  EXPECT_NE(stale.payload.find("blocker=stale"), std::string::npos);
  EXPECT_NE(stale.payload.find("report=timing"), std::string::npos);

  // An unproven report at the current revision refuses on its
  // proof, even though it is fresh.
  ASSERT_TRUE(run("register_report", {"power"}).ok);
  const ui::CommandResult unproven = run("export_check", {"power"});
  ASSERT_FALSE(unproven.ok);
  EXPECT_EQ(unproven.diag, ui::Diag::kProofUnknown);
  EXPECT_NE(unproven.payload.find("blocker=proof_not_pass"),
            std::string::npos);
  EXPECT_NE(unproven.payload.find("report=power"), std::string::npos);
}

}  // namespace
}  // namespace eos
