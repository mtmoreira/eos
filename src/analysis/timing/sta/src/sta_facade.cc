// sta facade implementation: the only EOS-authored translation unit
// in eda_analysis_sta. Everything upstream of this file is imported
// code (see PROVENANCE.md); this file translates between the EOS
// surface in include/eos/analysis/sta.h and the upstream OpenSTA
// API, and converts upstream failures (Tcl errors and exceptions)
// into EOS Status values, so no upstream type or exception crosses
// the boundary.
//
// File loading runs through the upstream Tcl commands
// (read_liberty, read_verilog, link_design, read_sdc, read_spef) on
// an interpreter owned by this engine: several of those operations
// exist upstream only as Tcl commands (SDC is defined by them), and
// they execute in process against the imported code, never in an
// external process. Slack queries use the upstream C++ API
// directly. OpenSTA delays are seconds internally (see the Units
// documentation in the imported include/sta/Units.hh); the facade
// converts through the loaded time unit: the raw value is expressed
// in the library time unit (the Unit::staToUser division by the unit
// scale) and then scaled to nanoseconds by the unit's own scale,
// which is exact for any Liberty time unit.
#include "eos/analysis/sta.h"

#include <tcl.h>

#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Clock.hh"
#include "ClkNetwork.hh"
#include "DataCheck.hh"
#include "Graph.hh"
#include "GraphDelayCalc.hh"
#include "Fuzzy.hh"
#include "Liberty.hh"
#include "MinMax.hh"
#include "Network.hh"
#include "Path.hh"
#include "PathEnd.hh"
#include "PathExpanded.hh"
#include "PathGroup.hh"
#include "PortDelay.hh"
#include "Sdc.hh"
#include "Search.hh"
#include "Sta.hh"
#include "StaMain.hh"
#include "TimingRole.hh"
#include "Transition.hh"
#include "Units.hh"
#include "VisitPathEnds.hh"
#include "search/CheckMinPeriods.hh"
#include "search/CheckMinPulseWidths.hh"
#include "search/GatedClk.hh"

// Defined by the SWIG wrapper generated from src/app/StaApp.i.
extern "C" {
extern int Sta_Init(Tcl_Interp *interp);
}

namespace sta {
// Defined by the generated StaTclInitVar.cc (see CMakeLists.txt).
extern const char *tcl_inits[];
}  // namespace sta

namespace eos::analysis {
namespace {

using foundation::ErrorCode;

// Upstream Sta is a global singleton (Sta::setSta). Guard it so a
// second live engine fails cleanly instead of corrupting the first.
std::atomic<bool> g_engine_alive{false};

// Tcl-list-safe quoting for one word: brace-quote when the word has
// no braces or backslashes, otherwise backslash-escape the special
// characters. File paths in tests and flows are plain, but a path
// with spaces must still arrive as a single argument.
std::string TclQuote(const std::string& word) {
  if (word.find_first_of("{}[]\"\\$; \t\n") == std::string::npos) {
    return word;
  }
  if (word.find_first_of("{}\\") == std::string::npos) {
    return "{" + word + "}";
  }
  std::string out;
  for (const char c : word) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '{' || c == '}' ||
        c == '[' || c == ']' || c == '"' || c == '\\' || c == '$' ||
        c == ';') {
      out += '\\';
    }
    out += c;
  }
  return out;
}

// Collects the worst constrained path end of one check kind seen
// while visiting one endpoint vertex, mirroring the upstream
// endpoint-slack visitors (EndpointPathEndVisitor in Sta.cc and
// FindEndSlackVisitor in Search.cc): unconstrained ends carry no
// slack and are skipped. The PathEnd object itself is only valid
// during visit(), so the visitor keeps the slack and the end's
// Path, which the search owns and keeps alive.
class WorstEndVisitor : public sta::PathEndVisitor {
 public:
  WorstEndVisitor(const sta::MinMax* min_max, const sta::StaState* sta)
      : min_max_(min_max), sta_(sta) {}

  sta::PathEndVisitor* copy() const override {
    return new WorstEndVisitor(*this);
  }

  void visit(sta::PathEnd* path_end) override {
    if (path_end->isUnconstrained()) return;
    if (path_end->minMax(sta_) != min_max_) return;
    const sta::Slack slack = path_end->slack(sta_);
    if (sta::delayInf(slack, sta_)) return;
    if (!found_ || sta::delayLess(slack, slack_, sta_)) {
      found_ = true;
      slack_ = slack;
      worst_path_ = path_end->path();
    }
  }

  bool found() const { return found_; }
  sta::Slack slack() const { return slack_; }
  const sta::Path* worst_path() const { return worst_path_; }

 private:
  const sta::MinMax* min_max_;
  const sta::StaState* sta_;
  bool found_ = false;
  sta::Slack slack_ = 0.0F;
  const sta::Path* worst_path_ = nullptr;
};

// Folds one upstream check role into the coverage check type
// (Immediate unit 7). Latch and non-sequential checks are the
// same check type as the setup/hold check they constrain; a data
// check is the no-change type whatever its min/max; a path-delay
// end already reports setup or hold through checkRole. Roles that
// are not timing checks (combinational, clock, ...) fold to
// null and are never coverage rows.
const char* FoldCheckRole(const sta::TimingRole* role) {
  if (role == sta::TimingRole::setup() ||
      role == sta::TimingRole::latchSetup() ||
      role == sta::TimingRole::nonSeqSetup()) {
    return "setup";
  }
  if (role == sta::TimingRole::hold() ||
      role == sta::TimingRole::latchHold() ||
      role == sta::TimingRole::nonSeqHold()) {
    return "hold";
  }
  if (role == sta::TimingRole::recovery()) return "recovery";
  if (role == sta::TimingRole::removal()) return "removal";
  if (role == sta::TimingRole::outputSetup()) return "out_setup";
  if (role == sta::TimingRole::outputHold()) return "out_hold";
  if (role == sta::TimingRole::gatedClockSetup()) {
    return "clock_gating_setup";
  }
  if (role == sta::TimingRole::gatedClockHold()) {
    return "clock_gating_hold";
  }
  if (role == sta::TimingRole::dataCheckSetup() ||
      role == sta::TimingRole::dataCheckHold() ||
      role == sta::TimingRole::nochange()) {
    return "no-change";
  }
  return nullptr;
}

