// registry: the core command set (L6 interface).
//
// Every handler follows the same discipline: validate the argument
// shape first (unsupported options are rejected, never ignored), then
// the session state (an unclosed design is its own diagnostic), then
// run the operation and map its Status onto the diagnostic taxonomy.
#include "eos/ui/registry.h"

#include "eos/analysis/sta.h"
#include "eos/pd/floorplan.h"
#include "odb/db.h"
#include "eos/session/flow.h"
#include "eos/synth/abc.h"
#include "eos/synth/yosys.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>


namespace eos::ui {
namespace {

using foundation::ErrorCode;
using foundation::ObjectKind;
using foundation::Result;

CommandResult FromStatus(const foundation::Status& status) {
  Diag diag = Diag::kInvalidConfiguration;
  if (status.code() == ErrorCode::kUnsupported) {
    diag = Diag::kUnsupportedFeature;
  } else if (status.code() == ErrorCode::kIoError) {
    diag = Diag::kIoError;
  }
  return CommandResult::Fail(diag, status.message());
}

std::optional<CommandResult> NeedDesign(const CommandContext& ctx) {
  if (!ctx.HasDesign()) {
    return CommandResult::Fail(Diag::kUnclosedDesign,
                               "no design is open (see eos::create_design)");
  }
  return std::nullopt;
}

std::optional<CommandResult> ExpectCount(
    const std::vector<std::string>& args, std::size_t count,
    const std::string& usage) {
  if (args.size() != count) {
    return CommandResult::Fail(Diag::kInvalidConfiguration,
                               "usage: eos::" + usage);
  }
  return std::nullopt;
}

// Collateral reads fail fast: a path that cannot be opened is an
// error here, not a deferred surprise at run_timing time. The
// commands below only recorded the path string, so a missing file
// reported success and the failure surfaced one command later.
std::optional<CommandResult> CheckReadable(const std::string& path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) {
    return CommandResult::Fail(Diag::kIoError,
                               "could not read file: " + path);
  }
  std::ifstream in(path);
  if (!in) {
    return CommandResult::Fail(Diag::kIoError,
                               "could not read file: " + path);
  }
  return std::nullopt;
}

bool ParseU64(const std::string& token, std::uint64_t& out) {
  const char* first = token.data();
  const char* last = first + token.size();
  const auto result = std::from_chars(first, last, out);
  return result.ec == std::errc() && result.ptr == last;
}

// Database-unit coordinates may be negative (a die area need not
// start at the origin), so floorplan areas parse as signed ints,
// unlike the unsigned counts ParseU64 serves elsewhere.
bool ParseCoord(const std::string& token, int& out) {
  const char* first = token.data();
  const char* last = first + token.size();
  const auto result = std::from_chars(first, last, out);
  return result.ec == std::errc() && result.ptr == last;
}

std::optional<session::ProofStatus> ParseProof(const std::string& word) {
  if (word == "pass") return session::ProofStatus::kPass;
  if (word == "fail") return session::ProofStatus::kFail;
  if (word == "unknown") return session::ProofStatus::kUnknown;
  return std::nullopt;
}

const char* ProofWord(session::ProofStatus proof) {
  switch (proof) {
    case session::ProofStatus::kPass: return "pass";
    case session::ProofStatus::kFail: return "fail";
    case session::ProofStatus::kUnknown: return "unknown";
  }
  return "unknown";
}

const char* StatusWord(db::BindingStatus status) {
  switch (status) {
    case db::BindingStatus::kBound: return "bound";
    case db::BindingStatus::kUnresolved: return "unresolved";
    case db::BindingStatus::kEmpty: return "empty";
  }
  return "unresolved";
}

const char* ConstraintKindWord(db::ConstraintKind kind) {
  switch (kind) {
    case db::ConstraintKind::kClock: return "clock";
    case db::ConstraintKind::kInputDelay: return "input_delay";
    case db::ConstraintKind::kOutputDelay: return "output_delay";
    case db::ConstraintKind::kFalsePath: return "false_path";
    case db::ConstraintKind::kMulticyclePath: return "multicycle";
    case db::ConstraintKind::kMaxDelay: return "max_delay";
    case db::ConstraintKind::kMinDelay: return "min_delay";
    case db::ConstraintKind::kCaseAnalysis: return "case_analysis";
  }
  return "clock";
}

std::optional<db::ConstraintKind> ParseConstraintKind(
    const std::string& word) {
  if (word == "clock") return db::ConstraintKind::kClock;
  if (word == "input_delay") return db::ConstraintKind::kInputDelay;
  if (word == "output_delay") return db::ConstraintKind::kOutputDelay;
  if (word == "false_path") return db::ConstraintKind::kFalsePath;
  if (word == "multicycle") return db::ConstraintKind::kMulticyclePath;
  if (word == "max_delay") return db::ConstraintKind::kMaxDelay;
  if (word == "min_delay") return db::ConstraintKind::kMinDelay;
  if (word == "case_analysis") return db::ConstraintKind::kCaseAnalysis;
  return std::nullopt;
}

std::optional<ObjectKind> ParseObjectKind(const std::string& word) {
  if (word == "cell") return ObjectKind::kCell;
  if (word == "instance") return ObjectKind::kInstance;
  if (word == "net") return ObjectKind::kNet;
  if (word == "port") return ObjectKind::kPort;
  if (word == "pin") return ObjectKind::kPin;
  return std::nullopt;
}

// Anchored glob: '*' matches any run (including empty), '?' exactly
// one character, '\' escapes the next character.
bool GlobMatch(const std::string& pattern, const std::string& name) {
  std::size_t p = 0;
  std::size_t n = 0;
  std::size_t star_p = std::string::npos;
  std::size_t star_n = 0;
  while (n < name.size()) {
    if (p < pattern.size() && pattern[p] == '*') {
      star_p = p++;
      star_n = n;
      continue;
    }
    bool matched = false;
    if (p < pattern.size()) {
      if (pattern[p] == '\\' && p + 1 < pattern.size()) {
        matched = pattern[p + 1] == name[n];
        if (matched) p += 2;
      } else if (pattern[p] == '?') {
        matched = true;
        p += 1;
      } else {
        matched = pattern[p] == name[n];
        if (matched) p += 1;
      }
    }
    if (matched) {
      n += 1;
      continue;
    }
    if (star_p != std::string::npos) {
      p = star_p + 1;
      n = ++star_n;
      continue;
    }
    return false;
  }
  while (p < pattern.size() && pattern[p] == '*') {
    p += 1;
  }
  return p == pattern.size();
}

// Enumerates a design's objects of one kind by walking the per-kind
// ID space (IDs are dense per kind, and the design is append-only).
std::vector<ObjectId> EnumerateKind(const db::Design& design,
                                    ObjectKind kind) {
  std::vector<ObjectId> out;
  const std::uint32_t next = design.allocator().next_index(kind);
  for (std::uint32_t i = 1; i < next; ++i) {
    const ObjectId id = ObjectId::Make(kind, i);
    if (design.Exists(id)) {
      out.push_back(id);
    }
  }
  return out;
}

std::string IdStr(ObjectId id) { return std::to_string(id.raw()); }

void ResetProject(CommandContext& ctx) {
  ctx.project.Reset();
  ctx.collections.clear();
  ctx.next_collection = 1;
  ctx.timing = CommandContext::TimingSession();
  ctx.synth = CommandContext::SynthSession();
  ctx.wire_rc_overrides.clear();
}

// A scratch path for a synthesis bridge file: the elaborated
// netlist Yosys hands ABC (BLIF), and the optimized netlist ABC
// hands back (Verilog). The optimized file is session state
// (the synth session records its path); the bridge file is
// deleted by its caller once ABC has read it. The process id is
// part of the name: a bare per-process counter collides across
// processes sharing /tmp, so two shells (or a test binary and a
// shell) would otherwise read each other's bridge files.
std::string SynthTempPath(const std::string& stem,
                          const std::string& extension) {
  static std::atomic<unsigned long> counter{0};
  return (std::filesystem::temp_directory_path() /
          ("eos_synth_" + stem + "_" +
           std::to_string(static_cast<long long>(::getpid())) + "_" +
           std::to_string(counter.fetch_add(1)) + extension))
      .string();
}

// Counts the mapped cells of one ABC mapped Verilog file: each
// gate line starts with its cell name, so a line whose first
// token is a genlib cell counts once for that cell. Shared by
// synth_map and synth_opt so the two commands never disagree
// about what a mapped netlist holds.
void ScanMappedCells(const std::string& path,
                     const std::set<std::string>& cell_set,
                     std::map<std::string, int>* by_type, int* total) {
  by_type->clear();
  *total = 0;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    const std::size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos) continue;
    const std::size_t end = line.find_first_of(" \t", first);
    const std::string token =
        line.substr(first, end == std::string::npos ? std::string::npos
                                                    : end - first);
    if (cell_set.count(token) == 0) continue;
    ++(*by_type)[token];
    ++(*total);
  }
}

std::string CellTypesText(const std::map<std::string, int>& by_type) {
  std::string types;
  for (const auto& entry : by_type) {
    if (!types.empty()) types += ",";
    types += entry.first + ":" + std::to_string(entry.second);
  }
  return types;
}

// File-list format (synthesis unit S4): one source entry per
// line. Blank lines are ignored, and a line whose first
// non-blank characters are '#' or '//' is a comment. An entry
// is resolved against the list file's directory unless it is
// absolute; a list given with no directory (a bare filename)
// resolves its entries against the working directory, which
// is where the list itself lives. The format is deliberately
// narrow: no inline comments, no wildcards, no nesting, so a
// list always means exactly the entries it names, in order.
// This is EOS-owned input handling, not upstream's
// read_verilog_file_list: that pass takes no -I or -D flags
// and aborts the process (exit 1) on a missing list file,
// while this command fails fast with a Status naming the
// list and the entry. File scope, not a registration-local
// lambda: command handlers outlive the registration function,
// so anything they capture must outlive it too.
struct FilelistEntry {
  std::string as_written;
  std::string resolved;
};

std::string TrimFilelistLine(const std::string& text) {
  const std::size_t first = text.find_first_not_of(" \t\r");
  if (first == std::string::npos) return std::string();
  const std::size_t last = text.find_last_not_of(" \t\r");
  return text.substr(first, last - first + 1);
}

// A -D token is NAME or NAME=VALUE, where NAME is a plain
// Verilog identifier, and neither a define token nor an
// include directory may contain whitespace or ';': both are
// interpolated into a Yosys pass command string, which Yosys
// tokenizes on whitespace and splits on ';' (the same rule
// that makes a top name a plain identifier in the facade).
// The registry validates these before the engine runs, so a
// bad flag is a pre-engine rejection that leaves the session
// untouched; the facade validates again as a public surface.
bool IsFilelistCommandToken(const std::string& token) {
  for (const char c : token) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ';') {
      return false;
    }
  }
  return true;
}

