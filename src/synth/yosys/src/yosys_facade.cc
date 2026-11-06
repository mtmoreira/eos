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
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
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

// ---- Panel rework: engine-output capture and elaboration issues ----

// The captured Yosys log: one line per entry, oldest first,
// bounded so a caller that never drains stays bounded. The
// capturing sink below feeds it; TakeLog drains it.
constexpr std::size_t kMaxCapturedLogLines = 2000;
std::deque<std::string> g_captured_log_lines;

// A log sink that keeps a copy of Yosys output in memory for
// interfaces (the GUI log window) without touching the console
// sink: every message still reaches the console, and this sink
// is only a second copy. Messages arrive formatted
// (cached_msg), so lines are split here and stored without the
// trailing newline; empty lines are dropped.
class CapturingLogSink : public Yosys::LogSink {
 public:
  void log(const Yosys::LogMessage& msg) override {
    const std::string& text = msg.cached_msg;
    std::size_t start = 0;
    while (start < text.size()) {
      const std::size_t newline = text.find('\n', start);
      const std::string line =
          newline == std::string::npos
              ? text.substr(start)
              : text.substr(start, newline - start);
      if (!line.empty()) {
        g_captured_log_lines.push_back(line);
        while (g_captured_log_lines.size() > kMaxCapturedLogLines) {
          g_captured_log_lines.pop_front();
        }
      }
      if (newline == std::string::npos) break;
      start = newline + 1;
    }
  }
};

// Splits captured log text into lines, dropping empty ones, so
// a parser sees one finding per line.
std::vector<std::string> SplitLogLines(const std::string& text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start < text.size()) {
    const std::size_t newline = text.find('\n', start);
    const std::string line =
        newline == std::string::npos
            ? text.substr(start)
            : text.substr(start, newline - start);
    if (!line.empty()) lines.push_back(line);
    if (newline == std::string::npos) break;
    start = newline + 1;
  }
  return lines;
}

// Parses a Yosys src attribute ("file.v:12.5-12.20" or
// "file.v:12") into its file and first line. A missing or
// unparsable location leaves file empty and line 0: the issue
// is still reported, only without a source location.
void ParseSrcLocation(const std::string& src, std::string& file,
                      int& line) {
  file.clear();
  line = 0;
  if (src.empty()) return;
  // The attribute can carry several locations; the first is the
  // defining one.
  std::string first = src;
  const std::size_t cut = first.find_first_of(" ,");
  if (cut != std::string::npos) first = first.substr(0, cut);
  const std::size_t colon = first.rfind(':');
  if (colon == std::string::npos) {
    file = first;
    return;
  }
  file = first.substr(0, colon);
  const std::string rest = first.substr(colon + 1);
  std::size_t digits = 0;
  while (digits < rest.size() && rest[digits] >= '0' &&
         rest[digits] <= '9') {
    ++digits;
  }
  // No digits, or a digit run too long for an int: the file is
  // the part before the colon and the line stays unknown.
  if (digits == 0 || digits > 9) return;
  line = std::stoi(rest.substr(0, digits));
}

// Severity order for issue sorting: errors first, then
// warnings, then info.
int IssueSeverityRank(const std::string& severity) {
  if (severity == "error") return 0;
  if (severity == "warning") return 1;
  return 2;
}

// Maps every module in the hierarchy to the first
// (path-sorted) instance path that instantiates it, so a
// diagnostic naming a module can name the hierarchy path the
// GUI selects. The top module maps to its own name. Modules
// the hierarchy never reaches keep no entry; callers fall back
// to the module name.
void ModuleInstancePaths(const YosysEngine::HierarchyNode& node,
                         std::map<std::string, std::string>& paths) {
  if (paths.find(node.module) == paths.end()) {
    paths[node.module] = node.path;
  }
  for (const auto& child : node.children) {
    ModuleInstancePaths(child, paths);
  }
}

std::string ModulePathFor(
    const std::string& module,
    const std::map<std::string, std::string>& paths) {
  const auto found = paths.find(module);
  return found == paths.end() ? module : found->second;
}