// Collects the (pin, check type) pairs with a constrained path
// end at the visited vertices: the tested set of the coverage
// report. The result set is shared with copies, so the visitor
// keeps working whichever copy the visit machinery uses. Only
// constrained ends count: an unconstrained end is the engine
// saying the check exists but no clocked path exercises it.
class CoverageVisitor : public sta::PathEndVisitor {
 public:
  using TestedSet = std::set<std::pair<std::string, std::string>>;
  CoverageVisitor(const sta::StaState* sta,
                  std::shared_ptr<TestedSet> tested)
      : sta_(sta), tested_(std::move(tested)) {}

  sta::PathEndVisitor* copy() const override {
    return new CoverageVisitor(*this);
  }

  void visit(sta::PathEnd* path_end) override {
    if (path_end->isUnconstrained()) return;
    const char* type = FoldCheckRole(path_end->checkRole(sta_));
    if (type == nullptr) return;
    sta::Vertex* vertex = path_end->vertex(sta_);
    if (vertex == nullptr) return;
    tested_->insert({sta_->network()->pathName(vertex->pin()), type});
  }

 private:
  const sta::StaState* sta_;
  std::shared_ptr<TestedSet> tested_;
};

}  // namespace

struct StaEngine::Impl {
  Tcl_Interp* interp = nullptr;
  sta::Sta* sta = nullptr;
  bool owns_singleton = false;
  bool linked = false;
  bool constrained = false;

  Impl() {
    bool expected = false;
    if (!g_engine_alive.compare_exchange_strong(expected, true)) {
      // Another engine owns the upstream singleton. Leave sta null;
      // every operation then fails with kInvalidState.
      return;
    }
    owns_singleton = true;
    sta::initSta();
    interp = Tcl_CreateInterp();
    Tcl_Init(interp);
    sta = new sta::Sta;
    sta::Sta::setSta(sta);
    sta->makeComponents();
    sta->setTclInterp(interp);
    Sta_Init(interp);
    sta::evalTclInit(interp, sta::tcl_inits);
    Tcl_Eval(interp, "init_sta_cmds");
  }

  ~Impl() {
    if (sta != nullptr) {
      sta::Sta::setSta(nullptr);
      delete sta;
    }
    if (interp != nullptr) {
      Tcl_DeleteInterp(interp);
    }
    if (owns_singleton) {
      g_engine_alive.store(false);
    }
  }

  Status Eval(const std::string& command, const std::string& what,
              const std::string& path) {
    if (sta == nullptr || interp == nullptr) {
      return Status::Error(
          ErrorCode::kInvalidState,
          "another StaEngine is alive; upstream STA is a singleton: " +
              path);
    }
    try {
      if (Tcl_Eval(interp, command.c_str()) != TCL_OK) {
        const char* result = Tcl_GetStringResult(interp);
        return Status::Error(ErrorCode::kIoError,
                             what + ": " + path + ": " +
                                 (result != nullptr ? result : ""));
      }
      return Status::Ok();
    } catch (const std::exception& ex) {
      return Status::Error(ErrorCode::kIoError,
                           what + ": " + path + ": " + ex.what());
    }
  }

  Status Run(const char* verb, const std::string& arg,
             const std::string& what) {
    return Eval(std::string(verb) + " " + TclQuote(arg), what, arg);
  }

  // Converts an upstream delay (seconds internally) to
  // nanoseconds, through the loaded time unit exactly as SlackNs
  // always has: the raw value is expressed in the library time
  // unit by dividing by the unit scale, then scaled back by the
  // same scale and 1e9 ns per second.
  double ToNs(sta::Slack slack) const {
    const sta::Unit* unit = sta->units()->timeUnit();
    const double in_unit =
        static_cast<double>(slack) / static_cast<double>(unit->scale());
    return in_unit * static_cast<double>(unit->scale()) * 1e9;
  }

  // Converts an upstream capacitance (farads internally, the SI
  // unit the analyzer stores) to picofarads.
  double ToPf(double capacitance) const { return capacitance * 1e12; }

  // The three electrical checks (unit F5). Each asks the
  // upstream analyzer for the worst checked pin (smallest limit
  // slack) and for the violating-pin count, and converts the
  // values into the facade's documented unit. A null worst pin
  // means no pin carries a limit for the check: the result then
  // reports has_limit false with zero value and count, per the
  // header. The upstream violation-count calls use the
  // violators form of the check, which collects every violating
  // pin (the count argument of that form is not a cap), so the
  // count is the full count, not a truncated one.
  Result<ElectricalCheck> MaxTransition() {
    const Status ready = Ready();
    if (!ready.ok()) return ready;
    try {
      const sta::Pin* pin = nullptr;
      sta::Slew slew = 0.0F;
      float slack = 0.0F;
      float limit = 0.0F;
      sta->maxSlewCheck(pin, slew, slack, limit);
      ElectricalCheck check;
      check.violation_count = sta->maxSlewViolationCount();
      if (pin != nullptr) {
        check.has_limit = true;
        check.worst_pin = sta->network()->pathName(pin);
        check.worst_value = ToNs(slew);
        check.worst_limit = ToNs(limit);
        check.worst_slack = ToNs(slack);
      }
      return check;
    } catch (const std::exception& ex) {
      return Status::Error(ErrorCode::kIoError, ex.what());
    }
  }

  Result<ElectricalCheck> MaxCapacitance() {
    const Status ready = Ready();
    if (!ready.ok()) return ready;
    try {
      const sta::Pin* pin = nullptr;
      float capacitance = 0.0F;
      float slack = 0.0F;
      float limit = 0.0F;
      sta->maxCapacitanceCheck(pin, capacitance, slack, limit);
      ElectricalCheck check;
      check.violation_count = sta->maxCapacitanceViolationCount();
      if (pin != nullptr) {
        check.has_limit = true;
        check.worst_pin = sta->network()->pathName(pin);
        check.worst_value = ToPf(capacitance);
        check.worst_limit = ToPf(limit);
        check.worst_slack = ToPf(slack);
      }
      return check;
    } catch (const std::exception& ex) {
      return Status::Error(ErrorCode::kIoError, ex.what());
    }
  }

  Result<ElectricalCheck> MaxFanout() {
    const Status ready = Ready();
    if (!ready.ok()) return ready;
    try {
      const sta::Pin* pin = nullptr;
      float fanout = 0.0F;
      float limit = 0.0F;
      float slack = 0.0F;
      const sta::Mode* mode = nullptr;
      sta->maxFanoutMinSlackPin(sta->modes(), pin, fanout, limit, slack,
                                mode);
      ElectricalCheck check;
      check.violation_count =
          sta->fanoutViolationCount(sta::MinMax::max(), sta->modes());
      if (pin != nullptr) {
        check.has_limit = true;
        check.worst_pin = sta->network()->pathName(pin);
        check.worst_value = static_cast<double>(fanout);
        check.worst_limit = static_cast<double>(limit);
        check.worst_slack = static_cast<double>(slack);
      }
      return check;
    } catch (const std::exception& ex) {
      return Status::Error(ErrorCode::kIoError, ex.what());
    }
  }

