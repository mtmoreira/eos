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
  const Result<db::RestoreInfo> info = restored.LoadProject(path);
  ASSERT_TRUE(info.ok());
  EXPECT_EQ(info.value().constraints_marked_unresolved, 0u);

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
  const Result<db::RestoreInfo> missing = target.LoadProject(path);
  ASSERT_FALSE(missing.ok());
  EXPECT_EQ(missing.status().code(), foundation::ErrorCode::kIoError);
  EXPECT_EQ(target.design->name(), "live");
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

}  // namespace
}  // namespace eos::session