// Finds a live cell or wire by its unescaped name, so a
// diagnostic naming it can read its source location. The
// check pass prints names in their stored form, which matches
// the unescaped name for the plain and $-prefixed names this
// flow produces; a name that matches neither form simply has
// no source location.
Yosys::RTLIL::Cell* FindCellByName(Yosys::RTLIL::Module* module,
                                   const std::string& name) {
  if (module == nullptr) return nullptr;
  for (const auto& entry : module->cells_) {
    if (entry.second->name.unescape() == name ||
        entry.second->name.str() == name) {
      return entry.second;
    }
  }
  return nullptr;
}

Yosys::RTLIL::Wire* FindWireByName(Yosys::RTLIL::Module* module,
                                   const std::string& name) {
  if (module == nullptr) return nullptr;
  for (const auto& entry : module->wires_) {
    if (entry.second->name.unescape() == name ||
        entry.second->name.str() == name) {
      return entry.second;
    }
  }
  return nullptr;
}

// The check pass prints wire and cell names in RTLIL escaped
// form, which prefixes a user identifier with a backslash
// ("\y", "\u_and1") while $-prefixed names carry none. The
// design stores the same objects under the unescaped name
// ("y", "u_and1"), so a parsed name keeps its backslash only
// when the design does not. Strip one leading backslash so a
// parsed target matches the name the walk and the GUI use.
std::string StripLeadingBackslash(const std::string& name) {
  if (!name.empty() && name[0] == '\\') return name.substr(1);
  return name;
}

// Strips a trailing "[n]" bit selector from a wire name the
// check pass prints (log_signal names one bit of a vector as
// "wire[n]"), so the target names the wire, not one bit of it.
std::string StripBitSelector(const std::string& name) {
  if (name.empty() || name.back() != ']') return name;
  const std::size_t bracket = name.rfind('[');
  if (bracket == std::string::npos) return name;
  for (std::size_t i = bracket + 1; i + 1 < name.size(); ++i) {
    if (name[i] < '0' || name[i] > '9') return name;
  }
  return name.substr(0, bracket);
}

// Reads the source location of a live cell or wire into an
// issue's file and line, leaving them empty and 0 when the
// object has none.
void FillSrcFromCell(Yosys::RTLIL::Module* module,
                     const std::string& cell_name, YosysEngine::YosysIssue& issue) {
  Yosys::RTLIL::Cell* cell = FindCellByName(module, cell_name);
  if (cell == nullptr) return;
  ParseSrcLocation(cell->get_src_attribute(), issue.file, issue.line);
}

void FillSrcFromWire(Yosys::RTLIL::Module* module,
                     const std::string& wire_name, YosysEngine::YosysIssue& issue) {
  Yosys::RTLIL::Wire* wire = FindWireByName(module, wire_name);
  if (wire == nullptr) return;
  ParseSrcLocation(wire->get_src_attribute(), issue.file, issue.line);
}

