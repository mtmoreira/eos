// transactions implementation: the all-scene verdict, then the
// checkpoint / edit / re-analyze / judge / commit-or-rollback
// protocol over an ODB database. See the header for the rules.
#include "eos/eco/transactions.h"

#include <atomic>
#include <filesystem>
#include <utility>

namespace eos::eco {
namespace {

using foundation::ErrorCode;

// The timing metrics acceptance is judged on, in deciding order
// within a scene: setup first (worst slack, then total negative
// slack), then the hold pair. All are higher-is-better with the
// timing gate at zero.
const char* const kJudgedMetrics[] = {
    "worst_slack_ns",
    "total_negative_slack_ns",
    "worst_hold_slack_ns",
    "total_negative_hold_slack_ns",
};

// The epsilon is float-reporting noise only: the STA facade
// reports doubles converted from the engine's floats, and a
// difference at this scale is not a design change.
constexpr double kEpsilonNs = 1e-9;

Status Invalid(const std::string& message) {
  return Status::Error(ErrorCode::kInvalidArgument, message);
}

// Restores database from the checkpoint file by replacement: a
// fresh database reads the snapshot, then takes the caller's
// place through the facade's move assignment.
Status RestoreFrom(db::OdbDatabase& database, const std::string& checkpoint) {
  db::OdbDatabase restored;
  const Status read = restored.ReadDb(checkpoint);
  if (!read.ok()) {
    return Status::Error(
        read.code(),
        "ECO rollback failed: the checkpoint could not be read back (" +
            read.message() +
            "); the design is in an unknown state and must be reloaded");
  }
  database = std::move(restored);
  return Status::Ok();
}

struct CheckpointGuard {
  std::filesystem::path path;
  ~CheckpointGuard() {
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }
};

}  // namespace

Result<EcoVerdict> EvaluateAllScenes(
    const std::vector<analysis::SceneResult>& before,
    const std::vector<analysis::SceneResult>& after) {
  if (before.empty() || before.size() != after.size()) {
    return Invalid("ECO verdict needs the same scenes before and after");
  }
  EcoVerdict verdict;
  for (const analysis::SceneResult& before_scene : before) {
    const analysis::SceneResult* after_scene = nullptr;
    for (const analysis::SceneResult& candidate : after) {
      if (candidate.scenario.name == before_scene.scenario.name) {
        after_scene = &candidate;
        break;
      }
    }
    if (after_scene == nullptr) {
      return Invalid("ECO verdict: scene " + before_scene.scenario.name +
                     " is missing after the edit");
    }
    for (const char* metric_name : kJudgedMetrics) {
      const auto b = before_scene.metrics.find(metric_name);
      const auto a = after_scene->metrics.find(metric_name);
      if (b == before_scene.metrics.end() ||
          a == after_scene->metrics.end()) {
        return Invalid("ECO verdict: scene " + before_scene.scenario.name +
                       " does not carry metric " + metric_name +
                       " on both sides");
      }
      if (b->second.unit != a->second.unit) {
        return Invalid("ECO verdict: metric " + std::string(metric_name) +
                       " changed unit in scene " +
                       before_scene.scenario.name);
      }
      const double before_value = b->second.value;
      const double after_value = a->second.value;
      const bool was_violating = before_value < -kEpsilonNs;
      const bool is_violating = after_value < -kEpsilonNs;
      const bool regressed =
          (is_violating && !was_violating) ||
          (was_violating && after_value < before_value - kEpsilonNs);
      if (regressed && !verdict.regressed) {
        verdict.regressed = true;
        verdict.scene_name = before_scene.scenario.name;
        verdict.metric_name = metric_name;
        verdict.reason =
            "scene " + before_scene.scenario.name + " metric " +
            metric_name + " regressed: " + std::to_string(before_value) +
            " ns -> " + std::to_string(after_value) + " ns";
      }
      if (!verdict.improved && after_value > before_value + kEpsilonNs) {
        verdict.improved = true;
        if (!verdict.regressed) {
          verdict.scene_name = before_scene.scenario.name;
          verdict.metric_name = metric_name;
          verdict.reason =
              "scene " + before_scene.scenario.name + " metric " +
              metric_name + " improved: " + std::to_string(before_value) +
              " ns -> " + std::to_string(after_value) +
              " ns, and no scene regressed";
        }
      }
    }
  }
  if (verdict.regressed) {
    verdict.accepted = false;
    return verdict;
  }
  if (!verdict.improved) {
    verdict.reason = "no timing improvement in any scene";
    return verdict;
  }
  verdict.accepted = true;
  return verdict;
}

Result<EcoOutcome> RunEcoTransaction(
    db::OdbDatabase& database, const EcoRequest& request,
    const std::function<Status(db::OdbDatabase&)>& edit) {
  if (request.label.empty()) {
    return Invalid("an ECO transaction needs a label");
  }
  if (!edit) {
    return Invalid("an ECO transaction needs an edit");
  }
  for (const analysis::SceneAnalysis& scene : request.scenes) {
    if (scene.scenario.parasitics == db::ParasiticsLevel::kExtracted) {
      return Invalid(
          "scene " + scene.scenario.name +
          " is at extracted parasitics; after an edit its SPEF no "
          "longer describes the design, so ECO acceptance runs at "
          "estimate fidelity only (extracted acceptance after edits "
          "stays deferred until incremental extraction is "
          "qualified)");
    }
  }
  if (!database.has_top_block()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "an ECO transaction needs a design with a top "
                         "block");
  }

