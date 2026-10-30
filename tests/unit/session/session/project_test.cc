// Unit tests: session/session Project and the project file.
#include "eos/session/project.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace eos::session {
namespace {

using db::BindingStatus;
using db::ConstraintKind;
using db::Direction;
using db::ParasiticsLevel;

std::string TempPath(const std::string& tag) {
  return "project_test_" + tag + ".eosproj";
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

void WriteAll(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

// A populated project: a small design, two scenarios (one inactive),
// two constraints (one bound, one unresolved), two committed
// transactions, one invalidated report and one live report.
struct Fixture {
  Project project;
  ArtifactId early_report = 0;
  ArtifactId live_report = 0;

  Fixture() {
    project.design.emplace("top");
    db::Design& design = *project.design;
    const foundation::ObjectId cell = design.CreateCell("dff").value();
    design.CreatePort("clk", Direction::kInput);
    const foundation::ObjectId u1 = design.CreateInstance("u1", cell).value();
    const foundation::ObjectId net = design.CreateNet("clk net").value();
    design.Connect(u1, "CK", net);
    project.scenarios.Create("func_tt", "func", "tt_025c",
                             ParasiticsLevel::kEstimate);
    const db::ScenarioId slow =
        project.scenarios
            .Create("func_ss", "func", "ss_125c",
                    ParasiticsLevel::kExtracted)
            .value();
    project.scenarios.SetActive(slow, false);
    project.constraints.Add(ConstraintKind::kClock, "clk_100", "clk", design);
    project.constraints.Add(ConstraintKind::kInputDelay, "din", "missing",
                            design);

    early_report = project.ledger.RegisterArtifact(
        ArtifactKind::kReport, "early-timing", ProofStatus::kPass);
    TransactionSpec first;
    first.input_revision = 0;
    first.label = "synthesize";
    first.tool_build = "eos-test";
    first.invalidates.push_back(early_report);
    project.ledger.BeginTransaction(first);
    project.ledger.CommitPending(ProofStatus::kPass);
    TransactionSpec second;
    second.input_revision = 1;
    second.label = "place";
    second.tool_build = "eos-test";
    project.ledger.BeginTransaction(second);
    project.ledger.CommitPending(ProofStatus::kPass);
    live_report = project.ledger.RegisterArtifact(
        ArtifactKind::kReport, "placed-timing", ProofStatus::kPass);
  }
};

TEST(Project, RoundTripPreservesDesignScenariosConstraintsAndLedger) {
  Fixture f;
  const std::string path = TempPath("roundtrip");
  Remove(path);
  ASSERT_TRUE(f.project.SaveProject(path).ok());
  std::ifstream tmp(path + ".tmp");
  EXPECT_FALSE(tmp.good());  // atomic save leaves no temp file

  Project restored;
  const Result<ProjectRestoreInfo> info = restored.LoadProject(path);
  ASSERT_TRUE(info.ok());
  EXPECT_EQ(info.value().checkpoint.constraints_marked_unresolved, 0u);
  EXPECT_EQ(info.value().bindings_restored, 0u);
  EXPECT_EQ(info.value().bindings_dropped, 0u);

  ASSERT_TRUE(restored.HasDesign());
  EXPECT_EQ(restored.design->name(), "top");
  EXPECT_FALSE(restored.design
                   ->Find(foundation::ObjectKind::kNet, "clk net")
                   .is_null());
  EXPECT_EQ(restored.design->Count(foundation::ObjectKind::kPin), 1u);
  EXPECT_EQ(restored.scenarios.size(), 2u);
  EXPECT_EQ(restored.scenarios.ActiveScenarios().size(), 1u);
  const db::CoverageReport coverage = restored.constraints.Coverage();
  EXPECT_EQ(coverage.total, 2u);
  EXPECT_EQ(coverage.bound, 1u);
  EXPECT_EQ(coverage.unresolved, 1u);

  // Ledger revision history and derived artifact validity survive.
  EXPECT_EQ(restored.ledger.current_revision(), 2u);
  ASSERT_EQ(restored.ledger.history().size(), 2u);
  EXPECT_EQ(restored.ledger.history()[0].label, "synthesize");
  EXPECT_EQ(restored.ledger.history()[1].label, "place");
  EXPECT_FALSE(restored.ledger.ArtifactValid(f.early_report));
  EXPECT_TRUE(restored.ledger.ArtifactValid(f.live_report));
  EXPECT_TRUE(restored.ledger.ArtifactCurrent(f.live_report));
  // Counters continue without reuse.
  const ArtifactId next = restored.ledger.RegisterArtifact(
      ArtifactKind::kAnalysis, "post-restore", ProofStatus::kUnknown);
  EXPECT_EQ(next, 3u);
  Remove(path);
}

TEST(Project, RoundTripWithAPendingTransaction) {
  Fixture f;
  TransactionSpec pending;
  pending.input_revision = 2;
  pending.label = "eco-resize";
  pending.tool_build = "eos-test";
  pending.invalidates.push_back(f.live_report);
  ASSERT_TRUE(f.project.ledger.BeginTransaction(pending).ok());

  const std::string path = TempPath("pending");
  Remove(path);
  ASSERT_TRUE(f.project.SaveProject(path).ok());

  Project restored;
  ASSERT_TRUE(restored.LoadProject(path).ok());
  ASSERT_TRUE(restored.ledger.HasPending());
  EXPECT_EQ(restored.ledger.Pending()->label, "eco-resize");
  // The resume decision works on the restored project.
  const Result<Revision> committed =
      restored.ledger.CommitPending(ProofStatus::kPass);
  ASSERT_TRUE(committed.ok());
  EXPECT_EQ(committed.value(), 3u);
  EXPECT_FALSE(restored.ledger.ArtifactValid(f.live_report));
  Remove(path);
}

TEST(Project, CorruptProjectFileIsRejectedAndLiveStateUntouched) {
  Fixture f;
  const std::string path = TempPath("corrupt");
  Remove(path);
  ASSERT_TRUE(f.project.SaveProject(path).ok());
  const std::string good = ReadAll(path);

  // A target with its own live state, which no failed load may touch.
  Project target;
  target.design.emplace("live");
  target.design->CreateNet("keepme");
  const ArtifactId marker = target.ledger.RegisterArtifact(
      ArtifactKind::kReport, "keep-report", ProofStatus::kPass);

  // Corrupt a byte inside the embedded checkpoint text.
  std::string flipped = good;
  const std::size_t mid = flipped.size() / 2;
  flipped[mid] = flipped[mid] == 'a' ? 'b' : 'a';
  WriteAll(path, flipped);
  EXPECT_FALSE(target.LoadProject(path).ok());

  // Truncated file.
  WriteAll(path, good.substr(0, good.size() / 3));
  EXPECT_FALSE(target.LoadProject(path).ok());

  // Not a project file at all.
  WriteAll(path, "hello world\n");
  EXPECT_FALSE(target.LoadProject(path).ok());

  ASSERT_TRUE(target.HasDesign());
  EXPECT_EQ(target.design->name(), "live");
  EXPECT_FALSE(
      target.design->Find(foundation::ObjectKind::kNet, "keepme").is_null());
  ASSERT_EQ(target.ledger.artifacts().size(), 1u);
  EXPECT_EQ(target.ledger.FindArtifact(marker)->name, "keep-report");

  // Missing file is an IO error, and still touches nothing.
  Remove(path);
  const Result<ProjectRestoreInfo> missing = target.LoadProject(path);
  ASSERT_FALSE(missing.ok());
  EXPECT_EQ(missing.status().code(), foundation::ErrorCode::kIoError);
  EXPECT_EQ(target.design->name(), "live");
  Remove(path);
}

// A hand-built binding over a fixture artifact, shaped exactly as
// ReportRegistry::RegisterReport would have made it.
ReportBinding MakeBinding(ArtifactId artifact, const std::string& name,
                          Revision revision) {
  ReportBinding binding;
  binding.artifact_id = artifact;
  binding.name = name;
  binding.bound_revision = revision;
  binding.timing_model = "NLDM";
  binding.tool_build = "eos-test";
  ScenarioSnapshot scenario;
  scenario.id = 1;
  scenario.name = "func tt";  // a space, to exercise the escaping
  scenario.mode = "func";
  scenario.corner = "tt_025c";
  scenario.parasitics = ParasiticsLevel::kEstimate;
  binding.scenarios.push_back(scenario);
  binding.parasitics = ParasiticsLevel::kEstimate;
  return binding;
}

void ExpectBindingEq(const ReportBinding& a, const ReportBinding& b) {
  EXPECT_EQ(a.artifact_id, b.artifact_id);
  EXPECT_EQ(a.name, b.name);
  EXPECT_EQ(a.bound_revision, b.bound_revision);
  EXPECT_EQ(a.timing_model, b.timing_model);
  EXPECT_EQ(a.tool_build, b.tool_build);
  EXPECT_EQ(a.parasitics, b.parasitics);
  ASSERT_EQ(a.scenarios.size(), b.scenarios.size());
  for (std::size_t i = 0; i < a.scenarios.size(); ++i) {
    EXPECT_EQ(a.scenarios[i].id, b.scenarios[i].id);
    EXPECT_EQ(a.scenarios[i].name, b.scenarios[i].name);
    EXPECT_EQ(a.scenarios[i].mode, b.scenarios[i].mode);
    EXPECT_EQ(a.scenarios[i].corner, b.scenarios[i].corner);
    EXPECT_EQ(a.scenarios[i].parasitics, b.scenarios[i].parasitics);
  }
}

TEST(Project, BindingsRoundTripWithTheProject) {
  Fixture f;
  f.project.report_bindings.push_back(
      MakeBinding(f.early_report, "early-timing", 0));
  ReportBinding mixed = MakeBinding(f.live_report, "placed-timing", 2);
  ScenarioSnapshot extracted;
  extracted.id = 2;
  extracted.name = "func_ss";
  extracted.mode = "func";
  extracted.corner = "ss_125c";
  extracted.parasitics = ParasiticsLevel::kExtracted;
  mixed.scenarios.push_back(extracted);
  mixed.parasitics.reset();  // mixed levels: no single level claimed
  f.project.report_bindings.push_back(mixed);

  const std::string path = TempPath("bindings");
  Remove(path);
  ASSERT_TRUE(f.project.SaveProject(path).ok());

  Project restored;
  const Result<ProjectRestoreInfo> info = restored.LoadProject(path);
  ASSERT_TRUE(info.ok());
  EXPECT_EQ(info.value().bindings_restored, 2u);
  EXPECT_EQ(info.value().bindings_dropped, 0u);
  ASSERT_EQ(restored.report_bindings.size(), 2u);
  ExpectBindingEq(restored.report_bindings[0],
                  f.project.report_bindings[0]);
  ExpectBindingEq(restored.report_bindings[1],
                  f.project.report_bindings[1]);
  Remove(path);
}

TEST(Project, BindingWhoseArtifactIsMissingIsDroppedAndCounted) {
  Fixture f;
  f.project.report_bindings.push_back(
      MakeBinding(f.live_report, "placed-timing", 2));
  // No artifact 99 exists in the ledger, and no artifact 3 either:
  // the first binding below names a report that was never
  // registered, the second names an analysis artifact, which is
  // present but is not the report the binding claims to describe.
  f.project.report_bindings.push_back(MakeBinding(99, "ghost", 2));
  const ArtifactId analysis = f.project.ledger.RegisterArtifact(
      ArtifactKind::kAnalysis, "some-analysis", ProofStatus::kPass);
  f.project.report_bindings.push_back(
      MakeBinding(analysis, "not-a-report", 2));

  const std::string path = TempPath("dropped");
  Remove(path);
  ASSERT_TRUE(f.project.SaveProject(path).ok());

  Project restored;
  const Result<ProjectRestoreInfo> info = restored.LoadProject(path);
  ASSERT_TRUE(info.ok());
  EXPECT_EQ(info.value().bindings_restored, 1u);
  EXPECT_EQ(info.value().bindings_dropped, 2u);
  ASSERT_EQ(restored.report_bindings.size(), 1u);
  EXPECT_EQ(restored.report_bindings[0].name, "placed-timing");
  Remove(path);
}

TEST(Project, ProjectFileWithoutAReportsSectionLoadsWithNoBindings) {
  Fixture f;
  // Compose the pre-2.0 file form by hand: magic, checkpoint and
  // ledger sections only.
  db::CheckpointData data;
  data.design = f.project.design->ExportState();
  data.scenarios = f.project.scenarios.ExportState();
  data.constraints = f.project.constraints.ExportState();
  const Result<std::string> checkpoint_text =
      db::Checkpoint::Serialize(data);
  ASSERT_TRUE(checkpoint_text.ok());
  const Result<std::string> ledger_text =
      SnapshotToText(f.project.ledger.Snapshot());
  ASSERT_TRUE(ledger_text.ok());
  std::string content = "EOSPROJ 1\n";
  content += "checkpoint " + std::to_string(checkpoint_text.value().size()) +
             "\n" + checkpoint_text.value();
  content += "ledger " + std::to_string(ledger_text.value().size()) + "\n" +
             ledger_text.value();

  const std::string path = TempPath("legacy");
  Remove(path);
  WriteAll(path, content);

  Project restored;
  const Result<ProjectRestoreInfo> info = restored.LoadProject(path);
  ASSERT_TRUE(info.ok());
  EXPECT_EQ(info.value().bindings_restored, 0u);
  EXPECT_EQ(info.value().bindings_dropped, 0u);
  EXPECT_TRUE(restored.report_bindings.empty());
  EXPECT_TRUE(restored.HasDesign());
  EXPECT_EQ(restored.ledger.current_revision(), 2u);
  Remove(path);
}

TEST(Project, CorruptBindingsSectionIsRejectedAndLiveStateUntouched) {
  Fixture f;
  f.project.report_bindings.push_back(
      MakeBinding(f.live_report, "placed-timing", 2));
  const std::string path = TempPath("corrupt_bindings");
  Remove(path);
  ASSERT_TRUE(f.project.SaveProject(path).ok());
  std::string content = ReadAll(path);
  const std::size_t at = content.find("EOSBINDINGS");
  ASSERT_NE(at, std::string::npos);
  content[at + 20] = content[at + 20] == 'a' ? 'b' : 'a';
  WriteAll(path, content);

  Project target;
  target.design.emplace("live");
  const ArtifactId kept = target.ledger.RegisterArtifact(
      ArtifactKind::kReport, "keep-report", ProofStatus::kPass);
  target.report_bindings.push_back(MakeBinding(kept, "keep-report", 0));

  EXPECT_FALSE(target.LoadProject(path).ok());
  ASSERT_TRUE(target.HasDesign());
  EXPECT_EQ(target.design->name(), "live");
  ASSERT_EQ(target.report_bindings.size(), 1u);
  EXPECT_EQ(target.report_bindings[0].name, "keep-report");
  Remove(path);
}

TEST(Project, SaveWithoutADesignFails) {
  Project empty;
  const std::string path = TempPath("nodesign");
  Remove(path);
  const Status saved = empty.SaveProject(path);
  ASSERT_FALSE(saved.ok());
  EXPECT_EQ(saved.code(), foundation::ErrorCode::kInvalidState);
  std::ifstream in(path);
  EXPECT_FALSE(in.good());
  Remove(path);
}

// S6 backfill: Reset clears every part of a populated project,
// and a fresh design can be created afterwards.
TEST(Project, ResetClearsDesignScenariosConstraintsLedgerAndBindings) {
  Fixture f;
  Project& project = f.project;
  ASSERT_TRUE(project.HasDesign());
  ASSERT_GT(project.scenarios.size(), 0u);
  ASSERT_GT(project.ledger.current_revision(), 0u);
  ReportBinding binding;
  binding.name = "timing";
  binding.artifact_id = 1;
  project.report_bindings.push_back(binding);
  ASSERT_FALSE(project.report_bindings.empty());
  project.Reset();
  EXPECT_FALSE(project.HasDesign());
  EXPECT_EQ(project.scenarios.size(), 0u);
  EXPECT_EQ(project.constraints.all().size(), 0u);
  EXPECT_EQ(project.ledger.current_revision(), 0u);
  EXPECT_TRUE(project.report_bindings.empty());
  project.design.emplace("fresh");
  EXPECT_TRUE(project.HasDesign());
  EXPECT_EQ(project.design->name(), "fresh");
}

}  // namespace
}  // namespace eos::session
