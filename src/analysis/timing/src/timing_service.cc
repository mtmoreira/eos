// timing service implementation: validates the scene list whole,
// then runs one STA engine session per scene (the upstream engine
// is a singleton, so scenes run strictly one after another, each
// with its own corner's Liberty and its own parasitics).
#include "eos/analysis/timing.h"

#include <filesystem>
#include <set>
#include <utility>

#include "eos/analysis/sta.h"

namespace eos::analysis {
namespace {

using foundation::ErrorCode;

Status Invalid(const std::string& message) {
  return Status::Error(ErrorCode::kInvalidArgument, message);
}

// Validates the shared inputs before any analysis runs.
Status Validate(const std::string& sdc_path,
                const std::vector<SceneAnalysis>& scenes) {
  if (sdc_path.empty()) {
    return Invalid("timing analysis needs an SDC file");
  }
  if (scenes.empty()) {
    return Invalid("timing analysis needs at least one scene");
  }
  std::set<db::ScenarioId> ids;
  std::set<std::string> names;
  std::set<std::string> triples;
  for (const SceneAnalysis& scene : scenes) {
    const db::Scenario& scenario = scene.scenario;
    if (!ids.insert(scenario.id).second) {
      return Invalid("duplicate scenario id in scene list: " +
                     std::to_string(scenario.id));
    }
    if (!names.insert(scenario.name).second) {
      return Invalid("duplicate scenario in scene list: " + scenario.name);
    }
    const std::string triple =
        scenario.mode + "/" + scenario.corner + "/" +
        (scenario.parasitics == db::ParasiticsLevel::kExtracted
             ? "extracted"
             : "estimate");
    if (!triples.insert(triple).second) {
      return Invalid("duplicate scene (mode, corner, parasitics): " +
                     triple);
    }
    if (scene.liberty_path.empty()) {
      return Invalid("scene " + scenario.name +
                     " names no Liberty for corner " + scenario.corner);
    }
    const bool extracted =
        scenario.parasitics == db::ParasiticsLevel::kExtracted;
    if (extracted && scene.spef_path.empty()) {
      return Invalid("scene " + scenario.name +
                     " is at extracted parasitics but names no SPEF");
    }
    if (!extracted && !scene.spef_path.empty()) {
      return Invalid("scene " + scenario.name +
                     " is at estimate parasitics but names a SPEF; "
                     "parasitics levels are never mixed silently");
    }
  }
  return Status::Ok();
}

// Prefixes a failure with the scene it happened in, keeping the
// status code.
Status SceneError(const SceneAnalysis& scene, const Status& status) {
  return Status::Error(status.code(),
                       "scene " + scene.scenario.name + ": " +
                           status.message());
}

Result<SceneResult> AnalyzeScene(const VerilogDesign& design,
                                 const SceneAnalysis& scene) {
  StaEngine engine;
  Status status = engine.ReadLiberty(scene.liberty_path);
  if (!status.ok()) return SceneError(scene, status);
  status = engine.ReadVerilog(design.verilog_path);
  if (!status.ok()) return SceneError(scene, status);
  status = engine.LinkDesign(design.top_cell);
  if (!status.ok()) return SceneError(scene, status);
  status = engine.ReadSdc(design.sdc_path);
  if (!status.ok()) return SceneError(scene, status);
  if (!scene.spef_path.empty()) {
    status = engine.ReadSpef(scene.spef_path);
    if (!status.ok()) return SceneError(scene, status);
  }
  SceneResult result;
  result.scenario = scene.scenario;
  const auto add = [&result, &engine, &scene](const char* name,
                                              Result<double> (StaEngine::*query)())
      -> Status {
    Result<double> value = (engine.*query)();
    if (!value.ok()) return SceneError(scene, value.status());
    result.metrics[name] = TimingMetric{value.value(), "ns"};
    return Status::Ok();
  };
  status = add("worst_slack_ns", &StaEngine::WorstSlackNs);
  if (!status.ok()) return status;
  status = add("total_negative_slack_ns", &StaEngine::TotalNegativeSlackNs);
  if (!status.ok()) return status;
  status = add("worst_hold_slack_ns", &StaEngine::WorstHoldSlackNs);
  if (!status.ok()) return status;
  status = add("total_negative_hold_slack_ns",
               &StaEngine::TotalNegativeHoldSlackNs);
  if (!status.ok()) return status;
  // The electrical checks (unit F5): worst value in the check's
  // own unit and the violating-pin count, per scene.
  const auto add_electrical =
      [&result, &engine, &scene](
          const char* value_name, const char* unit,
          const char* count_name,
          Result<ElectricalCheck> (StaEngine::*query)()) -> Status {
    Result<ElectricalCheck> value = (engine.*query)();
    if (!value.ok()) return SceneError(scene, value.status());
    result.metrics[value_name] = TimingMetric{value.value().worst_value, unit};
    result.metrics[count_name] =
        TimingMetric{static_cast<double>(value.value().violation_count),
                     "pins"};
    return Status::Ok();
  };
  status = add_electrical("max_transition_ns", "ns",
                          "max_transition_violations",
                          &StaEngine::MaxTransitionCheck);
  if (!status.ok()) return status;
  status = add_electrical("max_capacitance_pf", "pF",
                          "max_capacitance_violations",
                          &StaEngine::MaxCapacitanceCheck);
  if (!status.ok()) return status;
  status = add_electrical("max_fanout", "fanout", "max_fanout_violations",
                          &StaEngine::MaxFanoutCheck);
  if (!status.ok()) return status;
  const auto setup_endpoints = engine.ViolatingEndpoints(CheckKind::kSetup);
  if (!setup_endpoints.ok()) return SceneError(scene, setup_endpoints.status());
  result.setup_violating_endpoints = setup_endpoints.value();
  const auto hold_endpoints = engine.ViolatingEndpoints(CheckKind::kHold);
  if (!hold_endpoints.ok()) return SceneError(scene, hold_endpoints.status());
  result.hold_violating_endpoints = hold_endpoints.value();
  return result;
}

Result<std::vector<SceneResult>> AnalyzeChecked(
    const VerilogDesign& design, const std::vector<SceneAnalysis>& scenes) {
  std::vector<SceneResult> results;
  for (const SceneAnalysis& scene : scenes) {
    Result<SceneResult> result = AnalyzeScene(design, scene);
    if (!result.ok()) return result.status();
    results.push_back(std::move(result).value());
  }
  return results;
}

// Loads one scene's inputs into a fresh engine, exactly as
// AnalyzeScene does, without running any metric query.
Status PrepareEngine(StaEngine& engine, const VerilogDesign& design,
                     const SceneAnalysis& scene) {
  Status status = engine.ReadLiberty(scene.liberty_path);
  if (!status.ok()) return SceneError(scene, status);
  status = engine.ReadVerilog(design.verilog_path);
  if (!status.ok()) return SceneError(scene, status);
  status = engine.LinkDesign(design.top_cell);
  if (!status.ok()) return SceneError(scene, status);
  status = engine.ReadSdc(design.sdc_path);
  if (!status.ok()) return SceneError(scene, status);
  if (!scene.spef_path.empty()) {
    status = engine.ReadSpef(scene.spef_path);
    if (!status.ok()) return SceneError(scene, status);
  }
  return Status::Ok();
}

Result<ScenePathReports> ReportScene(const VerilogDesign& design,
                                     const SceneAnalysis& scene,
                                     const PathReportRequest& request) {
  StaEngine engine;
  const Status prepared = PrepareEngine(engine, design, scene);
  if (!prepared.ok()) return prepared;
  ScenePathReports out;
  out.scenario = scene.scenario;
  if (!request.endpoint.empty()) {
    Result<PathReport> report =
        engine.WorstPathReport(request.kind, request.endpoint);
    if (!report.ok()) return SceneError(scene, report.status());
    if (!request.slack_lesser_than.has_value() ||
        report.value().slack_ns < request.slack_lesser_than.value()) {
      out.paths.push_back(std::move(report).value());
    }
    return out;
  }
  Result<std::vector<EndpointSlack>> endpoints =
      engine.EndpointsBySlack(request.kind);
  if (!endpoints.ok()) return SceneError(scene, endpoints.status());
  for (const EndpointSlack& endpoint : endpoints.value()) {
    if (out.paths.size() >= request.max_paths) break;
    // Endpoints arrive worst first, so once one meets the ceiling
    // every later one does too.
    if (request.slack_lesser_than.has_value() &&
        endpoint.slack_ns >= request.slack_lesser_than.value()) {
      break;
    }
    Result<PathReport> report =
        engine.WorstPathReport(request.kind, endpoint.pin);
    if (!report.ok()) return SceneError(scene, report.status());
    out.paths.push_back(std::move(report).value());
  }
  return out;
}

}  // namespace

Result<std::vector<SceneResult>> TimingService::Analyze(
    const VerilogDesign& design, const std::vector<SceneAnalysis>& scenes) {
  if (design.verilog_path.empty() || design.top_cell.empty()) {
    return Invalid("timing analysis needs a netlist and its top cell");
  }
  const Status valid = Validate(design.sdc_path, scenes);
  if (!valid.ok()) return valid;
  return AnalyzeChecked(design, scenes);
}

Result<std::vector<SceneResult>> TimingService::Analyze(
    db::OdbDatabase& database, const std::string& sdc_path,
    const std::vector<SceneAnalysis>& scenes) {
  if (!database.has_top_block()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "timing analysis of an ODB database with no "
                         "top block");
  }
  const Status valid = Validate(sdc_path, scenes);
  if (!valid.ok()) return valid;
  // The netlist is a scratch product of the database, never a
  // deliverable: it lives in the temp directory and is removed
  // when this call returns, whatever the outcome.
  const std::filesystem::path netlist =
      std::filesystem::temp_directory_path() / "eos_timing_service_netlist.v";
  const Status written = database.WriteVerilog(netlist.string());
  if (!written.ok()) return written;
  struct NetlistGuard {
    std::filesystem::path path;
    ~NetlistGuard() {
      std::error_code ec;
      std::filesystem::remove(path, ec);
    }
  } guard{netlist};
  const VerilogDesign design{netlist.string(), database.TopBlockName(),
                             sdc_path};
  return AnalyzeChecked(design, scenes);
}

