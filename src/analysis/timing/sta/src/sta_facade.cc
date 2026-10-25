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

#include <atomic>
#include <exception>
#include <string>
#include <utility>

#include "MinMax.hh"
#include "Sta.hh"
#include "StaMain.hh"
#include "Units.hh"

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

  Result<double> SlackNs(bool total) {
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
    try {
      const sta::Slack slack =
          total ? sta->totalNegativeSlack(sta::MinMax::max())
                : sta->worstSlack(sta::MinMax::max());
      const sta::Unit* unit = sta->units()->timeUnit();
      // Unit::staToUser(value) is value / unit->scale(); staToUser is
      // not const-qualified upstream, so divide by the scale here.
      const double in_unit =
          static_cast<double>(slack) / static_cast<double>(unit->scale());
      // in_unit is expressed in the library time unit; one such unit
      // is unit->scale() seconds, and one second is 1e9 ns.
      return in_unit * static_cast<double>(unit->scale()) * 1e9;
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

Result<double> StaEngine::WorstSlackNs() { return impl_->SlackNs(false); }

Result<double> StaEngine::TotalNegativeSlackNs() {
  return impl_->SlackNs(true);
}

}  // namespace eos::analysis
