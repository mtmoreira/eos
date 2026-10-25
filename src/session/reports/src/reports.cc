// reports: the report registry and the export freshness gate
// (L5 session).
#include "eos/session/reports.h"

#include <utility>

namespace eos::session {

using foundation::ErrorCode;

const char* BlockerKindName(BlockerKind kind) {
  switch (kind) {
    case BlockerKind::kMissing: return "missing";
    case BlockerKind::kInvalid: return "invalid";
    case BlockerKind::kStale: return "stale";
    case BlockerKind::kProofNotPass: return "proof_not_pass";
    case BlockerKind::kCoverageIncomplete: return "coverage_incomplete";
  }
  return "missing";
}

Result<ArtifactId> ReportRegistry::RegisterReport(
    Project& project, const std::string& name, ProofStatus proof,
    const std::string& timing_model, const std::string& tool_build) {
  if (name.empty()) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "report name is empty");
  }
  if (timing_model.empty()) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "report timing model is empty");
  }
  if (FindByName(name) != nullptr) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "a report named " + name +
                             " is already registered");
  }
  const ArtifactId id = project.ledger.RegisterArtifact(
      ArtifactKind::kReport, name, proof);
  ReportBinding binding;
  binding.artifact_id = id;
  binding.name = name;
  binding.bound_revision = project.ledger.current_revision();
  binding.timing_model = timing_model;
  binding.tool_build = tool_build;
  bool uniform = true;
  bool first = true;
  for (const db::Scenario* scenario : project.scenarios.ActiveScenarios()) {
    ScenarioSnapshot snapshot;
    snapshot.id = scenario->id;
    snapshot.name = scenario->name;
    snapshot.mode = scenario->mode;
    snapshot.corner = scenario->corner;
    snapshot.parasitics = scenario->parasitics;
    if (first) {
      binding.parasitics = scenario->parasitics;
      first = false;
    } else if (binding.parasitics.has_value() &&
               *binding.parasitics != scenario->parasitics) {
      uniform = false;
    }
    binding.scenarios.push_back(std::move(snapshot));
  }
  if (!uniform) {
    binding.parasitics.reset();
  }
  bindings_.push_back(std::move(binding));
  return id;
}

const ReportBinding* ReportRegistry::FindByName(
    const std::string& name) const {
  for (const ReportBinding& binding : bindings_) {
    if (binding.name == name) {
      return &binding;
    }
  }
  return nullptr;
}

const ReportBinding* ReportRegistry::FindByArtifact(ArtifactId id) const {
  for (const ReportBinding& binding : bindings_) {
    if (binding.artifact_id == id) {
      return &binding;
    }
  }
  return nullptr;
}

ReportStatus ReportRegistry::StatusFor(const Project& project,
                                       const std::string& name) const {
  ReportStatus status;
  status.current_revision = project.ledger.current_revision();
  const ReportBinding* binding = FindByName(name);
  if (binding == nullptr) {
    return status;
  }
  const ArtifactRecord* record =
      project.ledger.FindArtifact(binding->artifact_id);
  if (record == nullptr) {
    return status;
  }
  status.found = true;
  status.artifact_id = binding->artifact_id;
  status.valid = project.ledger.ArtifactValid(binding->artifact_id);
  status.current = project.ledger.ArtifactCurrent(binding->artifact_id);
  status.proof = record->proof;
  status.bound_revision = record->bound_revision;
  return status;
}

ExportVerdict ReportRegistry::Evaluate(
    const Project& project,
    const std::vector<std::string>& required) const {
  ExportVerdict verdict;
  for (const std::string& name : required) {
    const ReportStatus status = StatusFor(project, name);
    if (!status.found) {
      verdict.blockers.push_back(ExportBlocker{
          BlockerKind::kMissing, name,
          "no report registered under this name"});
      continue;
    }
    if (!status.valid) {
      verdict.blockers.push_back(ExportBlocker{
          BlockerKind::kInvalid, name,
          "artifact " + std::to_string(status.artifact_id) +
              " is invalid at revision " +
              std::to_string(status.current_revision)});
    } else if (!status.current) {
      verdict.blockers.push_back(ExportBlocker{
          BlockerKind::kStale, name,
          "artifact " + std::to_string(status.artifact_id) +
              " is bound to revision " +
              std::to_string(status.bound_revision) +
              ", current revision is " +
              std::to_string(status.current_revision)});
    }
    if (status.proof != ProofStatus::kPass) {
      verdict.blockers.push_back(ExportBlocker{
          BlockerKind::kProofNotPass, name,
          "proof status is not pass"});
    }
  }
  const db::CoverageReport coverage = project.constraints.Coverage();
  if (!coverage.complete()) {
    verdict.blockers.push_back(ExportBlocker{
        BlockerKind::kCoverageIncomplete, "",
        "constraint coverage is incomplete: total=" +
            std::to_string(coverage.total) +
            " bound=" + std::to_string(coverage.bound) +
            " unresolved=" + std::to_string(coverage.unresolved) +
            " empty=" + std::to_string(coverage.empty)});
  }
  return verdict;
}

}  // namespace eos::session
