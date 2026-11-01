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

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

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

Status InvalidArgument(const std::string& message) {
  return Status::Error(ErrorCode::kInvalidArgument, message);
}

Status InvalidState(const std::string& message) {
  return Status::Error(ErrorCode::kInvalidState, message);
}

// A top name is interpolated into a Yosys pass command string, so
// it must be a plain Verilog identifier: anything else is rejected
// before any pass runs, and can never smuggle a second command in.
bool IsPlainIdentifier(const std::string& name) {
  if (name.empty()) return false;
  const auto is_alpha = [](char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
  };
  const auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
  if (!is_alpha(name[0])) return false;
  for (std::size_t i = 1; i < name.size(); ++i) {
    if (!is_alpha(name[i]) && !is_digit(name[i])) return false;
  }
  return true;
}

// Runs one Yosys pass command under the command-error throw scope,
// so a pass rejection comes back as a Status instead of aborting
// the caller. A log_error inside the pass (a syntax error, an
// internal failure) still aborts the process; that is the
// documented rule in the facade header, not a case this helper
// can recover.
Status RunPass(const std::string& command) {
  try {
    auto guard = Yosys::logger().error_throw_scope();
    Yosys::run_pass(command);
  } catch (const Yosys::log_cmd_error_exception&) {
    return InvalidArgument("Yosys rejected the command: " + command);
  } catch (const std::exception& e) {
    return InvalidState(std::string("Yosys failed: ") + e.what());
  }
  return Status::Ok();
}

// Builds one read_verilog pass command carrying every include
// directory and define. The flags use the attached forms (-Idir,
// -Dname) Yosys documents, so a directory or a define value is
// one token each; callers validate that neither contains
// whitespace or ';' before this runs.
std::string BuildReadVerilogCommand(
    const std::string& path, const std::vector<std::string>& include_dirs,
    const std::vector<std::string>& defines) {
  std::string command = "read_verilog";
  for (const std::string& dir : include_dirs) command += " -I" + dir;
  for (const std::string& define : defines) command += " -D" + define;
  command += " " + path;
  return command;
}

// A command-string token is safe only if Yosys's tokenizer
// cannot split it: no whitespace (token separator) and no ';'
// (command separator). A '#' is not rejected here: a value is
// always a flag argument, never the first token, so it cannot
// start Yosys's comment rule.
bool IsSafeCommandToken(const std::string& token) {
  for (const char c : token) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ';') {
      return false;
    }
  }
  return true;
}

// Validates one -D token: NAME or NAME=VALUE, where NAME is a
// plain Verilog identifier (the same rule as a top name, for
// the same command-string reason).
Status ValidateDefine(const std::string& define) {
  if (define.empty() || !IsSafeCommandToken(define)) {
    return InvalidArgument("malformed preprocessor define: " + define);
  }
  const std::size_t equal = define.find('=');
  const std::string name =
      equal == std::string::npos ? define : define.substr(0, equal);
  if (!IsPlainIdentifier(name)) {
    return InvalidArgument("malformed preprocessor define: " + define);
  }
  return Status::Ok();
}