bool IsFilelistDefine(const std::string& define) {
  if (define.empty() || !IsFilelistCommandToken(define)) return false;
  const std::size_t equal = define.find('=');
  const std::string name =
      equal == std::string::npos ? define : define.substr(0, equal);
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

// Reads one list file into entries. Returns an error string on
// failure, empty on success; an empty list is not an error
// here, the caller reports it with the list's name.
std::string ReadFilelistEntries(const std::string& list_path,
                                std::vector<FilelistEntry>* entries) {
  std::ifstream in(list_path);
  if (!in.good()) return "could not read file: " + list_path;
  const std::filesystem::path base =
      std::filesystem::path(list_path).parent_path();
  std::string line;
  while (std::getline(in, line)) {
    const std::string entry = TrimFilelistLine(line);
    if (entry.empty()) continue;
    if (entry[0] == '#') continue;
    if (entry.size() >= 2 && entry[0] == '/' && entry[1] == '/') {
      continue;
    }
    std::filesystem::path resolved(entry);
    if (!resolved.is_absolute()) resolved = base / resolved;
    entries->push_back(FilelistEntry{entry, resolved.string()});
  }
  return std::string();
}

std::string SynthStatsText(const synth::YosysNetlistStats& stats) {
  std::string types;
  for (const synth::YosysCellTypeCount& entry : stats.cells_by_type) {
    if (!types.empty()) types += ",";
    types += entry.type + ":" + std::to_string(entry.count);
  }
  return "module=" + stats.module +
         " cells=" + std::to_string(stats.cell_count) +
         " wires=" + std::to_string(stats.wire_count) +
         " depth=" + std::to_string(stats.logic_depth) +
         " types=" + types;
}

// Renders one hierarchy node per line, pre-order, so a consumer
// rebuilds the tree from paths alone: a node's parent is its path
// up to the last "/". Port names cannot contain commas or spaces
// (plain Verilog identifiers), so the comma list is unambiguous,
// and an empty list renders as an empty value, never omitted.
void HierarchyTextLines(const synth::YosysEngine::HierarchyNode& node,
                        std::string& out) {
  std::string ports;
  for (const std::string& port : node.ports) {
    if (!ports.empty()) ports += ",";
    ports += port;
  }
  out += "node path=" + node.path + " module=" + node.module +
         " ports=" + ports + "\n";
  for (const synth::YosysEngine::HierarchyNode& child : node.children) {
    HierarchyTextLines(child, out);
  }
}

std::string HierarchyText(const synth::YosysEngine::HierarchyNode& root) {
  std::string out = "top=" + root.module + "\n";
  HierarchyTextLines(root, out);
  return out;
}

// Renders one netlist stats record in the same key=value line
// style as HierarchyText, so the GUI model parses it with the
// same rules: a "module=<module>" line, a
// "cells=<n> wires=<n> depth=<n> sequential=<n>" line, then one
// "type <name> <count>" line per cell type, in the facade's
// name-sorted order. A design with no cells renders no type
// lines, never an omitted stats line.
std::string StatsText(const synth::YosysNetlistStats& stats) {
  std::string out = "module=" + stats.module + "\n";
  out += "cells=" + std::to_string(stats.cell_count) +
         " wires=" + std::to_string(stats.wire_count) +
         " depth=" + std::to_string(stats.logic_depth) +
         " sequential=" + std::to_string(stats.sequential_cells) + "\n";
  for (const auto& type : stats.cells_by_type) {
    out += "type " + type.type + " " + std::to_string(type.count) + "\n";
  }
  return out;
}

// Renders one gate-level schematic in the same key=value line
// style as BlockDiagramText, so the GUI model parses it with the
// same rules: names are plain Verilog identifiers, so comma
// lists are unambiguous, and an empty list renders as an empty
// value, never omitted.
std::string SchematicText(
    const synth::YosysEngine::SchematicData& schematic) {
  std::string out = "module=" + schematic.module + "\n";
  for (const auto& port : schematic.parent_ports) {
    out += "parent port=" + port.name + " direction=" + port.direction +
           "\n";
  }
  for (const auto& gate : schematic.gates) {
    std::string ports;
    for (const auto& port : gate.ports) {
      if (!ports.empty()) ports += ",";
      ports += port.name + ":" + port.direction;
    }
    out += "gate instance=" + gate.instance + " type=" + gate.type +
           " ports=" + ports + "\n";
  }
  for (const auto& net : schematic.nets) {
    std::string gates;
    for (const auto& endpoint : net.endpoints) {
      if (!gates.empty()) gates += ",";
      gates += endpoint.gate + ":" + endpoint.port;
    }
    std::string terminals;
    for (const auto& terminal : net.terminals) {
      if (!terminals.empty()) terminals += ",";
      terminals += terminal;
    }
    out += "net name=" + net.name + " gates=" + gates +
           " terminals=" + terminals + "\n";
  }
  return out;
}

// Renders one block diagram in the same key=value line style as
// HierarchyText, so the GUI model parses it with the same rules:
// names are plain Verilog identifiers, so comma lists are
// unambiguous, and an empty list renders as an empty value,
// never omitted.
std::string BlockDiagramText(
    const synth::YosysEngine::BlockDiagramData& diagram) {
  std::string out = "module=" + diagram.module + "\n";
  for (const auto& port : diagram.parent_ports) {
    out += "parent port=" + port.name + " direction=" + port.direction +
           "\n";
  }
  for (const auto& block : diagram.blocks) {
    std::string ports;
    for (const auto& port : block.ports) {
      if (!ports.empty()) ports += ",";
      ports += port.name + ":" + port.direction;
    }
    out += "block instance=" + block.instance + " module=" + block.module +
           " ports=" + ports + "\n";
  }
  for (const auto& net : diagram.nets) {
    std::string blocks;
    for (const auto& endpoint : net.endpoints) {
      if (!blocks.empty()) blocks += ",";
      blocks += endpoint.block + ":" + endpoint.port;
    }
    std::string terminals;
    for (const auto& terminal : net.terminals) {
      if (!terminals.empty()) terminals += ",";
      terminals += terminal;
    }
    out += "net name=" + net.name + " blocks=" + blocks +
           " terminals=" + terminals + "\n";
  }
  return out;
}

// Metric values print at full useful precision: enough significant
// digits that a picosecond-scale slack survives the payload.
std::string FormatNumber(double value) {
  std::ostringstream out;
  out << std::setprecision(10) << value;
  return out.str();
}

std::string MetricText(const analysis::TimingMetricMap& metrics,
                       const std::string& name) {
  const auto it = metrics.find(name);
  return it == metrics.end() ? "none" : FormatNumber(it->second.value);
}

// --- synth_opt (unit S3): the every-path optimization loop. ---

// The loop's comparison tolerance: slacks are float-derived
// upstream values, so a difference at or below this is equality,
// never an improvement and never a regression.
constexpr double kOptEpsNs = 1e-9;

bool ParseOptDouble(const std::string& token, double& out) {
  try {
    std::size_t used = 0;
    const double value = std::stod(token, &used);
    if (used != token.size() || !std::isfinite(value)) return false;
    out = value;
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseOptStrategy(const std::string& name,
                      synth::OptStrategy* out) {
  if (name == "classic") {
    *out = synth::OptStrategy::kClassic;
  } else if (name == "choice") {
    *out = synth::OptStrategy::kChoice;
  } else if (name == "abc9") {
    *out = synth::OptStrategy::kAbc9;
  } else if (name == "retime") {
    *out = synth::OptStrategy::kRetime;
  } else if (name == "delay") {
    *out = synth::OptStrategy::kDelay;
  } else if (name == "area") {
    *out = synth::OptStrategy::kArea;
  } else {
    return false;
  }
  return true;
}

const char* OptStrategyName(synth::OptStrategy strategy) {
  switch (strategy) {
    case synth::OptStrategy::kClassic: return "classic";
    case synth::OptStrategy::kChoice: return "choice";
    case synth::OptStrategy::kAbc9: return "abc9";
    case synth::OptStrategy::kRetime: return "retime";
    case synth::OptStrategy::kDelay: return "delay";
    case synth::OptStrategy::kArea: return "area";
  }
  return "unknown";
}

// One STA evaluation of one candidate netlist inside the loop's
// single resident StaEngine: the Liberty is loaded once by the
// caller, and every evaluation after the first clears the
// previous design out of the same session before loading its
// own netlist, so no evaluation pays for a fresh STA session.
struct OptEval {
  bool ok = false;
  std::string error;
  double worst_slack = 0.0;
  double total_negative = 0.0;
  double worst_hold = 0.0;
  double total_negative_hold = 0.0;
  int electrical_violations = 0;
  // Setup endpoint slacks in the facade's worst-first order.
  std::vector<analysis::EndpointSlack> endpoints;
};

OptEval EvaluateOptCandidate(analysis::StaEngine& sta,
                             const std::string& verilog,
                             const std::string& top,
                             const std::string& sdc, bool first) {
  OptEval eval;
  const auto fail = [&eval](const foundation::Status& status) {
    eval.error = status.message();
    return eval;
  };
  if (!first) {
    const foundation::Status cleared = sta.ClearDesign();
    if (!cleared.ok()) return fail(cleared);
  }
  foundation::Status status = sta.ReadVerilog(verilog);
  if (!status.ok()) return fail(status);
  status = sta.LinkDesign(top);
  if (!status.ok()) return fail(status);
  status = sta.ReadSdc(sdc);
  if (!status.ok()) return fail(status);
  const auto slack = [&eval](foundation::Result<double> result,
                             double* out) {
    if (!result.ok()) {
      eval.error = result.status().message();
      return false;
    }
    *out = result.value();
    return true;
  };
  if (!slack(sta.WorstSlackNs(), &eval.worst_slack)) return eval;
  if (!slack(sta.TotalNegativeSlackNs(), &eval.total_negative)) {
    return eval;
  }
  if (!slack(sta.WorstHoldSlackNs(), &eval.worst_hold)) return eval;
  if (!slack(sta.TotalNegativeHoldSlackNs(),
             &eval.total_negative_hold)) {
    return eval;
  }
  foundation::Result<std::vector<analysis::EndpointSlack>> endpoints =
      sta.EndpointsBySlack(analysis::CheckKind::kSetup);
  if (!endpoints.ok()) return fail(endpoints.status());
  eval.endpoints = endpoints.value();
  const auto electrical =
      [&eval](foundation::Result<analysis::ElectricalCheck> check) {
        if (!check.ok()) {
          eval.error = check.status().message();
          return false;
        }
        eval.electrical_violations +=
            static_cast<int>(check.value().violation_count);
        return true;
      };
  if (!electrical(sta.MaxTransitionCheck())) return eval;
  if (!electrical(sta.MaxCapacitanceCheck())) return eval;
  if (!electrical(sta.MaxFanoutCheck())) return eval;
  eval.ok = true;
  return eval;
}

// The plan's lexicographic cost: worst setup slack, then total
// negative setup slack, then worst hold slack, then total
// negative hold slack, then the electrical violation count.
// Returns +1 when a is better than b, -1 when worse, 0 when
// equal within the loop tolerance.
int CompareOptCost(const OptEval& a, const OptEval& b) {
  const auto cmp = [](double a_value, double b_value,
                      bool higher_better) {
    if (a_value > b_value + kOptEpsNs) return higher_better ? 1 : -1;
    if (b_value > a_value + kOptEpsNs) return higher_better ? -1 : 1;
    return 0;
  };
  if (int c = cmp(a.worst_slack, b.worst_slack, true)) return c;
  if (int c = cmp(a.total_negative, b.total_negative, true)) return c;
  if (int c = cmp(a.worst_hold, b.worst_hold, true)) return c;
  if (int c = cmp(a.total_negative_hold, b.total_negative_hold,
                 true)) {
    return c;
  }
  return cmp(static_cast<double>(a.electrical_violations),
             static_cast<double>(b.electrical_violations), false);
}

// A candidate must not regress any working-set endpoint, even
// when its global cost improves: fixing one path at another's
// expense is a rollback, not a commit. A working-set endpoint
// the candidate no longer reports counts as a regression, since
// its slack can no longer be shown.
bool OptRegressesWorkingSet(const OptEval& candidate,
                            const OptEval& incumbent,
                            const std::vector<std::string>& pins) {
  std::map<std::string, double> candidate_by_pin;
  for (const analysis::EndpointSlack& endpoint : candidate.endpoints) {
    candidate_by_pin[endpoint.pin] = endpoint.slack_ns;
  }
  std::map<std::string, double> incumbent_by_pin;
  for (const analysis::EndpointSlack& endpoint : incumbent.endpoints) {
    incumbent_by_pin[endpoint.pin] = endpoint.slack_ns;
  }
  for (const std::string& pin : pins) {
    const auto found = candidate_by_pin.find(pin);
    const auto was = incumbent_by_pin.find(pin);
    if (found == candidate_by_pin.end() ||
        was == incumbent_by_pin.end() ||
        found->second < was->second - kOptEpsNs) {
      return true;
    }
  }
  return false;
}

std::string OptEndpointListText(
    const std::vector<std::pair<std::string, double>>& entries) {
  if (entries.empty()) return "none";
  std::string text;
  for (const auto& entry : entries) {
    if (!text.empty()) text += ",";
    text += entry.first + ":" + FormatNumber(entry.second);
  }
  return text;
}

std::string OptNameListText(const std::vector<std::string>& names) {
  if (names.empty()) return "none";
  std::string text;
  for (const std::string& name : names) {
    if (!text.empty()) text += ",";
    text += name;
  }
  return text;
}

// Builds the scene list for the active scenarios from the timing
// session: one SceneAnalysis per active scenario, with that
// scenario's corner Liberty and, for extracted parasitics, the
// session SPEF. Shared by run_timing and report_timing so the two
// commands never disagree about what a scene is. Returns the
// failure result when the session cannot run timing at all, and
// sets from_odb for the caller's service call choice.
std::optional<CommandResult> BuildTimingScenes(
    CommandContext& ctx, bool& from_odb,
    std::vector<analysis::SceneAnalysis>& scenes) {
  const std::vector<const db::Scenario*> active =
      ctx.project.scenarios.ActiveScenarios();
  if (active.empty()) {
    return CommandResult::Fail(
        Diag::kInvalidConfiguration,
        "no active scenarios (see eos::create_scenario)");
  }
  from_odb =
      ctx.timing.odb.has_value() && ctx.timing.odb->has_top_block();
  const bool from_verilog = !ctx.timing.verilog_path.empty();
  if (!from_odb && !from_verilog) {
    return CommandResult::Fail(
        Diag::kUnclosedDesign,
        "no timing design source: eos::import_odb or "
        "eos::read_verilog first");
  }
  if (ctx.timing.sdc_path.empty()) {
    return CommandResult::Fail(Diag::kInvalidConfiguration,
                               "no SDC: eos::read_sdc first");
  }
  for (const db::Scenario* scenario : active) {
    analysis::SceneAnalysis scene;
    scene.scenario = *scenario;
    const auto liberty =
        ctx.timing.liberty_by_corner.find(scenario->corner);
    if (liberty == ctx.timing.liberty_by_corner.end()) {
      return CommandResult::Fail(
          Diag::kInvalidConfiguration,
          "no Liberty for corner " + scenario->corner +
              " (scene " + scenario->name +
              "): eos::read_liberty first");
    }
    scene.liberty_path = liberty->second;
    if (scenario->parasitics == db::ParasiticsLevel::kExtracted) {
      if (ctx.timing.spef_path.empty()) {
        return CommandResult::Fail(
            Diag::kInvalidConfiguration,
            "scene " + scenario->name +
                " is at extracted parasitics but no SPEF is "
                "set: eos::set_spef first");
      }
      scene.spef_path = ctx.timing.spef_path;
    }
    scenes.push_back(std::move(scene));
  }
  return std::nullopt;
}

// Parses "-key value" option pairs for report_timing. Unknown keys,
// positional arguments and missing values fail; nothing is silently
// ignored, per the registry's discipline.
std::optional<CommandResult> ParseReportOptions(
    const std::vector<std::string>& args,
    std::map<std::string, std::string>& options) {
  static const char* kKnown[] = {"delay_type", "max_paths", "to",
                                 "slack_lesser_than", "columns", "path_type",
                                 "digits", "file", "scene"};
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& token = args[i];
    if (token.size() < 2 || token[0] != '-') {
      return CommandResult::Fail(
          Diag::kInvalidConfiguration,
          "report_timing takes -key value options, got: " + token);
    }
    const std::string key = token.substr(1);
    bool known = false;
    for (const char* candidate : kKnown) {
      if (key == candidate) known = true;
    }
    if (!known) {
      return CommandResult::Fail(Diag::kInvalidConfiguration,
                                 "unknown report_timing option: -" + key);
    }
    if (i + 1 >= args.size()) {
      return CommandResult::Fail(Diag::kInvalidConfiguration,
                                 "option -" + key + " needs a value");
    }
    if (options.count(key) != 0) {
      return CommandResult::Fail(Diag::kInvalidConfiguration,
                                 "option -" + key + " given twice");
    }
    options[key] = args[++i];
  }
  return std::nullopt;
}

std::string FormatFixed(double value, int digits) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(digits) << value;
  return out.str();
}

// One report_timing column: its option name and its header title,
// in the default column order (the Tempus-style column model:
// the user picks the set and the order with -columns).
struct ReportColumn {
  const char* name;
  const char* title;
};

const ReportColumn kReportColumns[] = {
    {"point", "Point"}, {"cell", "Cell"}, {"edge", "Edge"},
    {"fanout", "Fanout"}, {"cap", "Cap"}, {"slew", "Slew"},
    {"incr", "Incr"}, {"arrival", "Arrival"},
};

// The report_timing usage line, shared by the command registration
// and its -help output so the two can never drift apart.
const char* kReportTimingUsage =
    "report_timing [-delay_type max|min] [-max_paths N] [-to "
    "<endpoint>] [-slack_lesser_than X] [-columns <list>] "
    "[-path_type end|summary|full] [-digits N] [-file <path>] "
    "[-scene <name>]";

std::string ColumnValue(const ReportColumn& column,
                        const analysis::PathPoint& point, int digits) {
  const std::string name = column.name;
  if (name == "point") return point.pin;
  if (name == "cell") return point.cell;
  if (name == "edge") return point.edge;
  if (name == "fanout") {
    return std::to_string(static_cast<long long>(point.fanout));
  }
  if (name == "cap") {
    return point.cap_pf < 0 ? "-" : FormatFixed(point.cap_pf, digits);
  }
  if (name == "slew") return FormatFixed(point.slew_ns, digits);
  if (name == "incr") return FormatFixed(point.incr_ns, digits);
  return FormatFixed(point.arrival_ns, digits);
}

// Renders one path: the header block (startpoint, endpoint,
// arrival, required, slack) plus, for the full path type, the
// point table in the requested columns. Times in nanoseconds,
// capacitance in picofarads.
std::string RenderPathReport(const analysis::PathReport& report,
                             const std::vector<ReportColumn>& columns,
                             const std::string& path_type, int digits) {
  std::string out;
  if (path_type == "end") {
    out += "Endpoint: " + report.endpoint + "  slack (" +
           (report.slack_ns < 0 ? "VIOLATED" : "MET") +
           "): " + FormatFixed(report.slack_ns, digits) + "\n";
    return out;
  }
  out += "Startpoint: " + report.startpoint + "\n";
  out += "Endpoint: " + report.endpoint + "\n";
  out += "data arrival time: " + FormatFixed(report.arrival_ns, digits) +
         "\n";
  out += "data required time: " + FormatFixed(report.required_ns, digits) +
         "\n";
  out += "slack (" + std::string(report.slack_ns < 0 ? "VIOLATED" : "MET") +
         "): " + FormatFixed(report.slack_ns, digits) + "\n";
  if (path_type == "full") {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> header;
    for (const ReportColumn& column : columns) header.push_back(column.title);
    rows.push_back(header);
    for (const analysis::PathPoint& point : report.points) {
      std::vector<std::string> row;
      for (const ReportColumn& column : columns) {
        row.push_back(ColumnValue(column, point, digits));
      }
      rows.push_back(std::move(row));
    }
    std::vector<std::size_t> widths(columns.size(), 0);
    for (const auto& row : rows) {
      for (std::size_t c = 0; c < row.size(); ++c) {
        widths[c] = std::max(widths[c], row[c].size());
      }
    }
    for (const auto& row : rows) {
      for (std::size_t c = 0; c < row.size(); ++c) {
        if (c != 0) out += "  ";
        out += row[c];
        out += std::string(widths[c] - row[c].size(), ' ');
      }
      out += "\n";
    }
  }
  return out;
}

// The closure loop's candidate knowledge, derived from the
// database's master list (Phase 2 unit 2.5). Drive strength is a
// Liberty fact the ODB facade does not carry, so the command
// layer derives the sizing families from the master naming
// convention the libraries in scope follow: masters that share a
// stem and a signal pin set, differing only in a trailing drive
// number (BUF_X1 and BUF_X2, sky130_fd_sc_hd__buf_1 and _buf_2),
// form one family ordered by that number. The derivation is a
// naming heuristic and is stated as such: the L5 service takes
// the families as given and validates their pin sets, and a
// library whose variants do not follow the convention yields
// fewer families, never wrong ones the pin check would not catch.
// Buffer masters are the buffer-shaped masters (exactly one
// input and one output signal pin) whose stem names a buffer,
// strongest first.
struct DriveStem {
  std::string stem;
  long drive = -1;  // -1: the name carries no trailing number
};

DriveStem SplitDrive(const std::string& name) {
  std::size_t digits = 0;
  while (digits < name.size() &&
         name[name.size() - 1 - digits] >= '0' &&
         name[name.size() - 1 - digits] <= '9') {
    ++digits;
  }
  if (digits == 0) {
    return DriveStem{name, -1};
  }
  DriveStem split;
  split.stem = name.substr(0, name.size() - digits);
  if (!split.stem.empty() && split.stem.back() == '_') {
    split.stem.pop_back();
  }
  split.drive = std::stol(name.substr(name.size() - digits));
  return split;
}

std::string PinSignature(const std::vector<db::OdbMasterPin>& pins) {
  std::string signature;
  for (const db::OdbMasterPin& pin : pins) {
    signature += pin.name + (pin.is_input ? ":i" : "") +
                 (pin.is_output ? ":o" : "") + ";";
  }
  return signature;
}

void DeriveClosureCandidates(
    const db::OdbDatabase& database,
    std::vector<std::vector<std::string>>* families,
    std::vector<std::string>* buffer_masters) {
  std::map<std::string, std::vector<std::pair<long, std::string>>> groups;
  for (const std::string& master : database.MasterNames()) {
    const std::vector<db::OdbMasterPin> pins = database.MasterPins(master);
    if (pins.empty()) {
      continue;
    }
    const DriveStem split = SplitDrive(master);
    groups[split.stem + "|" + PinSignature(pins)].push_back(
        {split.drive, master});
    bool buffer_shaped = pins.size() == 2;
    if (buffer_shaped) {
      std::size_t inputs = 0;
      std::size_t outputs = 0;
      for (const db::OdbMasterPin& pin : pins) {
        if (pin.is_input && !pin.is_output) ++inputs;
        if (pin.is_output && !pin.is_input) ++outputs;
      }
      buffer_shaped = inputs == 1 && outputs == 1;
    }
    std::string lower = split.stem;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (buffer_shaped && lower.find("buf") != std::string::npos) {
      buffer_masters->push_back(master);
    }
  }
  for (auto& entry : groups) {
    std::vector<std::pair<long, std::string>>& members = entry.second;
    if (members.size() < 2) {
      continue;
    }
    std::sort(members.begin(), members.end());
    std::vector<std::string> family;
    for (const auto& member : members) {
      family.push_back(member.second);
    }
    families->push_back(std::move(family));
  }
  std::sort(buffer_masters->begin(), buffer_masters->end(),
            [](const std::string& a, const std::string& b) {
              const long da = SplitDrive(a).drive;
              const long db = SplitDrive(b).drive;
              if (da != db) return da > db;
              return a < b;
            });
}

}  // namespace

