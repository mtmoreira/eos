// scenarios: scenario identity (L2 design-data).
//
// A scenario is one analysis context: a mode, a corner, and the
// parasitics fidelity the analysis runs at. That triple IS the
// scenario's identity; the name is only a handle for it. Two scenarios
// with the same triple would be the same analysis wearing two names,
// so the set refuses to hold both.
//
// Reports and timing results bind to scenarios by this identity, which
// is what lets a later layer say "this result was computed under
// exactly these conditions" and check it.
//
// This header is a stable surface.
#ifndef EOS_DB_SCENARIOS_H_
#define EOS_DB_SCENARIOS_H_

#include <cstdint>
#include <string>
#include <vector>

#include "eos/foundation/errors.h"

namespace eos::db {

using foundation::ErrorCode;
using foundation::Result;
using foundation::Status;

// The named fidelity level parasitics are taken at, per the Revised
// Plan's ParasiticsProvider: wire estimates before extraction exists,
// extracted values after.
enum class ParasiticsLevel : std::int32_t {
  kEstimate = 0,
  kExtracted = 1,
};

using ScenarioId = std::uint32_t;

struct Scenario {
  ScenarioId id = 0;
  std::string name;
  std::string mode;
  std::string corner;
  ParasiticsLevel parasitics = ParasiticsLevel::kEstimate;
  bool active = true;
};

// The complete serializable state of a ScenarioSet.
struct ScenarioState {
  std::vector<Scenario> scenarios;
  ScenarioId next_id = 1;
};

class ScenarioSet {
 public:
  // Creates a scenario, active by default (defining a scenario means
  // intending to analyze under it). Fails with kInvalidArgument when
  // the name is taken or when another scenario already owns the
  // (mode, corner, parasitics) triple.
  Result<ScenarioId> Create(const std::string& name, const std::string& mode,
                            const std::string& corner,
                            ParasiticsLevel parasitics);

  const Scenario* Find(ScenarioId id) const;
  const Scenario* FindByName(const std::string& name) const;
  const Scenario* FindByTriple(const std::string& mode,
                               const std::string& corner,
                               ParasiticsLevel parasitics) const;

  Status SetActive(ScenarioId id, bool active);

  // The active scenarios, in creation order. This list is what a
  // report binding snapshots when it records the scenario set it was
  // computed under.
  std::vector<const Scenario*> ActiveScenarios() const;

  const std::vector<Scenario>& all() const { return scenarios_; }
  std::size_t size() const { return scenarios_.size(); }

  // Checkpoint support: ExportState snapshots the set; FromState
  // validates it (nonzero unique ids, unique names, unique triples,
  // next_id ahead of every id) and only then builds.
  ScenarioState ExportState() const;
  static Result<ScenarioSet> FromState(const ScenarioState& state);

 private:
  ScenarioId next_id_ = 1;
  std::vector<Scenario> scenarios_;
};

}  // namespace eos::db

#endif  // EOS_DB_SCENARIOS_H_