  // Visits every pin of the design once, in the same order the
  // upstream bulk electrical checks use: the pins of every leaf
  // instance, then the top instance's pins (the top-level ports).
  // The violator enumerations below replicate those bulk checks
  // pin by pin through the public per-pin API, because the bulk
  // check objects themselves are private members of Sta.
  template <typename Fn>
  void VisitAllPins(Fn&& visit) {
    sta::Network* network = sta->network();
    sta::LeafInstanceIterator* inst_iter =
        network->leafInstanceIterator();
    while (inst_iter->hasNext()) {
      const sta::Instance* inst = inst_iter->next();
      sta::InstancePinIterator* pin_iter = network->pinIterator(inst);
      while (pin_iter->hasNext()) visit(pin_iter->next());
      delete pin_iter;
    }
    delete inst_iter;
    sta::InstancePinIterator* top_iter =
        network->pinIterator(network->topInstance());
    while (top_iter->hasNext()) visit(top_iter->next());
    delete top_iter;
  }

  // Worst slack first, ties by pin path name, so the order is
  // deterministic across runs.
  static void SortViolators(std::vector<ElectricalViolation>& violators) {
    std::sort(violators.begin(), violators.end(),
              [](const ElectricalViolation& a,
                 const ElectricalViolation& b) {
                if (a.slack != b.slack) return a.slack < b.slack;
                return a.pin < b.pin;
              });
  }

  // The three violator enumerations (Immediate unit 6). Each
  // mirrors the upstream bulk check's violators form: the per-pin
  // call, the filter that decides a pin violates, and the pin
  // iteration above. The preamble calls are required before the
  // per-pin calls, exactly as the bulk forms require them.
  Result<std::vector<ElectricalViolation>> MaxTransitionViolators() {
    const Status ready = Ready();
    if (!ready.ok()) return ready;
    try {
      sta->checkSlewsPreamble();
      std::vector<ElectricalViolation> violators;
      VisitAllPins([this, &violators](const sta::Pin* pin) {
        sta::Slew slew = 0.0F;
        float limit = 0.0F;
        float slack = 0.0F;
        const sta::RiseFall* rf = nullptr;
        const sta::Scene* scene = nullptr;
        sta->checkSlew(pin, sta->scenes(), sta::MinMax::max(), true, slew,
                       limit, slack, rf, scene);
        // The bulk check keeps exactly the pins whose limit scene
        // exists and whose slack is negative, with a plain float
        // comparison (CheckSlews::checkPin).
        if (scene != nullptr && slack < 0.0F) {
          violators.push_back(ElectricalViolation{
              sta->network()->pathName(pin), ToNs(slew), ToNs(limit),
              ToNs(slack)});
        }
      });
      SortViolators(violators);
      return violators;
    } catch (const std::exception& ex) {
      return Status::Error(ErrorCode::kIoError, ex.what());
    }
  }

  Result<std::vector<ElectricalViolation>> MaxCapacitanceViolators() {
    const Status ready = Ready();
    if (!ready.ok()) return ready;
    try {
      sta->checkCapacitancesPreamble(sta->scenes());
      std::vector<ElectricalViolation> violators;
      VisitAllPins([this, &violators](const sta::Pin* pin) {
        float capacitance = 0.0F;
        float limit = 0.0F;
        float slack = 0.0F;
        const sta::RiseFall* rf = nullptr;
        const sta::Scene* scene = nullptr;
        sta->checkCapacitance(pin, sta->scenes(), sta::MinMax::max(),
                              capacitance, limit, slack, rf, scene);
        // The bulk check keeps the minimum-slack check per pin
        // only when its slack is fuzzily negative
        // (CheckCapacitances::check with violators true).
        if (scene != nullptr && sta::fuzzyLess(slack, 0.0F)) {
          violators.push_back(ElectricalViolation{
              sta->network()->pathName(pin), ToPf(capacitance),
              ToPf(limit), ToPf(slack)});
        }
      });
      SortViolators(violators);
      return violators;
    } catch (const std::exception& ex) {
      return Status::Error(ErrorCode::kIoError, ex.what());
    }
  }

  Result<std::vector<ElectricalViolation>> MaxFanoutViolators() {
    const Status ready = Ready();
    if (!ready.ok()) return ready;
    try {
      sta->checkFanoutPreamble();
      std::vector<ElectricalViolation> violators;
      VisitAllPins([this, &violators](const sta::Pin* pin) {
        for (const sta::Mode* mode : sta->modes()) {
          float fanout = 0.0F;
          float limit = 0.0F;
          float slack = 0.0F;
          sta->checkFanout(pin, mode, sta::MinMax::max(), fanout, limit,
                           slack);
          // The bulk check keeps one entry per violating (pin,
          // mode): the per-pin check returns the null check
          // (slack INF) for pins with no limit or outside the
          // check gate, so a plain slack comparison replicates
          // it exactly (CheckFanouts::checkPin).
          if (slack < 0.0F) {
            violators.push_back(ElectricalViolation{
                sta->network()->pathName(pin),
                static_cast<double>(fanout), static_cast<double>(limit),
                static_cast<double>(slack)});
          }
        }
      });
      SortViolators(violators);
      return violators;
    } catch (const std::exception& ex) {
      return Status::Error(ErrorCode::kIoError, ex.what());
    }
  }

