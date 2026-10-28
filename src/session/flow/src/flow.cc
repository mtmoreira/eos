// flow implementation: the timing closure loop. See the header
// for the search order, the gate, the ledger discipline and the
// fidelity rule; this file is the protocol those rules describe.
#include "eos/session/flow.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <utility>

#include "eos/eco/timing_eco.h"
#include "eos/eco/transactions.h"
#include "eos/session/reports.h"

namespace eos::session {
namespace {

using foundation::ErrorCode;

// The epsilon is float-reporting noise only, the same value the
// ECO transaction verdict uses: a difference at this scale is
// not a design change.
constexpr double kEpsilonNs = 1e-9;

const char* const kJudgedMetrics[] = {
    "worst_slack_ns",
    "total_negative_slack_ns",
    "worst_hold_slack_ns",
    "total_negative_hold_slack_ns",
};

// The electrical violating-pin counts (unit F5). The gate
// requires each count present in a scene to be zero. A scene
// that does not carry a count is not failed for its absence:
// the timing service emits all three counts on every scene it
// produces, so real closure scenes always carry them, and a
// hand-built scene without them is gated on timing alone, as
// before F5.
const char* const kElectricalCounts[] = {
    "max_transition_violations",
    "max_capacitance_violations",
    "max_fanout_violations",
};

Status Invalid(const std::string& message) {
  return Status::Error(ErrorCode::kInvalidArgument, message);
}

std::set<std::pair<std::string, std::pair<bool, bool>>> PinSet(
    const std::vector<db::OdbMasterPin>& pins) {
  std::set<std::pair<std::string, std::pair<bool, bool>>> set;
  for (const db::OdbMasterPin& pin : pins) {
    set.insert({pin.name, {pin.is_input, pin.is_output}});
  }
  return set;
}

// The first scene and metric below the gate, in words, or an
// empty string when the gate is met.
std::string FirstViolation(
    const std::vector<analysis::SceneResult>& scenes) {
  for (const analysis::SceneResult& scene : scenes) {
    for (const char* metric_name : kJudgedMetrics) {
      const auto metric = scene.metrics.find(metric_name);
      if (metric == scene.metrics.end()) {
        return "scene " + scene.scenario.name + " carries no metric " +
               metric_name;
      }
      if (metric->second.value < -kEpsilonNs) {
        return "scene " + scene.scenario.name + " metric " + metric_name +
               " is " + std::to_string(metric->second.value) +
               " ns, below the gate at 0 ns";
      }
    }
    for (const char* count_name : kElectricalCounts) {
      const auto count = scene.metrics.find(count_name);
      if (count != scene.metrics.end() && count->second.value > 0.5) {
        return "scene " + scene.scenario.name + " metric " + count_name +
               " is " + std::to_string(count->second.value) +
               " violating pins, above the gate at 0";
      }
    }
  }
  return "";
}

MetricMap Flatten(const std::vector<analysis::SceneResult>& scenes) {
  MetricMap metrics;
  for (const analysis::SceneResult& scene : scenes) {
    for (const auto& entry : scene.metrics) {
      metrics[scene.scenario.name + "/" + entry.first] =
          MetricValue{entry.second.value, entry.second.unit};
    }
  }
  return metrics;
}

struct Candidate {
  std::string label;
  std::function<Status(db::OdbDatabase&)> edit;
  // Unit 2.9: the violating endpoint this candidate aims at, or
  // empty for a whole-design fallback candidate. The footprint
  // names the design objects the edit touches, so an accepted
  // edit can invalidate rejection-memory entries whose target
  // cone it intersects.
  std::string target_endpoint;
  std::set<std::string> footprint_instances;
  std::set<std::string> footprint_nets;
};

using FamilyMap =
    std::map<std::string, std::pair<std::size_t, std::size_t>>;

// One sizing candidate for instance, stepping it one family step
// up. False when the instance's master is in no family or is
// already at its family's strongest member.
bool MakeSizingCandidate(const db::OdbInstanceInfo& instance,
                         const CloseTimingRequest& request,
                         const FamilyMap& family_of, Candidate* out) {
  const auto family = family_of.find(instance.master);
  if (family == family_of.end()) {
    return false;
  }
  const std::vector<std::string>& members =
      request.sizing_families[family->second.first];
  const std::size_t next = family->second.second + 1;
  if (next >= members.size()) {
    return false;
  }
  const std::string target = members[next];
  out->label = "size " + instance.name + " " + instance.master + " -> " +
               target;
  out->edit = [name = instance.name, target](db::OdbDatabase& design) {
    return eco::ResizeInstance(design, name, target);
  };
  out->footprint_instances = {instance.name};
  return true;
}

// The single instance driver of a net, or nullptr when the net
// does not have exactly one driver and it is an instance pin.
const db::OdbNetConnection* SingleInstanceDriver(
    const std::vector<db::OdbNetConnection>& connections) {
  const db::OdbNetConnection* driver = nullptr;
  std::size_t drivers = 0;
  for (const db::OdbNetConnection& connection : connections) {
    if (connection.is_driver) {
      driver = &connection;
      ++drivers;
    }
  }
  if (drivers != 1 || driver == nullptr || driver->is_port) {
    return nullptr;
  }
  return driver;
}

// One resynthesis candidate for net. False when the net has no
// single instance driver or its driver is a sequential master.
bool MakeResynthesisCandidate(const db::OdbDatabase& database,
                              const std::string& net,
                              const CloseTimingRequest& request,
                              Candidate* out) {
  const std::vector<db::OdbNetConnection> connections =
      database.NetConnections(net);
  const db::OdbNetConnection* driver = SingleInstanceDriver(connections);
  if (driver == nullptr) {
    return false;
  }
  db::OdbInstanceInfo driver_info;
  if (!database.FindInstance(driver->instance, &driver_info)) {
    return false;
  }
  const std::set<std::string> sequential(
      request.resynthesis.sequential_masters.begin(),
      request.resynthesis.sequential_masters.end());
  if (sequential.count(driver_info.master)) {
    return false;
  }
  eco::ResynthesisRequest resynth;
  resynth.target_net = net;
  resynth.sequential_masters = request.resynthesis.sequential_masters;
  resynth.liberty_path = request.resynthesis.liberty_path;
  resynth.scratch_dir = request.resynthesis.scratch_dir;
  out->label = "resynthesize cone at net " + net;
  out->edit = [resynth](db::OdbDatabase& design) -> Status {
    const auto outcome = eco::ResynthesizeCone(design, resynth);
    if (!outcome.ok()) {
      // A capability refusal (an unmappable cone, a name the
      // proof cannot carry) is a screening outcome, not a
      // failure of the loop: present the design unchanged, so
      // the protocol rejects the candidate as no improvement.
      // The adapter has made no edit on a Status path before
      // its checkpoint, and rolls back its own edits on the
      // paths after it, so the design is as it was.
      if (outcome.status().code() == ErrorCode::kUnsupported ||
          outcome.status().code() == ErrorCode::kInvalidArgument ||
          outcome.status().code() == ErrorCode::kNotFound) {
        return Status::Ok();
      }
      return outcome.status();
    }
    if (!outcome.value().applied) {
      // The proof did not pass; the adapter already rolled
      // the splice back. Present the unchanged design to the
      // protocol, which rejects the candidate.
      return Status::Ok();
    }
    return Status::Ok();
  };
  out->footprint_nets = {net};
  return true;
}

// One buffering candidate for net, with the request's first
// buffer master. False when the net is outside the adapter's
// scope (not exactly one instance driver, fewer than two loads,
// or a port among the loads).
bool MakeBufferCandidate(const db::OdbDatabase& database,
                         const std::string& net,
                         const CloseTimingRequest& request,
                         Candidate* out) {
  const std::string buffer_master = request.buffer_masters.front();
  const std::vector<db::OdbNetConnection> connections =
      database.NetConnections(net);
  const db::OdbNetConnection* driver = nullptr;
  std::size_t drivers = 0;
  std::size_t loads = 0;
  bool port_load = false;
  for (const db::OdbNetConnection& connection : connections) {
    if (connection.is_driver) {
      driver = &connection;
      ++drivers;
    } else {
      ++loads;
      if (connection.is_port) {
        port_load = true;
      }
    }
  }
  if (drivers != 1 || driver == nullptr || driver->is_port || loads < 2 ||
      port_load) {
    return false;
  }
  std::string suffix;
  for (int attempt = 1;; ++attempt) {
    suffix = attempt == 1 ? "_buf" : "_buf" + std::to_string(attempt);
    if (!database.HasNet(net + suffix) &&
        !database.HasInstance(net + suffix + "_inst")) {
      break;
    }
  }
  const std::string new_net = net + suffix;
  const std::string new_instance = net + suffix + "_inst";
  out->label = "buffer net " + net + " with " + buffer_master + " as " +
               new_instance;
  out->edit = [net, buffer_master, new_instance,
               new_net](db::OdbDatabase& design) {
    return eco::InsertBuffer(design, net, buffer_master, new_instance,
                             new_net);
  };
  out->footprint_instances = {new_instance};
  out->footprint_nets = {net, new_net};
  return true;
}

// Generates the whole-design fallback candidates for the design
// as it stands, in a deterministic order: sizing candidates by
// instance name, then resynthesis candidates by net name (when
// enabled), then buffering candidates by net name. Candidates
// whose label is in exclude (the targeted candidates of this
// iteration) are not generated again.
std::vector<Candidate> GenerateCandidates(
    const db::OdbDatabase& database, const CloseTimingRequest& request,
    const FamilyMap& family_of, const std::set<std::string>& exclude) {
  std::vector<Candidate> candidates;
  const auto offer = [&candidates, &exclude](Candidate&& candidate) {
    if (!exclude.count(candidate.label)) {
      candidates.push_back(std::move(candidate));
    }
  };
  std::vector<db::OdbInstanceInfo> instances = database.Instances();
  std::sort(instances.begin(), instances.end(),
            [](const db::OdbInstanceInfo& a, const db::OdbInstanceInfo& b) {
              return a.name < b.name;
            });
  for (const db::OdbInstanceInfo& instance : instances) {
    Candidate candidate;
    if (MakeSizingCandidate(instance, request, family_of, &candidate)) {
      offer(std::move(candidate));
    }
  }
  if (request.resynthesis.enabled) {
    std::vector<std::string> nets = database.NetNames();
    std::sort(nets.begin(), nets.end());
    for (const std::string& net : nets) {
      Candidate candidate;
      if (MakeResynthesisCandidate(database, net, request, &candidate)) {
        offer(std::move(candidate));
      }
    }
  }
  if (!request.buffer_masters.empty()) {
    std::vector<std::string> nets = database.NetNames();
    std::sort(nets.begin(), nets.end());
    for (const std::string& net : nets) {
      Candidate candidate;
      if (MakeBufferCandidate(database, net, request, &candidate)) {
        offer(std::move(candidate));
      }
    }
  }
  return candidates;
}

// One violating endpoint to aim at: its pin path name and its
// worst slack over every scene and check kind, in nanoseconds.
struct EndpointTarget {
  std::string pin;
  double slack_ns = 0.0;
};

// The violating endpoints of the current state, worst first:
// every endpoint the per-scene reports name, merged across
// scenes and check kinds at its worst slack, sorted by slack
// ascending with pin-name tie-breaks, so the order is
// deterministic.
std::vector<EndpointTarget> ViolatingTargets(
    const std::vector<analysis::SceneResult>& scenes) {
  std::map<std::string, double> worst;
  for (const analysis::SceneResult& scene : scenes) {
    for (const std::vector<analysis::EndpointSlack>* endpoints :
         {&scene.setup_violating_endpoints,
          &scene.hold_violating_endpoints}) {
      for (const analysis::EndpointSlack& endpoint : *endpoints) {
        const auto [it, inserted] = worst.emplace(endpoint.pin, endpoint.slack_ns);
        if (!inserted && endpoint.slack_ns < it->second) {
          it->second = endpoint.slack_ns;
        }
      }
    }
  }
  std::vector<EndpointTarget> targets;
  for (const auto& entry : worst) {
    targets.push_back(EndpointTarget{entry.first, entry.second});
  }
  std::sort(targets.begin(), targets.end(),
            [](const EndpointTarget& a, const EndpointTarget& b) {
              if (a.slack_ns != b.slack_ns) return a.slack_ns < b.slack_ns;
              return a.pin < b.pin;
            });
  return targets;
}

// The fanin cone of one endpoint, by connectivity: the instances
// timing paths to the endpoint pass through, each with its depth
// (the endpoint's own instance is depth 0, its fanin drivers one
// deeper, and so on), and every net visited on the way. Clock
// and data fanin are both walked; a port driver ends a walk.
struct FaninCone {
  std::map<std::string, std::size_t> instance_depth;
  std::set<std::string> nets;
};

// Net connectivity, indexed once per generation pass.
struct NetlistIndex {
  std::map<std::string, std::vector<db::OdbNetConnection>> connections;
  // (instance, pin) -> net; a port pin's instance is empty.
  std::map<std::pair<std::string, std::string>, std::string> net_of_pin;
};

NetlistIndex BuildIndex(const db::OdbDatabase& database) {
  NetlistIndex index;
  for (const std::string& net : database.NetNames()) {
    std::vector<db::OdbNetConnection> connections =
        database.NetConnections(net);
    for (const db::OdbNetConnection& connection : connections) {
      index.net_of_pin[{connection.instance, connection.pin}] = net;
    }
    index.connections[net] = std::move(connections);
  }
  return index;
}

// Expands the cone from its seed instances (each already recorded
// in cone at its depth) by walking input-pin nets to their
// drivers, breadth first.
void ExpandCone(const db::OdbDatabase& database, const NetlistIndex& index,
                FaninCone* cone) {
  std::vector<std::pair<std::string, std::size_t>> queue(
      cone->instance_depth.begin(), cone->instance_depth.end());
  for (std::size_t head = 0; head < queue.size(); ++head) {
    const auto [instance, depth] = queue[head];
    db::OdbInstanceInfo info;
    if (!database.FindInstance(instance, &info)) {
      continue;
    }
    for (const db::OdbMasterPin& pin : database.MasterPins(info.master)) {
      if (!pin.is_input) {
        continue;
      }
      const auto net = index.net_of_pin.find({instance, pin.name});
      if (net == index.net_of_pin.end()) {
        continue;
      }
      cone->nets.insert(net->second);
      const auto connections = index.connections.find(net->second);
      if (connections == index.connections.end()) {
        continue;
      }
      for (const db::OdbNetConnection& connection : connections->second) {
        if (!connection.is_driver || connection.is_port) {
          continue;
        }
        if (!cone->instance_depth.count(connection.instance)) {
          cone->instance_depth[connection.instance] = depth + 1;
          queue.push_back({connection.instance, depth + 1});
        }
      }
    }
  }
}

FaninCone ConeOf(const db::OdbDatabase& database, const NetlistIndex& index,
                 const std::string& endpoint_pin) {
  FaninCone cone;
  const std::size_t slash = endpoint_pin.rfind('/');
  if (slash != std::string::npos) {
    cone.instance_depth[endpoint_pin.substr(0, slash)] = 0;
    const auto net =
        index.net_of_pin.find({endpoint_pin.substr(0, slash),
                               endpoint_pin.substr(slash + 1)});
    if (net != index.net_of_pin.end()) {
      cone.nets.insert(net->second);
    }
  } else {
    // A top-level port endpoint: its cone starts at the instance
    // drivers of the port's net.
    const auto net = index.net_of_pin.find({"", endpoint_pin});
    if (net == index.net_of_pin.end()) {
      return cone;
    }
    cone.nets.insert(net->second);
    const auto connections = index.connections.find(net->second);
    if (connections != index.connections.end()) {
      for (const db::OdbNetConnection& connection : connections->second) {
        if (connection.is_driver && !connection.is_port) {
          cone.instance_depth[connection.instance] = 0;
        }
      }
    }
  }
  ExpandCone(database, index, &cone);
  return cone;
}

// The cone behind one net (the fanin of its driver instance),
// used to give a resynthesis candidate a footprint that covers
// the instances its edit may replace.
FaninCone ConeBehindNet(const db::OdbDatabase& database,
                        const NetlistIndex& index, const std::string& net) {
  FaninCone cone;
  cone.nets.insert(net);
  const auto connections = index.connections.find(net);
  if (connections != index.connections.end()) {
    const db::OdbNetConnection* driver =
        SingleInstanceDriver(connections->second);
    if (driver != nullptr) {
      cone.instance_depth[driver->instance] = 0;
    }
  }
  ExpandCone(database, index, &cone);
  return cone;
}

// Generates the targeted candidates: for each violating endpoint,
// worst first, the sizing candidates of its cone in path order
// (deepest fanin first, name order within a depth), then its
// resynthesis candidates, then its buffering candidates, nets in
// the same path order by their driver's depth. A candidate
// claimed by an earlier (worse) endpoint is not generated again
// under a later one. cones is parallel to targets.
std::vector<Candidate> GenerateTargetedCandidates(
    const db::OdbDatabase& database, const NetlistIndex& index,
    const CloseTimingRequest& request, const FamilyMap& family_of,
    const std::vector<EndpointTarget>& targets,
    const std::vector<FaninCone>& cones) {
  std::vector<Candidate> candidates;
  std::set<std::string> emitted;
  const auto offer = [&candidates, &emitted](Candidate&& candidate) {
    if (emitted.insert(candidate.label).second) {
      candidates.push_back(std::move(candidate));
    }
  };
  for (std::size_t t = 0; t < targets.size(); ++t) {
    const FaninCone& cone = cones[t];
    // Cone instances in path order.
    std::vector<std::pair<std::size_t, std::string>> by_depth;
    for (const auto& entry : cone.instance_depth) {
      by_depth.push_back({entry.second, entry.first});
    }
    std::sort(by_depth.begin(), by_depth.end(),
              [](const std::pair<std::size_t, std::string>& a,
                 const std::pair<std::size_t, std::string>& b) {
                if (a.first != b.first) return a.first > b.first;
                return a.second < b.second;
              });
    for (const auto& entry : by_depth) {
      db::OdbInstanceInfo info;
      if (!database.FindInstance(entry.second, &info)) {
        continue;
      }
      Candidate candidate;
      if (MakeSizingCandidate(info, request, family_of, &candidate)) {
        candidate.target_endpoint = targets[t].pin;
        offer(std::move(candidate));
      }
    }
    // Cone nets in path order, by the depth of the net's driver
    // in this cone (a net with no instance driver in the cone
    // sorts last, by name).
    std::vector<std::pair<std::size_t, std::string>> nets_by_depth;
    for (const std::string& net : cone.nets) {
      std::size_t depth = std::numeric_limits<std::size_t>::max();
      const auto connections = index.connections.find(net);
      if (connections != index.connections.end()) {
        const db::OdbNetConnection* driver =
            SingleInstanceDriver(connections->second);
        if (driver != nullptr) {
          const auto at = cone.instance_depth.find(driver->instance);
          if (at != cone.instance_depth.end()) {
            depth = at->second;
          }
        }
      }
      nets_by_depth.push_back({depth, net});
    }
    std::sort(nets_by_depth.begin(), nets_by_depth.end(),
              [](const std::pair<std::size_t, std::string>& a,
                 const std::pair<std::size_t, std::string>& b) {
                // Path order is deepest first, except that the
                // no-driver sentinel (the largest depth) goes
                // last, not first.
                const bool a_last =
                    a.first == std::numeric_limits<std::size_t>::max();
                const bool b_last =
                    b.first == std::numeric_limits<std::size_t>::max();
                if (a_last != b_last) return b_last;
                if (a.first != b.first) return a.first > b.first;
                return a.second < b.second;
              });
    if (request.resynthesis.enabled) {
      for (const auto& entry : nets_by_depth) {
        Candidate candidate;
        if (MakeResynthesisCandidate(database, entry.second, request,
                                     &candidate)) {
          candidate.target_endpoint = targets[t].pin;
          const FaninCone behind =
              ConeBehindNet(database, index, entry.second);
          for (const auto& behind_instance : behind.instance_depth) {
            candidate.footprint_instances.insert(behind_instance.first);
          }
          candidate.footprint_nets.insert(behind.nets.begin(),
                                          behind.nets.end());
          offer(std::move(candidate));
        }
      }
    }
    if (!request.buffer_masters.empty()) {
      for (const auto& entry : nets_by_depth) {
        Candidate candidate;
        if (MakeBufferCandidate(database, entry.second, request,
                                &candidate)) {
          candidate.target_endpoint = targets[t].pin;
          offer(std::move(candidate));
        }
      }
    }
  }
  return candidates;
}

}  // namespace

bool TimingGateMet(const std::vector<analysis::SceneResult>& scenes) {
  if (scenes.empty()) {
    return false;
  }
  return FirstViolation(scenes).empty();
}

Result<ClosureOutcome> RunTimingClosure(Project& project,
                                        db::OdbDatabase& database,
                                        const CloseTimingRequest& request) {
  if (request.scenes.empty()) {
    return Invalid("timing closure needs at least one scene");
  }
  if (request.max_iterations == 0) {
    return Invalid("timing closure needs an iteration bound of at least 1");
  }
  if (request.sdc_path.empty()) {
    return Invalid("timing closure needs an SDC file");
  }
  for (const analysis::SceneAnalysis& scene : request.scenes) {
    if (scene.scenario.parasitics == db::ParasiticsLevel::kExtracted) {
      return Invalid(
          "scene " + scene.scenario.name +
          " is at extracted parasitics; after an edit its SPEF no "
          "longer describes the design, so closure acceptance runs "
          "at estimate fidelity only (extracted acceptance after "
          "edits stays deferred until incremental extraction is "
          "qualified)");
    }
  }
  if (!database.has_top_block()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "timing closure needs a design with a top block");
  }
  // The sizing families are the caller's drive-strength knowledge;
  // validate their shape whole before any analysis runs, so a
  // malformed family cannot surface halfway through the search as
  // a stream of failed edits.
  std::map<std::string, std::pair<std::size_t, std::size_t>> family_of;
  for (std::size_t f = 0; f < request.sizing_families.size(); ++f) {
    const std::vector<std::string>& family = request.sizing_families[f];
    if (family.empty()) {
      return Invalid("sizing family " + std::to_string(f) + " is empty");
    }
    std::set<std::pair<std::string, std::pair<bool, bool>>> pins;
    for (std::size_t i = 0; i < family.size(); ++i) {
      if (!family_of.emplace(family[i], std::make_pair(f, i)).second) {
        return Invalid("master " + family[i] +
                       " appears in the sizing families more than once");
      }
      const auto member_pins = PinSet(database.MasterPins(family[i]));
      if (member_pins.empty()) {
        return Invalid("sizing family member " + family[i] +
                       " names no master with signal pins");
      }
      if (i == 0) {
        pins = member_pins;
      } else if (member_pins != pins) {
        return Invalid("sizing family member " + family[i] +
                       " does not carry the same signal pins as " +
                       family[0] + "; a sizing family swaps by pin set");
      }
    }
  }
  if (request.resynthesis.enabled) {
    if (request.resynthesis.liberty_path.empty()) {
      return Invalid(
          "resynthesis candidates need a Liberty library to map "
          "against");
    }
    if (request.resynthesis.scratch_dir.empty()) {
      return Invalid(
          "resynthesis candidates need a scratch directory for the "
          "adapter's working files");
    }
  }
  for (const std::string& master : request.buffer_masters) {
    const std::vector<db::OdbMasterPin> pins = database.MasterPins(master);
    if (pins.empty()) {
      return Invalid("buffer master " + master +
                     " names no master with signal pins");
    }
    std::size_t inputs = 0;
    std::size_t outputs = 0;
    for (const db::OdbMasterPin& pin : pins) {
      if (pin.is_input && !pin.is_output) ++inputs;
      if (pin.is_output && !pin.is_input) ++outputs;
    }
    if (inputs != 1 || outputs != 1 || pins.size() != 2) {
      return Invalid("buffer master " + master +
                     " is not one input pin and one output pin");
    }
  }