// Parses the captured output of "check -nolatches" into
// issues. Only lines starting with "Warning: " carry findings;
// the pass header, the per-module "Checking module ..." lines,
// and the "Found and reported N problems." summary are skipped.
// Multi-line findings (multiple drivers, loops) consume their
// indented body lines for the first cell and source location.
// Severity follows the reviews, not the pass: the pass logs
// everything as a warning, but multiple drivers and loops are
// errors (they break synthesis and placement), while latches,
// undriven nets, and init attributes stay warnings.
void ParseCheckLog(const std::string& log, const std::string& top,
                   const std::map<std::string, std::string>& module_paths,
                   std::vector<YosysEngine::YosysIssue>& issues) {
  const std::vector<std::string> lines = SplitLogLines(log);
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const std::string& line = lines[i];
    if (line.rfind("Warning: ", 0) != 0) continue;
    const std::string body = line.substr(9);
    // The indented body of a multi-line finding, if any.
    std::vector<std::string> detail;
    while (i + 1 < lines.size() &&
           lines[i + 1].rfind("    ", 0) == 0) {
      detail.push_back(lines[++i]);
    }
    // First "source: <src>" in the body, for the loop's first
    // cell or wire.
    std::string detail_file;
    int detail_line = 0;
    std::string detail_target;
    for (const std::string& detail_line_text : detail) {
      const std::size_t cell_at = detail_line_text.find("cell ");
      if (detail_target.empty() && cell_at != std::string::npos) {
        const std::size_t start = cell_at + 5;
        const std::size_t end = detail_line_text.find(' ', start);
        if (end != std::string::npos) {
          detail_target = StripLeadingBackslash(
              detail_line_text.substr(start, end - start));
        }
      }
      const std::size_t src_at = detail_line_text.find("source: ");
      if (src_at != std::string::npos) {
        ParseSrcLocation(detail_line_text.substr(src_at + 8),
                         detail_file, detail_line);
        break;
      }
    }
    YosysEngine::YosysIssue issue;
    if (body.rfind("Cell ", 0) == 0) {
      const std::size_t dot = body.find('.', 5);
      if (dot == std::string::npos) continue;
      const std::string module_name =
          StripLeadingBackslash(body.substr(5, dot - 5));
      const std::size_t unknown_at =
          body.find(" has unknown internal type ");
      const std::size_t latch_at = body.find(" is a latch of type ");
      if (unknown_at != std::string::npos) {
        issue.severity = "error";
        issue.code = "unknown_cell_type";
        issue.target = StripLeadingBackslash(
            body.substr(dot + 1, unknown_at - dot - 1));
        issue.message = "Cell " + issue.target + " in module " +
                        module_name + " has an unknown internal type";
        issue.module = module_name;
        issue.path = ModulePathFor(module_name, module_paths);
        FillSrcFromCell(FindModule(module_name), issue.target, issue);
      } else if (latch_at != std::string::npos) {
        issue.severity = "warning";
        issue.code = "inferred_latch";
        issue.target = StripLeadingBackslash(
            body.substr(dot + 1, latch_at - dot - 1));
        issue.message = "Cell " + issue.target + " in module " +
                        module_name + " is an inferred latch";
        issue.module = module_name;
        issue.path = ModulePathFor(module_name, module_paths);
        FillSrcFromCell(FindModule(module_name), issue.target, issue);
      } else {
        continue;
      }
    } else if (body.rfind("Wire ", 0) == 0) {
      const std::size_t dot = body.find('.', 5);
      if (dot == std::string::npos) continue;
      const std::string module_name =
          StripLeadingBackslash(body.substr(5, dot - 5));
      const std::size_t undriven_at =
          body.find(" is used but has no driver.");
      const std::size_t init_at =
          body.find(" has an unprocessed 'init' attribute.");
      if (undriven_at != std::string::npos) {
        issue.severity = "warning";
        issue.code = "undriven_net";
        issue.target = StripLeadingBackslash(
            StripBitSelector(body.substr(dot + 1, undriven_at - dot - 1)));
        issue.message = "Wire " + issue.target + " in module " +
                        module_name + " is used but has no driver";
        issue.module = module_name;
        issue.path = ModulePathFor(module_name, module_paths);
        FillSrcFromWire(FindModule(module_name), issue.target, issue);
      } else if (init_at != std::string::npos) {
        issue.severity = "warning";
        issue.code = "init_attribute";
        issue.target = StripLeadingBackslash(
            StripBitSelector(body.substr(dot + 1, init_at - dot - 1)));
        issue.message = "Wire " + issue.target + " in module " +
                        module_name + " has an unprocessed 'init' attribute";
        issue.module = module_name;
        issue.path = ModulePathFor(module_name, module_paths);
        FillSrcFromWire(FindModule(module_name), issue.target, issue);
      } else {
        continue;
      }
    } else if (body.rfind("multiple conflicting drivers for ", 0) == 0) {
      const std::string rest =
          body.substr(std::string("multiple conflicting drivers for ").size());
      const std::size_t dot = rest.find('.');
      if (dot == std::string::npos) continue;
      const std::string module_name =
          StripLeadingBackslash(rest.substr(0, dot));
      const std::size_t colon = rest.find(':', dot);
      if (colon == std::string::npos) continue;
      issue.severity = "error";
      issue.code = "multiple_drivers";
      issue.target = StripLeadingBackslash(
          StripBitSelector(rest.substr(dot + 1, colon - dot - 1)));
      issue.message = "Wire " + issue.target + " in module " +
                      module_name + " has multiple conflicting drivers";
      issue.module = module_name;
      issue.path = ModulePathFor(module_name, module_paths);
      if (!detail_target.empty()) {
        FillSrcFromCell(FindModule(module_name), detail_target, issue);
      }
      if (issue.file.empty()) {
        FillSrcFromWire(FindModule(module_name), issue.target, issue);
      }
    } else if (body.rfind("Drivers conflicting with a constant ", 0) == 0) {
      issue.severity = "error";
      issue.code = "multiple_drivers";
      issue.message = body;
      issue.module = top;
      issue.path = ModulePathFor(top, module_paths);
    } else if (body.rfind("found logic loop in module ", 0) == 0) {
      const std::string module_name = StripLeadingBackslash(
          body.substr(std::string("found logic loop in module ").size(),
                      body.size() -
                          std::string("found logic loop in module ").size() -
                          1));
      issue.severity = "error";
      issue.code = "combinational_loop";
      issue.message =
          "Combinational loop in module " + module_name;
      issue.module = module_name;
      issue.path = ModulePathFor(module_name, module_paths);
      issue.target = detail_target;
      issue.file = detail_file;
      issue.line = detail_line;
    } else {
      continue;
    }
    issues.push_back(std::move(issue));
  }
}