  // The analysis coverage (Immediate unit 7, review item I4).
  // The universe is enumerated per pin from the definitions the
  // engine itself evaluates: Liberty check arcs (the in-edge
  // roles at the pin's vertex), SDC output delays, gated-clock
  // enables, SDC data checks, and min-pulse-width constraints
  // (SDC or Liberty). Tested is decided by visiting the
  // constrained path ends at every vertex; a set_max_delay or
  // set_min_delay endpoint reached this way adds its row (defined
  // and tested) since a path-delay constraint has no check arc to
  // enumerate from. Untested reasons follow the documented
  // precedence: constant_disabled, no_clock,
  // no_startpoint_clock, no_paths.
  Result<std::vector<CoveragePin>> AnalysisCoverage() {
    const Status ready = Ready();
    if (!ready.ok()) return ready;
    try {
      sta->ensureGraph();
      sta::Graph* graph = sta->graph();
      sta::Network* network = sta->network();
      const sta::ModeSeq& modes = sta->modes();
      if (modes.empty()) {
        return Status::Error(ErrorCode::kInvalidState,
                             "analysis coverage needs an analysis mode");
      }
      const sta::Sdc* sdc = (*modes.begin())->sdc();
      // One universe row plus the facts the untested reason
      // needs. clock_present is per row: the check's clock, which
      // lives on the constrained pin for the check-arc and
      // data-check types, on the gated clock pin for the gating
      // checks, and on the output delay itself for out_setup and
      // out_hold; for min_pulse_width it is whether a clocked
      // path reaches the pin.
      struct RowFacts {
        CoveragePin pin;
        bool disabled = false;
        bool clock_present = true;
        bool has_paths = false;
        bool has_clocked_path = false;
      };
      std::map<std::pair<std::string, std::string>, RowFacts> rows;
      const auto add_row = [&rows](const std::string& pin,
                                   const std::string& related,
                                   const char* type, bool clock_present) {
        const auto key = std::make_pair(pin, std::string(type));
        if (rows.find(key) == rows.end()) {
          RowFacts facts;
          facts.pin.pin = pin;
          facts.pin.related_pin = related;
          facts.pin.check_type = type;
          facts.clock_present = clock_present;
          rows[key] = std::move(facts);
        }
      };
      // The output delays are enumerated once, not per pin: one
      // OutputDelay covers its leaf pins, and its clock or
      // reference pin is the related pin for the out_setup and
      // out_hold rows.
      for (sta::OutputDelay* delay : sdc->outputDelays()) {
        std::string related = "-";
        if (delay->refPin() != nullptr) {
          related = network->pathName(delay->refPin());
        } else if (delay->clock() != nullptr) {
          related = delay->clock()->name();
        }
        const bool delay_clocked = delay->clock() != nullptr ||
                                   delay->refPin() != nullptr;
        for (const sta::Pin* leaf : delay->leafPins()) {
          const std::string leaf_name = network->pathName(leaf);
          for (const sta::RiseFall* rf : sta::RiseFall::range()) {
            float margin = 0.0F;
            bool exists = false;
            delay->delays()->value(rf, sta::MinMax::max(), margin,
                                   exists);
            if (exists) {
              add_row(leaf_name, related, "out_setup", delay_clocked);
            }
            delay->delays()->value(rf, sta::MinMax::min(), margin,
                                   exists);
            if (exists) {
              add_row(leaf_name, related, "out_hold", delay_clocked);
            }
          }
          bool leaf_disabled = false;
          for (const sta::Mode* mode : modes) {
            if (sta->isConstant(leaf, mode)) leaf_disabled = true;
          }
          if (leaf_disabled || sdc->isDisabledConstraint(leaf)) {
            const auto setup_key =
                std::make_pair(leaf_name, std::string("out_setup"));
            const auto hold_key =
                std::make_pair(leaf_name, std::string("out_hold"));
            if (rows.find(setup_key) != rows.end()) {
              rows[setup_key].disabled = true;
            }
            if (rows.find(hold_key) != rows.end()) {
              rows[hold_key].disabled = true;
            }
          }
        }
      }
      auto tested = std::make_shared<CoverageVisitor::TestedSet>();
      VisitAllPins([this, &rows, &add_row, &tested, graph, sdc,
                    network](const sta::Pin* pin) {
        const std::string pin_name = network->pathName(pin);
        sta::Vertex* vertex = graph->pinLoadVertex(pin);
        if (vertex == nullptr) return;
        const sta::ModeSeq& modes = sta->modes();
        bool constant = false;
        for (const sta::Mode* mode : modes) {
          if (sta->isConstant(pin, mode)) constant = true;
        }
        // Required times first, exactly as the endpoint queries
        // do: this runs the search, so the path scan below and
        // the path-end visit see the paths that exist.
        sta->findRequired(vertex);
        bool has_paths = false;
        bool has_clocked_path = false;
        sta::VertexPathIterator path_iter(vertex, sta);
        while (path_iter.hasNext()) {
          const sta::Path* path = path_iter.next();
          has_paths = true;
          if (path->clock(sta) != nullptr) has_clocked_path = true;
        }
        const auto note_paths = [&rows, &pin_name, has_paths,
                                 has_clocked_path](const char* type) {
          const auto key =
              std::make_pair(pin_name, std::string(type));
          const auto it = rows.find(key);
          if (it != rows.end()) {
            it->second.has_paths = has_paths;
            it->second.has_clocked_path = has_clocked_path;
          }
        };
        // The Liberty check arcs into the pin. When the pin
        // carries an output delay the engine evaluates the delay
        // instead of the arcs (VisitPathEnds precedence), so the
        // arcs contribute no rows for that pin.
        if (!sdc->hasOutputDelay(pin)) {
          sta::VertexInEdgeIterator edge_iter(vertex, graph);
          while (edge_iter.hasNext()) {
            sta::Edge* edge = edge_iter.next();
            const char* type = FoldCheckRole(edge->role());
            if (type == nullptr) continue;
            sta::Vertex* from_vertex = edge->from(graph);
            const sta::Pin* from_pin =
                from_vertex == nullptr ? nullptr : from_vertex->pin();
            const std::string related =
                from_pin == nullptr ? "-"
                                    : network->pathName(from_pin);
            // The check's clock lives on the related pin (the
            // clock pin of the check arc), not on the constrained
            // pin: a D pin carries no clock, its CK pin does. The
            // clock network is the query that answers it: the
            // SDC pin maps only record the pins a clock was
            // created on, while the network traces the clock to
            // its leaves.
            bool arc_clocked = false;
            if (from_pin != nullptr) {
              for (const sta::Mode* mode : modes) {
                if (mode->clkNetwork()->isClock(from_pin)) {
                  arc_clocked = true;
                }
              }
            }
            add_row(pin_name, related, type, arc_clocked);
            bool edge_disabled = constant;
            if (!edge_disabled) {
              for (const sta::Mode* mode : modes) {
                if (sta->isDisabledConstant(edge, mode)) {
                  edge_disabled = true;
                }
              }
            }
            if (!edge_disabled &&
                sdc->isDisabledConstraint(edge)) {
              edge_disabled = true;
            }
            if (edge_disabled) {
              rows[std::make_pair(pin_name, std::string(type))]
                  .disabled = true;
            }
            note_paths(type);
          }
        }
        // The SDC data checks targeting this pin (no-change).
        // A data check has no single clock pin: its clock is
        // the launch clock of the path that reaches the pin, so
        // the check's clock is present exactly when the design
        // has any clock at all.
        const bool design_has_clock = !sdc->clocks().empty();
        const sta::DataCheckSet* data_checks = sdc->dataChecksTo(pin);
        if (data_checks != nullptr) {
          for (const sta::DataCheck* check : *data_checks) {
            const sta::Pin* from_pin = check->from();
            add_row(pin_name,
                    from_pin == nullptr
                        ? "-"
                        : network->pathName(from_pin),
                    "no-change", design_has_clock);
            if (constant) {
              rows[std::make_pair(pin_name, std::string("no-change"))]
                  .disabled = true;
            }
            note_paths("no-change");
          }
        }
        // The clock-gating checks at this pin, when it is the
        // enable of a gated clock.
        for (const sta::Mode* mode : modes) {
          bool is_enable = false;
          const sta::Pin* clk_pin = nullptr;
          sta::LogicValue active_value;
          sta->search()->gatedClk()->isGatedClkEnable(
              vertex, mode, is_enable, clk_pin, active_value);
          if (!is_enable || clk_pin == nullptr) continue;
          const std::string clk_name = network->pathName(clk_pin);
          bool clk_clocked = false;
          for (const sta::Mode* mode : modes) {
            if (mode->clkNetwork()->isClock(clk_pin)) {
              clk_clocked = true;
            }
          }
          add_row(pin_name, clk_name, "clock_gating_setup", clk_clocked);
          add_row(pin_name, clk_name, "clock_gating_hold", clk_clocked);
          sta::Instance* inst = network->instance(pin);
          const bool gating_disabled =
              constant || sdc->isDisableClockGatingCheck(pin) ||
              (inst != nullptr &&
               sdc->isDisableClockGatingCheck(inst));
          if (gating_disabled) {
            rows[std::make_pair(pin_name,
                                std::string("clock_gating_setup"))]
                .disabled = true;
            rows[std::make_pair(pin_name,
                                std::string("clock_gating_hold"))]
                .disabled = true;
          }
          note_paths("clock_gating_setup");
          note_paths("clock_gating_hold");
        }
        // Min pulse width: the SDC constraint (pin, instance,
        // clock or global scope, either transition) or the
        // Liberty arc at this vertex.
        for (const sta::RiseFall* rf :
             {sta::RiseFall::rise(), sta::RiseFall::fall()}) {
          float width = 0.0F;
          bool exists = false;
          sdc->minPulseWidth(pin, nullptr, rf, width, exists);
          if (!exists) {
            for (const sta::Clock* clk : sdc->clocks()) {
              sdc->minPulseWidth(pin, clk, rf, width, exists);
              if (exists) break;
            }
          }
          if (!exists) {
            sta::Edge* mpw_edge = nullptr;
            sta::TimingArc* mpw_arc = nullptr;
            graph->minPulseWidthArc(vertex, rf, mpw_edge, mpw_arc);
            exists = mpw_edge != nullptr;
          }
          if (exists) {
            add_row(pin_name, "-", "min_pulse_width", has_clocked_path);
            if (constant) {
              rows[std::make_pair(pin_name,
                                  std::string("min_pulse_width"))]
                  .disabled = true;
            }
            note_paths("min_pulse_width");
          }
        }
        // The constrained path ends at this vertex decide tested.
        sta::VisitPathEnds visit(sta);
        CoverageVisitor visitor(sta, tested);
        visit.visitPathEnds(vertex, &visitor);
      });
      // A constrained end the visitor saw at a pin with no
      // universe row for its type is a set_max_delay or
      // set_min_delay endpoint: the check is defined (the engine
      // evaluated it) and tested.
      for (const auto& entry : *tested) {
        const auto it = rows.find(entry);
        if (it == rows.end()) {
          RowFacts facts;
          facts.pin.pin = entry.first;
          facts.pin.related_pin = "-";
          facts.pin.check_type = entry.second;
          facts.pin.tested = true;
          facts.has_paths = true;
          facts.has_clocked_path = true;
          rows[entry] = std::move(facts);
        } else {
          it->second.pin.tested = true;
        }
      }
      // Min pulse width tested comes from the upstream bulk
      // check, which evaluates the check per pin: a pin it
      // returns has a min-pulse-width check with a clock path
      // and a close path.
      sta::CheckMinPulseWidths mpw(sta);
      const sta::MinPulseWidthCheckSeq& mpw_checks = mpw.check(
          nullptr, std::numeric_limits<std::size_t>::max(), false,
          sta->makeSceneSeq(nullptr));
      for (const sta::MinPulseWidthCheck& check : mpw_checks) {
        const std::string pin_name =
            network->pathName(check.pin(sta));
        const auto key =
            std::make_pair(pin_name, std::string("min_pulse_width"));
        const auto it = rows.find(key);
        if (it != rows.end()) it->second.pin.tested = true;
      }
      std::vector<CoveragePin> out;
      for (auto& entry : rows) {
        RowFacts& facts = entry.second;
        CoveragePin& row = facts.pin;
        if (!row.tested) {
          if (facts.disabled) {
            row.untested_reason = "constant_disabled";
          } else if (!facts.clock_present) {
            row.untested_reason = "no_clock";
          } else if (facts.has_paths && !facts.has_clocked_path) {
            row.untested_reason = "no_startpoint_clock";
          } else {
            row.untested_reason = "no_paths";
          }
        }
        out.push_back(row);
      }
      std::sort(out.begin(), out.end(),
                [](const CoveragePin& a, const CoveragePin& b) {
                  if (a.pin != b.pin) return a.pin < b.pin;
                  return a.check_type < b.check_type;
                });
      return out;
    } catch (const std::exception& ex) {
      return Status::Error(ErrorCode::kIoError, ex.what());
    }
  }

