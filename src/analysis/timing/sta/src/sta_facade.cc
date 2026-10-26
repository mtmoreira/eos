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
#include <string>
#include <utility>
#include <vector>

#include "Graph.hh"
#include "MinMax.hh"
#include "Network.hh"
#include "PathEnd.hh"
#include "PathExpanded.hh"
#include "Sta.hh"
#include "StaMain.hh"
#include "Units.hh"
#include "VisitPathEnds.hh"

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

  Result<std::vector<EndpointSlack>> Endpoints(bool hold) {
    const Status ready = Ready();
    if (!ready.ok()) return ready;
    try {
      const sta::MinMax* min_max =
          hold ? sta::MinMax::min() : sta::MinMax::max();
      std::vector<EndpointSlack> endpoints;
      for (sta::Vertex* vertex : sta->endpoints()) {
        const WorstEndVisitor visitor = VisitWorstEnd(vertex, min_max);
        if (visitor.found() &&
            sta::delayLess(visitor.slack(), 0.0F, sta)) {
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

  Result<std::vector<PathPoint>> WorstPath(bool hold,
                                           const std::string& endpoint_pin) {
    const Status ready = Ready();
    if (!ready.ok()) return ready;
    try {
      const sta::MinMax* min_max =
          hold ? sta::MinMax::min() : sta::MinMax::max();
      sta::Vertex* target = nullptr;
      for (sta::Vertex* vertex : sta->endpoints()) {
        if (sta->network()->pathName(vertex->pin()) == endpoint_pin) {
          target = vertex;
          break;
        }
      }
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
      const sta::PathExpanded expanded(visitor.worst_path(), sta);
      std::vector<PathPoint> points;
      for (size_t i = expanded.startIndex(); i < expanded.size(); ++i) {
        const sta::Path* path = expanded.path(i);
        if (path == nullptr) continue;
        sta::Pin* pin = path->pin(sta);
        if (pin == nullptr) continue;
        PathPoint point;
        point.pin = sta->network()->pathName(pin);
        point.instance = sta->network()->isTopLevelPort(pin)
                             ? ""
                             : sta->network()->pathName(
                                   sta->network()->instance(pin));
        points.push_back(std::move(point));
      }
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
  return impl_->Endpoints(kind == CheckKind::kHold);
}

Result<std::vector<PathPoint>> StaEngine::WorstPath(
    CheckKind kind, const std::string& endpoint_pin) {
  return impl_->WorstPath(kind == CheckKind::kHold, endpoint_pin);
}

}  // namespace eos::analysis