// Adds the design-walk issues: unresolved references and
// black-box instantiations (which the check pass does not
// report), unconnected cell ports and dead module inputs, top
// ports, and empty modules. The walk reads the live design
// only; it runs no pass.
void WalkDesignIssues(const std::string& top,
                      const std::map<std::string, std::string>& module_paths,
                      std::vector<YosysEngine::YosysIssue>& issues) {
  if (Yosys::yosys_design == nullptr) return;
  for (const auto& entry : Yosys::yosys_design->modules_) {
    Yosys::RTLIL::Module* module = entry.second;
    const std::string module_name = module->name.unescape();
    const std::string path = ModulePathFor(module_name, module_paths);
    if (module->cells_.empty() && module->connections_.empty() &&
        !module->get_blackbox_attribute()) {
      issues.push_back(YosysEngine::YosysIssue{"info", "empty_module",
                                  "Module " + module_name + " has no cells",
                                  module_name, path, "", "", 0});
    }
    if (module_name == top) {
      bool has_input = false;
      bool has_output = false;
      for (const auto& port : module->ports) {
        Yosys::RTLIL::Wire* wire = module->wire(port);
        if (wire == nullptr) continue;
        if (wire->port_input) has_input = true;
        if (wire->port_output) has_output = true;
      }
      if (!has_input) {
        issues.push_back(YosysEngine::YosysIssue{"warning", "top_no_ports",
                                    "Top module " + top +
                                        " has no input ports",
                                    module_name, path, "", "", 0});
      }
      if (!has_output) {
        issues.push_back(YosysEngine::YosysIssue{"warning", "top_no_ports",
                                    "Top module " + top +
                                        " has no output ports",
                                    module_name, path, "", "", 0});
      }
    }
    for (const auto& cell_entry : module->cells_) {
      Yosys::RTLIL::Cell* cell = cell_entry.second;
      const std::string cell_name = cell->name.unescape();
      const std::string type = cell->type.unescape();
      std::string file;
      int line = 0;
      ParseSrcLocation(cell->get_src_attribute(), file, line);
      if (!type.empty() && type[0] != '$') {
        Yosys::RTLIL::Module* target_module = FindModule(type);
        if (target_module == nullptr) {
          issues.push_back(YosysEngine::YosysIssue{
              "error", "unresolved_reference",
              "Module " + type + " instantiated at " + path + "/" +
                  cell_name + " is not defined",
              module_name, path, cell_name, file, line});
        } else if (target_module->get_blackbox_attribute()) {
          issues.push_back(YosysEngine::YosysIssue{
              "warning", "blackbox_instantiation",
              "Module " + type + " instantiated at " + path + "/" +
                  cell_name + " is a black box (no RTL interior)",
              module_name, path, cell_name, file, line});
        }
      }
      for (const auto& connection : cell->connections()) {
        if (connection.second.empty()) {
          issues.push_back(YosysEngine::YosysIssue{
              "warning", "unconnected_port",
              "Port " + connection.first.unescape() + " of cell " +
                  cell_name + " is unconnected",
              module_name, path, cell_name, file, line});
        }
      }
    }
    for (const auto& port : module->ports) {
      Yosys::RTLIL::Wire* wire = module->wire(port);
      if (wire == nullptr || !wire->port_input || wire->port_output) {
        continue;
      }
      bool touched = false;
      for (const auto& cell_entry : module->cells_) {
        for (const auto& connection : cell_entry.second->connections()) {
          if (SigTouchesWire(connection.second, wire)) {
            touched = true;
            break;
          }
        }
        if (touched) break;
      }
      // A continuous assign also uses the wire: a port read only
      // by "assign y = a;" is connected, even though no cell
      // touches it. The check pass counts the same way (cell
      // ports and module inputs are drivers, not module
      // connections), so this is the walk's own correction.
      if (!touched) {
        for (const auto& connection : module->connections_) {
          if (SigTouchesWire(connection.first, wire) ||
              SigTouchesWire(connection.second, wire)) {
            touched = true;
            break;
          }
        }
      }
      if (!touched) {
        std::string file;
        int line = 0;
        ParseSrcLocation(wire->get_src_attribute(), file, line);
        issues.push_back(YosysEngine::YosysIssue{
            "warning", "unconnected_port",
            "Input port " + port.unescape() + " of module " +
                module_name + " drives nothing",
            module_name, path, port.unescape(), file, line});
      }
    }
  }
}

}  // namespace