  Status Ready() const {
    if (sta == nullptr) {
      return Status::Error(
          ErrorCode::kInvalidState,
          "another StaEngine is alive; upstream STA is a singleton");
    }
    if (!linked || !constrained) {
      return Status::Error(
          ErrorCode::kInvalidState,
          "slack requested before the design is linked and "
          "constrained");
    }
    return Status::Ok();
  }

  Result<double> SlackNs(bool total, bool hold) {
    const Status ready = Ready();
    if (!ready.ok()) return ready;
    try {
      const sta::MinMax* min_max = hold ? sta::MinMax::min() : sta::MinMax::max();
      const sta::Slack slack = total ? sta->totalNegativeSlack(min_max)
                                     : sta->worstSlack(min_max);
      return ToNs(slack);
    } catch (const std::exception& ex) {
      return Status::Error(ErrorCode::kIoError, ex.what());
    }
  }

  // Visits the constrained path ends of one endpoint vertex and
  // returns the worst one for min_max; the caller decides what
  // the slack means. Required times are found first, exactly as
  // the upstream endpointSlack does before visiting.
  WorstEndVisitor VisitWorstEnd(sta::Vertex* vertex,
                                const sta::MinMax* min_max) {
    sta->findRequired(vertex);
    sta::VisitPathEnds visit(sta);
    WorstEndVisitor visitor(min_max, sta);
    visit.visitPathEnds(vertex, &visitor);
    return visitor;
  }