  // Checkpoint: the whole database in its binary form, in the temp
  // directory, removed when this call returns whatever happens.
  static std::atomic<unsigned long> counter{0};
  const std::filesystem::path checkpoint_path =
      std::filesystem::temp_directory_path() /
      ("eos_eco_checkpoint_" + std::to_string(counter.fetch_add(1)) + ".odb");
  CheckpointGuard guard{checkpoint_path};
  const Status checkpoint = database.WriteDb(checkpoint_path.string());
  if (!checkpoint.ok()) {
    return Status::Error(checkpoint.code(),
                         "ECO checkpoint failed: " + checkpoint.message());
  }

  analysis::TimingService timing;
  const auto before = timing.Analyze(database, request.sdc_path,
                                     request.scenes);
  if (!before.ok()) {
    return before.status();
  }

  EcoOutcome outcome;
  outcome.label = request.label;
  outcome.before = before.value();
  outcome.parasitics_note =
      "after metrics are a full re-analysis of every required scene "
      "at estimate parasitics fidelity; no extracted re-analysis "
      "after edits until incremental extraction is qualified";

  const Status edited = edit(database);
  if (!edited.ok()) {
    const Status restored = RestoreFrom(database, checkpoint_path.string());
    if (!restored.ok()) {
      return restored;
    }
    return Status::Error(edited.code(),
                         "ECO edit failed and the design was rolled "
                         "back: " +
                             edited.message());
  }

  const auto after = timing.Analyze(database, request.sdc_path,
                                    request.scenes);
  if (!after.ok()) {
    const Status restored = RestoreFrom(database, checkpoint_path.string());
    if (!restored.ok()) {
      return restored;
    }
    return Status::Error(after.status().code(),
                         "ECO re-analysis failed and the design was "
                         "rolled back: " +
                             after.status().message());
  }
  outcome.after = after.value();

  const auto verdict = EvaluateAllScenes(outcome.before, outcome.after);
  if (!verdict.ok()) {
    const Status restored = RestoreFrom(database, checkpoint_path.string());
    if (!restored.ok()) {
      return restored;
    }
    return verdict.status();
  }
  outcome.reason = verdict.value().reason;
  if (!verdict.value().accepted) {
    const Status restored = RestoreFrom(database, checkpoint_path.string());
    if (!restored.ok()) {
      return restored;
    }
    outcome.committed = false;
    return outcome;
  }
  outcome.committed = true;
  return outcome;
}

}  // namespace eos::eco