YosysEngine::YosysEngine() {
  if (!g_engine_alive.exchange(true)) {
    owns_ = true;
    if (!g_yosys_setup_done) {
      Yosys::yosys_setup();
      Yosys::logger().add_sink<Yosys::ConsoleLogSink>();
      Yosys::logger().add_sink<CapturingLogSink>();
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

std::string YosysEngine::TakeLog() {
  std::string out;
  for (const std::string& line : g_captured_log_lines) {
    if (!out.empty()) out += '\n';
    out += line;
  }
  g_captured_log_lines.clear();
  return out;
}

Result<std::vector<YosysEngine::YosysIssue>> YosysEngine::Issues(
    const std::string& top) {
  if (!owns_) {
    return InvalidState("Yosys engine does not own the Yosys session");
  }
  if (!IsPlainIdentifier(top)) {
    return InvalidArgument("top module name is not a plain identifier: " + top);
  }
  if (FindModule(top) == nullptr) {
    return InvalidArgument("top module not found in the live design: " + top);
  }
  // Instance paths for diagnostics come from the hierarchy
  // walk: children are path-sorted, so the first path recorded
  // for a module is its smallest instance path.
  const Result<HierarchyNode> hierarchy = Hierarchy(top);
  if (!hierarchy.ok()) return hierarchy.status();
  std::map<std::string, std::string> module_paths;
  ModuleInstancePaths(hierarchy.value(), module_paths);
  std::vector<YosysEngine::YosysIssue> issues;
  // The check pass's own findings, captured from its log under
  // a scoped sink so the capture stays local to this call. The
  // check output also reaches the permanent capturing sink, so
  // the log window still shows the raw check lines.
  std::ostringstream captured;
  Status check_status;
  {
    Yosys::LogManager::Scoped scope(Yosys::logger());
    Yosys::logger().add_sink<Yosys::StreamLogSink>(captured);
    check_status = RunPass("check -nolatches");
  }
  if (!check_status.ok()) return check_status;
  ParseCheckLog(captured.str(), top, module_paths, issues);
  WalkDesignIssues(top, module_paths, issues);
  std::sort(issues.begin(), issues.end(),
            [](const YosysEngine::YosysIssue& a, const YosysEngine::YosysIssue& b) {
              const int rank_a = IssueSeverityRank(a.severity);
              const int rank_b = IssueSeverityRank(b.severity);
              if (rank_a != rank_b) return rank_a < rank_b;
              if (a.path != b.path) return a.path < b.path;
              if (a.code != b.code) return a.code < b.code;
              return a.target < b.target;
            });
  return issues;
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

Result<YosysEngine::SchematicData> YosysEngine::Schematic(
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
  YosysEngine::SchematicData schematic;
  schematic.module = module->name.unescape();
  schematic.parent_ports = ModulePorts(module);
  // Gates: one per primitive cell, sorted by instance. A cell
  // connection with no wire bits (an unconnected or
  // constant-only port) still lists as a port: the gate's
  // interface is the cell's, and such a port simply touches no
  // net.
  std::vector<Yosys::RTLIL::Cell*> gate_cells;
  for (const auto& entry : module->cells_) {
    Yosys::RTLIL::Cell* cell = entry.second;
    if (IsBlockCell(cell)) continue;
    gate_cells.push_back(cell);
    YosysEngine::SchematicGate gate;
    gate.instance = cell->name.unescape();
    gate.type = cell->type.unescape();
    for (const auto& connection : cell->connections()) {
      YosysEngine::SchematicPort port;
      port.name = connection.first.unescape();
      port.direction = cell->output(connection.first) ? "out" : "in";
      gate.ports.push_back(std::move(port));
    }
    std::sort(gate.ports.begin(), gate.ports.end(),
              [](const YosysEngine::SchematicPort& a,
                 const YosysEngine::SchematicPort& b) {
                return a.name < b.name;
              });
    schematic.gates.push_back(std::move(gate));
  }
  std::sort(schematic.gates.begin(), schematic.gates.end(),
            [](const YosysEngine::SchematicGate& a,
               const YosysEngine::SchematicGate& b) {
              return a.instance < b.instance;
            });
  // Nets: one per wire that touches a gate or a parent port.
  // A wire that only touches submodule instances is skipped,
  // as in BlockDiagram.
  for (const auto& entry : module->wires_) {
    Yosys::RTLIL::Wire* wire = entry.second;
    YosysEngine::SchematicNet net;
    net.name = wire->name.unescape();
    net.width = wire->width;
    if (wire->port_id != 0) net.terminals.push_back(net.name);
    for (Yosys::RTLIL::Cell* cell : gate_cells) {
      for (const auto& connection : cell->connections()) {
        if (!SigTouchesWire(connection.second, wire)) continue;
        YosysEngine::SchematicNetEndpoint endpoint;
        endpoint.gate = cell->name.unescape();
        endpoint.port = connection.first.unescape();
        net.endpoints.push_back(std::move(endpoint));
      }
    }
    if (net.endpoints.empty() && net.terminals.empty()) continue;
    std::sort(net.endpoints.begin(), net.endpoints.end(),
              [](const YosysEngine::SchematicNetEndpoint& a,
                 const YosysEngine::SchematicNetEndpoint& b) {
                if (a.gate != b.gate) return a.gate < b.gate;
                return a.port < b.port;
              });
    schematic.nets.push_back(std::move(net));
  }
  std::sort(schematic.nets.begin(), schematic.nets.end(),
            [](const YosysEngine::SchematicNet& a,
               const YosysEngine::SchematicNet& b) {
              return a.name < b.name;
            });
  return schematic;
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