  Result<std::vector<EndpointSlack>> Endpoints(bool hold,
                                               bool violating_only) {
    const Status ready = Ready();
    if (!ready.ok()) return ready;
    try {
      const sta::MinMax* min_max =
          hold ? sta::MinMax::min() : sta::MinMax::max();
      std::vector<EndpointSlack> endpoints;
      for (sta::Vertex* vertex : sta->endpoints()) {
        const WorstEndVisitor visitor = VisitWorstEnd(vertex, min_max);
        if (visitor.found() &&
            (!violating_only ||
             sta::delayLess(visitor.slack(), 0.0F, sta))) {
          endpoints.push_back(EndpointSlack{
              sta->network()->pathName(vertex->pin()),
              ToNs(visitor.slack())});
        }
      }
      std::sort(endpoints.begin(), endpoints.end(),
                [](const EndpointSlack& a, const EndpointSlack& b) {
                  if (a.slack_ns != b.slack_ns) return a.slack_ns < b.slack_ns;
                  return a.pin < b.pin;
                });
      return endpoints;
    } catch (const std::exception& ex) {
      return Status::Error(ErrorCode::kIoError, ex.what());
    }
  }

  // Walks one worst path into enriched points, in path order.
  // Arrival, slew and edge come straight off the analyzer's path
  // objects; the incremental delay is the arrival difference from
  // the previous point (the arrival itself for the first point);
  // the cell is the owning instance's cell; the load capacitance
  // (drivers only) and the load-pin fanout follow the analyzer's
  // own report code, which reads the load cap off the delay
  // calculator and counts load pins on the driven net.
  std::vector<PathPoint> WalkPoints(const sta::Path* worst_path) {
    const sta::PathExpanded expanded(worst_path, sta);
    std::vector<PathPoint> points;
    double prev_arrival = 0.0;
    for (size_t i = expanded.startIndex(); i < expanded.size(); ++i) {
      const sta::Path* path = expanded.path(i);
      if (path == nullptr) continue;
      sta::Pin* pin = path->pin(sta);
      if (pin == nullptr) continue;
      PathPoint point;
      point.pin = sta->network()->pathName(pin);
      sta::Instance* inst = sta->network()->instance(pin);
      point.instance =
          inst == nullptr ? "" : sta->network()->pathName(inst);
      point.cell = inst == nullptr ? "" : sta->network()->cellName(inst);
      const sta::RiseFall* rf = path->transition(sta);
      point.edge = rf == nullptr ? "" : rf->shortName();
      point.arrival_ns = ToNs(path->arrival());
      point.incr_ns =
          points.empty() ? point.arrival_ns : point.arrival_ns - prev_arrival;
      prev_arrival = point.arrival_ns;
      point.slew_ns = ToNs(path->slew(sta));
      if (sta->network()->isDriver(pin)) {
        if (rf != nullptr) {
          point.cap_pf = ToPf(sta->graphDelayCalc()->loadCap(
              pin, rf, path->scene(sta), path->minMax(sta)));
        } else {
          point.cap_pf = ToPf(sta->graphDelayCalc()->loadCap(
              pin, path->scene(sta), path->minMax(sta)));
        }
        sta::PinConnectedPinIterator* iter =
            sta->network()->connectedPinIterator(pin);
        double fanout = 0.0;
        while (iter->hasNext()) {
          if (sta->network()->isLoad(iter->next())) fanout += 1.0;
        }
        delete iter;
        point.fanout = fanout;
      }
      points.push_back(std::move(point));
    }
    return points;
  }

  // Finds the endpoint vertex for a path query. Null when no
  // endpoint pin has the given path name.
  sta::Vertex* FindEndpoint(const std::string& endpoint_pin) {
    for (sta::Vertex* vertex : sta->endpoints()) {
      if (sta->network()->pathName(vertex->pin()) == endpoint_pin) {
        return vertex;
      }
    }
    return nullptr;
  }

  Result<std::vector<PathPoint>> WorstPath(bool hold,
                                           const std::string& endpoint_pin) {
    const Status ready = Ready();
    if (!ready.ok()) return ready;
    try {
      const sta::MinMax* min_max =
          hold ? sta::MinMax::min() : sta::MinMax::max();
      sta::Vertex* target = FindEndpoint(endpoint_pin);
      if (target == nullptr) {
        return Status::Error(ErrorCode::kInvalidArgument,
                             "no endpoint pin named " + endpoint_pin);
      }
      const WorstEndVisitor visitor = VisitWorstEnd(target, min_max);
      if (!visitor.found() || visitor.worst_path() == nullptr) {
        return Status::Error(
            ErrorCode::kInvalidArgument,
            "endpoint " + endpoint_pin +
                " has no constrained path end for this check kind");
      }
      std::vector<PathPoint> points = WalkPoints(visitor.worst_path());
      if (points.empty()) {
        return Status::Error(ErrorCode::kInvalidState,
                             "worst path to endpoint " + endpoint_pin +
                                 " expanded to no points");
      }
      return points;
    } catch (const std::exception& ex) {
      return Status::Error(ErrorCode::kIoError, ex.what());
    }
  }

