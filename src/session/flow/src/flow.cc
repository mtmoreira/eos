// flow implementation: the timing closure loop. See the header
// for the search order, the gate, the ledger discipline and the
// fidelity rule; this file is the protocol those rules describe.
#include "eos/session/flow.h"

#include <algorithm>
#include <functional>
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
};

// Generates the candidates for the design as it stands, in a
// deterministic order: sizing candidates by instance name, then
// resynthesis candidates by net name (when enabled), then
// buffering candidates by net name.
std::vector<Candidate> GenerateCandidates(
    const db::OdbDatabase& database, const CloseTimingRequest& request,
    const std::map<std::string, std::pair<std::size_t, std::size_t>>&
        family_of) {
  std::vector<Candidate> candidates;
  std::vector<db::OdbInstanceInfo> instances = database.Instances();
  std::sort(instances.begin(), instances.end(),
            [](const db::OdbInstanceInfo& a, const db::OdbInstanceInfo& b) {
              return a.name < b.name;
            });
  for (const db::OdbInstanceInfo& instance : instances) {
    const auto family = family_of.find(instance.master);
    if (family == family_of.end()) {
      continue;
    }
    const std::vector<std::string>& members =
        request.sizing_families[family->second.first];
    const std::size_t next = family->second.second + 1;
    if (next >= members.size()) {
      continue;
    }
    const std::string target = members[next];
    Candidate candidate;
    candidate.label = "size " + instance.name + " " + instance.master +
                      " -> " + target;
    candidate.edit = [name = instance.name, target](db::OdbDatabase& design) {
      return eco::ResizeInstance(design, name, target);
    };
    candidates.push_back(std::move(candidate));
  }
  if (request.resynthesis.enabled) {
    const std::set<std::string> sequential(
        request.resynthesis.sequential_masters.begin(),
        request.resynthesis.sequential_masters.end());
    std::vector<std::string> nets = database.NetNames();
    std::sort(nets.begin(), nets.end());
    for (const std::string& net : nets) {
      const std::vector<db::OdbNetConnection> connections =
          database.NetConnections(net);
      const db::OdbNetConnection* driver = nullptr;
      std::size_t drivers = 0;
      for (const db::OdbNetConnection& connection : connections) {
        if (connection.is_driver) {
          driver = &connection;
          ++drivers;
        }
      }
      if (drivers != 1 || driver == nullptr || driver->is_port) {
        continue;
      }
      db::OdbInstanceInfo driver_info;
      if (!database.FindInstance(driver->instance, &driver_info)) {
        continue;
      }
      if (sequential.count(driver_info.master)) {
        continue;
      }
      eco::ResynthesisRequest resynth;
      resynth.target_net = net;
      resynth.sequential_masters = request.resynthesis.sequential_masters;
      resynth.liberty_path = request.resynthesis.liberty_path;
      resynth.scratch_dir = request.resynthesis.scratch_dir;
      Candidate candidate;
      candidate.label = "resynthesize cone at net " + net;
      candidate.edit = [resynth](db::OdbDatabase& design) -> Status {
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
      candidates.push_back(std::move(candidate));
    }
  }
  if (!request.buffer_masters.empty()) {
    const std::string buffer_master = request.buffer_masters.front();
    std::vector<std::string> nets = database.NetNames();
    std::sort(nets.begin(), nets.end());
    for (const std::string& net : nets) {
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
      if (drivers != 1 || driver == nullptr || driver->is_port ||
          loads < 2 || port_load) {
        continue;
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
      Candidate candidate;
      candidate.label = "buffer net " + net + " with " + buffer_master +
                        " as " + new_instance;
      candidate.edit = [net, buffer_master, new_instance,
                        new_net](db::OdbDatabase& design) {
        return eco::InsertBuffer(design, net, buffer_master, new_instance,
                                 new_net);
      };
      candidates.push_back(std::move(candidate));
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
  while (!outcome.closed && outcome.steps.size() < request.max_iterations) {
    const std::vector<Candidate> candidates =
        GenerateCandidates(database, request, family_of);
    if (candidates.empty()) {
      outcome.stop_reason =
          "no sizing, resynthesis or buffering candidate can be "
          "generated for the current design, and the gate is "
          "unmet: " +
          FirstViolation(outcome.final);
      break;
    }
    bool accepted = false;
    for (const Candidate& candidate : candidates) {
      ++outcome.candidates_tried;
      eco::EcoRequest eco_request;
      eco_request.label = candidate.label;
      eco_request.sdc_path = request.sdc_path;
      eco_request.scenes = request.scenes;
      const auto screened =
          eco::RunEcoTransaction(database, eco_request, candidate.edit);
      if (!screened.ok()) {
        return screened.status();
      }
      if (!screened.value().committed) {
        ++outcome.candidates_rejected;
        last_rejection = screened.value().reason;
        continue;
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