Result<std::vector<ScenePathReports>> TimingService::ReportPaths(
    const VerilogDesign& design,
    const std::vector<SceneAnalysis>& scenes,
    const PathReportRequest& request) {
  if (design.verilog_path.empty() || design.top_cell.empty()) {
    return Invalid("timing analysis needs a netlist and its top cell");
  }
  if (request.max_paths == 0) {
    return Invalid("path report needs max_paths of at least 1");
  }
  const Status valid = Validate(design.sdc_path, scenes);
  if (!valid.ok()) return valid;
  std::vector<ScenePathReports> reports;
  for (const SceneAnalysis& scene : scenes) {
    Result<ScenePathReports> report = ReportScene(design, scene, request);
    if (!report.ok()) return report.status();
    reports.push_back(std::move(report).value());
  }
  return reports;
}

Result<std::vector<ScenePathReports>> TimingService::ReportPaths(
    db::OdbDatabase& database, const std::string& sdc_path,
    const std::vector<SceneAnalysis>& scenes,
    const PathReportRequest& request) {
  if (!database.has_top_block()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "timing analysis of an ODB database with no "
                         "top block");
  }
  // The netlist is a scratch product of the database, never a
  // deliverable: it lives in the temp directory and is removed
  // when this call returns, whatever the outcome.
  const std::filesystem::path netlist =
      std::filesystem::temp_directory_path() / "eos_timing_service_netlist.v";
  const Status written = database.WriteVerilog(netlist.string());
  if (!written.ok()) return written;
  struct NetlistGuard {
    std::filesystem::path path;
    ~NetlistGuard() {
      std::error_code ec;
      std::filesystem::remove(path, ec);
    }
  } guard{netlist};
  const VerilogDesign design{netlist.string(), database.TopBlockName(),
                             sdc_path};
  return ReportPaths(design, scenes, request);
}

}  // namespace eos::analysis