  Result<PathReport> WorstPathReport(bool hold,
                                     const std::string& endpoint_pin) {
    const Status ready = Ready();
    if (!ready.ok()) return ready;
    try {
      const sta::MinMax* min_max =
          hold ? sta::MinMax::min() : sta::MinMax::max();
      sta::Vertex* target = FindEndpoint(endpoint_pin);
      if (target == nullptr) {
        return Status::Error(ErrorCode::kInvalidArgument,
                             "no endpoint pin named " + endpoint_pin);
      }
      const WorstEndVisitor visitor = VisitWorstEnd(target, min_max);
      if (!visitor.found() || visitor.worst_path() == nullptr) {
        return Status::Error(
            ErrorCode::kInvalidArgument,
            "endpoint " + endpoint_pin +
                " has no constrained path end for this check kind");
      }
      std::vector<PathPoint> points = WalkPoints(visitor.worst_path());
      if (points.empty()) {
        return Status::Error(ErrorCode::kInvalidState,
                             "worst path to endpoint " + endpoint_pin +
                                 " expanded to no points");
      }
      PathReport report;
      report.endpoint = endpoint_pin;
      report.startpoint = points.front().pin;
      report.slack_ns = ToNs(visitor.slack());
      report.arrival_ns = points.back().arrival_ns;
      report.required_ns = report.arrival_ns + report.slack_ns;
      report.points = std::move(points);
      return report;
    } catch (const std::exception& ex) {
      return Status::Error(ErrorCode::kIoError, ex.what());
    }
  }

  // The path groups for one check kind, through the upstream
  // reporting enumeration: findPathEnds builds the groups (named
  // group_path groups, one group per clock, the async, gated-clock
  // and path-delay groups) and assigns each returned end to every
  // group it belongs to, one end per endpoint per group. The
  // group count is unbounded and the slack bounds are infinite,
  // so no constrained end is pruned: the union of the groups is
  // the full constrained-endpoint population the visitors see.
  // The ends are owned by the upstream search and die on the next
  // findPathEnds call, so the names and slacks are copied out
  // before this returns.
  Result<std::vector<PathGroupSlack>> PathGroupSlacks(bool hold) {
    const Status ready = Ready();
    if (!ready.ok()) return ready;
    try {
      sta::StringSeq group_names;
      const sta::PathEndSeq ends = sta->findPathEnds(
          nullptr, nullptr, nullptr, false, sta->scenes(),
          hold ? sta::MinMaxAll::min() : sta::MinMaxAll::max(),
          std::numeric_limits<int>::max(), 1, true, false, -sta::INF,
          sta::INF, true, group_names, !hold, hold, !hold, hold,
          !hold, hold);
      std::map<std::string, std::vector<EndpointSlack>> by_group;
      for (const sta::PathEnd* end : ends) {
        const sta::PathGroup* group = end->pathGroup();
        if (group == nullptr) continue;
        const sta::Slack slack = end->slack(sta);
        if (sta::delayInf(slack, sta)) continue;
        sta::Vertex* vertex = end->vertex(sta);
        if (vertex == nullptr) continue;
        by_group[group->name()].push_back(EndpointSlack{
            sta->network()->pathName(vertex->pin()), ToNs(slack)});
      }
      std::vector<PathGroupSlack> groups;
      for (auto& [name, endpoints] : by_group) {
        std::sort(endpoints.begin(), endpoints.end(),
                  [](const EndpointSlack& a, const EndpointSlack& b) {
                    if (a.slack_ns != b.slack_ns)
                      return a.slack_ns < b.slack_ns;
                    return a.pin < b.pin;
                  });
        groups.push_back(PathGroupSlack{name, std::move(endpoints)});
      }
      std::sort(groups.begin(), groups.end(),
                [](const PathGroupSlack& a, const PathGroupSlack& b) {
                  return a.name < b.name;
                });
      return groups;
    } catch (const std::exception& ex) {
      return Status::Error(ErrorCode::kIoError, ex.what());
    }
  }

  // The clocks of the scenario (Immediate unit 10, review item
  // T8): the SDC of the first mode lists the clocks the engine
  // read, each with its period in seconds, converted to
  // nanoseconds. Sorted by name so the order is deterministic.
  // Note the bulk minimum-period check was not used here: it
  // only reports Liberty minimum_period constraints and SDF
  // annotations, none of which exist in EOS fixtures, so it
  // returns no rows; the per-clock minimum period is a derived
  // value, period minus the clock group's worst setup slack,
  // and that derivation lives in the timing service.
  Result<std::vector<ClockPeriod>> ClockPeriods() {
    const Status ready = Ready();
    if (!ready.ok()) return ready;
    try {
      const sta::ModeSeq& modes = sta->modes();
      if (modes.empty()) {
        return Status::Error(ErrorCode::kInvalidState,
                             "the timing session has no scenario");
      }
      const sta::Sdc* sdc = (*modes.begin())->sdc();
      std::vector<ClockPeriod> out;
      for (const sta::Clock* clock : sdc->clocks()) {
        if (clock == nullptr) continue;
        out.push_back(ClockPeriod{clock->name(), ToNs(clock->period())});
      }
      std::sort(out.begin(), out.end(),
                [](const ClockPeriod& a, const ClockPeriod& b) {
                  return a.name < b.name;
                });
      return out;
    } catch (const std::exception& ex) {
      return Status::Error(ErrorCode::kIoError, ex.what());
    }
  }

  // The minimum-pulse-width checks (Immediate unit 10, review
  // item T8): the upstream bulk check, constructed directly
  // exactly as the coverage query does, after ensuring the clock
  // arrivals the check needs. One row per evaluated pulse; the
  // opening edge names the pulse (rise opens the high pulse,
  // fall the low pulse). The bulk check returns the rows worst
  // slack first; the facade copies the pin, transition, width,
  // minimum width and slack out before returning, sorted again
  // with the pin and transition as tie-breakers.
  Result<std::vector<MinPulseWidthCheck>> MinPulseWidthChecks() {
    const Status ready = Ready();
    if (!ready.ok()) return ready;
    try {
      sta->ensureClkArrivals();
      sta::CheckMinPulseWidths checker(sta);
      const sta::MinPulseWidthCheckSeq& checks = checker.check(
          nullptr, std::numeric_limits<std::size_t>::max(), false,
          sta->makeSceneSeq(nullptr));
      std::vector<MinPulseWidthCheck> out;
      for (const sta::MinPulseWidthCheck& check : checks) {
        if (check.isNull()) continue;
        const sta::Pin* pin = check.pin(sta);
        if (pin == nullptr) continue;
        out.push_back(MinPulseWidthCheck{
            sta->network()->pathName(pin),
            check.openTransition(sta) == sta::RiseFall::rise()
                ? "rise"
                : "fall",
            ToNs(check.width(sta)), ToNs(check.minWidth(sta)),
            ToNs(check.slack(sta))});
      }
      std::sort(out.begin(), out.end(),
                [](const MinPulseWidthCheck& a,
                   const MinPulseWidthCheck& b) {
                  if (a.slack_ns != b.slack_ns)
                    return a.slack_ns < b.slack_ns;
                  if (a.pin != b.pin) return a.pin < b.pin;
                  return a.transition < b.transition;
                });
      return out;
    } catch (const std::exception& ex) {
      return Status::Error(ErrorCode::kIoError, ex.what());
    }
  }

