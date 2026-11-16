// timing service implementation: validates the scene list whole,
// then runs one STA engine session per scene (the upstream engine
// is a singleton, so scenes run strictly one after another, each
// with its own corner's Liberty and its own parasitics).
#include "eos/analysis/timing.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

#include "eos/analysis/sta.h"

namespace eos::analysis {
namespace {

using foundation::ErrorCode;

Status Invalid(const std::string& message) {
  return Status::Error(ErrorCode::kInvalidArgument, message);
}
Status NotFound(const std::string& message) {
  return Status::Error(ErrorCode::kNotFound, message);
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
  // own unit and the violating-pin count, per scene. The full
  // check record is kept in the result (Immediate unit 6) so the
  // verdict-shaped report can name the worst pin, its limit and
  // its slack and can tell "no limit defined" apart from "limit
  // defined and clean".
  const auto add_electrical =
      [&result, &engine, &scene](
          const char* value_name, const char* unit,
          const char* count_name, ElectricalCheck& check_out,
          Result<ElectricalCheck> (StaEngine::*query)()) -> Status {
    Result<ElectricalCheck> value = (engine.*query)();
    if (!value.ok()) return SceneError(scene, value.status());
    check_out = value.value();
    result.metrics[value_name] = TimingMetric{value.value().worst_value, unit};
    result.metrics[count_name] =
        TimingMetric{static_cast<double>(value.value().violation_count),
                     "pins"};
    return Status::Ok();
  };
  status = add_electrical("max_transition_ns", "ns",
                          "max_transition_violations",
                          result.max_transition_check,
                          &StaEngine::MaxTransitionCheck);
  if (!status.ok()) return status;
  status = add_electrical("max_capacitance_pf", "pF",
                          "max_capacitance_violations",
                          result.max_capacitance_check,
                          &StaEngine::MaxCapacitanceCheck);
  if (!status.ok()) return status;
  status = add_electrical("max_fanout", "fanout", "max_fanout_violations",
                          result.max_fanout_check,
                          &StaEngine::MaxFanoutCheck);
  if (!status.ok()) return status;
  // The electrical violators (Immediate unit 6): every violating
  // pin per check, worst slack first, so the report's cost is
  // the exact sum of the violations.
  const auto add_violators =
      [&engine, &scene](
          std::vector<ElectricalViolation>& out,
          Result<std::vector<ElectricalViolation>> (StaEngine::*query)())
      -> Status {
    Result<std::vector<ElectricalViolation>> value = (engine.*query)();
    if (!value.ok()) return SceneError(scene, value.status());
    out = value.value();
    return Status::Ok();
  };
  status = add_violators(result.max_transition_violators,
                         &StaEngine::MaxTransitionViolators);
  if (!status.ok()) return status;
  status = add_violators(result.max_capacitance_violators,
                         &StaEngine::MaxCapacitanceViolators);
  if (!status.ok()) return status;
  status = add_violators(result.max_fanout_violators,
                         &StaEngine::MaxFanoutViolators);
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
  const PathFilter filter{request.from, request.through};
  const auto passes_ceiling = [&request](double slack) {
    return !request.slack_lesser_than.has_value() ||
           slack < request.slack_lesser_than.value();
  };
  // A named group restricts the endpoints: resolve it against
  // the scene's groups first, so an unknown group fails naming
  // what exists, and a known group yields its endpoints worst
  // first (the T8 pattern).
  std::vector<EndpointSlack> group_endpoints;
  if (!request.group.empty()) {
    Result<std::vector<PathGroupSlack>> groups =
        engine.PathGroupSlacks(request.kind);
    if (!groups.ok()) return SceneError(scene, groups.status());
    std::vector<PathGroupSlack> listed = std::move(groups).value();
    std::string available;
    bool found = false;
    for (const PathGroupSlack& group : listed) {
      if (!available.empty()) available += ", ";
      available += group.name;
      if (group.name == request.group) {
        group_endpoints = group.endpoints;
        found = true;
      }
    }
    if (!found) {
      return SceneError(
          scene,
          NotFound("unknown path group: " + request.group +
                  " (available: " +
                  (available.empty() ? std::string("(none)") : available) +
                  ")"));
    }
  }
  if (!request.endpoint.empty()) {
    if (!request.group.empty()) {
      // -to and -group combine as AND: an endpoint outside the
      // group yields no paths, not an error.
      const bool in_group =
          std::any_of(group_endpoints.begin(), group_endpoints.end(),
                      [&request](const EndpointSlack& endpoint) {
                        return endpoint.pin == request.endpoint;
                      });
      if (!in_group) return out;
    }
    Result<std::vector<PathReport>> reports = engine.WorstPaths(
        request.kind, request.endpoint, request.nworst, filter);
    if (!reports.ok()) return SceneError(scene, reports.status());
    for (PathReport& report : reports.value()) {
      // The stored group is the group the path was selected
      // through; the facade fills it from the end's group
      // otherwise.
      if (!request.group.empty()) report.group = request.group;
      if (passes_ceiling(report.slack_ns)) {
        out.paths.push_back(std::move(report));
      }
    }
    return out;
  }
  std::vector<EndpointSlack> endpoints;
  if (!request.group.empty()) {
    endpoints = group_endpoints;
  } else {
    Result<std::vector<EndpointSlack>> all =
        engine.EndpointsBySlack(request.kind);
    if (!all.ok()) return SceneError(scene, all.status());
    endpoints = std::move(all).value();
  }
  for (const EndpointSlack& endpoint : endpoints) {
    if (request.nworst == 1 &&
        out.paths.size() >= request.max_paths) {
      break;
    }
    // Endpoints arrive worst first, so once one meets the ceiling
    // every later one does too: a filtered path's slack is never
    // better than its endpoint's unfiltered worst.
    if (request.slack_lesser_than.has_value() &&
        endpoint.slack_ns >= request.slack_lesser_than.value()) {
      break;
    }
    Result<std::vector<PathReport>> reports = engine.WorstPaths(
        request.kind, endpoint.pin, request.nworst, filter);
    if (!reports.ok()) return SceneError(scene, reports.status());
    for (PathReport& report : reports.value()) {
      if (!request.group.empty()) report.group = request.group;
      if (passes_ceiling(report.slack_ns)) {
        out.paths.push_back(std::move(report));
      }
    }
  }
  if (request.nworst > 1) {
    // Several paths per endpoint break the endpoint order: the
    // collected set is re-sorted worst first and capped, so
    // -max_paths still takes the worst paths overall.
    std::stable_sort(out.paths.begin(), out.paths.end(),
                     [](const PathReport& a, const PathReport& b) {
                       return a.slack_ns < b.slack_ns;
                     });
    if (out.paths.size() > request.max_paths) {
      out.paths.resize(request.max_paths);
    }
  }
  return out;
}

Result<CoverageResult> CoverageScene(const VerilogDesign& design,
                                     const SceneAnalysis& scene) {
  StaEngine engine;
  const Status prepared = PrepareEngine(engine, design, scene);
  if (!prepared.ok()) return prepared;
  CoverageResult out;
  out.scenario = scene.scenario;
  Result<std::vector<CoveragePin>> pins = engine.AnalysisCoverage();
  if (!pins.ok()) return SceneError(scene, pins.status());
  out.pins = std::move(pins).value();
  return out;
}

Result<QorResult> QorScene(const VerilogDesign& design,
                           const SceneAnalysis& scene) {
  StaEngine engine;
  const Status prepared = PrepareEngine(engine, design, scene);
  if (!prepared.ok()) return prepared;
  QorResult out;
  out.scenario = scene.scenario;
  const auto add = [&out, &engine, &scene](const char* name,
                                           Result<double> (StaEngine::*query)())
      -> Status {
    Result<double> value = (engine.*query)();
    if (!value.ok()) return SceneError(scene, value.status());
    out.metrics[name] = TimingMetric{value.value(), "ns"};
    return Status::Ok();
  };
  Status status = add("worst_slack_ns", &StaEngine::WorstSlackNs);
  if (!status.ok()) return status;
  status = add("total_negative_slack_ns", &StaEngine::TotalNegativeSlackNs);
  if (!status.ok()) return status;
  status = add("worst_hold_slack_ns", &StaEngine::WorstHoldSlackNs);
  if (!status.ok()) return status;
  status = add("total_negative_hold_slack_ns",
               &StaEngine::TotalNegativeHoldSlackNs);
  if (!status.ok()) return status;
  const auto add_electrical =
      [&out, &engine, &scene](
          const char* value_name, const char* unit,
          const char* count_name, ElectricalCheck& check_out,
          Result<ElectricalCheck> (StaEngine::*query)()) -> Status {
    Result<ElectricalCheck> value = (engine.*query)();
    if (!value.ok()) return SceneError(scene, value.status());
    check_out = value.value();
    out.metrics[value_name] = TimingMetric{value.value().worst_value, unit};
    out.metrics[count_name] =
        TimingMetric{static_cast<double>(value.value().violation_count),
                     "pins"};
    return Status::Ok();
  };
  status = add_electrical("max_transition_ns", "ns",
                          "max_transition_violations",
                          out.max_transition_check,
                          &StaEngine::MaxTransitionCheck);
  if (!status.ok()) return status;
  status = add_electrical("max_capacitance_pf", "pF",
                          "max_capacitance_violations",
                          out.max_capacitance_check,
                          &StaEngine::MaxCapacitanceCheck);
  if (!status.ok()) return status;
  status = add_electrical("max_fanout", "fanout", "max_fanout_violations",
                          out.max_fanout_check,
                          &StaEngine::MaxFanoutCheck);
  if (!status.ok()) return status;
  // The electrical violators (waiver-model unit): every violating
  // pin per check, worst slack first, exactly as AnalyzeScene
  // fills SceneResult's, so report_qor can count waived DRC
  // violations per pin.
  const auto add_violators =
      [&engine, &scene](
          std::vector<ElectricalViolation>& violators_out,
          Result<std::vector<ElectricalViolation>> (StaEngine::*query)())
      -> Status {
    Result<std::vector<ElectricalViolation>> value = (engine.*query)();
    if (!value.ok()) return SceneError(scene, value.status());
    violators_out = value.value();
    return Status::Ok();
  };
  status = add_violators(out.max_transition_violators,
                         &StaEngine::MaxTransitionViolators);
  if (!status.ok()) return status;
  status = add_violators(out.max_capacitance_violators,
                         &StaEngine::MaxCapacitanceViolators);
  if (!status.ok()) return status;
  status = add_violators(out.max_fanout_violators,
                         &StaEngine::MaxFanoutViolators);
  if (!status.ok()) return status;
  const auto add_groups =
      [&out, &engine, &scene](
          std::vector<PathGroupResult>& groups_out, CheckKind kind)
      -> Status {
    Result<std::vector<PathGroupSlack>> groups =
        engine.PathGroupSlacks(kind);
    if (!groups.ok()) return SceneError(scene, groups.status());
    for (PathGroupSlack& group : groups.value()) {
      groups_out.push_back(PathGroupResult{
          std::move(group.name), std::move(group.endpoints)});
    }
    return Status::Ok();
  };
  status = add_groups(out.setup_groups, CheckKind::kSetup);
  if (!status.ok()) return status;
  status = add_groups(out.hold_groups, CheckKind::kHold);
  if (!status.ok()) return status;
  Result<CellAreaResult> cells = engine.CellArea();
  if (!cells.ok()) return SceneError(scene, cells.status());
  for (const CellTypeArea& row : cells.value().rows) {
    out.cells.push_back(
        CellTypeRow{row.cell, row.count, row.area});
  }
  out.total_cells = cells.value().total_cells;
  out.total_area = cells.value().total_area;
  return out;
}

// One scene's check-specific data (Immediate unit 10, review
// item T8): the per-endpoint slacks, the per-group endpoints
// and the worst path of each group's worst endpoint, the
// minimum-period and minimum-pulse-width checks, and the
// analysis-coverage pins. The worst path of a group comes from
// the group's worst endpoint, which the groups list worst
// first; the endpoint carries a constrained path end for the
// kind by construction, so the worst-path query cannot fail
// for it.
Result<CheckReportsResult> CheckReportsScene(
    const VerilogDesign& design, const SceneAnalysis& scene) {
  StaEngine engine;
  const Status prepared = PrepareEngine(engine, design, scene);
  if (!prepared.ok()) return prepared;
  CheckReportsResult out;
  out.scenario = scene.scenario;
  const auto add_endpoints =
      [&out, &engine, &scene](
          std::vector<EndpointSlack>& endpoints_out, CheckKind kind)
      -> Status {
    Result<std::vector<EndpointSlack>> endpoints =
        engine.EndpointsBySlack(kind);
    if (!endpoints.ok()) return SceneError(scene, endpoints.status());
    endpoints_out = std::move(endpoints).value();
    return Status::Ok();
  };
  Status status =
      add_endpoints(out.setup_endpoints, CheckKind::kSetup);
  if (!status.ok()) return status;
  status = add_endpoints(out.hold_endpoints, CheckKind::kHold);
  if (!status.ok()) return status;
  const auto add_groups =
      [&out, &engine, &scene](
          std::vector<PathGroupResult>& groups_out,
          std::vector<GroupWorstPath>& paths_out, CheckKind kind)
      -> Status {
    Result<std::vector<PathGroupSlack>> groups =
        engine.PathGroupSlacks(kind);
    if (!groups.ok()) return SceneError(scene, groups.status());
    for (PathGroupSlack& group : groups.value()) {
      if (group.endpoints.empty()) continue;
      const std::string name = group.name;
      Result<PathReport> path =
          engine.WorstPathReport(kind, group.endpoints[0].pin);
      if (!path.ok()) return SceneError(scene, path.status());
      paths_out.push_back(
          GroupWorstPath{name, std::move(path).value()});
      groups_out.push_back(PathGroupResult{
          std::move(group.name), std::move(group.endpoints)});
    }
    return Status::Ok();
  };
  status = add_groups(out.setup_groups, out.setup_group_paths,
                      CheckKind::kSetup);
  if (!status.ok()) return status;
  status = add_groups(out.hold_groups, out.hold_group_paths,
                      CheckKind::kHold);
  if (!status.ok()) return status;
  // The derived minimum periods (Immediate unit 10, review
  // item T8): per clock, period minus the worst setup slack of
  // the clock's setup group; per endpoint, period minus that
  // endpoint's setup slack. Only groups named after a clock
  // contribute: a named group_path is not a clock, and a clock
  // with no setup group has no row. Setup only: hold slack
  // does not depend on the period.
  Result<std::vector<ClockPeriod>> clock_periods =
      engine.ClockPeriods();
  if (!clock_periods.ok()) {
    return SceneError(scene, clock_periods.status());
  }
  for (const ClockPeriod& clock : clock_periods.value()) {
    const auto group = std::find_if(
        out.setup_groups.begin(), out.setup_groups.end(),
        [&clock](const PathGroupResult& g) {
          return g.name == clock.name;
        });
    if (group == out.setup_groups.end() || group->endpoints.empty()) {
      continue;
    }
    const double worst = group->endpoints[0].slack_ns;
    out.min_period_clocks.push_back(MinPeriodClockRow{
        clock.name, clock.period_ns, clock.period_ns - worst, worst,
        group->endpoints[0].pin});
    for (const EndpointSlack& endpoint : group->endpoints) {
      out.min_period_endpoints.push_back(MinPeriodEndpointRow{
          endpoint.pin, clock.name, clock.period_ns,
          clock.period_ns - endpoint.slack_ns, endpoint.slack_ns});
    }
  }
  std::sort(out.min_period_clocks.begin(), out.min_period_clocks.end(),
            [](const MinPeriodClockRow& a, const MinPeriodClockRow& b) {
              if (a.slack_ns != b.slack_ns)
                return a.slack_ns < b.slack_ns;
              return a.clock < b.clock;
            });
  std::sort(out.min_period_endpoints.begin(),
            out.min_period_endpoints.end(),
            [](const MinPeriodEndpointRow& a,
               const MinPeriodEndpointRow& b) {
              if (a.slack_ns != b.slack_ns)
                return a.slack_ns < b.slack_ns;
              if (a.pin != b.pin) return a.pin < b.pin;
              return a.clock < b.clock;
            });
  Result<std::vector<MinPulseWidthCheck>> min_pulse_width =
      engine.MinPulseWidthChecks();
  if (!min_pulse_width.ok()) {
    return SceneError(scene, min_pulse_width.status());
  }
  out.min_pulse_width_checks = std::move(min_pulse_width).value();
  Result<std::vector<CoveragePin>> coverage = engine.AnalysisCoverage();
  if (!coverage.ok()) return SceneError(scene, coverage.status());
  out.coverage_pins = std::move(coverage).value();
  return out;
}

// Translates an SDC text through a pin map (SDC pin-resolution
// design, section 4.5). A word equal to a map key is replaced by
// the map's value; every other byte is preserved, so line numbers
// stay true and engine messages cite the user's lines. The scan
// follows io/sdc's documented lexical subset: statements split on
// newlines and semicolons, {...} groups nest, [...] groups are
// command substitutions, double quotes group words, a backslash
// escapes the next character (backslash-newline continues a
// statement), and a '#' at the start of a token, outside braces,
// comments out the rest of the line and is never rewritten. A
// [digits] suffix directly on a word is a bit select and part of
// the word. A word that is one quoted string is compared by its
// content, quotes preserved. Unmapped words pass through
// byte-identical: the translator never drops, patterns, or
// guesses a reference.
std::string TranslateSdcPinReferences(const std::string& text,
                                      const SdcPinMap& map) {
  if (map.empty()) return text;
  std::string out;
  out.reserve(text.size());
  std::string word;
  bool in_word = false;
  bool in_quote = false;
  int brace_depth = 0;
  const std::size_t n = text.size();
  const auto flush_word = [&]() {
    if (!in_word) return;
    const auto direct = map.find(word);
    if (direct != map.end()) {
      out += direct->second;
    } else if (word.size() >= 2 && word.front() == '"' &&
               word.back() == '"' &&
               word.find('"', 1) == word.size() - 1) {
      const auto inner = map.find(word.substr(1, word.size() - 2));
      if (inner != map.end()) {
        out += '"';
        out += inner->second;
        out += '"';
      } else {
        out += word;
      }
    } else {
      out += word;
    }
    word.clear();
    in_word = false;
  };
  for (std::size_t i = 0; i < n; ++i) {
    const char c = text[i];
    if (in_quote) {
      word += c;
      if (c == '\\' && i + 1 < n) word += text[++i];
      if (c == '"') in_quote = false;
      continue;
    }
    if (c == '#' && !in_word && brace_depth == 0) {
      // A comment runs to the end of the line and keeps the
      // user's words; the newline itself is emitted after.
      while (i < n && text[i] != '\n') out += text[i++];
      if (i < n) out += text[i];
      continue;
    }
    if (c == '\\' && i + 1 < n) {
      in_word = true;
      word += c;
      word += text[++i];
      continue;
    }
    if (c == '"') {
      in_word = true;
      in_quote = true;
      word += c;
      continue;
    }
    if (c == '{') {
      flush_word();
      ++brace_depth;
      out += c;
      continue;
    }
    if (c == '}') {
      flush_word();
      if (brace_depth > 0) --brace_depth;
      out += c;
      continue;
    }
    if (c == '[' && in_word) {
      // A [digits] suffix directly on a word is a bit select and
      // part of the word; any other bracket delimits.
      std::size_t j = i + 1;
      while (j < n && text[j] >= '0' && text[j] <= '9') ++j;
      if (j > i + 1 && j < n && text[j] == ']') {
        word.append(text, i, j - i + 1);
        i = j;
        continue;
      }
      flush_word();
      out += c;
      continue;
    }
    if (c == '[' || c == ']' || c == ';' || c == ' ' || c == '\t' ||
        c == '\r' || c == '\n' || c == '\f' || c == '\v') {
      flush_word();
      out += c;
      continue;
    }
    in_word = true;
    word += c;
  }
  flush_word();
  return out;
}

// The scratch products of an ODB analysis (design section 4.4):
// the database's netlist, and, when a pin map is present, the
// translated SDC beside it. Both live in the temp directory and
// are removed when the owning call returns, whatever the outcome.
struct OdbScratchDesign {
  VerilogDesign design;
  std::filesystem::path netlist_path;
  std::filesystem::path sdc_scratch_path;  // empty: no translation
  std::string user_sdc_path;
  ~OdbScratchDesign() {
    std::error_code ec;
    if (!netlist_path.empty()) std::filesystem::remove(netlist_path, ec);
    if (!sdc_scratch_path.empty()) {
      std::filesystem::remove(sdc_scratch_path, ec);
    }
  }
};

// Writes the scratch netlist from the database, exactly as the
// ODB overloads always have, and, when the pin map is non-empty,
// a scratch translated SDC beside it; the design then names the
// scratch SDC. With an empty map, or when the user's SDC text
// cannot be read for translation, the design names the user's
// SDC path itself and the engine produces its own read behavior,
// byte-identical to the pre-map service.
Status PrepareOdbDesign(db::OdbDatabase& database,
                        const std::string& sdc_path,
                        const SdcPinMap& pin_map,
                        OdbScratchDesign* scratch) {
  scratch->user_sdc_path = sdc_path;
  scratch->netlist_path = std::filesystem::temp_directory_path() /
                          "eos_timing_service_netlist.v";
  const Status written =
      database.WriteVerilog(scratch->netlist_path.string());
  if (!written.ok()) return written;
  std::string effective_sdc = sdc_path;
  if (!pin_map.empty()) {
    std::ifstream in(sdc_path, std::ios::binary);
    if (in) {
      const std::string text{std::istreambuf_iterator<char>(in),
                             std::istreambuf_iterator<char>()};
      if (!in.bad()) {
        const std::filesystem::path scratch_path =
            std::filesystem::temp_directory_path() /
            "eos_timing_service_sdc.sdc";
        scratch->sdc_scratch_path = scratch_path;
        std::ofstream out(scratch_path,
                          std::ios::binary | std::ios::trunc);
        if (out) {
          out << TranslateSdcPinReferences(text, pin_map);
          out.flush();
          if (out.good()) effective_sdc = scratch_path.string();
        }
      }
    }
  }
  scratch->design = VerilogDesign{scratch->netlist_path.string(),
                                  database.TopBlockName(), effective_sdc};
  return Status::Ok();
}

// On a failure from a translated session, names the user's SDC
// file in place of the scratch translation (design section 4.4):
// translation preserved line numbers, so the substituted message
// names the user's file and the true line, exactly as messages
// on untranslated sessions do. Two forms are substituted: the
// scratch path itself (the facade's "could not read SDC file"
// prefix carries the path it was given) and the scratch file's
// basename (the engine's own error text names only the
// basename: "eos_timing_service_sdc.sdc line 2" for OpenSTA's
// Error 374, measured).
template <typename T>
Result<T> SubstituteScratchSdcPath(Result<T> result,
                                   const OdbScratchDesign& scratch) {
  if (result.ok() || scratch.sdc_scratch_path.empty() ||
      scratch.design.sdc_path != scratch.sdc_scratch_path.string()) {
    return std::move(result);
  }
  const auto replace_all = [](std::string text, const std::string& needle,
                              const std::string& value) {
    if (needle.empty()) return text;
    std::string replaced;
    std::size_t pos = 0;
    for (std::size_t hit = text.find(needle, pos);
         hit != std::string::npos; hit = text.find(needle, pos)) {
      replaced.append(text, pos, hit - pos);
      replaced += value;
      pos = hit + needle.size();
    }
    replaced.append(text, pos, std::string::npos);
    return replaced;
  };
  std::string replaced =
      replace_all(result.status().message(),
                  scratch.sdc_scratch_path.string(), scratch.user_sdc_path);
  replaced = replace_all(
      std::move(replaced), scratch.sdc_scratch_path.filename().string(),
      std::filesystem::path(scratch.user_sdc_path).filename().string());
  if (replaced == result.status().message()) return std::move(result);
  return Status::Error(result.status().code(), replaced);
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
    const std::vector<SceneAnalysis>& scenes, const SdcPinMap& pin_map) {
  if (!database.has_top_block()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "timing analysis of an ODB database with no "
                         "top block");
  }
  const Status valid = Validate(sdc_path, scenes);
  if (!valid.ok()) return valid;
  OdbScratchDesign scratch;
  const Status prepared =
      PrepareOdbDesign(database, sdc_path, pin_map, &scratch);
  if (!prepared.ok()) return prepared;
  return SubstituteScratchSdcPath(AnalyzeChecked(scratch.design, scenes),
                                  scratch);
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
  if (request.nworst == 0) {
    return Invalid("path report needs nworst of at least 1");
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
    const PathReportRequest& request, const SdcPinMap& pin_map) {
  if (!database.has_top_block()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "timing analysis of an ODB database with no "
                         "top block");
  }
  OdbScratchDesign scratch;
  const Status prepared =
      PrepareOdbDesign(database, sdc_path, pin_map, &scratch);
  if (!prepared.ok()) return prepared;
  return SubstituteScratchSdcPath(
      ReportPaths(scratch.design, scenes, request), scratch);
}

Result<std::vector<CoverageResult>> TimingService::AnalyzeCoverage(
    const VerilogDesign& design,
    const std::vector<SceneAnalysis>& scenes) {
  if (design.verilog_path.empty() || design.top_cell.empty()) {
    return Invalid("timing analysis needs a netlist and its top cell");
  }
  const Status valid = Validate(design.sdc_path, scenes);
  if (!valid.ok()) return valid;
  std::vector<CoverageResult> results;
  for (const SceneAnalysis& scene : scenes) {
    Result<CoverageResult> result = CoverageScene(design, scene);
    if (!result.ok()) return result.status();
    results.push_back(std::move(result).value());
  }
  return results;
}

Result<std::vector<CoverageResult>> TimingService::AnalyzeCoverage(
    db::OdbDatabase& database, const std::string& sdc_path,
    const std::vector<SceneAnalysis>& scenes, const SdcPinMap& pin_map) {
  if (!database.has_top_block()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "timing analysis of an ODB database with no "
                         "top block");
  }
  OdbScratchDesign scratch;
  const Status prepared =
      PrepareOdbDesign(database, sdc_path, pin_map, &scratch);
  if (!prepared.ok()) return prepared;
  return SubstituteScratchSdcPath(
      AnalyzeCoverage(scratch.design, scenes), scratch);
}

