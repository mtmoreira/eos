// scenarios: scenario identity (L2 design-data).
#include "eos/db/scenarios.h"

#include <utility>

namespace eos::db {

Result<ScenarioId> ScenarioSet::Create(const std::string& name,
                                       const std::string& mode,
                                       const std::string& corner,
                                       ParasiticsLevel parasitics) {
  if (FindByName(name) != nullptr) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "duplicate scenario name: " + name);
  }
  if (FindByTriple(mode, corner, parasitics) != nullptr) {
    return Status::Error(
        ErrorCode::kInvalidArgument,
        "a scenario already owns this mode, corner and parasitics triple");
  }
  Scenario scenario;
  scenario.id = next_id_++;
  scenario.name = name;
  scenario.mode = mode;
  scenario.corner = corner;
  scenario.parasitics = parasitics;
  scenario.active = true;
  scenarios_.push_back(std::move(scenario));
  return scenarios_.back().id;
}

const Scenario* ScenarioSet::Find(ScenarioId id) const {
  for (const Scenario& scenario : scenarios_) {
    if (scenario.id == id) {
      return &scenario;
    }
  }
  return nullptr;
}

const Scenario* ScenarioSet::FindByName(const std::string& name) const {
  for (const Scenario& scenario : scenarios_) {
    if (scenario.name == name) {
      return &scenario;
    }
  }
  return nullptr;
}

const Scenario* ScenarioSet::FindByTriple(const std::string& mode,
                                          const std::string& corner,
                                          ParasiticsLevel parasitics) const {
  for (const Scenario& scenario : scenarios_) {
    if (scenario.mode == mode && scenario.corner == corner &&
        scenario.parasitics == parasitics) {
      return &scenario;
    }
  }
  return nullptr;
}

Status ScenarioSet::SetActive(ScenarioId id, bool active) {
  for (Scenario& scenario : scenarios_) {
    if (scenario.id == id) {
      scenario.active = active;
      return Status::Ok();
    }
  }
  return Status::Error(ErrorCode::kNotFound, "unknown scenario");
}

std::vector<const Scenario*> ScenarioSet::ActiveScenarios() const {
  std::vector<const Scenario*> active;
  for (const Scenario& scenario : scenarios_) {
    if (scenario.active) {
      active.push_back(&scenario);
    }
  }
  return active;
}

ScenarioState ScenarioSet::ExportState() const {
  ScenarioState state;
  state.scenarios = scenarios_;
  state.next_id = next_id_;
  return state;
}

Result<ScenarioSet> ScenarioSet::FromState(const ScenarioState& state) {
  ScenarioSet set;
  ScenarioId max_id = 0;
  for (const Scenario& scenario : state.scenarios) {
    if (scenario.id == 0 || set.Find(scenario.id) != nullptr ||
        set.FindByName(scenario.name) != nullptr ||
        set.FindByTriple(scenario.mode, scenario.corner,
                         scenario.parasitics) != nullptr) {
      return Status::Error(
          ErrorCode::kInvalidArgument,
          "scenario state: duplicate or invalid scenario");
    }
    set.scenarios_.push_back(scenario);
    if (scenario.id > max_id) {
      max_id = scenario.id;
    }
  }
  if (state.next_id <= max_id) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "scenario state: next_id would reuse an id");
  }
  set.next_id_ = state.next_id;
  return set;
}

}  // namespace eos::db