  // The cell census: one row per Liberty cell type over the leaf
  // instances, in the same visit order as VisitAllPins, so a
  // hierarchical instance's leaves are counted exactly once and
  // the top instance is never counted as a cell.
  Result<CellAreaResult> CellArea() {
    const Status ready = Ready();
    if (!ready.ok()) return ready;
    try {
      sta::Network* network = sta->network();
      std::map<std::string, CellTypeArea> by_cell;
      std::size_t total_cells = 0;
      double total_area = 0.0;
      sta::LeafInstanceIterator* inst_iter =
          network->leafInstanceIterator();
      while (inst_iter->hasNext()) {
        const sta::Instance* inst = inst_iter->next();
        const sta::LibertyCell* cell = network->libertyCell(inst);
        const std::string name =
            cell == nullptr ? "(no liberty cell)" : cell->name();
        const double area =
            cell == nullptr ? 0.0 : static_cast<double>(cell->area());
        CellTypeArea& row = by_cell[name];
        row.cell = name;
        row.count += 1;
        row.area += area;
        total_cells += 1;
        total_area += area;
      }
      delete inst_iter;
      CellAreaResult result;
      result.total_cells = total_cells;
      result.total_area = total_area;
      for (auto& [name, row] : by_cell) {
        result.rows.push_back(std::move(row));
      }
      std::sort(result.rows.begin(), result.rows.end(),
                [](const CellTypeArea& a, const CellTypeArea& b) {
                  if (a.count != b.count) return a.count > b.count;
                  return a.cell < b.cell;
                });
      return result;
    } catch (const std::exception& ex) {
      return Status::Error(ErrorCode::kIoError, ex.what());
    }
  }
};

StaEngine::StaEngine() : impl_(new Impl) {}
StaEngine::~StaEngine() = default;
StaEngine::StaEngine(StaEngine&&) noexcept = default;
StaEngine& StaEngine::operator=(StaEngine&&) noexcept = default;

Status StaEngine::ReadLiberty(const std::string& path) {
  return impl_->Run("read_liberty", path, "could not read Liberty file");
}

Status StaEngine::ReadVerilog(const std::string& path) {
  return impl_->Run("read_verilog", path, "could not read Verilog file");
}

Status StaEngine::LinkDesign(const std::string& top_cell) {
  Status status = impl_->Run("link_design", top_cell, "could not link design");
  if (status.ok()) {
    impl_->linked = true;
  }
  return status;
}

Status StaEngine::ReadSdc(const std::string& path) {
  Status status = impl_->Run("read_sdc", path, "could not read SDC file");
  if (status.ok()) {
    impl_->constrained = true;
  }
  return status;
}

Status StaEngine::ReadSpef(const std::string& path) {
  return impl_->Run("read_spef", path, "could not read SPEF file");
}

Status StaEngine::ClearDesign() {
  if (impl_->sta == nullptr) {
    return Status::Error(
        ErrorCode::kInvalidState,
        "another StaEngine is alive; upstream STA is a singleton");
  }
  try {
    // Upstream clear() drops the network, the SDC and the
    // parasitics, and keeps the library scenes: exactly the
    // reset a multi-candidate loop needs. The facade flags
    // follow, so no query answers on the cleared session until
    // it is linked and constrained again.
    impl_->sta->clear();
  } catch (const std::exception& ex) {
    return Status::Error(ErrorCode::kIoError, ex.what());
  }
  impl_->linked = false;
  impl_->constrained = false;
  return Status::Ok();
}

Result<double> StaEngine::WorstSlackNs() { return impl_->SlackNs(false, false); }

Result<double> StaEngine::TotalNegativeSlackNs() {
  return impl_->SlackNs(true, false);
}

Result<double> StaEngine::WorstHoldSlackNs() {
  return impl_->SlackNs(false, true);
}

Result<double> StaEngine::TotalNegativeHoldSlackNs() {
  return impl_->SlackNs(true, true);
}

Result<std::vector<EndpointSlack>> StaEngine::ViolatingEndpoints(
    CheckKind kind) {
  return impl_->Endpoints(kind == CheckKind::kHold, true);
}

Result<std::vector<EndpointSlack>> StaEngine::EndpointsBySlack(
    CheckKind kind) {
  return impl_->Endpoints(kind == CheckKind::kHold, false);
}

Result<std::vector<PathPoint>> StaEngine::WorstPath(
    CheckKind kind, const std::string& endpoint_pin) {
  return impl_->WorstPath(kind == CheckKind::kHold, endpoint_pin);
}

Result<PathReport> StaEngine::WorstPathReport(
    CheckKind kind, const std::string& endpoint_pin) {
  return impl_->WorstPathReport(kind == CheckKind::kHold, endpoint_pin);
}

Result<std::vector<PathGroupSlack>> StaEngine::PathGroupSlacks(
    CheckKind kind) {
  return impl_->PathGroupSlacks(kind == CheckKind::kHold);
}

Result<std::vector<ClockPeriod>> StaEngine::ClockPeriods() {
  return impl_->ClockPeriods();
}

Result<std::vector<MinPulseWidthCheck>> StaEngine::MinPulseWidthChecks() {
  return impl_->MinPulseWidthChecks();
}

Result<CellAreaResult> StaEngine::CellArea() {
  return impl_->CellArea();
}

Result<ElectricalCheck> StaEngine::MaxTransitionCheck() {
  return impl_->MaxTransition();
}

Result<ElectricalCheck> StaEngine::MaxCapacitanceCheck() {
  return impl_->MaxCapacitance();
}

Result<ElectricalCheck> StaEngine::MaxFanoutCheck() {
  return impl_->MaxFanout();
}

Result<std::vector<ElectricalViolation>>
StaEngine::MaxTransitionViolators() {
  return impl_->MaxTransitionViolators();
}

Result<std::vector<ElectricalViolation>>
StaEngine::MaxCapacitanceViolators() {
  return impl_->MaxCapacitanceViolators();
}

Result<std::vector<ElectricalViolation>> StaEngine::MaxFanoutViolators() {
  return impl_->MaxFanoutViolators();
}

Result<std::vector<CoveragePin>> StaEngine::AnalysisCoverage() {
  return impl_->AnalysisCoverage();
}

}  // namespace eos::analysis