void RegisterCoreCommands(CommandRegistry& registry) {
  auto add = [&registry](const std::string& name, const std::string& usage,
                         CommandHandler handler) {
    // Registration of the built-in set cannot fail unless this file
    // names a command twice; that is a build-time bug, caught by the
    // registry unit test that counts the set.
    (void)registry.Register(name, usage, std::move(handler));
  };

  add("create_design", "create_design <name>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 1, "create_design <name>")) {
          return *bad;
        }
        if (ctx.HasDesign()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "a design is already open: " + ctx.project.design->name() +
                  " (eos::close_design first)");
        }
        ResetProject(ctx);
        ctx.project.design.emplace(args[0]);
        return CommandResult::Ok("design=" + args[0] + " revision=0");
      });

  add("close_design", "close_design",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 0, "close_design")) return *bad;
        if (auto bad = NeedDesign(ctx)) return *bad;
        ResetProject(ctx);
        return CommandResult::Ok("closed=1");
      });

  add("current_design", "current_design",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 0, "current_design")) return *bad;
        if (auto bad = NeedDesign(ctx)) return *bad;
        std::size_t objects = 0;
        for (const ObjectKind kind :
             {ObjectKind::kCell, ObjectKind::kInstance, ObjectKind::kNet,
              ObjectKind::kPort, ObjectKind::kTerm, ObjectKind::kPin}) {
          objects += ctx.project.design->Count(kind);
        }
        return CommandResult::Ok(
            "design=" + ctx.project.design->name() + " revision=" +
            std::to_string(ctx.project.ledger.current_revision()) +
            " objects=" + std::to_string(objects));
      });

  add("create_cell", "create_cell <name> [-macro]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (args.empty() || args.size() > 2) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "usage: eos::create_cell <name> "
                                     "[-macro]");
        }
        bool macro = false;
        if (args.size() == 2) {
          if (args[1] != "-macro") {
            return CommandResult::Fail(Diag::kInvalidConfiguration,
                                       "unsupported option: " + args[1]);
          }
          macro = true;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        const Result<ObjectId> id =
            ctx.project.design->CreateCell(args[0], macro);
        if (!id.ok()) return FromStatus(id.status());
        return CommandResult::Ok("id=" + IdStr(id.value()) +
                                 " name=" + args[0]);
      });

  add("create_instance", "create_instance <name> <cell>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 2,
                                   "create_instance <name> <cell>")) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        const ObjectId cell =
            ctx.project.design->Find(ObjectKind::kCell, args[1]);
        if (cell.is_null()) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "unknown cell: " + args[1]);
        }
        const Result<ObjectId> id =
            ctx.project.design->CreateInstance(args[0], cell);
        if (!id.ok()) return FromStatus(id.status());
        return CommandResult::Ok("id=" + IdStr(id.value()) +
                                 " name=" + args[0]);
      });

  add("create_net", "create_net <name>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 1, "create_net <name>")) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        const Result<ObjectId> id = ctx.project.design->CreateNet(args[0]);
        if (!id.ok()) return FromStatus(id.status());
        return CommandResult::Ok("id=" + IdStr(id.value()) +
                                 " name=" + args[0]);
      });

  add("create_port", "create_port <name> <in|out|inout>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad =
                ExpectCount(args, 2, "create_port <name> <in|out|inout>")) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        db::Direction direction;
        if (args[1] == "in") {
          direction = db::Direction::kInput;
        } else if (args[1] == "out") {
          direction = db::Direction::kOutput;
        } else if (args[1] == "inout") {
          direction = db::Direction::kInout;
        } else {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "direction must be in, out or inout, got: " + args[1]);
        }
        const Result<ObjectId> id =
            ctx.project.design->CreatePort(args[0], direction);
        if (!id.ok()) return FromStatus(id.status());
        return CommandResult::Ok("id=" + IdStr(id.value()) +
                                 " name=" + args[0]);
      });

  add("connect", "connect <instance> <pin> <net>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad =
                ExpectCount(args, 3, "connect <instance> <pin> <net>")) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        const ObjectId instance =
            ctx.project.design->Find(ObjectKind::kInstance, args[0]);
        if (instance.is_null()) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "unknown instance: " + args[0]);
        }
        const ObjectId net = ctx.project.design->Find(ObjectKind::kNet, args[2]);
        if (net.is_null()) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "unknown net: " + args[2]);
        }
        const Result<ObjectId> id =
            ctx.project.design->Connect(instance, args[1], net);
        if (!id.ok()) return FromStatus(id.status());
        return CommandResult::Ok("id=" + IdStr(id.value()) + " name=" +
                                 args[0] + "/" + args[1]);
      });

  add("create_scenario",
      "create_scenario <name> <mode> <corner> <estimate|extracted>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 4,
                                   "create_scenario <name> <mode> "
                                   "<corner> <estimate|extracted>")) {
          return *bad;
        }
        db::ParasiticsLevel level;
        if (args[3] == "estimate") {
          level = db::ParasiticsLevel::kEstimate;
        } else if (args[3] == "extracted") {
          level = db::ParasiticsLevel::kExtracted;
        } else {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "parasitics level must be estimate or extracted, got: " +
                  args[3]);
        }
        const Result<db::ScenarioId> id =
            ctx.project.scenarios.Create(args[0], args[1], args[2], level);
        if (!id.ok()) return FromStatus(id.status());
        return CommandResult::Ok("scenario=" + args[0] +
                                 " id=" + std::to_string(id.value()));
      });

  add("list_scenarios", "list_scenarios",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 0, "list_scenarios")) return *bad;
        std::string out;
        for (const db::Scenario& s : ctx.project.scenarios.all()) {
          if (!out.empty()) out += "\n";
          out += "scenario " + s.name + " mode=" + s.mode +
                 " corner=" + s.corner + " parasitics=" +
                 std::string(s.parasitics == db::ParasiticsLevel::kEstimate
                                 ? "estimate"
                                 : "extracted") +
                 (s.active ? " active=1" : " active=0");
        }
        if (out.empty()) out = "scenarios=0";
        return CommandResult::Ok(out);
      });

  add("create_clock", "create_clock <name> <target>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad =
                ExpectCount(args, 2, "create_clock <name> <target>")) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        const db::ConstraintId id = ctx.project.constraints.Add(
            db::ConstraintKind::kClock, args[0], args[1], *ctx.project.design);
        const db::Constraint* c = ctx.project.constraints.Find(id);
        std::string payload = "constraint=" + std::to_string(id) +
                              " status=" + StatusWord(c->status) +
                              " target=" + args[1];
        if (c->status == db::BindingStatus::kBound) {
          payload += " object=" + IdStr(c->bound_object);
        }
        return CommandResult::Ok(payload);
      });

  add("add_constraint", "add_constraint <kind> <name> <target>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(
                args, 3, "add_constraint <kind> <name> <target>")) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        const auto kind = ParseConstraintKind(args[0]);
        if (!kind.has_value()) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "unknown constraint kind: " + args[0]);
        }
        const db::ConstraintId id =
            ctx.project.constraints.Add(*kind, args[1], args[2], *ctx.project.design);
        const db::Constraint* c = ctx.project.constraints.Find(id);
        return CommandResult::Ok("constraint=" + std::to_string(id) +
                                 " status=" + StatusWord(c->status) +
                                 " target=" + args[2]);
      });

  add("constraint_coverage", "constraint_coverage",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 0, "constraint_coverage")) {
          return *bad;
        }
        const db::CoverageReport report = ctx.project.constraints.Coverage();
        return CommandResult::Ok(
            "total=" + std::to_string(report.total) +
            " bound=" + std::to_string(report.bound) +
            " unresolved=" + std::to_string(report.unresolved) +
            " empty=" + std::to_string(report.empty) +
            (report.complete() ? " complete=1" : " complete=0"));
      });

  add("list_constraints", "list_constraints",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 0, "list_constraints")) {
          return *bad;
        }
        std::string out;
        for (const db::Constraint& c : ctx.project.constraints.all()) {
          if (!out.empty()) out += "\n";
          out += "constraint " + std::to_string(c.id) + " kind=" +
                 ConstraintKindWord(c.kind) + " name=" + c.name +
                 " target=" + c.target_spec +
                 " status=" + StatusWord(c.status);
          if (c.status == db::BindingStatus::kBound) {
            out += " object=" + IdStr(c.bound_object);
          }
        }
        if (out.empty()) out = "constraints=0";
        return CommandResult::Ok(out);
      });

  add("rebind_constraints", "rebind_constraints",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 0, "rebind_constraints")) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        ctx.project.constraints.RebindAll(*ctx.project.design);
        const db::CoverageReport report = ctx.project.constraints.Coverage();
        return CommandResult::Ok(
            "total=" + std::to_string(report.total) +
            " bound=" + std::to_string(report.bound) +
            " unresolved=" + std::to_string(report.unresolved) +
            " empty=" + std::to_string(report.empty) +
            (report.complete() ? " complete=1" : " complete=0"));
      });

  add("get_objects", "get_objects <cell|instance|net|port|pin> <pattern>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(
                args, 2,
                "get_objects <cell|instance|net|port|pin> <pattern>")) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        const auto kind = ParseObjectKind(args[0]);
        if (!kind.has_value()) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "unknown object kind: " + args[0]);
        }
        std::vector<ObjectId> members;
        for (const ObjectId id : EnumerateKind(*ctx.project.design, *kind)) {
          if (GlobMatch(args[1], ctx.project.design->NameOf(id))) {
            members.push_back(id);
          }
        }
        const std::string handle =
            "eos_coll_" + std::to_string(ctx.next_collection++);
        const std::size_t count = members.size();
        ctx.collections[handle] = std::move(members);
        return CommandResult::Ok("collection=" + handle +
                                 " count=" + std::to_string(count));
      });

  add("sizeof_collection", "sizeof_collection <collection>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad =
                ExpectCount(args, 1, "sizeof_collection <collection>")) {
          return *bad;
        }
        const auto it = ctx.collections.find(args[0]);
        if (it == ctx.collections.end()) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "unknown collection: " + args[0]);
        }
        return CommandResult::Ok("count=" +
                                 std::to_string(it->second.size()));
      });

  add("list_collection", "list_collection <collection>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad =
                ExpectCount(args, 1, "list_collection <collection>")) {
          return *bad;
        }
        const auto it = ctx.collections.find(args[0]);
        if (it == ctx.collections.end()) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "unknown collection: " + args[0]);
        }
        std::string out;
        for (const ObjectId id : it->second) {
          if (!out.empty()) out += "\n";
          const std::string name =
              ctx.HasDesign() ? ctx.project.design->NameOf(id) : std::string();
          out += name.empty() ? std::string("<deleted>") : name;
        }
        return CommandResult::Ok(out);
      });

  add("begin_transaction", "begin_transaction <label>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (args.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "usage: eos::begin_transaction <label>");
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        std::string label = args[0];
        for (std::size_t i = 1; i < args.size(); ++i) {
          label += " " + args[i];
        }
        session::TransactionSpec spec;
        spec.input_revision = ctx.project.ledger.current_revision();
        spec.label = label;
        spec.tool_build = "eos-phase1";
        const foundation::Status status = ctx.project.ledger.BeginTransaction(spec);
        if (!status.ok()) return FromStatus(status);
        return CommandResult::Ok(
            "pending=1 revision=" +
            std::to_string(ctx.project.ledger.current_revision()));
      });

  add("commit_transaction", "commit_transaction [pass|fail|unknown]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (args.size() > 1) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "usage: eos::commit_transaction [pass|fail|unknown]");
        }
        session::ProofStatus proof = session::ProofStatus::kUnknown;
        if (args.size() == 1) {
          const auto parsed = ParseProof(args[0]);
          if (!parsed.has_value()) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "proof must be pass, fail or unknown, got: " + args[0]);
          }
          proof = *parsed;
        }
        const Result<session::Revision> committed =
            ctx.project.ledger.CommitPending(proof);
        if (!committed.ok()) return FromStatus(committed.status());
        return CommandResult::Ok("revision=" +
                                 std::to_string(committed.value()));
      });

  add("abort_transaction", "abort_transaction",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 0, "abort_transaction")) {
          return *bad;
        }
        const foundation::Status status = ctx.project.ledger.AbortPending();
        if (!status.ok()) return FromStatus(status);
        return CommandResult::Ok(
            "aborted=1 revision=" +
            std::to_string(ctx.project.ledger.current_revision()));
      });

  add("rollback", "rollback <revision>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 1, "rollback <revision>")) {
          return *bad;
        }
        std::uint64_t target = 0;
        if (!ParseU64(args[0], target)) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "not a revision: " + args[0]);
        }
        const foundation::Status status = ctx.project.ledger.RollbackTo(target);
        if (!status.ok()) return FromStatus(status);
        return CommandResult::Ok("revision=" + std::to_string(target));
      });

  add("current_revision", "current_revision",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 0, "current_revision")) {
          return *bad;
        }
        return CommandResult::Ok(
            "revision=" + std::to_string(ctx.project.ledger.current_revision()));
      });

  add("register_report",
      "register_report <name> [pass|fail|unknown] [timing_model]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (args.empty() || args.size() > 3) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "usage: eos::register_report <name> [pass|fail|unknown] "
              "[timing_model]");
        }
        session::ProofStatus proof = session::ProofStatus::kUnknown;
        if (args.size() >= 2) {
          const auto parsed = ParseProof(args[1]);
          if (!parsed.has_value()) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "proof must be pass, fail or unknown, got: " + args[1]);
          }
          proof = *parsed;
        }
        const std::string timing_model =
            args.size() == 3 ? args[2] : "NLDM";
        const Result<session::ArtifactId> id = ctx.reports.RegisterReport(
            ctx.project, args[0], proof, timing_model);
        if (!id.ok()) return FromStatus(id.status());
        return CommandResult::Ok(
            "artifact=" + std::to_string(id.value()) + " revision=" +
            std::to_string(ctx.project.ledger.current_revision()));
      });

  add("report_status", "report_status <artifact>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 1, "report_status <artifact>")) {
          return *bad;
        }
        std::uint64_t id = 0;
        if (!ParseU64(args[0], id)) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "not an artifact id: " + args[0]);
        }
        const session::ArtifactRecord* record = ctx.project.ledger.FindArtifact(id);
        if (record == nullptr) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "unknown artifact: " + args[0]);
        }
        return CommandResult::Ok(
            "artifact=" + std::to_string(id) +
            (ctx.project.ledger.ArtifactValid(id) ? " valid=1" : " valid=0") +
            (ctx.project.ledger.ArtifactCurrent(id) ? " current=1" : " current=0") +
            " proof=" + ProofWord(record->proof) + " bound_revision=" +
            std::to_string(record->bound_revision));
      });

  add("export_check", "export_check <report>...",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (args.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "usage: eos::export_check <report>...");
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        const session::ExportVerdict verdict =
            ctx.reports.Evaluate(ctx.project, args);
        if (verdict.allowed()) {
          CommandResult result;
          result.ok = true;
          result.diag = Diag::kVerifiedPass;
          result.payload =
              "export=pass required=" + std::to_string(args.size());
          return result;
        }
        std::string listing;
        for (const session::ExportBlocker& blocker : verdict.blockers) {
          if (!listing.empty()) listing += "\n";
          listing += "blocker=";
          listing += session::BlockerKindName(blocker.kind);
          if (!blocker.report.empty()) {
            listing += " report=" + blocker.report;
          }
          listing += " detail=\"" + blocker.detail + "\"";
        }
        CommandResult result;
        result.ok = false;
        result.diag = Diag::kProofUnknown;
        result.message = listing;
        result.payload = listing;
        return result;
      });

  add("checkpoint", "checkpoint <path>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 1, "checkpoint <path>")) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        const foundation::Status status = ctx.project.SaveProject(args[0]);
        if (!status.ok()) return FromStatus(status);
        return CommandResult::Ok("checkpoint=" + args[0]);
      });

  add("restore", "restore <path>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 1, "restore <path>")) {
          return *bad;
        }
        // Project load is all-or-nothing: a bad file leaves the
        // current project exactly as it was.
        const Result<session::ProjectRestoreInfo> info =
            ctx.project.LoadProject(args[0]);
        if (!info.ok()) return FromStatus(info.status());
        // Collections named objects of the previous project state;
        // they do not survive a restore. Report bindings are project
        // state: the restored project carries the bindings its file
        // held, linked to its ledger, with any binding whose
        // artifact the ledger lacks already dropped and counted.
        ctx.collections.clear();
        ctx.next_collection = 1;
        ctx.timing = CommandContext::TimingSession();
        ctx.synth = CommandContext::SynthSession();
        return CommandResult::Ok(
            "restored=1 design=" + ctx.project.design->name() +
            " revision=" +
            std::to_string(ctx.project.ledger.current_revision()) +
            " unresolved=" +
            std::to_string(
                info.value().checkpoint.constraints_marked_unresolved) +
            " bindings_restored=" +
            std::to_string(info.value().bindings_restored) +
            " bindings_dropped=" +
            std::to_string(info.value().bindings_dropped));
      });

  add("import_odb", "import_odb <path>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 1, "import_odb <path>")) {
          return *bad;
        }
        db::OdbDatabase database;
        const foundation::Status status = database.ReadDb(args[0]);
        if (!status.ok()) return FromStatus(status);
        CommandResult result = CommandResult::Ok(
            "design=" + database.TopBlockName() +
            " instances=" + std::to_string(database.InstanceCount()) +
            " nets=" + std::to_string(database.NetCount()) +
            " masters=" + std::to_string(database.MasterCount()));
        ctx.timing.odb = std::move(database);
        return result;
      });

  add("floorplan",
      "floorplan [-die_area <lx> <ly> <ux> <uy>] "
      "[-core_area <lx> <ly> <ux> <uy>] [-site <name>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        pd::FloorplanOptions options;
        bool die_seen = false;
        bool core_seen = false;
        bool site_seen = false;
        for (std::size_t i = 0; i < args.size();) {
          const std::string& flag = args[i];
          bool is_area = false;
          std::array<int, 4>* area = nullptr;
          bool* seen = nullptr;
          if (flag == "-die_area") {
            is_area = true;
            area = &options.die_area;
            seen = &die_seen;
          } else if (flag == "-core_area") {
            is_area = true;
            area = &options.core_area;
            seen = &core_seen;
          }
          if (is_area) {
            if (*seen) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option " + flag + " given twice");
            }
            if (i + 4 >= args.size()) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option " + flag + " needs four coordinates");
            }
            for (int k = 0; k < 4; ++k) {
              if (!ParseCoord(args[i + 1 + k], (*area)[k])) {
                return CommandResult::Fail(
                    Diag::kInvalidConfiguration,
                    "option " + flag + " coordinate is not an integer: " +
                        args[i + 1 + k]);
              }
            }
            *seen = true;
            if (flag == "-die_area") {
              options.has_die_area = true;
            } else {
              options.has_core_area = true;
            }
            i += 5;
            continue;
          }
          if (flag == "-site") {
            if (site_seen) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -site given twice");
            }
            if (i + 1 >= args.size()) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -site needs a value");
            }
            site_seen = true;
            options.site = args[i + 1];
            i += 2;
            continue;
          }
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "unknown floorplan option: " + flag);
        }
        if (!ctx.timing.odb.has_value() ||
            ctx.timing.odb->RawDatabase() == nullptr ||
            ctx.timing.odb->RawDatabase()->getTech() == nullptr) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no technology data loaded: eos::import_odb first");
        }
        if (!ctx.timing.odb->has_top_block()) {
          return CommandResult::Fail(
              Diag::kUnclosedDesign,
              "no mapped design: the imported database holds no top "
              "block");
        }
        pd::FloorplanReport report;
        const foundation::Status status = pd::RunFloorplan(
            ctx.timing.odb->RawDatabase(), options, &report);
        if (!status.ok()) return FromStatus(status);
        auto area_text = [](const std::array<int, 4>& area) {
          return std::to_string(area[0]) + "," + std::to_string(area[1]) +
                 "," + std::to_string(area[2]) + "," +
                 std::to_string(area[3]);
        };
        return CommandResult::Ok(
            "die_area=" + area_text(report.die_area) +
            " core_area=" + area_text(report.core_area) +
            " rows=" + std::to_string(report.row_count) +
            " track_grids=" + std::to_string(report.track_grid_count) +
            " io_placed=" + std::to_string(report.io_placed_count) +
            " io_total=" + std::to_string(report.io_total_count));
      });

  add("read_liberty", "read_liberty <corner> <path>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad =
                ExpectCount(args, 2, "read_liberty <corner> <path>")) {
          return *bad;
        }
        if (auto bad = CheckReadable(args[1])) return *bad;
        ctx.timing.liberty_by_corner[args[0]] = args[1];
        return CommandResult::Ok("corner=" + args[0] +
                                 " liberty=" + args[1]);
      });

  add("read_filelist",
      "read_filelist <path> <top> [-I <dir>...] [-D <define>...]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        // The plan's Section 1b signature shows only the list
        // path and the flags; the top is positional here,
        // mirroring read_verilog, because elaborate needs a top
        // and the S4 acceptance (reproduce a single-file
        // elaboration exactly) is unstatable without one. A top
        // is never inferred from the list: inferring it would
        // make two lists with the same entries but different
        // intended tops elaborate differently by accident.
        if (args.size() < 2) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "usage: eos::read_filelist <path> <top> "
              "[-I <dir>...] [-D <define>...]");
        }
        const std::string& list_path = args[0];
        const std::string& top = args[1];
        std::vector<std::string> include_dirs;
        std::vector<std::string> defines;
        for (std::size_t i = 2; i < args.size(); ++i) {
          const std::string& arg = args[i];
          std::string flag;
          if (arg == "-I" || arg == "-D") {
            flag = arg;
            if (i + 1 >= args.size()) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option " + arg + " needs a value");
            }
            if (arg == "-I") {
              include_dirs.push_back(args[++i]);
            } else {
              defines.push_back(args[++i]);
            }
          } else if (arg.size() > 2 && arg.compare(0, 2, "-I") == 0) {
            flag = "-I";
            include_dirs.push_back(arg.substr(2));
          } else if (arg.size() > 2 && arg.compare(0, 2, "-D") == 0) {
            flag = "-D";
            defines.push_back(arg.substr(2));
          } else {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "unknown read_filelist option: " + arg);
          }
          // -I and -D repeat by design (a list takes several
          // of each), so there is no duplicate-option rejection
          // for them, unlike synth_generic's single -script.
          // An empty value is a rejection, not a silent skip,
          // and so is a value the engine could not safely
          // carry in a pass command string.
          const std::string& value =
              flag == "-I" ? include_dirs.back() : defines.back();
          if (value.empty()) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "option " + flag + " needs a non-empty value");
          }
          if (flag == "-I" && !IsFilelistCommandToken(value)) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "invalid read_filelist include directory: " + value);
          }
          if (flag == "-D" && !IsFilelistDefine(value)) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "invalid read_filelist define: " + value);
          }
        }
        if (auto bad = CheckReadable(list_path)) return *bad;
        std::vector<FilelistEntry> entries;
        const std::string list_error =
            ReadFilelistEntries(list_path, &entries);
        if (!list_error.empty()) {
          return CommandResult::Fail(Diag::kIoError, list_error);
        }
        if (entries.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "file list '" + list_path + "' names no source files");
        }
        // Fail fast, before Yosys is touched: every entry must
        // read, and the failure names the list and the entry
        // as written, not just the resolved path.
        std::vector<std::string> files;
        for (const FilelistEntry& entry : entries) {
          if (auto bad = CheckReadable(entry.resolved)) {
            return CommandResult::Fail(
                Diag::kIoError,
                "file list '" + list_path + "' entry '" +
                    entry.as_written + "' cannot be read: " +
                    entry.resolved);
          }
          files.push_back(entry.resolved);
        }
        synth::YosysEngine engine;
        const foundation::Result<synth::YosysDesignSummary> parsed =
            engine.ParseVerilogFiles(files, include_dirs, defines);
        if (!parsed.ok()) {
          // Every failure that reaches this point touched the
          // engine (bad flags were rejected above, before any
          // engine ran): a failed parse replaces the live
          // design or leaves it partial, so the session no
          // longer holds the design it claims: clear it, the
          // same rationale as a failed elaboration.
          ctx.synth = CommandContext::SynthSession();
          return FromStatus(parsed.status());
        }
        std::string modules;
        bool found = false;
        for (const synth::YosysModuleSummary& module :
             parsed.value().modules) {
          if (!modules.empty()) modules += ",";
          modules += module.name;
          if (module.name == top) found = true;
        }
        if (!found) {
          // The parse replaced the live design, so the old
          // session's claim is stale even though the parse
          // itself succeeded: clear it and record nothing.
          ctx.synth = CommandContext::SynthSession();
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "top module '" + top + "' not found in file list " +
                  list_path + " (modules: " + modules + ")");
        }
        ctx.synth = CommandContext::SynthSession();
        ctx.synth.parsed = true;
        ctx.synth.top_cell = top;
        ctx.synth.filelist_path = list_path;
        ctx.synth.filelist_files = files;
        std::string entries_text;
        for (const FilelistEntry& entry : entries) {
          if (!entries_text.empty()) entries_text += ",";
          entries_text += entry.as_written;
        }
        std::string includes_text;
        for (const std::string& dir : include_dirs) {
          if (!includes_text.empty()) includes_text += ",";
          includes_text += dir;
        }
        std::string defines_text;
        for (const std::string& define : defines) {
          if (!defines_text.empty()) defines_text += ",";
          defines_text += define;
        }
        return CommandResult::Ok("filelist=" + list_path +
                                 " files=" +
                                 std::to_string(entries.size()) +
                                 " entries=" + entries_text +
                                 " top=" + top +
                                 " modules=" + modules +
                                 " includes=" + includes_text +
                                 " defines=" + defines_text);
      });

  add("read_verilog", "read_verilog <path> <top>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad =
                ExpectCount(args, 2, "read_verilog <path> <top>")) {
          return *bad;
        }
        if (auto bad = CheckReadable(args[0])) return *bad;
        // Changed semantics in synthesis unit S1: the file is
        // really parsed by the Yosys front end, not only
        // recorded. The path and top are still recorded for the
        // timing commands, which read the file themselves, so
        // run_timing and report_timing are unchanged. A file the
        // front end rejects at command level, or a top that is
        // not among the parsed modules, records nothing.
        // A malformed file is the documented exception: Yosys
        // aborts the calling process with exit 1, exactly as the
        // yosys executable does; it is not a command result.
        synth::YosysEngine engine;
        const foundation::Result<synth::YosysDesignSummary> parsed =
            engine.ParseVerilog(args[0]);
        if (!parsed.ok()) return FromStatus(parsed.status());
        std::string modules;
        bool found = false;
        for (const synth::YosysModuleSummary& module :
             parsed.value().modules) {
          if (!modules.empty()) modules += ",";
          modules += module.name;
          if (module.name == args[1]) found = true;
        }
        if (!found) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "top module '" + args[1] + "' not found in " + args[0] +
                  " (modules: " + modules + ")");
        }
        ctx.timing.verilog_path = args[0];
        ctx.timing.top_cell = args[1];
        ctx.synth = CommandContext::SynthSession();
        ctx.synth.parsed = true;
        ctx.synth.verilog_path = args[0];
        ctx.synth.top_cell = args[1];
        return CommandResult::Ok("verilog=" + args[0] + " top=" + args[1] +
                                 " modules=" + modules);
      });

  add("elaborate", "elaborate",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 0, "elaborate")) return *bad;
        if (!ctx.synth.parsed) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no parsed design: eos::read_verilog first");
        }
        synth::YosysEngine engine;
        const foundation::Result<synth::YosysNetlistStats> stats =
            engine.Elaborate(ctx.synth.top_cell);
        if (!stats.ok()) {
          // A failed elaboration may have mutated the live
          // design, so the session no longer holds the parsed
          // design it claims: clear the synth state and let the
          // caller re-run read_verilog. The timing recording is
          // untouched; it names a file, not the live design.
          ctx.synth = CommandContext::SynthSession();
          return FromStatus(stats.status());
        }
        ctx.synth.elaborated = true;
        ctx.synth.elaborated_cells = stats.value().cell_count;
        ctx.synth.elaborated_wires = stats.value().wire_count;
        ctx.synth.elaborated_depth = stats.value().logic_depth;
        ctx.synth.generic_optimized = false;
        ctx.synth.generic_script.clear();
        ctx.synth.generic_verilog.clear();
        return CommandResult::Ok(SynthStatsText(stats.value()));
      });

  // Reports the live elaborated design's hierarchy for the session
  // top, as the hierarchy browser (GUI unit U2) consumes it. The
  // command is read-only: it runs no pass and changes no session
  // state, so it is safe to call on every browser refresh. It
  // fails fast when no elaborated design exists, mirroring
  // elaborate's own precondition style.
  add("synth_hierarchy", "synth_hierarchy",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 0, "synth_hierarchy")) {
          return *bad;
        }
        if (!ctx.synth.elaborated) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no elaborated design: eos::elaborate first");
        }
        synth::YosysEngine engine;
        const foundation::Result<synth::YosysEngine::HierarchyNode>
            hierarchy = engine.Hierarchy(ctx.synth.top_cell);
        if (!hierarchy.ok()) {
          return FromStatus(hierarchy.status());
        }
        return CommandResult::Ok(HierarchyText(hierarchy.value()));
      });

  // Reports one module's block diagram for the live elaborated
  // design, as the block diagram widget (GUI unit U3) consumes
  // it. Read-only like synth_hierarchy: it runs no pass and
  // changes no session state. It fails fast when no elaborated
  // design exists, when the argument count is wrong, or when
  // the module is not a plain identifier or not in the live
  // design.
  add("synth_blocks", "synth_blocks <module>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 1, "synth_blocks <module>")) {
          return *bad;
        }
        if (!ctx.synth.elaborated) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no elaborated design: eos::elaborate first");
        }
        synth::YosysEngine engine;
        const foundation::Result<synth::YosysEngine::BlockDiagramData>
            diagram = engine.BlockDiagram(args[0]);
        if (!diagram.ok()) {
          return FromStatus(diagram.status());
        }
        return CommandResult::Ok(BlockDiagramText(diagram.value()));
      });

  // Reports one module's gate-level schematic for the live
  // elaborated design, as the schematic widget (GUI unit U4)
  // consumes it. Read-only like synth_hierarchy and
  // synth_blocks: it runs no pass and changes no session state.
  // It fails fast when no elaborated design exists, when the
  // argument count is wrong, or when the module is not a plain
  // identifier or not in the live design. The schematic reads
  // the elaborated design, not the post-generic netlist:
  // synth_generic replaces the live design with a flattened
  // optimized netlist, so a nested leaf module no longer
  // exists to query after it runs.
  add("synth_schematic", "synth_schematic <module>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 1, "synth_schematic <module>")) {
          return *bad;
        }
        if (!ctx.synth.elaborated) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no elaborated design: eos::elaborate first");
        }
        synth::YosysEngine engine;
        const foundation::Result<synth::YosysEngine::SchematicData>
            schematic = engine.Schematic(args[0]);
        if (!schematic.ok()) {
          return FromStatus(schematic.status());
        }
        return CommandResult::Ok(SchematicText(schematic.value()));
      });

  // Reports the live design's netlist stats for the session's
  // top module, as the right panels (GUI unit U5) consume them.
  // Read-only like synth_hierarchy: it runs no pass and
  // changes no session state. It fails fast when no elaborated
  // design exists or when the argument count is wrong. After
  // synth_generic the live design is the generic netlist, so
  // the stats describe the generic result; before it runs they
  // describe the elaborated design.
  add("synth_stats", "synth_stats",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 0, "synth_stats")) {
          return *bad;
        }
        if (!ctx.synth.elaborated) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no elaborated design: eos::elaborate first");
        }
        synth::YosysEngine engine;
        const foundation::Result<synth::YosysNetlistStats> stats =
            engine.Stats(ctx.synth.top_cell);
        if (!stats.ok()) {
          return FromStatus(stats.status());
        }
        return CommandResult::Ok(StatsText(stats.value()));
      });

  add("synth_generic", "synth_generic [-script <name>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        std::string script = "default";
        bool script_seen = false;
        for (std::size_t i = 0; i < args.size(); ++i) {
          if (args[i] != "-script") {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "unknown synth_generic option: " + args[i]);
          }
          if (i + 1 >= args.size()) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "option -script needs a value");
          }
          if (script_seen) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "option -script given twice");
          }
          script_seen = true;
          script = args[++i];
        }
        if (script != "default" && script != "balance" &&
            script != "rewrite" && script != "none") {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "unknown generic script: " + script);
        }
        if (!ctx.synth.parsed || !ctx.synth.elaborated) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no elaborated design: eos::elaborate first");
        }
        const int elaborated_cells = ctx.synth.elaborated_cells;
        const int elaborated_wires = ctx.synth.elaborated_wires;
        const int elaborated_depth = ctx.synth.elaborated_depth;
        const std::string bridge = SynthTempPath("bridge", ".blif");
        const std::string optimized = SynthTempPath("generic", ".v");
        // The bridge runs Yosys first, then ABC, then Yosys
        // again, each engine constructed and released in turn:
        // both engines guard a process singleton, so they must
        // never be alive at the same time. The Yosys design is
        // mutated by aigmap before ABC runs, so any failure
        // after that point clears the synth state: the live
        // design is no longer the elaborated one, and the
        // recovery is read_verilog followed by elaborate. The
        // bridge is BLIF, not Verilog: see WriteBlif for why
        // ABC cannot read Yosys's sequential Verilog form.
        const auto fail_after_mutation =
            [&ctx](const foundation::Status& status) {
              ctx.synth = CommandContext::SynthSession();
              return FromStatus(status);
            };
        synth::AbcGenericResult generic;
        {
          synth::YosysEngine yosys;
          const foundation::Status mapped = yosys.AigMap();
          if (!mapped.ok()) return fail_after_mutation(mapped);
          const foundation::Status written =
              yosys.WriteBlif(bridge, ctx.synth.top_cell);
          if (!written.ok()) return fail_after_mutation(written);
        }
        {
          synth::AbcEngine abc;
          const foundation::Result<synth::AbcGenericResult> result =
              abc.OptimizeGeneric(bridge, script, optimized);
          std::error_code ec;
          std::filesystem::remove(bridge, ec);
          if (!result.ok()) return fail_after_mutation(result.status());
          generic = result.value();
        }
        synth::YosysNetlistStats post;
        {
          synth::YosysEngine yosys;
          const foundation::Result<synth::YosysDesignSummary> parsed =
              yosys.ParseVerilog(optimized);
          if (!parsed.ok()) return fail_after_mutation(parsed.status());
          bool found = false;
          for (const synth::YosysModuleSummary& module :
               parsed.value().modules) {
            if (module.name == ctx.synth.top_cell) found = true;
          }
          if (!found) {
            return fail_after_mutation(foundation::Status::Error(
                foundation::ErrorCode::kInvalidArgument,
                "optimized netlist has no top module: " +
                    ctx.synth.top_cell));
          }
          const foundation::Result<synth::YosysNetlistStats> stats =
              yosys.Elaborate(ctx.synth.top_cell);
          if (!stats.ok()) return fail_after_mutation(stats.status());
          post = stats.value();
        }
        ctx.synth.elaborated = true;
        ctx.synth.elaborated_cells = post.cell_count;
        ctx.synth.elaborated_wires = post.wire_count;
        ctx.synth.elaborated_depth = post.logic_depth;
        ctx.synth.generic_optimized = true;
        ctx.synth.generic_script = script;
        ctx.synth.generic_before_and = generic.before.and_count;
        ctx.synth.generic_before_levels = generic.before.levels;
        ctx.synth.generic_after_and = generic.after.and_count;
        ctx.synth.generic_after_levels = generic.after.levels;
        ctx.synth.generic_verilog = optimized;
        return CommandResult::Ok(
            "script=" + script +
            " elaborated_cells=" + std::to_string(elaborated_cells) +
            " elaborated_wires=" + std::to_string(elaborated_wires) +
            " elaborated_depth=" + std::to_string(elaborated_depth) +
            " before_and=" + std::to_string(generic.before.and_count) +
            " before_levels=" + std::to_string(generic.before.levels) +
            " after_and=" + std::to_string(generic.after.and_count) +
            " after_levels=" + std::to_string(generic.after.levels) +
            " cells=" + std::to_string(post.cell_count) +
            " wires=" + std::to_string(post.wire_count) +
            " depth=" + std::to_string(post.logic_depth) +
            " verilog=" + optimized);
      });

  // Technology mapping (synthesis unit S2): the elaborated
  // design, mapped onto the cells of a Liberty library through
  // the Liberty-to-genlib conversion, and judged by STA when
  // the timing session can build scenes. The Liberty comes
  // from -liberty, else the sole recorded Liberty, else the
  // Liberty shared by every active scenario's corner; anything
  // else fails naming the ambiguity, before any engine runs.
  // Sequential designs fail before the bridge: ABC's mapped
  // Verilog writes latches as behavioural always blocks, not
  // mapped gates, so a mapped sequential netlist would be a
  // lie the STA Verilog path cannot link. The bridge runs
  // Yosys first, then ABC, then nothing else, each engine
  // constructed and released in turn (both guard a process
  // singleton). The Yosys design is mutated by aigmap before
  // ABC runs, so any failure after that point clears the synth
  // state, exactly as synth_generic does. The STA judgement
  // runs on the mapped netlist without registering a report
  // binding (that is run_timing's job); on success the timing
  // session is pointed at the mapped netlist, so the next
  // run_timing is the end-to-end STA on the mapped design.
  add("synth_map", "synth_map [-target delay|area] [-liberty <path>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        std::string target = "delay";
        bool target_seen = false;
        std::string liberty;
        bool liberty_seen = false;
        for (std::size_t i = 0; i < args.size(); ++i) {
          if (args[i] == "-target") {
            if (i + 1 >= args.size()) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -target needs a value");
            }
            if (target_seen) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -target given twice");
            }
            target_seen = true;
            target = args[++i];
            if (target != "delay" && target != "area") {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "unknown mapping target: " + target);
            }
          } else if (args[i] == "-liberty") {
            if (i + 1 >= args.size()) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -liberty needs a value");
            }
            if (liberty_seen) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -liberty given twice");
            }
            if (auto bad = CheckReadable(args[i + 1])) return *bad;
            liberty_seen = true;
            liberty = args[++i];
          } else {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "unknown synth_map option: " + args[i]);
          }
        }
        if (!ctx.synth.parsed || !ctx.synth.elaborated) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no elaborated design: eos::elaborate first");
        }
        std::string liberty_path = liberty;
        if (liberty_path.empty()) {
          if (ctx.timing.liberty_by_corner.size() == 1) {
            liberty_path = ctx.timing.liberty_by_corner.begin()->second;
          } else {
            const std::vector<const db::Scenario*> active =
                ctx.project.scenarios.ActiveScenarios();
            std::string shared;
            bool shared_ok = !active.empty();
            for (const db::Scenario* scenario : active) {
              const auto found =
                  ctx.timing.liberty_by_corner.find(scenario->corner);
              if (found == ctx.timing.liberty_by_corner.end()) {
                shared_ok = false;
                break;
              }
              if (shared.empty()) {
                shared = found->second;
              } else if (shared != found->second) {
                shared_ok = false;
                break;
              }
            }
            if (shared_ok) liberty_path = shared;
          }
        }
        if (liberty_path.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no Liberty for mapping: eos::synth_map -liberty "
              "<path> or eos::read_liberty first");
        }
        const auto fail_after_mutation =
            [&ctx](const foundation::Status& status) {
              ctx.synth = CommandContext::SynthSession();
              return FromStatus(status);
            };
        // The sequential guard runs before any mutation, so a
        // rejected sequential design leaves the session
        // untouched. Stats itself does not mutate the design.
        int sequential_cells = 0;
        {
          synth::YosysEngine yosys;
          const foundation::Result<synth::YosysNetlistStats> stats =
              yosys.Stats(ctx.synth.top_cell);
          if (!stats.ok()) {
            // The session claims an elaborated design the live
            // engine does not hold; clear it rather than let a
            // later command operate on the disagreement.
            ctx.synth = CommandContext::SynthSession();
            return FromStatus(stats.status());
          }
          sequential_cells = stats.value().sequential_cells;
        }
        if (sequential_cells > 0) {
          return CommandResult::Fail(
              Diag::kUnsupportedFeature,
              "synth_map maps combinational designs only: the "
              "elaborated design has " +
                  std::to_string(sequential_cells) +
                  " sequential cell(s), which ABC's mapped "
                  "Verilog renders as behavioural blocks rather "
                  "than mapped gates");
        }
        const std::string bridge = SynthTempPath("map_bridge", ".blif");
        const std::string genlib = SynthTempPath("map_genlib", ".genlib");
        const std::string mapped = SynthTempPath("mapped", ".v");
        {
          synth::YosysEngine yosys;
          const foundation::Status mapped_aig = yosys.AigMap();
          if (!mapped_aig.ok()) return fail_after_mutation(mapped_aig);
          const foundation::Status written =
              yosys.WriteBlif(bridge, ctx.synth.top_cell);
          if (!written.ok()) return fail_after_mutation(written);
        }
        std::vector<std::string> genlib_cells;
        {
          synth::AbcEngine abc;
          const foundation::Result<std::vector<std::string>> converted =
              abc.LibertyToGenlib(liberty_path, genlib);
          if (!converted.ok()) return fail_after_mutation(converted.status());
          genlib_cells = converted.value();
          const foundation::Status status = abc.MapTechnology(
              bridge, genlib, mapped,
              target == "area" ? synth::MapTarget::kArea
                               : synth::MapTarget::kDelay);
          std::error_code ec;
          std::filesystem::remove(bridge, ec);
          if (!status.ok()) return fail_after_mutation(status);
        }
        // Cell counts scanned from ABC's mapped Verilog writer
        // format: each gate line starts with its cell name.
        // The mapped netlist is not re-parsed by Yosys: its
        // Liberty cells are unknown modules there.
        std::set<std::string> cell_set(genlib_cells.begin(),
                                       genlib_cells.end());
        std::map<std::string, int> by_type;
        int total_cells = 0;
        ScanMappedCells(mapped, cell_set, &by_type, &total_cells);
        const std::string types = CellTypesText(by_type);
        std::string genlib_list;
        for (const std::string& cell : genlib_cells) {
          if (!genlib_list.empty()) genlib_list += ",";
          genlib_list += cell;
        }
        // The STA judgement: scenes are built exactly as
        // run_timing builds them. No scenes (no scenario, no
        // SDC, no corner Liberty) is not a mapping failure:
        // the mapping stands and the record says so, and the
        // next run_timing supplies the judgement. A scene
        // set that builds but whose analysis fails is a
        // mapping failure: the record cannot be judged, and
        // the mutated live design is cleared with it.
        bool judged = false;
        double worst_slack = 0.0;
        double total_negative = 0.0;
        int electrical_violations = 0;
        std::string timing_lines;
        {
          bool from_odb = false;
          std::vector<analysis::SceneAnalysis> scenes;
          if (auto bad = BuildTimingScenes(ctx, from_odb, scenes)) {
            timing_lines = " timing=not_run reason=" + bad->message;
          } else {
            analysis::TimingService service;
            const foundation::Result<std::vector<analysis::SceneResult>>
                analyzed = service.Analyze(
                    analysis::VerilogDesign{mapped, ctx.synth.top_cell,
                                            ctx.timing.sdc_path},
                    scenes);
            if (!analyzed.ok()) {
              return fail_after_mutation(analyzed.status());
            }
            const auto metric = [](const analysis::SceneResult& result,
                                   const std::string& name, double* out) {
              const auto found = result.metrics.find(name);
              if (found == result.metrics.end()) return false;
              *out = found->second.value;
              return true;
            };
            judged = true;
            bool first = true;
            for (const analysis::SceneResult& result : analyzed.value()) {
              double slack = 0.0;
              double tns = 0.0;
              double transition = 0.0;
              double capacitance = 0.0;
              double fanout = 0.0;
              if (!metric(result, "worst_slack_ns", &slack) ||
                  !metric(result, "total_negative_slack_ns", &tns) ||
                  !metric(result, "max_transition_violations",
                          &transition) ||
                  !metric(result, "max_capacitance_violations",
                          &capacitance) ||
                  !metric(result, "max_fanout_violations", &fanout)) {
                return fail_after_mutation(foundation::Status::Error(
                    foundation::ErrorCode::kInvalidState,
                    "STA record is missing a mapping metric for scene " +
                        result.scenario.name));
              }
              if (first || slack < worst_slack) worst_slack = slack;
              total_negative += tns;
              electrical_violations += static_cast<int>(transition) +
                                       static_cast<int>(capacitance) +
                                       static_cast<int>(fanout);
              first = false;
              timing_lines +=
                  "\nscene=" + result.scenario.name +
                  " worst_slack_ns=" + FormatNumber(slack) +
                  " total_negative_slack_ns=" + FormatNumber(tns) +
                  " electrical_violations=" +
                  std::to_string(static_cast<int>(transition) +
                                 static_cast<int>(capacitance) +
                                 static_cast<int>(fanout));
            }
          }
        }
        ctx.synth.mapped = true;
        ctx.synth.mapped_target = target;
        ctx.synth.mapped_liberty = liberty_path;
        ctx.synth.mapped_genlib = genlib;
        ctx.synth.mapped_verilog = mapped;
        ctx.synth.mapped_genlib_cells = genlib_list;
        ctx.synth.mapped_cells = total_cells;
        ctx.synth.mapped_types = types;
        ctx.synth.mapped_timing_judged = judged;
        ctx.synth.mapped_worst_slack_ns = worst_slack;
        ctx.synth.mapped_total_negative_slack_ns = total_negative;
        ctx.synth.mapped_electrical_violations = electrical_violations;
        ctx.timing.verilog_path = mapped;
        ctx.timing.top_cell = ctx.synth.top_cell;
        return CommandResult::Ok(
            "target=" + target + " liberty=" + liberty_path +
            " genlib=" + genlib + " genlib_cells=" + genlib_list +
            " cells=" + std::to_string(total_cells) +
            " types=" + types + " verilog=" + mapped + timing_lines);
      });

  // The every-path optimization loop (synthesis unit S3).
  // Starts from the mapped netlist (or the previous loop's
  // winner on a re-run) and judges every candidate the same
  // way: one resident StaEngine evaluates the incumbent and
  // every strategy candidate in turn, the Liberty loaded once
  // and each candidate loaded into the same session after a
  // clear, so no candidate pays for a fresh STA session. The
  // working set is every setup endpoint within the margin of
  // violation at the baseline (violating endpoints plus
  // endpoints within margin_ns of violating; margin default
  // 0.1 ns, the plan's value). Each iteration tries every
  // strategy, proves each candidate equivalent to the
  // incumbent with ABC cec (a candidate that is not
  // kEquivalent is rejected, never committed), and commits
  // the best candidate that strictly improves the
  // lexicographic cost without regressing any working-set
  // endpoint. The loop ends when every working-set endpoint
  // meets timing, when no strategy improves the incumbent, or
  // at the iteration cap; a run that ends on the cap names
  // exactly which endpoints still violate. The loop judges on
  // the mapping corner (the mapped Liberty) under the session
  // SDC, a single scene, so every comparison is on identical
  // inputs. Cloning is not a strategy here: the strategy list
  // is whole-design ABC strategies, none of which needs a
  // fanout clone, so the plan's cloning work item does not
  // arise. Per-path ECO sizing and buffering stay the ECO
  // engine's existing surface and are not candidates of this
  // loop. On success the timing session is pointed at the
  // winning netlist, so the next run_timing is the STA on the
  // optimized design; the synth_map record is left untouched.
  add("synth_opt",
      "synth_opt [-strategies <list>] [-margin <ns>] "
      "[-max_iterations <n>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        std::vector<synth::OptStrategy> strategies;
        bool strategies_seen = false;
        double margin = 0.1;
        bool margin_seen = false;
        std::uint64_t max_iterations = 20;
        bool max_iterations_seen = false;
        for (std::size_t i = 0; i < args.size(); ++i) {
          if (args[i] == "-strategies") {
            if (i + 1 >= args.size()) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -strategies needs a value");
            }
            if (strategies_seen) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -strategies given twice");
            }
            strategies_seen = true;
            const std::string list = args[++i];
            if (list.empty()) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -strategies needs a non-empty list");
            }
            std::set<std::string> seen_names;
            std::size_t start = 0;
            while (start <= list.size()) {
              const std::size_t comma = list.find(',', start);
              const std::string name = list.substr(
                  start, comma == std::string::npos ? std::string::npos
                                                    : comma - start);
              synth::OptStrategy strategy;
              if (name.empty() || !ParseOptStrategy(name, &strategy)) {
                return CommandResult::Fail(
                    Diag::kInvalidConfiguration,
                    "unknown optimization strategy: " + name);
              }
              if (!seen_names.insert(name).second) {
                return CommandResult::Fail(
                    Diag::kInvalidConfiguration,
                    "strategy given twice: " + name);
              }
              strategies.push_back(strategy);
              if (comma == std::string::npos) break;
              start = comma + 1;
            }
          } else if (args[i] == "-margin") {
            if (i + 1 >= args.size()) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -margin needs a value");
            }
            if (margin_seen) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -margin given twice");
            }
            margin_seen = true;
            if (!ParseOptDouble(args[++i], margin) || margin < 0.0) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -margin needs a non-negative number");
            }
          } else if (args[i] == "-max_iterations") {
            if (i + 1 >= args.size()) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -max_iterations needs a value");
            }
            if (max_iterations_seen) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -max_iterations given twice");
            }
            max_iterations_seen = true;
            if (!ParseU64(args[++i], max_iterations) ||
                max_iterations < 1) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -max_iterations needs a positive integer");
            }
          } else {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "unknown synth_opt option: " + args[i]);
          }
        }
        if (!strategies_seen) {
          strategies = {synth::OptStrategy::kClassic,
                        synth::OptStrategy::kChoice,
                        synth::OptStrategy::kAbc9};
        }
        // Every precondition is checked before any engine runs,
        // so a rejected run leaves the session untouched.
        if (!ctx.synth.mapped) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no mapped design: eos::synth_map first");
        }
        if (ctx.timing.sdc_path.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no SDC: eos::read_sdc first");
        }
        if (auto bad = CheckReadable(ctx.timing.sdc_path)) return *bad;
        if (auto bad = CheckReadable(ctx.synth.mapped_liberty)) {
          return *bad;
        }
        if (auto bad = CheckReadable(ctx.synth.mapped_genlib)) {
          return *bad;
        }
        std::string incumbent =
            ctx.synth.opt_verilog.empty()
                ? ctx.synth.mapped_verilog
                : ctx.synth.opt_verilog;
        if (auto bad = CheckReadable(incumbent)) return *bad;
        std::set<std::string> cell_set;
        {
          std::size_t start = 0;
          const std::string& cells = ctx.synth.mapped_genlib_cells;
          while (start <= cells.size()) {
            const std::size_t comma = cells.find(',', start);
            const std::string cell = cells.substr(
                start, comma == std::string::npos ? std::string::npos
                                                  : comma - start);
            if (!cell.empty()) cell_set.insert(cell);
            if (comma == std::string::npos) break;
            start = comma + 1;
          }
        }
        // The resident STA session: Liberty loaded once, then
        // one evaluation per netlist in the same session.
        analysis::StaEngine sta;
        foundation::Status status =
            sta.ReadLiberty(ctx.synth.mapped_liberty);
        if (!status.ok()) return FromStatus(status);
        int sta_evaluations = 0;
        OptEval incumbent_eval = EvaluateOptCandidate(
            sta, incumbent, ctx.synth.top_cell, ctx.timing.sdc_path,
            true);
        if (!incumbent_eval.ok) return FromStatus(foundation::Status::Error(
            foundation::ErrorCode::kIoError, incumbent_eval.error));
        ++sta_evaluations;
        // Unit S7: a new loop run replaces the previous run's
        // per-strategy record, including the empty-working-set
        // path below, which returns without committing.
        ctx.synth.opt_strategy_records.clear();
        std::vector<std::string> working_pins;
        std::vector<std::pair<std::string, double>> working_baseline;
        for (const analysis::EndpointSlack& endpoint :
             incumbent_eval.endpoints) {
          if (endpoint.slack_ns < margin) {
            working_pins.push_back(endpoint.pin);
            working_baseline.emplace_back(endpoint.pin,
                                          endpoint.slack_ns);
          }
        }
        const auto scan_incumbent = [&]() {
          std::map<std::string, int> by_type;
          int total = 0;
          ScanMappedCells(incumbent, cell_set, &by_type, &total);
          return std::pair<std::string, int>(CellTypesText(by_type),
                                             total);
        };
        // An empty working set means nothing to optimize: no
        // strategy runs, and the record says so.
        if (working_pins.empty()) {
          const auto scanned = scan_incumbent();
          ctx.synth.opt_run = true;
          ctx.synth.opt_winner.clear();
          ctx.synth.opt_iterations = 0;
          ctx.synth.opt_verilog = incumbent;
          ctx.synth.opt_types = scanned.first;
          ctx.synth.opt_cells = scanned.second;
          ctx.synth.opt_worst_slack_ns = incumbent_eval.worst_slack;
          ctx.synth.opt_total_negative_slack_ns =
              incumbent_eval.total_negative;
          ctx.synth.opt_still_violating.clear();
          ctx.timing.verilog_path = incumbent;
          return CommandResult::Ok(
              "iterations=0 max_iterations=" +
              std::to_string(max_iterations) +
              " margin_ns=" + FormatNumber(margin) +
              " working_set=none winner=none" +
              " final_worst_slack_ns=" +
              FormatNumber(incumbent_eval.worst_slack) +
              " final_total_negative_slack_ns=" +
              FormatNumber(incumbent_eval.total_negative) +
              " sta_evaluations=" + std::to_string(sta_evaluations) +
              " sta_sessions=1 verilog=" + incumbent +
              " endpoints=none still_violating=none");
        }
        const double baseline_worst_slack = incumbent_eval.worst_slack;
        const double baseline_total_negative =
            incumbent_eval.total_negative;
        std::vector<std::string> committed;
        std::vector<std::string> failed_strategies;
        int equivalence_rejected = 0;
        std::uint64_t iterations = 0;
        std::string incumbent_temp;
        bool cleared = false;
        for (std::uint64_t iteration = 1;
             iteration <= max_iterations; ++iteration) {
          iterations = iteration;
          // The best improving candidate of this iteration, or
          // none: ties keep the earliest strategy in list order.
          bool have_best = false;
          synth::OptStrategy best_strategy =
              synth::OptStrategy::kClassic;
          std::string best_file;
          OptEval best_eval;
          for (synth::OptStrategy strategy : strategies) {
            const std::string candidate = SynthTempPath("opt", ".v");
            foundation::Status abc_status;
            {
              synth::AbcEngine abc;
              abc_status = abc.OptimizeMapped(
                  incumbent, ctx.synth.mapped_genlib, strategy,
                  candidate);
            }
            if (!abc_status.ok()) {
              failed_strategies.push_back(
                  OptStrategyName(strategy));
              std::error_code ec;
              std::filesystem::remove(candidate, ec);
              continue;
            }
            synth::EquivalenceVerdict verdict;
            {
              synth::AbcEngine abc;
              const foundation::Result<synth::EquivalenceVerdict>
                  proof = abc.ProveEquivalent(
                      incumbent, candidate, ctx.synth.mapped_genlib);
              if (!proof.ok()) {
                failed_strategies.push_back(
                    OptStrategyName(strategy));
                std::error_code ec;
                std::filesystem::remove(candidate, ec);
                continue;
              }
              verdict = proof.value();
            }
            if (verdict != synth::EquivalenceVerdict::kEquivalent) {
              ++equivalence_rejected;
              std::error_code ec;
              std::filesystem::remove(candidate, ec);
              continue;
            }
            OptEval candidate_eval = EvaluateOptCandidate(
                sta, candidate, ctx.synth.top_cell,
                ctx.timing.sdc_path, false);
            if (!candidate_eval.ok) {
              failed_strategies.push_back(
                  OptStrategyName(strategy));
              std::error_code ec;
              std::filesystem::remove(candidate, ec);
              continue;
            }
            ++sta_evaluations;
            if (CompareOptCost(candidate_eval, incumbent_eval) <= 0 ||
                OptRegressesWorkingSet(candidate_eval, incumbent_eval,
                                       working_pins)) {
              std::error_code ec;
              std::filesystem::remove(candidate, ec);
              continue;
            }
            if (!have_best ||
                CompareOptCost(candidate_eval, best_eval) > 0) {
              if (have_best) {
                std::error_code ec;
                std::filesystem::remove(best_file, ec);
              }
              have_best = true;
              best_strategy = strategy;
              best_file = candidate;
              best_eval = candidate_eval;
            } else {
              std::error_code ec;
              std::filesystem::remove(candidate, ec);
            }
          }
          if (!have_best) break;
          committed.push_back(OptStrategyName(best_strategy));
          // Unit S7: the commit's own record. The candidate
          // was proven kEquivalent against the incumbent
          // before it could become the best of this
          // iteration, so the proof term records that gate;
          // the STA numbers are this candidate's evaluation
          // in the resident session, and the cell count is
          // scanned from the candidate file itself.
          {
            std::map<std::string, int> commit_types;
            int commit_cells = 0;
            ScanMappedCells(best_file, cell_set, &commit_types,
                            &commit_cells);
            ctx.synth.opt_strategy_records.push_back(
                std::string(OptStrategyName(best_strategy)) + ":" +
                FormatNumber(best_eval.worst_slack) + ":" +
                FormatNumber(best_eval.total_negative) + ":" +
                std::to_string(commit_cells) + ":proof=equivalent");
          }
          if (!incumbent_temp.empty()) {
            std::error_code ec;
            std::filesystem::remove(incumbent_temp, ec);
          }
          incumbent_temp = best_file;
          incumbent = best_file;
          incumbent_eval = best_eval;
          cleared = true;
          for (const std::string& pin : working_pins) {
            bool met = false;
            for (const analysis::EndpointSlack& endpoint :
                 incumbent_eval.endpoints) {
              if (endpoint.pin == pin &&
                  endpoint.slack_ns >= -kOptEpsNs) {
                met = true;
              }
            }
            if (!met) cleared = false;
          }
          if (cleared) break;
        }
        std::vector<std::pair<std::string, double>> final_endpoints;
        std::vector<std::string> still_violating;
        for (const std::string& pin : working_pins) {
          double slack = 0.0;
          bool found = false;
          for (const analysis::EndpointSlack& endpoint :
               incumbent_eval.endpoints) {
            if (endpoint.pin == pin) {
              slack = endpoint.slack_ns;
              found = true;
            }
          }
          if (found) {
            final_endpoints.emplace_back(pin, slack);
            if (slack < -kOptEpsNs) {
              still_violating.push_back(pin + ":" +
                                        FormatNumber(slack));
            }
          }
        }
        const auto scanned = scan_incumbent();
        ctx.synth.opt_run = true;
        ctx.synth.opt_winner =
            committed.empty() ? "" : committed.back();
        ctx.synth.opt_iterations = static_cast<int>(iterations);
        ctx.synth.opt_verilog = incumbent;
        ctx.synth.opt_types = scanned.first;
        ctx.synth.opt_cells = scanned.second;
        ctx.synth.opt_worst_slack_ns = incumbent_eval.worst_slack;
        ctx.synth.opt_total_negative_slack_ns =
            incumbent_eval.total_negative;
        ctx.synth.opt_still_violating = still_violating;
        ctx.timing.verilog_path = incumbent;
        return CommandResult::Ok(
            "iterations=" + std::to_string(iterations) +
            " max_iterations=" + std::to_string(max_iterations) +
            " margin_ns=" + FormatNumber(margin) +
            " baseline_worst_slack_ns=" +
            FormatNumber(baseline_worst_slack) +
            " baseline_total_negative_slack_ns=" +
            FormatNumber(baseline_total_negative) +
            " working_set=" + OptEndpointListText(working_baseline) +
            " committed=" + OptNameListText(committed) +
            " winner=" +
            (committed.empty() ? "none" : committed.back()) +
            " final_worst_slack_ns=" +
            FormatNumber(incumbent_eval.worst_slack) +
            " final_total_negative_slack_ns=" +
            FormatNumber(incumbent_eval.total_negative) +
            " sta_evaluations=" + std::to_string(sta_evaluations) +
            " sta_sessions=1 verilog=" + incumbent +
            " endpoints=" + OptEndpointListText(final_endpoints) +
            " still_violating=" + OptNameListText(still_violating) +
            " equivalence_rejected=" +
            std::to_string(equivalence_rejected) +
            " failed_strategies=" +
            OptNameListText(failed_strategies));
      });

  // --- write_verilog, check_equivalence, synth_report (unit
  // S7): output, equivalence, and qualification. ---
  //
  // write_verilog copies the current netlist to the path the
  // caller names: the loop's winner when the loop ran, the
  // mapped netlist otherwise. It is a copy, not a
  // re-synthesis: the deliverable is byte-identical to the
  // netlist the loop or the mapper produced, and the session
  // records which source it came from. A failed write leaves
  // the previous output record untouched.
  add("write_verilog", "write_verilog <path>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 1, "write_verilog <path>")) {
          return *bad;
        }
        if (!ctx.synth.mapped) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no mapped design: eos::synth_map first");
        }
        const std::string source =
            ctx.synth.opt_run ? ctx.synth.opt_verilog
                              : ctx.synth.mapped_verilog;
        const std::string source_name =
            ctx.synth.opt_run ? "opt" : "mapped";
        if (auto bad = CheckReadable(source)) return *bad;
        const std::string& dest = args[0];
        std::error_code ec;
        const bool same =
            std::filesystem::equivalent(source, dest, ec) && !ec;
        if (!same) {
          std::ifstream in(source, std::ios::binary);
          if (!in) {
            return CommandResult::Fail(
                Diag::kIoError, "could not read file: " + source);
          }
          std::ofstream out(dest, std::ios::binary);
          if (!out) {
            return CommandResult::Fail(
                Diag::kIoError, "could not write file: " + dest);
          }
          out << in.rdbuf();
          out.flush();
          if (!out) {
            return CommandResult::Fail(
                Diag::kIoError, "could not write file: " + dest);
          }
        }
        std::set<std::string> cell_set;
        {
          std::size_t start = 0;
          const std::string& cells = ctx.synth.mapped_genlib_cells;
          while (start <= cells.size()) {
            const std::size_t comma = cells.find(',', start);
            const std::string cell = cells.substr(
                start, comma == std::string::npos ? std::string::npos
                                                  : comma - start);
            if (!cell.empty()) cell_set.insert(cell);
            if (comma == std::string::npos) break;
            start = comma + 1;
          }
        }
        std::map<std::string, int> by_type;
        int total_cells = 0;
        ScanMappedCells(dest, cell_set, &by_type, &total_cells);
        const std::string types = CellTypesText(by_type);
        ctx.synth.output_written = true;
        ctx.synth.output_verilog = dest;
        ctx.synth.output_source = source_name;
        ctx.synth.output_cells = total_cells;
        ctx.synth.output_types = types;
        return CommandResult::Ok(
            "path=" + dest + " source=" + source_name +
            " cells=" + std::to_string(total_cells) +
            " types=" + types);
      });

  // check_equivalence proves two gate-level netlists
  // equivalent with ABC cec, through the same facade the
  // loop uses: an unreadable input is a precondition failure
  // before the prover runs, and a proof that does not
  // conclude (limit hit, name mismatch, unreadable netlist
  // inside the prover) is kUnknown. Only kEquivalent passes:
  // a not-equivalent or unknown proof fails the step, never
  // passes it, and the verdict is recorded in the session
  // either way so synth_report can report it. The diagnostic
  // taxonomy has no not-equivalent code, so both failing
  // verdicts share kProofUnknown with the exact verdict in
  // the message. The genlib defaults to the mapping's
  // conversion; an explicit -genlib overrides it.
  add("check_equivalence",
      "check_equivalence <first> <second> [-genlib <path>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        std::vector<std::string> positionals;
        std::string genlib;
        bool genlib_seen = false;
        for (std::size_t i = 0; i < args.size(); ++i) {
          if (args[i] == "-genlib") {
            if (i + 1 >= args.size()) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -genlib needs a value");
            }
            if (genlib_seen) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -genlib given twice");
            }
            genlib_seen = true;
            genlib = args[++i];
            if (genlib.empty()) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -genlib needs a non-empty value");
            }
          } else if (!args[i].empty() && args[i][0] == '-') {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "unknown check_equivalence option: " + args[i]);
          } else {
            positionals.push_back(args[i]);
          }
        }
        if (positionals.size() != 2) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "usage: eos::check_equivalence <first> <second> "
              "[-genlib <path>]");
        }
        if (genlib.empty()) genlib = ctx.synth.mapped_genlib;
        if (genlib.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no genlib for the proof: eos::check_equivalence "
              "-genlib <path> or eos::synth_map first");
        }
        if (auto bad = CheckReadable(positionals[0])) return *bad;
        if (auto bad = CheckReadable(positionals[1])) return *bad;
        if (auto bad = CheckReadable(genlib)) return *bad;
        synth::AbcEngine abc;
        const foundation::Result<synth::EquivalenceVerdict> proof =
            abc.ProveEquivalent(positionals[0], positionals[1],
                                genlib);
        if (!proof.ok()) return FromStatus(proof.status());
        const char* verdict = "unknown";
        if (proof.value() == synth::EquivalenceVerdict::kEquivalent) {
          verdict = "equivalent";
        } else if (proof.value() ==
                   synth::EquivalenceVerdict::kNotEquivalent) {
          verdict = "not_equivalent";
        }
        ctx.synth.equiv_run = true;
        ctx.synth.equiv_verdict = verdict;
        ctx.synth.equiv_first = positionals[0];
        ctx.synth.equiv_second = positionals[1];
        if (proof.value() != synth::EquivalenceVerdict::kEquivalent) {
          return CommandResult::Fail(
              Diag::kProofUnknown,
              std::string("equivalence proof: ") + verdict + " for " +
                  positionals[0] + " vs " + positionals[1]);
        }
        return CommandResult::Ok(
            std::string("verdict=equivalent first=") +
            positionals[0] + " second=" + positionals[1] +
            " genlib=" + genlib);
      });

  // synth_report is the phase exit report: one payload
  // carrying every stage's record, the per-strategy STA
  // metrics of the loop's commits with their equivalence
  // proofs, the winning strategy, the deliverable, and the
  // most recent equivalence verdict. It never fails for
  // lack of progress: a stage not reached reports "none"
  // for its values, so the report is readable at any point
  // in the flow. The winner is the loop's last commit, and
  // each commit strictly improved the loop's lexicographic
  // cost (worst slack, then total negative slack, then
  // worst hold slack, then total negative hold slack, then
  // electrical violations), so the winner is the
  // lexicographic best of the strategies that ran. No power
  // metric is reported: no synthesis command in this phase
  // measures power on a mapped netlist, so the report
  // records cell count (the area term) per strategy and
  // does not invent a power number.
  add("synth_report", "synth_report",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 0, "synth_report")) {
          return *bad;
        }
        const CommandContext::SynthSession& synth = ctx.synth;
        std::string stages;
        const auto add_stage = [&stages](const char* stage) {
          if (!stages.empty()) stages += ",";
          stages += stage;
        };
        if (synth.parsed) add_stage("parsed");
        if (synth.elaborated) add_stage("elaborated");
        if (synth.generic_optimized) add_stage("generic");
        if (synth.mapped) add_stage("mapped");
        if (synth.opt_run) add_stage("opt");
        if (synth.output_written) add_stage("output");
        if (synth.equiv_run) add_stage("equiv");
        if (stages.empty()) stages = "none";
        std::string payload = "stages=" + stages +
                              " top=" + synth.top_cell;
        payload += " generic_script=" +
                   (synth.generic_script.empty() ? "none"
                                                 : synth.generic_script);
        payload += " generic_before_and=" +
                   std::to_string(synth.generic_before_and) +
                   " generic_before_levels=" +
                   std::to_string(synth.generic_before_levels) +
                   " generic_after_and=" +
                   std::to_string(synth.generic_after_and) +
                   " generic_after_levels=" +
                   std::to_string(synth.generic_after_levels);
        payload += " mapped_target=" +
                   (synth.mapped_target.empty() ? "none"
                                                : synth.mapped_target);
        payload += " mapped_cells=" + std::to_string(synth.mapped_cells) +
                   " mapped_types=" +
                   (synth.mapped_types.empty() ? "none"
                                               : synth.mapped_types);
        payload += std::string(" mapped_timing_judged=") +
                   (synth.mapped_timing_judged ? "true" : "false");
        payload += " mapped_worst_slack_ns=" +
                   (synth.mapped_timing_judged
                        ? FormatNumber(synth.mapped_worst_slack_ns)
                        : "none");
        payload += " mapped_total_negative_slack_ns=" +
                   (synth.mapped_timing_judged
                        ? FormatNumber(
                              synth.mapped_total_negative_slack_ns)
                        : "none");
        payload += " mapped_electrical_violations=" +
                   std::to_string(synth.mapped_electrical_violations);
        payload += std::string(" opt_run=") +
                   (synth.opt_run ? "true" : "false");
        payload += " opt_winner=" +
                   (synth.opt_winner.empty() ? "none" : synth.opt_winner);
        payload += " opt_iterations=" +
                   std::to_string(synth.opt_iterations);
        payload += " opt_cells=" + std::to_string(synth.opt_cells) +
                   " opt_types=" +
                   (synth.opt_types.empty() ? "none" : synth.opt_types);
        payload += " opt_worst_slack_ns=" +
                   (synth.opt_run
                        ? FormatNumber(synth.opt_worst_slack_ns)
                        : "none");
        payload += " opt_total_negative_slack_ns=" +
                   (synth.opt_run
                        ? FormatNumber(
                              synth.opt_total_negative_slack_ns)
                        : "none");
        payload += " opt_still_violating=" +
                   OptNameListText(synth.opt_still_violating);
        payload += " strategies=";
        if (synth.opt_strategy_records.empty()) {
          payload += "none";
        } else {
          for (std::size_t i = 0;
               i < synth.opt_strategy_records.size(); ++i) {
            if (i > 0) payload += ";";
            payload += synth.opt_strategy_records[i];
          }
        }
        payload +=
            " winner_rule=lexicographic:worst_slack_ns,"
            "total_negative_slack_ns,worst_hold_slack_ns,"
            "total_negative_hold_slack_ns,electrical_violations";
        payload += " winner=" +
                   (synth.opt_winner.empty() ? "none" : synth.opt_winner);
        payload += " output=" +
                   (synth.output_verilog.empty() ? "none"
                                                 : synth.output_verilog);
        payload += " output_source=" +
                   (synth.output_source.empty() ? "none"
                                                : synth.output_source);
        payload += " output_cells=" +
                   std::to_string(synth.output_cells);
        payload += " equivalence=" +
                   (synth.equiv_verdict.empty() ? "not_run"
                                                : synth.equiv_verdict);
        const std::string deliverable =
            synth.output_written
                ? synth.output_verilog
                : (synth.opt_run ? synth.opt_verilog
                                 : (synth.mapped ? synth.mapped_verilog
                                                : ""));
        payload += " deliverable=" +
                   (deliverable.empty() ? "none" : deliverable);
        return CommandResult::Ok(payload);
      });

  add("read_sdc", "read_sdc <path>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 1, "read_sdc <path>")) {
          return *bad;
        }
        if (auto bad = CheckReadable(args[0])) return *bad;
        ctx.timing.sdc_path = args[0];
        return CommandResult::Ok("sdc=" + args[0]);
      });

  add("set_spef", "set_spef <path>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 1, "set_spef <path>")) {
          return *bad;
        }
        if (auto bad = CheckReadable(args[0])) return *bad;
        ctx.timing.spef_path = args[0];
        return CommandResult::Ok("spef=" + args[0]);
      });

  add("run_timing", "run_timing",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 0, "run_timing")) return *bad;
        bool from_odb = false;
        std::vector<analysis::SceneAnalysis> scenes;
        if (auto bad = BuildTimingScenes(ctx, from_odb, scenes)) {
          return *bad;
        }
        analysis::TimingService service;
        const Result<std::vector<analysis::SceneResult>> analyzed =
            from_odb
                ? service.Analyze(*ctx.timing.odb, ctx.timing.sdc_path,
                                  scenes)
                : service.Analyze(
                      analysis::VerilogDesign{ctx.timing.verilog_path,
                                              ctx.timing.top_cell,
                                              ctx.timing.sdc_path},
                      scenes);
        if (!analyzed.ok()) return FromStatus(analyzed.status());
        // One report binding per scene, registered through
        // session/reports at the current ledger revision. The
        // analysis completed, so the report's proof is pass: the
        // metrics are the measured result, whatever their sign.
        std::string payload = std::string("source=") +
                              (from_odb ? "odb" : "verilog") +
                              " scenes=" +
                              std::to_string(analyzed.value().size()) +
                              " revision=" +
                              std::to_string(
                                  ctx.project.ledger.current_revision());
        for (const analysis::SceneResult& result : analyzed.value()) {
          const std::string report = "timing_" + result.scenario.name;
          const Result<session::ArtifactId> artifact =
              ctx.reports.RegisterReport(ctx.project, report,
                                         session::ProofStatus::kPass,
                                         "NLDM");
          if (!artifact.ok()) return FromStatus(artifact.status());
          payload += "\nscene=" + result.scenario.name +
                     " mode=" + result.scenario.mode +
                     " corner=" + result.scenario.corner +
                     " parasitics=" +
                     std::string(result.scenario.parasitics ==
                                         db::ParasiticsLevel::kExtracted
                                     ? "extracted"
                                     : "estimate") +
                     " worst_slack_ns=" +
                     MetricText(result.metrics, "worst_slack_ns") +
                     " total_negative_slack_ns=" +
                     MetricText(result.metrics, "total_negative_slack_ns") +
                     " worst_hold_slack_ns=" +
                     MetricText(result.metrics, "worst_hold_slack_ns") +
                     " total_negative_hold_slack_ns=" +
                     MetricText(result.metrics,
                                "total_negative_hold_slack_ns") +
                     " report=" + report + " artifact=" +
                     std::to_string(artifact.value());
        }
        ctx.timing.last_results = analyzed.value();
        return CommandResult::Ok(payload);
      });

  // Signoff-style timing path report, on the Tempus column model
  // over the PrimeTime selection vocabulary. The command runs the
  // analysis itself for the selected scenes (it does not reuse a
  // previous run_timing: the session keeps no live engine), then
  // reports the worst paths: -delay_type max (setup, the default)
  // or min (hold); -max_paths across endpoints, worst first; -to
  // for one endpoint's worst path; -slack_lesser_than as a slack
  // ceiling; -columns as a comma-separated column list in the
  // requested order (default: point,cell,edge,fanout,cap,slew,incr,
  // arrival); -path_type end (slack line only), summary (header
  // block, the default is full) or full (header plus point table);
  // -digits for decimals in the table (default 3); -file to write
  // the report to a file and register it; -scene to report one
  // active scenario instead of all of them. Per-endpoint path
  // enumeration is not implemented: -to with -max_paths above 1
  // fails rather than silently reporting one path.
  add("report_timing", kReportTimingUsage,
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        // A help flag anywhere in the arguments prints the usage
        // and succeeds, without running anything: this is the only
        // command that takes -key options, so it is the only one
        // with a flag-level help path. Every command is covered by
        // eos::help instead.
        for (const std::string& arg : args) {
          if (arg == "-help" || arg == "--help" || arg == "-h") {
            return CommandResult::Ok(kReportTimingUsage);
          }
        }
        std::map<std::string, std::string> options;
        if (auto bad = ParseReportOptions(args, options)) return *bad;
        analysis::PathReportRequest request;
        const auto at = [&options](const char* key) {
          return options.find(key);
        };
        if (at("delay_type") != options.end()) {
          if (options["delay_type"] == "max") {
            request.kind = analysis::CheckKind::kSetup;
          } else if (options["delay_type"] == "min") {
            request.kind = analysis::CheckKind::kHold;
          } else {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "delay_type must be max or min, got: " +
                    options["delay_type"]);
          }
        }
        if (at("max_paths") != options.end()) {
          std::uint64_t max_paths = 0;
          if (!ParseU64(options["max_paths"], max_paths) ||
              max_paths == 0) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "max_paths must be a positive integer, got: " +
                    options["max_paths"]);
          }
          request.max_paths = static_cast<std::size_t>(max_paths);
        }
        if (at("to") != options.end()) {
          request.endpoint = options["to"];
          if (at("max_paths") != options.end() &&
              request.max_paths != 1) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "-to selects one endpoint's worst path; -max_paths "
                "above 1 needs per-endpoint enumeration, which "
                "report_timing does not implement yet");
          }
        }
        if (at("slack_lesser_than") != options.end()) {
          const std::string& token = options["slack_lesser_than"];
          try {
            std::size_t used = 0;
            const double ceiling = std::stod(token, &used);
            if (used != token.size()) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "slack_lesser_than must be a number, got: " + token);
            }
            request.slack_lesser_than = ceiling;
          } catch (const std::exception&) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "slack_lesser_than must be a number, got: " + token);
          }
        }
        std::vector<ReportColumn> columns;
        if (at("columns") != options.end()) {
          std::stringstream list(options["columns"]);
          std::string token;
          while (std::getline(list, token, ',')) {
            bool known = false;
            for (const ReportColumn& candidate : kReportColumns) {
              if (token == candidate.name) {
                columns.push_back(candidate);
                known = true;
              }
            }
            if (!known) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "unknown report_timing column: " + token);
            }
          }
          if (columns.empty()) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "-columns needs at least one column");
          }
        } else {
          for (const ReportColumn& candidate : kReportColumns) {
            columns.push_back(candidate);
          }
        }
        std::string path_type = "full";
        if (at("path_type") != options.end()) {
          path_type = options["path_type"];
          if (path_type != "end" && path_type != "summary" &&
              path_type != "full") {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "path_type must be end, summary or full, got: " +
                    path_type);
          }
        }
        int digits = 3;
        if (at("digits") != options.end()) {
          std::uint64_t parsed = 0;
          if (!ParseU64(options["digits"], parsed)) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "digits must be a non-negative integer, got: " +
                    options["digits"]);
          }
          digits = static_cast<int>(parsed);
        }
        bool from_odb = false;
        std::vector<analysis::SceneAnalysis> scenes;
        if (auto bad = BuildTimingScenes(ctx, from_odb, scenes)) {
          return *bad;
        }
        if (at("scene") != options.end()) {
          std::vector<analysis::SceneAnalysis> selected;
          for (const analysis::SceneAnalysis& scene : scenes) {
            if (scene.scenario.name == options["scene"]) {
              selected.push_back(scene);
            }
          }
          if (selected.empty()) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "unknown scenario: " + options["scene"]);
          }
          scenes = std::move(selected);
        }
        analysis::TimingService service;
        const Result<std::vector<analysis::ScenePathReports>> reported =
            from_odb
                ? service.ReportPaths(*ctx.timing.odb, ctx.timing.sdc_path,
                                      scenes, request)
                : service.ReportPaths(
                      analysis::VerilogDesign{ctx.timing.verilog_path,
                                              ctx.timing.top_cell,
                                              ctx.timing.sdc_path},
                      scenes, request);
        if (!reported.ok()) return FromStatus(reported.status());
        std::string payload = std::string("source=") +
                              (from_odb ? "odb" : "verilog") +
                              " delay_type=" +
                              (request.kind == analysis::CheckKind::kHold
                                   ? "min"
                                   : "max") +
                              " scenes=" +
                              std::to_string(reported.value().size()) +
                              " revision=" +
                              std::to_string(
                                  ctx.project.ledger.current_revision());
        std::string text;
        for (const analysis::ScenePathReports& scene :
             reported.value()) {
          const std::string report =
              "timing_report_" + scene.scenario.name;
          const Result<session::ArtifactId> artifact =
              ctx.reports.RegisterReport(ctx.project, report,
                                         session::ProofStatus::kPass,
                                         "NLDM");
          if (!artifact.ok()) return FromStatus(artifact.status());
          payload += "\nscene=" + scene.scenario.name +
                     " paths=" + std::to_string(scene.paths.size()) +
                     " report=" + report + " artifact=" +
                     std::to_string(artifact.value());
          text += "\nScene: " + scene.scenario.name + "\n";
          for (const analysis::PathReport& path : scene.paths) {
            text += RenderPathReport(path, columns, path_type, digits);
            text += "\n";
          }
        }
        if (at("file") != options.end()) {
          const std::string file = options["file"];
          std::ofstream out(file);
          if (!out) {
            return CommandResult::Fail(Diag::kIoError,
                                       "could not write file: " + file);
          }
          out << text;
          out.close();
          if (!out) {
            return CommandResult::Fail(Diag::kIoError,
                                       "could not write file: " + file);
          }
          payload += "\nfile=" + file;
        }
        payload += "\n" + text;
        return CommandResult::Ok(payload);
      });

  add("close_timing", "close_timing <max_iterations>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad =
                ExpectCount(args, 1, "close_timing <max_iterations>")) {
          return *bad;
        }
        std::uint64_t max_iterations = 0;
        if (!ParseU64(args[0], max_iterations) || max_iterations == 0) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "max_iterations must be a positive integer, got: " + args[0]);
        }
        if (!ctx.timing.odb.has_value() ||
            !ctx.timing.odb->has_top_block()) {
          return CommandResult::Fail(
              Diag::kUnclosedDesign,
              "no ODB design: eos::import_odb first (timing closure "
              "edits the physical database; a Verilog source has no "
              "instances to size or buffer)");
        }
        if (ctx.timing.sdc_path.empty()) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "no SDC: eos::read_sdc first");
        }
        const std::vector<const db::Scenario*> active =
            ctx.project.scenarios.ActiveScenarios();
        if (active.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no active scenarios (see eos::create_scenario)");
        }
        std::vector<analysis::SceneAnalysis> scenes;
        for (const db::Scenario* scenario : active) {
          analysis::SceneAnalysis scene;
          scene.scenario = *scenario;
          const auto liberty =
              ctx.timing.liberty_by_corner.find(scenario->corner);
          if (liberty == ctx.timing.liberty_by_corner.end()) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "no Liberty for corner " + scenario->corner +
                    " (scene " + scenario->name +
                    "): eos::read_liberty first");
          }
          scene.liberty_path = liberty->second;
          if (scenario->parasitics == db::ParasiticsLevel::kExtracted) {
            scene.spef_path = ctx.timing.spef_path;
          }
          scenes.push_back(std::move(scene));
        }
        session::CloseTimingRequest request;
        request.sdc_path = ctx.timing.sdc_path;
        request.scenes = std::move(scenes);
        request.max_iterations =
            static_cast<std::size_t>(max_iterations);
        DeriveClosureCandidates(*ctx.timing.odb, &request.sizing_families,
                                &request.buffer_masters);
        const Result<session::ClosureOutcome> outcome =
            session::RunTimingClosure(ctx.project, *ctx.timing.odb, request);
        if (!outcome.ok()) return FromStatus(outcome.status());
        std::string payload =
            std::string("closed=") + (outcome.value().closed ? "1" : "0") +
            " iterations=" +
            std::to_string(outcome.value().steps.size()) +
            " candidates_tried=" +
            std::to_string(outcome.value().candidates_tried) +
            " candidates_rejected=" +
            std::to_string(outcome.value().candidates_rejected) +
            " revision=" +
            std::to_string(ctx.project.ledger.current_revision()) +
            " report=timing_closure";
        for (std::size_t i = 0; i < outcome.value().final.size(); ++i) {
          const analysis::SceneResult& scene = outcome.value().final[i];
          const analysis::SceneResult& initial = outcome.value().initial[i];
          payload += "\nscene=" + scene.scenario.name +
                     " worst_slack_ns=" +
                     MetricText(scene.metrics, "worst_slack_ns") +
                     " total_negative_slack_ns=" +
                     MetricText(scene.metrics, "total_negative_slack_ns") +
                     " worst_hold_slack_ns=" +
                     MetricText(scene.metrics, "worst_hold_slack_ns") +
                     " total_negative_hold_slack_ns=" +
                     MetricText(scene.metrics,
                                "total_negative_hold_slack_ns") +
                     " initial_worst_slack_ns=" +
                     MetricText(initial.metrics, "worst_slack_ns");
        }
        payload += "\nstop_reason=" + outcome.value().stop_reason;
        ctx.timing.last_results = outcome.value().final;
        CommandResult result = CommandResult::Ok(payload);
        if (outcome.value().closed) {
          result.diag = Diag::kVerifiedPass;
        }
        return result;
      });

  // Per-command help. With no argument it prints every command's
  // usage line, the same list list_commands prints; with a command
  // name it prints just that command's usage. The "eos::" namespace
  // prefix is accepted and stripped. This is the discoverability
  // entry point the shell's tab completion points at: list_commands
  // stays as the script-friendly full listing.
  add("help", "help [command]",
      [&registry](CommandContext&, const std::vector<std::string>& args)
          -> CommandResult {
        if (args.size() > 1) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "usage: eos::help [command]");
        }
        if (args.empty()) {
          std::string out;
          for (const std::string& name : registry.Names()) {
            if (!out.empty()) out += "\n";
            out += name + " " + registry.Find(name)->usage;
          }
          return CommandResult::Ok(out);
        }
        std::string name = args[0];
        if (name.rfind("eos::", 0) == 0) {
          name = name.substr(5);
        }
        const CommandInfo* info = registry.Find(name);
        if (info == nullptr) {
          return CommandResult::Fail(Diag::kInvalidCommand,
                                     "unknown command: " + args[0]);
        }
        return CommandResult::Ok(info->name + " " + info->usage);
      });

  add("list_commands", "list_commands",
      [&registry](CommandContext&, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 0, "list_commands")) return *bad;
        std::string out;
        for (const std::string& name : registry.Names()) {
          if (!out.empty()) out += "\n";
          out += name + " " + registry.Find(name)->usage;
        }
        return CommandResult::Ok(out);
      });

  RegisterRepairCommands(registry);
}

}  // namespace eos::ui