  ClosureOutcome outcome;
  outcome.parasitics_note =
      "all closure metrics are a full re-analysis of every required "
      "scene at estimate parasitics fidelity; no extracted "
      "re-analysis after edits until incremental extraction is "
      "qualified";

  analysis::TimingService timing;
  const auto initial =
      timing.Analyze(database, request.sdc_path, request.scenes);
  if (!initial.ok()) {
    return initial.status();
  }
  outcome.initial = initial.value();
  outcome.final = initial.value();

  if (TimingGateMet(outcome.final)) {
    outcome.closed = true;
    outcome.stop_reason =
        "the timing gate was already met in every required scene; "
        "no edit was needed";
  }

  std::string last_rejection;
  // Unit 2.9 screening-economics state. The rejection memory maps
  // a screened-and-rejected targeted candidate's label to the
  // state its verdict was reached in: its target endpoint, that
  // endpoint's slack at the time, and the target cone. An entry
  // is dropped the moment an accepted edit touches the cone, so
  // a surviving entry names an edit whose cone and endpoint
  // slack are both unchanged since its rejection.
  struct RejectionMemory {
    std::string target_endpoint;
    double target_slack_ns = 0.0;
    std::set<std::string> cone_instances;
    std::set<std::string> cone_nets;
  };
  std::map<std::string, RejectionMemory> rejection_memory;
  std::vector<Candidate> previous_targeted;
  std::set<std::string> previous_screened;
  while (!outcome.closed && outcome.steps.size() < request.max_iterations) {
    const std::vector<EndpointTarget> targets =
        ViolatingTargets(outcome.final);
    const NetlistIndex index = BuildIndex(database);
    std::vector<FaninCone> cones;
    cones.reserve(targets.size());
    for (const EndpointTarget& target : targets) {
      cones.push_back(ConeOf(database, index, target.pin));
    }
    std::vector<Candidate> targeted = GenerateTargetedCandidates(
        database, index, request, family_of, targets, cones);
    // Stale-target accounting: a candidate of the previous
    // iteration that was never screened, whose endpoint no
    // longer violates and which this iteration does not generate
    // again, is skipped stale. A candidate whose endpoint still
    // violates is regenerated above and never counted here.
    if (!previous_targeted.empty()) {
      std::set<std::string> current_pins;
      for (const EndpointTarget& target : targets) {
        current_pins.insert(target.pin);
      }
      std::set<std::string> regenerated;
      for (const Candidate& candidate : targeted) {
        regenerated.insert(candidate.label);
      }
      for (const Candidate& candidate : previous_targeted) {
        if (previous_screened.count(candidate.label)) continue;
        if (current_pins.count(candidate.target_endpoint)) continue;
        if (regenerated.count(candidate.label)) continue;
        ++outcome.candidates_skipped_stale;
      }
    }
    previous_targeted = targeted;
    previous_screened.clear();
    // Rejection-memory filter over the targeted candidates.
    std::vector<Candidate> candidates;
    std::set<std::string> targeted_labels;
    for (const Candidate& candidate : targeted) {
      targeted_labels.insert(candidate.label);
    }
    for (Candidate& candidate : targeted) {
      const auto memory = rejection_memory.find(candidate.label);
      if (memory != rejection_memory.end() &&
          memory->second.target_endpoint == candidate.target_endpoint) {
        double current_slack = 0.0;
        bool target_found = false;
        for (const EndpointTarget& target : targets) {
          if (target.pin == candidate.target_endpoint) {
            current_slack = target.slack_ns;
            target_found = true;
            break;
          }
        }
        if (target_found &&
            std::abs(current_slack - memory->second.target_slack_ns) <=
                kEpsilonNs) {
          ++outcome.candidates_skipped_memory;
          continue;
        }
        rejection_memory.erase(memory);
      }
      candidates.push_back(std::move(candidate));
    }
    // The whole-design fallback, after the targeted candidates.
    std::vector<Candidate> fallback =
        GenerateCandidates(database, request, family_of, targeted_labels);
    for (Candidate& candidate : fallback) {
      // A fallback resynthesis candidate gets the same full cone
      // footprint as a targeted one, for memory invalidation.
      if (candidate.footprint_instances.empty() &&
          candidate.label.rfind("resynthesize cone at net ", 0) == 0) {
        const std::string net = candidate.label.substr(
            std::string("resynthesize cone at net ").size());
        const FaninCone behind = ConeBehindNet(database, index, net);
        for (const auto& behind_instance : behind.instance_depth) {
          candidate.footprint_instances.insert(behind_instance.first);
        }
        candidate.footprint_nets.insert(behind.nets.begin(),
                                        behind.nets.end());
      }
      candidates.push_back(std::move(candidate));
    }
    if (candidates.empty()) {
      if (!targeted.empty()) {
        outcome.stop_reason =
            "every candidate for the current design was already "
            "screened and rejected against an unchanged target "
            "cone in this run, and the gate is unmet: " +
            FirstViolation(outcome.final);
      } else {
        outcome.stop_reason =
            "no sizing, resynthesis or buffering candidate can be "
            "generated for the current design, and the gate is "
            "unmet: " +
            FirstViolation(outcome.final);
      }
      break;
    }
    bool accepted = false;
    for (const Candidate& candidate : candidates) {
      ++outcome.candidates_tried;
      previous_screened.insert(candidate.label);
      eco::EcoRequest eco_request;
      eco_request.label = candidate.label;
      eco_request.sdc_path = request.sdc_path;
      eco_request.scenes = request.scenes;
      // The loop already holds a full analysis of the design as
      // it stands (the initial analysis, or the accepted step's
      // after state); hand it to the transaction so the screening
      // pays one re-analysis, not two.
      eco_request.known_before = outcome.final;
      const auto screened =
          eco::RunEcoTransaction(database, eco_request, candidate.edit);
      if (!screened.ok()) {
        return screened.status();
      }
      if (!screened.value().committed) {
        ++outcome.candidates_rejected;
        last_rejection = screened.value().reason;
        if (!candidate.target_endpoint.empty()) {
          RejectionMemory entry;
          entry.target_endpoint = candidate.target_endpoint;
          for (const EndpointTarget& target : targets) {
            if (target.pin == candidate.target_endpoint) {
              entry.target_slack_ns = target.slack_ns;
              break;
            }
          }
          for (std::size_t t = 0; t < targets.size(); ++t) {
            if (targets[t].pin == candidate.target_endpoint) {
              for (const auto& cone_instance : cones[t].instance_depth) {
                entry.cone_instances.insert(cone_instance.first);
              }
              entry.cone_nets = cones[t].nets;
              break;
            }
          }
          rejection_memory[candidate.label] = std::move(entry);
        }
        continue;
      }
      // The accepted edit invalidates every memory entry whose
      // target cone it touches: those verdicts were reached
      // against a cone that has now changed.
      for (auto memory = rejection_memory.begin();
           memory != rejection_memory.end();) {
        bool touched = false;
        for (const std::string& instance : candidate.footprint_instances) {
          if (memory->second.cone_instances.count(instance)) {
            touched = true;
            break;
          }
        }
        if (!touched) {
          for (const std::string& net : candidate.footprint_nets) {
            if (memory->second.cone_nets.count(net)) {
              touched = true;
              break;
            }
          }
        }
        if (touched) {
          memory = rejection_memory.erase(memory);
        } else {
          ++memory;
        }
      }
      // The accepted iteration becomes one ledger transaction,
      // recorded with the ECO outcome's own before and after
      // metrics. The ECO already committed the design edit; a
      // ledger failure here is a bookkeeping failure on a design
      // that stands, and it is returned as such.
      TransactionSpec spec;
      spec.input_revision = project.ledger.current_revision();
      spec.label = "close_timing: " + candidate.label;
      spec.tool_build = "eos-phase2";
      spec.before_metrics = Flatten(screened.value().before);
      spec.after_metrics = Flatten(screened.value().after);
      const Status begun = project.ledger.BeginTransaction(spec);
      if (!begun.ok()) {
        return begun;
      }
      const auto committed = project.ledger.CommitPending(ProofStatus::kUnknown);
      if (!committed.ok()) {
        return committed.status();
      }
      ClosureStep step;
      step.label = candidate.label;
      step.before = screened.value().before;
      step.after = screened.value().after;
      outcome.steps.push_back(std::move(step));
      outcome.final = screened.value().after;
      accepted = true;
      break;
    }
    if (!accepted) {
      outcome.stop_reason =
          "every candidate was rejected by the all-scene rule; "
          "last rejection: " +
          last_rejection + "; the gate is unmet: " +
          FirstViolation(outcome.final);
      break;
    }
    if (TimingGateMet(outcome.final)) {
      outcome.closed = true;
      outcome.stop_reason =
          "the timing gate is met in every required scene after " +
          std::to_string(outcome.steps.size()) + " accepted iteration(s)";
    }
  }
  if (!outcome.closed && outcome.stop_reason.empty()) {
    outcome.stop_reason =
        "the iteration bound of " +
        std::to_string(request.max_iterations) +
        " accepted iteration(s) was reached with the gate unmet: " +
        FirstViolation(outcome.final);
  }

  ReportRegistry reports;
  const auto registered = reports.RegisterReport(
      project, "timing_closure",
      outcome.closed ? ProofStatus::kPass : ProofStatus::kUnknown, "NLDM",
      "eos-phase2");
  if (!registered.ok()) {
    return registered.status();
  }
  return outcome;
}

}  // namespace eos::session