Result<std::vector<QorResult>> TimingService::AnalyzeQor(
    const VerilogDesign& design,
    const std::vector<SceneAnalysis>& scenes) {
  if (design.verilog_path.empty() || design.top_cell.empty()) {
    return Invalid("timing analysis needs a netlist and its top cell");
  }
  const Status valid = Validate(design.sdc_path, scenes);
  if (!valid.ok()) return valid;
  std::vector<QorResult> results;
  for (const SceneAnalysis& scene : scenes) {
    Result<QorResult> result = QorScene(design, scene);
    if (!result.ok()) return result.status();
    results.push_back(std::move(result).value());
  }
  return results;
}

Result<std::vector<QorResult>> TimingService::AnalyzeQor(
    db::OdbDatabase& database, const std::string& sdc_path,
    const std::vector<SceneAnalysis>& scenes, const SdcPinMap& pin_map) {
  if (!database.has_top_block()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "timing analysis of an ODB database with no "
                         "top block");
  }
  OdbScratchDesign scratch;
  const Status prepared =
      PrepareOdbDesign(database, sdc_path, pin_map, &scratch);
  if (!prepared.ok()) return prepared;
  return SubstituteScratchSdcPath(AnalyzeQor(scratch.design, scenes),
                                  scratch);
}

Result<std::vector<CheckReportsResult>>
TimingService::AnalyzeCheckReports(
    const VerilogDesign& design,
    const std::vector<SceneAnalysis>& scenes) {
  if (design.verilog_path.empty() || design.top_cell.empty()) {
    return Invalid("timing analysis needs a netlist and its top cell");
  }
  const Status valid = Validate(design.sdc_path, scenes);
  if (!valid.ok()) return valid;
  std::vector<CheckReportsResult> results;
  for (const SceneAnalysis& scene : scenes) {
    Result<CheckReportsResult> result = CheckReportsScene(design, scene);
    if (!result.ok()) return result.status();
    results.push_back(std::move(result).value());
  }
  return results;
}

Result<std::vector<CheckReportsResult>>
TimingService::AnalyzeCheckReports(
    db::OdbDatabase& database, const std::string& sdc_path,
    const std::vector<SceneAnalysis>& scenes, const SdcPinMap& pin_map) {
  if (!database.has_top_block()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "timing analysis of an ODB database with no "
                         "top block");
  }
  OdbScratchDesign scratch;
  const Status prepared =
      PrepareOdbDesign(database, sdc_path, pin_map, &scratch);
  if (!prepared.ok()) return prepared;
  return SubstituteScratchSdcPath(
      AnalyzeCheckReports(scratch.design, scenes), scratch);
}

}  // namespace eos::analysis
