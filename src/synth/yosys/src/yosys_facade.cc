// yosys facade implementation: the only EOS-authored translation
// unit in eda_synth_yosys. Everything upstream of this file is
// imported code (see PROVENANCE.md); this file translates between
// the EOS surface in include/eos/synth/yosys.h and the upstream
// Yosys embedding API, and converts upstream outcomes into EOS
// Status values, so no upstream type crosses the boundary.
//
// Upstream Yosys is driven the way its own library embedding
// prescribes: yosys_setup creates the global design, commands run
// through run_pass in process. Two upstream facts shape the
// session handling here, and both are load-bearing:
//
// - Pass and frontend registration is one-shot per process.
//   Pass objects register themselves through global constructors
//   into a queue that yosys_setup drains (init_register), and
//   yosys_shutdown clears the registries (done_register) without
//   refilling the queue. A second yosys_setup therefore registers
//   nothing, and every later command fails with "No such
//   command". The facade consequently sets Yosys up exactly once
//   per process and never shuts it down: an engine's destructor
//   releases only the single-engine guard, and a later engine
//   reuses the live session. Each ParseVerilog call gets a fresh
//   design by deleting the global design object and creating a
//   new one, which is the reset half of what shutdown does,
//   without the registry half.
// - The log sinks are installed by the yosys executable's main,
//   which this library does not compile, so a library session
//   starts with no sinks and all Yosys output is discarded. The
//   facade installs upstream's non-tty default sink
//   (ConsoleLogSink: errors to stderr, the rest to stdout) at
//   first setup, so command errors are visible instead of silent.
//
// Each ParseVerilog call runs with the command-error throw scope
// held, so a command-level rejection (not a syntax error, which
// aborts upstream; see the header) comes back as a Status
// instead of killing the caller.
#include "eos/synth/yosys.h"

#include <atomic>
#include <fstream>
#include <utility>

#include "kernel/log.h"
#include "kernel/rtlil.h"
#include "kernel/yosys.h"

namespace eos::synth {
namespace {

using foundation::ErrorCode;

std::atomic<bool> g_engine_alive{false};
bool g_yosys_setup_done = false;

Status IoError(const std::string& what, const std::string& path) {
  return Status::Error(ErrorCode::kIoError, what + ": " + path);
}

}  // namespace

YosysEngine::YosysEngine() {
  if (!g_engine_alive.exchange(true)) {
    owns_ = true;
    if (!g_yosys_setup_done) {
      Yosys::yosys_setup();
      Yosys::logger().add_sink<Yosys::ConsoleLogSink>();
      g_yosys_setup_done = true;
    }
  }
}

YosysEngine::~YosysEngine() {
  if (owns_) {
    // Deliberately no yosys_shutdown: it would unregister every
    // pass and frontend, and a later setup registers nothing
    // (see the file note). The session lives for the process.
    g_engine_alive.store(false);
  }
}

YosysEngine::YosysEngine(YosysEngine&& other) noexcept
    : owns_(other.owns_) {
  other.owns_ = false;
}

YosysEngine& YosysEngine::operator=(YosysEngine&& other) noexcept {
  if (this != &other) {
    if (owns_) {
      g_engine_alive.store(false);
    }
    owns_ = other.owns_;
    other.owns_ = false;
  }
  return *this;
}

std::string YosysEngine::Version() {
  return Yosys::yosys_version_str;
}

Result<YosysDesignSummary> YosysEngine::ParseVerilog(
    const std::string& path) {
  if (!owns_) {
    return Status::Error(ErrorCode::kInvalidState,
                         "Yosys engine does not own the Yosys session");
  }
  {
    std::ifstream in(path);
    if (!in.good()) {
      return IoError("cannot read Verilog file", path);
    }
  }
  // Fresh design for this parse, without touching the session:
  // a previous parse on this engine must not leak modules into
  // the next one.
  delete Yosys::yosys_design;
  Yosys::yosys_design = new Yosys::RTLIL::Design;
  try {
    auto guard = Yosys::logger().error_throw_scope();
    Yosys::run_pass("read_verilog " + path);
  } catch (const Yosys::log_cmd_error_exception&) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "Yosys rejected the read_verilog command");
  } catch (const std::exception& e) {
    return Status::Error(ErrorCode::kInvalidState,
                         std::string("Yosys failed: ") + e.what());
  }
  YosysDesignSummary summary;
  if (Yosys::yosys_design != nullptr) {
    for (const auto& entry : Yosys::yosys_design->modules_) {
      YosysModuleSummary module;
      // Upstream stores public names escaped (a leading
      // backslash: "\demo"); unescape reports the name as
      // written in the source. Internal names such as
      // $auto$... pass through unescape unchanged.
      module.name = entry.first.unescape();
      for (const auto& port : entry.second->ports) {
        module.ports.push_back(port.unescape());
      }
      module.wire_count = static_cast<int>(entry.second->wires_.size());
      summary.modules.push_back(std::move(module));
    }
  }
  return summary;
}

}  // namespace eos::synth