// Summarizes the live design's modules the way ParseVerilog
// reports them (names unescaped, ports in declaration order).
YosysDesignSummary SummarizeDesign() {
  YosysDesignSummary summary;
  if (Yosys::yosys_design != nullptr) {
    for (const auto& entry : Yosys::yosys_design->modules_) {
      YosysModuleSummary module;
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

// Finds a module of the live design by its source name, the way
// ParseVerilog reports names (unescaped).
Yosys::RTLIL::Module* FindModule(const std::string& name) {
  if (Yosys::yosys_design == nullptr) return nullptr;
  for (const auto& entry : Yosys::yosys_design->modules_) {
    if (entry.first.unescape() == name) return entry.second;
  }
  return nullptr;
}

// Cell types whose outputs start a new timing path instead of
// continuing one: flip-flops, latches, memories and FSMs, in both
// the $ form elaborate produces and the $_ form techmap produces.
bool IsSequentialCell(const std::string& type) {
  static const char* kPrefixes[] = {"$dff",  "$adff", "$sdff", "$dffsr",
                                    "$dffe", "$mem",  "$fsm",  "$_DFF",
                                    "$_SDFF", "$_DLATCH", "$sr"};
  for (const char* prefix : kPrefixes) {
    if (type.compare(0, std::string(prefix).size(), prefix) == 0) {
      return true;
    }
  }
  return false;
}

YosysNetlistStats ComputeStats(Yosys::RTLIL::Module* module) {
  YosysNetlistStats stats;
  stats.module = module->name.unescape();
  stats.cell_count = static_cast<int>(module->cells_.size());
  stats.wire_count = static_cast<int>(module->wires_.size());
  std::map<std::string, int> by_type;
  for (const auto& entry : module->cells_) {
    by_type[entry.second->type.str()]++;
    if (IsSequentialCell(entry.second->type.str())) {
      ++stats.sequential_cells;
    }
  }
  for (const auto& entry : by_type) {
    stats.cells_by_type.push_back(YosysCellTypeCount{entry.first, entry.second});
  }
  // Logic depth by fixed-point propagation over wires. Every wire
  // starts at depth 0 (a primary input, an undriven wire, or a
  // sequential output); a combinational cell raises its output
  // wires to one more than the deepest of its input wires.
  // Iterating to a fixed point removes any dependence on the
  // order Yosys stores cells in, and the iteration count is
  // bounded by the cell count because a wire's depth never
  // exceeds the longest cell chain.
  std::map<Yosys::RTLIL::Wire*, int> depth;
  for (const auto& entry : module->wires_) {
    depth[entry.second] = 0;
  }
  for (std::size_t pass = 0; pass <= module->cells_.size(); ++pass) {
    bool changed = false;
    for (const auto& entry : module->cells_) {
      Yosys::RTLIL::Cell* cell = entry.second;
      if (IsSequentialCell(cell->type.str())) continue;
      int deepest_input = -1;
      bool has_input = false;
      std::vector<Yosys::RTLIL::Wire*> outputs;
      for (const auto& port : cell->connections_) {
        if (cell->output(port.first)) {
          for (const auto& bit : port.second) {
            if (bit.wire != nullptr) outputs.push_back(bit.wire);
          }
        } else {
          for (const auto& bit : port.second) {
            if (bit.wire != nullptr) {
              has_input = true;
              const auto found = depth.find(bit.wire);
              if (found != depth.end() && found->second > deepest_input) {
                deepest_input = found->second;
              }
            }
          }
        }
      }
      if (!has_input || outputs.empty()) continue;
      const int candidate = deepest_input + 1;
      for (Yosys::RTLIL::Wire* wire : outputs) {
        if (candidate > depth[wire]) {
          depth[wire] = candidate;
          changed = true;
        }
      }
    }
    if (!changed) break;
  }
  for (const auto& entry : depth) {
    if (entry.second > stats.logic_depth) stats.logic_depth = entry.second;
  }
  return stats;
}

// Maximum hierarchy depth the Hierarchy walk descends: real
// designs nest a handful of levels, so 256 only bounds
// pathological input, never a legal design.
constexpr int kMaxHierarchyDepth = 256;

// Fills one hierarchy node from a live module: its path, module
// name, and ports, then one child per cell whose type names a
// module of the live design. Cell types starting with '$' are
// upstream primitives, never modules, so they are skipped before
// the module lookup. A module already on the current path
// contributes the node but no deeper children (the cycle guard);
// depth runs out the same way. Children are sorted by path, so
// the walk order never depends on Yosys's cell map order.
void BuildHierarchyNode(Yosys::RTLIL::Module* module, const std::string& path,
                        std::vector<std::string>& stack,
                        YosysEngine::HierarchyNode& node, int depth) {
  node.path = path;
  node.module = module->name.unescape();
  for (const auto& port : module->ports) {
    node.ports.push_back(port.unescape());
  }
  if (depth <= 0) return;
  if (std::find(stack.begin(), stack.end(), node.module) != stack.end()) {
    return;
  }
  stack.push_back(node.module);
  for (const auto& entry : module->cells_) {
    const std::string type = entry.second->type.unescape();
    if (type.empty() || type[0] == '$') continue;
    Yosys::RTLIL::Module* child_module = FindModule(type);
    if (child_module == nullptr) continue;
    YosysEngine::HierarchyNode child;
    BuildHierarchyNode(child_module,
                       path + "/" + entry.second->name.unescape(), stack,
                       child, depth - 1);
    node.children.push_back(std::move(child));
  }
  stack.pop_back();
  std::sort(node.children.begin(), node.children.end(),
            [](const YosysEngine::HierarchyNode& a,
               const YosysEngine::HierarchyNode& b) {
              return a.path < b.path;
            });
}

// Direction of a port wire from its RTLIL flags. A wire flagged
// both input and output, or neither, reads as "inout".
std::string PortDirection(Yosys::RTLIL::Wire* wire) {
  if (wire->port_input && !wire->port_output) return "in";
  if (wire->port_output && !wire->port_input) return "out";
  return "inout";
}

// Ports of a module in declaration order, with directions from
// the port wires. A port with no wire reads as "inout".
std::vector<YosysEngine::BlockPort> ModulePorts(
    Yosys::RTLIL::Module* module) {
  std::vector<YosysEngine::BlockPort> ports;
  if (module == nullptr) return ports;
  for (const auto& port : module->ports) {
    Yosys::RTLIL::Wire* wire = module->wire(port);
    YosysEngine::BlockPort block_port;
    block_port.name = port.unescape();
    block_port.direction = wire == nullptr ? "inout" : PortDirection(wire);
    ports.push_back(std::move(block_port));
  }
  return ports;
}

// Whether a cell is a submodule instance: its type names a module
// of the live design and is not an upstream primitive. Mirrors
// the Hierarchy walk's rule.
bool IsBlockCell(Yosys::RTLIL::Cell* cell) {
  const std::string type = cell->type.unescape();
  if (type.empty() || type[0] == '$') return false;
  return FindModule(type) != nullptr;
}

// Whether any bit of a connection touches a wire.
bool SigTouchesWire(const Yosys::RTLIL::SigSpec& sig,
                    Yosys::RTLIL::Wire* wire) {
  for (const auto& bit : sig.bits()) {
    if (bit.wire == wire) return true;
  }
  return false;
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
    // Point Yosys's share lookup at the build-tree staging
    // (CMakeLists stages techmap.v there at configure time).
    // Upstream resolves its share dir from the host executable,
    // which for EOS is a test binary or the shell, so without
    // this override techmap looks for /techmap.v and fails.
    // The override is set on every engine construction, so it
    // holds regardless of which engine ran setup first.
#ifdef EOS_YOSYS_SHARE_DIR
    Yosys::yosys_share_dirname = EOS_YOSYS_SHARE_DIR;
#endif
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

Result<YosysDesignSummary> YosysEngine::ParseVerilogFiles(
    const std::vector<std::string>& paths,
    const std::vector<std::string>& include_dirs,
    const std::vector<std::string>& defines) {
  if (!owns_) {
    return Status::Error(ErrorCode::kInvalidState,
                         "Yosys engine does not own the Yosys session");
  }
  if (paths.empty()) {
    return InvalidArgument("no Verilog files to parse");
  }
  for (const std::string& dir : include_dirs) {
    if (dir.empty()) {
      return InvalidArgument("empty include directory");
    }
    if (!IsSafeCommandToken(dir)) {
      return InvalidArgument("include directory is not a safe command "
                             "token: " +
                             dir);
    }
  }
  for (const std::string& define : defines) {
    const Status status = ValidateDefine(define);
    if (!status.ok()) return status;
  }
  // Every file is checked before Yosys is touched, so a missing
  // file fails fast with nothing parsed: the caller already
  // resolved the entries, but this method is a public surface
  // and must not depend on the caller's checks.
  for (const std::string& path : paths) {
    std::ifstream in(path);
    if (!in.good()) {
      return IoError("cannot read Verilog file", path);
    }
  }
  // Fresh design for this parse, without touching the session,
  // exactly as ParseVerilog does; the files then accumulate
  // into the one design, in order.
  delete Yosys::yosys_design;
  Yosys::yosys_design = new Yosys::RTLIL::Design;
  for (const std::string& path : paths) {
    try {
      auto guard = Yosys::logger().error_throw_scope();
      Yosys::run_pass(BuildReadVerilogCommand(path, include_dirs, defines));
    } catch (const Yosys::log_cmd_error_exception&) {
      return Status::Error(
          ErrorCode::kInvalidArgument,
          "Yosys rejected the read_verilog command for file: " + path);
    } catch (const std::exception& e) {
      return Status::Error(ErrorCode::kInvalidState,
                           std::string("Yosys failed: ") + e.what());
    }
  }
  return SummarizeDesign();
}

Result<YosysNetlistStats> YosysEngine::Elaborate(const std::string& top) {
  if (!owns_) {
    return InvalidState("Yosys engine does not own the Yosys session");
  }
  if (!IsPlainIdentifier(top)) {
    return InvalidArgument("top module name is not a plain identifier: " + top);
  }
  if (FindModule(top) == nullptr) {
    return InvalidArgument("top module not found in the parsed design: " + top);
  }
  // The unoptimized netlist: hierarchy, process lowering, FSM and
  // memory identification, and nothing else. Optimization passes
  // are synth_generic's job, so the elaborated stats are the
  // baseline the generic script is judged against.
  static const char* kPasses[] = {"hierarchy", "proc", "fsm", "memory"};
  for (const char* pass : kPasses) {
    std::string command(pass);
    if (std::string(pass) == "hierarchy") command += " -top " + top;
    const Status status = RunPass(command);
    if (!status.ok()) return status;
  }
  Yosys::RTLIL::Module* module = FindModule(top);
  if (module == nullptr) {
    return InvalidArgument("top module not found after elaboration: " + top);
  }
  return ComputeStats(module);
}

Result<YosysNetlistStats> YosysEngine::Stats(const std::string& top) {
  if (!owns_) {
    return InvalidState("Yosys engine does not own the Yosys session");
  }
  if (!IsPlainIdentifier(top)) {
    return InvalidArgument("top module name is not a plain identifier: " + top);
  }
  Yosys::RTLIL::Module* module = FindModule(top);
  if (module == nullptr) {
    return InvalidArgument("module not found in the live design: " + top);
  }
  return ComputeStats(module);
}

Result<YosysEngine::HierarchyNode> YosysEngine::Hierarchy(
    const std::string& top) {
  if (!owns_) {
    return InvalidState("Yosys engine does not own the Yosys session");
  }
  if (!IsPlainIdentifier(top)) {
    return InvalidArgument("top module name is not a plain identifier: " + top);
  }
  Yosys::RTLIL::Module* module = FindModule(top);
  if (module == nullptr) {
    return InvalidArgument("module not found in the live design: " + top);
  }
  YosysEngine::HierarchyNode root;
  std::vector<std::string> stack;
  BuildHierarchyNode(module, top, stack, root, kMaxHierarchyDepth);
  return root;
}

Result<YosysEngine::BlockDiagramData> YosysEngine::BlockDiagram(
    const std::string& module_name) {
  if (!owns_) {
    return InvalidState("Yosys engine does not own the Yosys session");
  }
  if (!IsPlainIdentifier(module_name)) {
    return InvalidArgument("module name is not a plain identifier: " +
                           module_name);
  }
  Yosys::RTLIL::Module* module = FindModule(module_name);
  if (module == nullptr) {
    return InvalidArgument("module not found in the live design: " +
                           module_name);
  }
  YosysEngine::BlockDiagramData diagram;
  diagram.module = module->name.unescape();
  diagram.parent_ports = ModulePorts(module);
  // Blocks: one per submodule-instance cell, sorted by instance.
  std::vector<Yosys::RTLIL::Cell*> block_cells;
  for (const auto& entry : module->cells_) {
    Yosys::RTLIL::Cell* cell = entry.second;
    if (!IsBlockCell(cell)) continue;
    block_cells.push_back(cell);
    YosysEngine::BlockInstance block;
    block.instance = cell->name.unescape();
    block.module = cell->type.unescape();
    block.ports = ModulePorts(FindModule(block.module));
    diagram.blocks.push_back(std::move(block));
  }
  std::sort(diagram.blocks.begin(), diagram.blocks.end(),
            [](const YosysEngine::BlockInstance& a,
               const YosysEngine::BlockInstance& b) {
              return a.instance < b.instance;
            });
  // Nets: one per wire that touches a block or a parent port.
  // Wires between primitives only are internal glue and are not
  // reported.
  for (const auto& entry : module->wires_) {
    Yosys::RTLIL::Wire* wire = entry.second;
    YosysEngine::BlockNet net;
    net.name = wire->name.unescape();
    if (wire->port_id != 0) net.terminals.push_back(net.name);
    for (Yosys::RTLIL::Cell* cell : block_cells) {
      for (const auto& connection : cell->connections()) {
        if (!SigTouchesWire(connection.second, wire)) continue;
        YosysEngine::BlockNetEndpoint endpoint;
        endpoint.block = cell->name.unescape();
        endpoint.port = connection.first.unescape();
        net.endpoints.push_back(std::move(endpoint));
      }
    }
    if (net.endpoints.empty() && net.terminals.empty()) continue;
    std::sort(net.endpoints.begin(), net.endpoints.end(),
              [](const YosysEngine::BlockNetEndpoint& a,
                 const YosysEngine::BlockNetEndpoint& b) {
                if (a.block != b.block) return a.block < b.block;
                return a.port < b.port;
              });
    diagram.nets.push_back(std::move(net));
  }
  std::sort(diagram.nets.begin(), diagram.nets.end(),
            [](const YosysEngine::BlockNet& a, const YosysEngine::BlockNet& b) {
              return a.name < b.name;
            });
  return diagram;
}

Status YosysEngine::AigMap() {
  if (!owns_) {
    return InvalidState("Yosys engine does not own the Yosys session");
  }
  // techmap first: it converts sequential cells to the simple
  // forms ($dff to $_DFF_P_) that the BLIF backend writes as
  // .latch lines; without it write_blif emits $dff as a .subckt
  // reference ABC cannot resolve. techmap reads its rules from
  // the staged share techmap.v (see the engine constructor).
  const Status techmap = RunPass("techmap");
  if (!techmap.ok()) return techmap;
  return RunPass("aigmap");
}

Status YosysEngine::WriteBlif(const std::string& path,
                              const std::string& top) {
  if (!owns_) {
    return InvalidState("Yosys engine does not own the Yosys session");
  }
  if (!IsPlainIdentifier(top)) {
    return InvalidArgument("top module name is not a plain identifier: " + top);
  }
  {
    std::ofstream out(path);
    if (!out.good()) {
      return IoError("cannot write BLIF file", path);
    }
  }
  const Status status = RunPass("write_blif -top " + top + " " + path);
  if (!status.ok()) return status;
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) ||
      std::filesystem::file_size(path, ec) == 0) {
    return IoError("Yosys wrote no BLIF file", path);
  }
  return Status::Ok();
}

Status YosysEngine::WriteVerilog(const std::string& path) {
  if (!owns_) {
    return InvalidState("Yosys engine does not own the Yosys session");
  }
  {
    std::ofstream out(path);
    if (!out.good()) {
      return IoError("cannot write Verilog file", path);
    }
  }
  // -noattr: ABC's Verilog reader rejects Yosys attribute lines
  // (it fails reading the module directive at the first one),
  // and the bridge file carries no information in attributes
  // that the optimization needs.
  const Status status = RunPass("write_verilog -noattr " + path);
  if (!status.ok()) return status;
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) ||
      std::filesystem::file_size(path, ec) == 0) {
    return IoError("Yosys wrote no Verilog file", path);
  }
  return Status::Ok();
}

}  // namespace eos::synth
