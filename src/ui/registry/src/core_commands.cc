// registry: the core command set (L6 interface).
//
// Every handler follows the same discipline: validate the argument
// shape first (unsupported options are rejected, never ignored), then
// the session state (an unclosed design is its own diagnostic), then
// run the operation and map its Status onto the diagnostic taxonomy.
#include "eos/ui/registry.h"

#include "eos/analysis/sta.h"
#include "eos/pd/cts.h"
#include "eos/pd/finish.h"
#include "eos/pd/floorplan.h"
#include "eos/pd/place.h"
#include "eos/pd/route.h"
#include "eos/pv/extract.h"
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
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <unistd.h>
#include <zlib.h>


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

// CPU seconds since a std::clock reading, for the compile-CPU
// record (Immediate unit 9): the process CPU time the synthesis
// commands took, not wall time.
double CpuSecondsSince(std::clock_t start) {
  return static_cast<double>(std::clock() - start) /
         static_cast<double>(CLOCKS_PER_SEC);
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

// Glob match for eos::help patterns: '*' matches any run of
// characters, '?' matches exactly one character. Case-sensitive,
// like the exact command lookup.
bool GlobMatches(const std::string& pattern, const std::string& name) {
  std::size_t p = 0;
  std::size_t n = 0;
  std::size_t star_p = std::string::npos;
  std::size_t star_n = 0;
  while (n < name.size()) {
    if (p < pattern.size() &&
        (pattern[p] == '?' || pattern[p] == name[n])) {
      ++p;
      ++n;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star_p = p++;
      star_n = n;
    } else if (star_p != std::string::npos) {
      p = star_p + 1;
      n = ++star_n;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') ++p;
  return p == pattern.size();
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
    case db::ConstraintKind::kClockGatingCheck: return "clock_gating_check";
    case db::ConstraintKind::kDataCheck: return "data_check";
    case db::ConstraintKind::kMinPulseWidth: return "min_pulse_width";
    case db::ConstraintKind::kSense: return "sense";
    case db::ConstraintKind::kIdealNetwork: return "ideal_network";
    case db::ConstraintKind::kIdealLatency: return "ideal_latency";
    case db::ConstraintKind::kIdealTransition: return "ideal_transition";
    case db::ConstraintKind::kGroupPath: return "group_path";
    case db::ConstraintKind::kMaxTimeBorrow: return "max_time_borrow";
    case db::ConstraintKind::kGeneratedClock: return "generated_clock";
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
  if (word == "clock_gating_check") {
    return db::ConstraintKind::kClockGatingCheck;
  }
  if (word == "data_check") return db::ConstraintKind::kDataCheck;
  if (word == "min_pulse_width") return db::ConstraintKind::kMinPulseWidth;
  if (word == "sense") return db::ConstraintKind::kSense;
  if (word == "ideal_network") return db::ConstraintKind::kIdealNetwork;
  if (word == "ideal_latency") return db::ConstraintKind::kIdealLatency;
  if (word == "ideal_transition") {
    return db::ConstraintKind::kIdealTransition;
  }
  if (word == "group_path") return db::ConstraintKind::kGroupPath;
  if (word == "max_time_borrow") return db::ConstraintKind::kMaxTimeBorrow;
  if (word == "generated_clock") return db::ConstraintKind::kGeneratedClock;
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
  ctx.engine_log_lines.clear();
}

// Drains the engine's captured log output into the context's
// engine_log_lines, so the GUI log window can show it on its
// next refresh. The drain is the registry's only contact with
// the capture buffer: a command that touches the engine calls
// this once when it finishes, success or failure, so the log
// window shows the engine's own lines for the command just
// run, in order. The field is capped at 2000 lines, the oldest
// dropping, matching the facade's cap. The drain is static, so
// it is safe to call after the command's engine scope has
// closed; it constructs no engine and never initializes Yosys
// on its own.
void AppendEngineLog(CommandContext& ctx) {
  const std::string drained = synth::YosysEngine::TakeLog();
  if (drained.empty()) return;
  std::size_t start = 0;
  while (start < drained.size()) {
    const std::size_t newline = drained.find('\n', start);
    const std::string line =
        newline == std::string::npos
            ? drained.substr(start)
            : drained.substr(start, newline - start);
    if (!line.empty()) {
      ctx.engine_log_lines.push_back(line);
      while (ctx.engine_log_lines.size() > 2000) {
        ctx.engine_log_lines.erase(ctx.engine_log_lines.begin());
      }
    }
    if (newline == std::string::npos) break;
    start = newline + 1;
  }
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

// Mapped-netlist reader and DEF writer (unit F0, synth-to-PD
// bridge).
//
// The bridge turns the mapped Verilog synth_map wrote (ABC's mapped
// Verilog writer format, see ioWriteVerilog.c in the imported ABC
// tree) into a DEF file the ODB facade can read back, so a design
// synthesized in-session can reach floorplan without an ORFS run.
// The reader is deliberately narrow: it accepts exactly the
// constructs ABC's mapped writer emits, and fails fast naming the
// line of anything else, so a netlist from another writer can
// never be misread as a mapped design. Known limits, stated here
// rather than hidden: ABC writes constant nets as `assign <net> =
// 1'b0;` lines (unsupported: tie the constant off in the RTL),
// drops feedthrough outputs from the module port list (a design
// whose output is driven straight by an input loses that pin
// here), and a gate line carrying a twin node's second output
// surfaces as a duplicate pin and is rejected at the LEF check.
// A gate must have exactly one output pin in the LEF, so
// multi-output cells are rejected the same way.
struct MappedGate {
  std::string cell;
  std::string instance;
  // (pin, net) in written order; which pin is the output is
  // decided by the LEF check, not by position.
  std::vector<std::pair<std::string, std::string>> pins;
};

struct MappedNetlist {
  std::string top;
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  // Internal nets, in wire-declaration order.
  std::vector<std::string> wires;
  std::vector<MappedGate> gates;
};

struct VerilogToken {
  std::string text;
  int line = 1;
};

Result<std::vector<VerilogToken>> TokenizeVerilog(
    const std::string& text, const std::string& path) {
  std::vector<VerilogToken> tokens;
  std::size_t i = 0;
  int line = 1;
  auto fail = [&](const std::string& message) {
    return Status::Error(
        ErrorCode::kInvalidArgument,
        "mapped Verilog " + path + " line " + std::to_string(line) +
            ": " + message);
  };
  while (i < text.size()) {
    const char c = text[i];
    if (c == '\n') {
      ++line;
      ++i;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r') {
      ++i;
      continue;
    }
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      while (i < text.size() && text[i] != '\n') ++i;
      continue;
    }
    if (c == '\\') {
      // Verilog escaped identifier: a backslash plus every
      // character up to whitespace. ABC writes exactly this form
      // for names holding characters outside [A-Za-z0-9_], and
      // for names starting with a digit; the raw name is the
      // token text.
      const std::size_t start = i + 1;
      std::size_t end = start;
      while (end < text.size() && text[end] != ' ' &&
             text[end] != '\t' && text[end] != '\r' &&
             text[end] != '\n') {
        ++end;
      }
      if (end == start) return fail("stray backslash");
      tokens.push_back({text.substr(start, end - start), line});
      i = end;
      continue;
    }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
        c == '$') {
      const std::size_t start = i;
      while (i < text.size() &&
             ((text[i] >= 'a' && text[i] <= 'z') ||
              (text[i] >= 'A' && text[i] <= 'Z') ||
              (text[i] >= '0' && text[i] <= '9') || text[i] == '_' ||
              text[i] == '$')) {
        ++i;
      }
      tokens.push_back({text.substr(start, i - start), line});
      continue;
    }
    if (c >= '0' && c <= '9') {
      const std::size_t start = i;
      while (i < text.size() &&
             ((text[i] >= '0' && text[i] <= '9') || text[i] == '\'' ||
              (text[i] >= 'a' && text[i] <= 'z') ||
              (text[i] >= 'A' && text[i] <= 'Z'))) {
        ++i;
      }
      tokens.push_back({text.substr(start, i - start), line});
      continue;
    }
    if (c == '(' || c == ')' || c == ',' || c == ';' || c == '.' ||
        c == '=') {
      tokens.push_back({std::string(1, c), line});
      ++i;
      continue;
    }
    return fail(std::string("unexpected character '") + c + "'");
  }
  return tokens;
}

// Reads one ABC mapped Verilog file. cell_set is the genlib cell
// list from the mapping run: a line whose first token is a genlib
// cell starts a gate instantiation, and any other statement shape
// is an error. Port and wire lists may wrap across lines; gate
// lines are one instantiation each, as ABC writes them.
Result<MappedNetlist> ParseMappedVerilog(
    const std::string& path, const std::set<std::string>& cell_set) {
  std::ifstream in(path);
  if (!in) {
    return Status::Error(ErrorCode::kIoError,
                         "could not read file: " + path);
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  if (in.bad()) {
    return Status::Error(ErrorCode::kIoError,
                         "could not read file: " + path);
  }
  const Result<std::vector<VerilogToken>> tokenized =
      TokenizeVerilog(buffer.str(), path);
  if (!tokenized.ok()) return tokenized.status();
  const std::vector<VerilogToken>& tokens = tokenized.value();
  std::size_t pos = 0;
  auto at_end = [&]() { return pos >= tokens.size(); };
  auto peek = [&]() -> const VerilogToken& { return tokens[pos]; };
  auto fail_at = [&](const VerilogToken& token,
                     const std::string& message) {
    return Status::Error(
        ErrorCode::kInvalidArgument,
        "mapped Verilog " + path + " line " +
            std::to_string(token.line) + ": " + message);
  };
  auto expect = [&](const std::string& text)
      -> std::optional<Status> {
    if (at_end() || peek().text != text) {
      const std::string got =
          at_end() ? std::string("end of file") : peek().text;
      const int line = at_end() ? 0 : peek().line;
      return Status::Error(
          ErrorCode::kInvalidArgument,
          "mapped Verilog " + path +
              (line > 0 ? " line " + std::to_string(line) : "") +
              ": expected '" + text + "', found '" + got + "'");
    }
    ++pos;
    return std::nullopt;
  };
  auto read_list = [&](std::vector<std::string>* out,
                       std::vector<std::size_t>* positions,
                       const std::string& terminator)
      -> std::optional<Status> {
    // Reads identifiers separated by commas up to the terminator
    // (';' for declarations, ')' for the module header list,
    // which the caller then closes with ';'), recording each
    // name's token position for error messages.
    while (true) {
      if (at_end()) {
        return Status::Error(
            ErrorCode::kInvalidArgument,
            "mapped Verilog " + path + ": unterminated list");
      }
      positions->push_back(pos);
      out->push_back(peek().text);
      ++pos;
      if (at_end()) {
        return Status::Error(
            ErrorCode::kInvalidArgument,
            "mapped Verilog " + path + ": unterminated list");
      }
      if (peek().text == terminator) {
        ++pos;
        return std::nullopt;
      }
      if (peek().text != ",") {
        return fail_at(peek(), "expected ',' or '" + terminator +
                                   "' in list");
      }
      ++pos;
    }
  };

  MappedNetlist netlist;
  if (auto bad = expect("module")) return *bad;
  if (at_end()) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "mapped Verilog " + path +
                             ": module has no name");
  }
  netlist.top = peek().text;
  ++pos;
  if (auto bad = expect("(")) return *bad;
  {
    std::vector<std::string> header_ports;
    std::vector<std::size_t> header_positions;
    if (auto bad = read_list(&header_ports, &header_positions, ")")) {
      return *bad;
    }
    if (auto bad = expect(";")) return *bad;
    if (header_ports.empty()) {
      return Status::Error(ErrorCode::kInvalidArgument,
                           "mapped Verilog " + path +
                               ": module has no ports");
    }
  }
  std::set<std::string> declared;
  auto declare = [&](const std::string& name, const VerilogToken& token)
      -> std::optional<Status> {
    if (!declared.insert(name).second) {
      return fail_at(token, "duplicate net '" + name + "'");
    }
    return std::nullopt;
  };
  bool saw_endmodule = false;
  while (!at_end() && !saw_endmodule) {
    const VerilogToken& token = peek();
    if (token.text == "endmodule") {
      ++pos;
      saw_endmodule = true;
      break;
    }
    if (token.text == "input" || token.text == "output" ||
        token.text == "wire") {
      const std::string kind = token.text;
      ++pos;
      std::vector<std::string> names;
      std::vector<std::size_t> positions;
      if (auto bad = read_list(&names, &positions, ";")) return *bad;
      for (std::size_t k = 0; k < names.size(); ++k) {
        if (auto bad = declare(names[k], tokens[positions[k]])) {
          return *bad;
        }
      }
      if (kind == "input") {
        netlist.inputs.insert(netlist.inputs.end(), names.begin(),
                              names.end());
      } else if (kind == "output") {
        netlist.outputs.insert(netlist.outputs.end(), names.begin(),
                               names.end());
      } else {
        netlist.wires.insert(netlist.wires.end(), names.begin(),
                             names.end());
      }
      continue;
    }
    if (token.text == "assign") {
      ++pos;
      if (at_end()) {
        return Status::Error(ErrorCode::kInvalidArgument,
                             "mapped Verilog " + path +
                                 ": unterminated assign");
      }
      const VerilogToken& lhs = peek();
      ++pos;
      // ABC's writer emits `assign po_username<N> = <net>;` for
      // every driven output; the name is never declared, and the
      // net is the real output net. Skip those lines.
      if (lhs.text.rfind("po_username", 0) == 0) {
        if (auto bad = expect("=")) return *bad;
        if (at_end()) {
          return Status::Error(ErrorCode::kInvalidArgument,
                               "mapped Verilog " + path +
                                   ": unterminated assign");
        }
        ++pos;
        if (auto bad = expect(";")) return *bad;
        continue;
      }
      return fail_at(lhs, "constant or unexpected assign for net '" +
                              lhs.text +
                              "': the bridge takes gate netlists "
                              "only, tie constants off in the RTL");
    }
    if (cell_set.count(token.text) != 0) {
      MappedGate gate;
      gate.cell = token.text;
      ++pos;
      if (at_end()) {
        return Status::Error(ErrorCode::kInvalidArgument,
                             "mapped Verilog " + path +
                                 ": gate has no instance name");
      }
      gate.instance = peek().text;
      ++pos;
      if (auto bad = expect("(")) return *bad;
      while (true) {
        if (at_end() || peek().text != ".") {
          return Status::Error(
              ErrorCode::kInvalidArgument,
              "mapped Verilog " + path + ": gate '" + gate.instance +
                  "' expects a '.pin(net)' connection");
        }
        ++pos;
        if (at_end()) {
          return Status::Error(ErrorCode::kInvalidArgument,
                               "mapped Verilog " + path + ": gate '" +
                                   gate.instance +
                                   "' has an unterminated pin");
        }
        const std::string pin = peek().text;
        ++pos;
        if (auto bad = expect("(")) return *bad;
        if (at_end()) {
          return Status::Error(ErrorCode::kInvalidArgument,
                               "mapped Verilog " + path + ": gate '" +
                                   gate.instance +
                                   "' has an unterminated pin");
        }
        const std::string net = peek().text;
        ++pos;
        if (auto bad = expect(")")) return *bad;
        gate.pins.push_back({pin, net});
        if (at_end()) {
          return Status::Error(ErrorCode::kInvalidArgument,
                               "mapped Verilog " + path + ": gate '" +
                                   gate.instance +
                                   "' is unterminated");
        }
        if (peek().text == ",") {
          ++pos;
          continue;
        }
        if (peek().text == ")") {
          ++pos;
          break;
        }
        return fail_at(peek(), "expected ',' or ')' in gate '" +
                                   gate.instance + "'");
      }
      if (auto bad = expect(";")) return *bad;
      netlist.gates.push_back(std::move(gate));
      continue;
    }
    return fail_at(token, "unexpected statement '" + token.text +
                              "': not an ABC mapped netlist");
  }
  if (!saw_endmodule) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "mapped Verilog " + path +
                             ": no endmodule");
  }
  if (!at_end()) {
    return fail_at(peek(), "unexpected content after endmodule");
  }
  if (netlist.inputs.empty() && netlist.outputs.empty() &&
      netlist.gates.empty()) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "mapped Verilog " + path +
                             ": empty netlist");
  }
  std::set<std::string> instances;
  for (const MappedGate& gate : netlist.gates) {
    if (!instances.insert(gate.instance).second) {
      return Status::Error(ErrorCode::kInvalidArgument,
                           "mapped Verilog " + path +
                               ": duplicate instance '" + gate.instance +
                               "'");
    }
    for (const auto& pin : gate.pins) {
      if (declared.count(pin.second) == 0) {
        return Status::Error(
            ErrorCode::kInvalidArgument,
            "mapped Verilog " + path + ": gate '" + gate.instance +
                "' connects undeclared net '" + pin.second + "'");
      }
    }
  }
  return netlist;
}

// One LEF macro: its size in microns and its signal pins with
// directions. Parsed line by line: LEF is line-oriented enough
// for generated libraries, and a line parser fails on structure
// it does not recognize instead of guessing.
struct LefMacro {
  std::string name;
  double width_um = 0;
  double height_um = 0;
  // Pin name to true when the pin is an output.
  std::map<std::string, bool> pins;
};

struct LefData {
  std::map<std::string, LefMacro> macros;
  // First PORT layer seen in any macro pin, for DEF pin geometry.
  std::string pin_layer;
};

std::string TrimLine(const std::string& line) {
  const std::size_t first = line.find_first_not_of(" \t\r");
  if (first == std::string::npos) return "";
  const std::size_t last = line.find_last_not_of(" \t\r");
  return line.substr(first, last - first + 1);
}

std::vector<std::string> SplitLine(const std::string& line) {
  std::vector<std::string> words;
  std::stringstream stream(line);
  std::string word;
  while (stream >> word) words.push_back(word);
  return words;
}

bool ParseDoubleToken(const std::string& token, double* out) {
  try {
    std::size_t used = 0;
    *out = std::stod(token, &used);
    return used == token.size();
  } catch (const std::exception&) {
    return false;
  }
}

Result<LefData> ParseLefMacros(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return Status::Error(ErrorCode::kIoError,
                         "could not read file: " + path);
  }
  LefData data;
  std::string line;
  int line_no = 0;
  bool in_macro = false;
  bool in_pin = false;
  bool in_port = false;
  LefMacro macro;
  std::string pin_name;
  bool pin_is_output = false;
  bool pin_direction_seen = false;
  auto fail = [&](const std::string& message) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "LEF " + path + " line " +
                             std::to_string(line_no) + ": " + message);
  };
  while (std::getline(in, line)) {
    ++line_no;
    const std::size_t hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    const std::string trimmed = TrimLine(line);
    if (trimmed.empty()) continue;
    const std::vector<std::string> words = SplitLine(trimmed);
    if (words.empty()) continue;
    if (!in_macro) {
      if (words[0] == "MACRO") {
        if (words.size() < 2) return fail("MACRO has no name");
        in_macro = true;
        macro = LefMacro();
        macro.name = words[1];
      }
      continue;
    }
    if (!in_pin) {
      if (words[0] == "END" && words.size() >= 2 &&
          words[1] == macro.name) {
        if (macro.width_um <= 0 || macro.height_um <= 0) {
          return fail("macro '" + macro.name +
                      "' has no positive SIZE");
        }
        if (!data.macros.emplace(macro.name, macro).second) {
          return fail("duplicate macro '" + macro.name + "'");
        }
        in_macro = false;
        continue;
      }
      if (words[0] == "SIZE" && words.size() >= 4 &&
          words[2] == "BY") {
        if (!ParseDoubleToken(words[1], &macro.width_um) ||
            !ParseDoubleToken(words[3], &macro.height_um)) {
          return fail("macro '" + macro.name +
                      "' has a non-numeric SIZE");
        }
        continue;
      }
      if (words[0] == "PIN") {
        if (words.size() < 2) return fail("PIN has no name");
        in_pin = true;
        pin_name = words[1];
        pin_is_output = false;
        pin_direction_seen = false;
        in_port = false;
        continue;
      }
      continue;
    }
    if (words[0] == "END" && words.size() >= 2 &&
        words[1] == pin_name) {
      if (!pin_direction_seen) {
        return fail("pin '" + pin_name + "' in macro '" + macro.name +
                    "' has no DIRECTION");
      }
      macro.pins[pin_name] = pin_is_output;
      in_pin = false;
      in_port = false;
      continue;
    }
    if (words[0] == "DIRECTION" && words.size() >= 2) {
      if (words[1] == "INPUT") {
        pin_is_output = false;
        pin_direction_seen = true;
      } else if (words[1] == "OUTPUT") {
        pin_is_output = true;
        pin_direction_seen = true;
      } else if (words[1] == "INOUT") {
        return fail("pin '" + pin_name + "' in macro '" + macro.name +
                    "' is INOUT, the bridge takes input and output "
                    "pins only");
      }
      continue;
    }
    if (words[0] == "PORT") {
      in_port = true;
      continue;
    }
    if (in_port && words[0] == "LAYER" && words.size() >= 2) {
      if (data.pin_layer.empty()) data.pin_layer = words[1];
      continue;
    }
    if (in_port && words[0] == "END") {
      in_port = false;
      continue;
    }
  }
  if (in.bad()) {
    return Status::Error(ErrorCode::kIoError,
                         "could not read file: " + path);
  }
  if (in_macro) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "LEF " + path + ": unterminated macro '" +
                             macro.name + "'");
  }
  if (data.macros.empty()) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "LEF " + path + ": no macros");
  }
  return data;
}

// Database units per micron from a tech LEF's UNITS section.
Result<int> ParseLefDbu(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return Status::Error(ErrorCode::kIoError,
                         "could not read file: " + path);
  }
  std::string line;
  int line_no = 0;
  bool in_units = false;
  while (std::getline(in, line)) {
    ++line_no;
    const std::size_t hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    const std::vector<std::string> words =
        SplitLine(TrimLine(line));
    if (words.empty()) continue;
    if (words[0] == "UNITS") {
      in_units = true;
      continue;
    }
    if (in_units && words[0] == "END") {
      in_units = false;
      continue;
    }
    if (in_units && words[0] == "DATABASE" && words.size() >= 4 &&
        words[1] == "MICRONS") {
      int dbu = 0;
      const std::string& token = words[2];
      const char* first = token.data();
      const char* last = first + token.size();
      const auto parsed = std::from_chars(first, last, dbu);
      if (parsed.ec != std::errc() || parsed.ptr != last || dbu <= 0) {
        return Status::Error(
            ErrorCode::kInvalidArgument,
            "tech LEF " + path + " line " + std::to_string(line_no) +
                ": DATABASE MICRONS is not a positive integer");
      }
      return dbu;
    }
  }
  if (in.bad()) {
    return Status::Error(ErrorCode::kIoError,
                         "could not read file: " + path);
  }
  return Status::Error(ErrorCode::kInvalidArgument,
                       "tech LEF " + path +
                           ": no DATABASE MICRONS in UNITS");
}

// Writes the mapped netlist as DEF. The die area is a square at
// 60 percent utilization over the summed macro areas, snapped up
// to whole microns, unless the caller gives an explicit area.
// Instances are unplaced: floorplan and place own placement.
// Input pins sit on the left die edge, output pins on the right,
// spread evenly; each carries a small PORT rectangle on the
// LEF's first pin layer, because odb creates a placed BPin only
// from port geometry. Nets list the driver connection first,
// then the loads in gate order, so the file reads the same way
// every run.
struct DefWriteResult {
  std::array<int, 4> die_area = {0, 0, 0, 0};
  int nets = 0;
};

Result<DefWriteResult> WriteMappedDef(
    const std::string& path, const MappedNetlist& netlist,
    const LefData& lef, int dbu_per_micron,
    const std::optional<std::array<int, 4>>& die_area_override) {
  auto fail = [&](const std::string& message) {
    return Status::Error(ErrorCode::kInvalidArgument, message);
  };
  if (lef.pin_layer.empty()) {
    return fail("LEF has no pin PORT layer for DEF pin geometry");
  }
  // Semantic checks against the LEF: every gate cell exists, every
  // pin exists, each pin appears at most once per gate, and each
  // gate has exactly one output pin. The output pin of each gate
  // is its driver, which the net checks below use.
  std::map<std::string, std::string> net_driver;
  std::map<std::string, std::string> gate_output_net;
  std::map<std::string, std::string> gate_output_pin;
  for (const MappedGate& gate : netlist.gates) {
    const auto found = lef.macros.find(gate.cell);
    if (found == lef.macros.end()) {
      return fail("mapped cell '" + gate.cell + "' (instance '" +
                  gate.instance +
                  "') has no macro in the LEF: map with a Liberty "
                  "whose cells the LEF provides");
    }
    const LefMacro& macro = found->second;
    std::set<std::string> seen_pins;
    std::string output_net;
    std::string output_pin;
    int output_pins = 0;
    for (const auto& pin : gate.pins) {
      if (!seen_pins.insert(pin.first).second) {
        return fail("gate '" + gate.instance + "' connects pin '" +
                    pin.first + "' twice");
      }
      const auto pin_found = macro.pins.find(pin.first);
      if (pin_found == macro.pins.end()) {
        return fail("gate '" + gate.instance + "' of cell '" +
                    gate.cell + "' has no pin '" + pin.first +
                    "' in the LEF");
      }
      if (pin_found->second) {
        ++output_pins;
        output_net = pin.second;
        output_pin = pin.first;
      }
    }
    if (output_pins != 1) {
      return fail("gate '" + gate.instance + "' of cell '" +
                  gate.cell + "' has " + std::to_string(output_pins) +
                  " output pins, the bridge takes one");
    }
    gate_output_net[gate.instance] = output_net;
    gate_output_pin[gate.instance] = output_pin;
    const auto driver = net_driver.find(output_net);
    if (driver != net_driver.end()) {
      return fail("net '" + output_net + "' is driven by gates '" +
                  driver->second + "' and '" + gate.instance + "'");
    }
    net_driver[output_net] = gate.instance;
  }
  std::set<std::string> input_set(netlist.inputs.begin(),
                                  netlist.inputs.end());
  std::set<std::string> output_set(netlist.outputs.begin(),
                                   netlist.outputs.end());
  for (const std::string& port : netlist.outputs) {
    if (input_set.count(port) != 0) continue;
    if (net_driver.count(port) == 0) {
      return fail("output port '" + port +
                  "' is driven by no gate: a feedthrough output is "
                  "dropped by ABC's Verilog writer and has no DEF "
                  "pin here");
    }
  }
  for (const std::string& net : netlist.wires) {
    if (net_driver.count(net) == 0) {
      return fail("internal net '" + net +
                  "' is driven by no gate");
    }
  }
  double total_um2 = 0;
  for (const MappedGate& gate : netlist.gates) {
    const LefMacro& macro = lef.macros.at(gate.cell);
    total_um2 += macro.width_um * macro.height_um;
  }
  std::array<int, 4> die = {0, 0, 0, 0};
  if (die_area_override.has_value()) {
    die = *die_area_override;
    if (die[2] <= die[0] || die[3] <= die[1] || die[0] < 0 ||
        die[1] < 0) {
      return fail("-die_area must be a non-empty rectangle with "
                  "non-negative origin");
    }
  } else {
    const double side_um =
        std::ceil(std::sqrt(total_um2 / 0.6));
    const int side_dbu = static_cast<int>(side_um) * dbu_per_micron;
    die = {0, 0, side_dbu, side_dbu};
  }
  const int half_rect = std::max(1, dbu_per_micron / 20);
  std::ofstream out(path);
  if (!out) {
    return Status::Error(ErrorCode::kIoError,
                         "could not write file: " + path);
  }
  out << "VERSION 5.8 ;\n";
  out << "DIVIDERCHAR \"/\" ;\n";
  out << "BUSBITCHARS \"[]\" ;\n";
  out << "DESIGN " << netlist.top << " ;\n";
  out << "UNITS DISTANCE MICRONS " << dbu_per_micron << " ;\n";
  out << "DIEAREA ( " << die[0] << " " << die[1] << " ) ( " << die[2]
      << " " << die[3] << " ) ;\n";
  out << "\nCOMPONENTS " << netlist.gates.size() << " ;\n";
  for (const MappedGate& gate : netlist.gates) {
    out << "- " << gate.instance << " " << gate.cell << " ;\n";
  }
  out << "END COMPONENTS\n";
  const auto pin_y = [&](std::size_t index, std::size_t count) {
    return static_cast<int>((static_cast<long long>(index + 1) *
                             (die[3] - die[1])) /
                            static_cast<long long>(count + 1)) +
           die[1];
  };
  out << "\nPINS "
      << (netlist.inputs.size() + netlist.outputs.size()) << " ;\n";
  for (std::size_t i = 0; i < netlist.inputs.size(); ++i) {
    const std::string& name = netlist.inputs[i];
    out << "- " << name << " + NET " << name
        << " + DIRECTION INPUT + USE SIGNAL\n";
    out << "  + PORT\n";
    out << "  + LAYER " << lef.pin_layer << " ( " << -half_rect << " "
        << -half_rect << " ) ( " << half_rect << " " << half_rect
        << " )\n";
    out << "  + PLACED ( " << die[0] << " "
        << pin_y(i, netlist.inputs.size()) << " ) N ;\n";
  }
  for (std::size_t i = 0; i < netlist.outputs.size(); ++i) {
    const std::string& name = netlist.outputs[i];
    out << "- " << name << " + NET " << name
        << " + DIRECTION OUTPUT + USE SIGNAL\n";
    out << "  + PORT\n";
    out << "  + LAYER " << lef.pin_layer << " ( " << -half_rect << " "
        << -half_rect << " ) ( " << half_rect << " " << half_rect
        << " )\n";
    out << "  + PLACED ( " << die[2] << " "
        << pin_y(i, netlist.outputs.size()) << " ) N ;\n";
  }
  out << "END PINS\n";
  // Nets in first-use order: inputs, then internal nets in
  // wire-declaration order, then outputs. Each net lists its
  // driver connection first, then loads in gate order.
  std::vector<std::string> nets;
  nets.insert(nets.end(), netlist.inputs.begin(),
              netlist.inputs.end());
  nets.insert(nets.end(), netlist.wires.begin(), netlist.wires.end());
  nets.insert(nets.end(), netlist.outputs.begin(),
              netlist.outputs.end());
  std::map<std::string, std::vector<std::string>> loads;
  for (const MappedGate& gate : netlist.gates) {
    for (const auto& pin : gate.pins) {
      if (gate_output_net[gate.instance] == pin.second) continue;
      loads[pin.second].push_back("( " + gate.instance + " " +
                                  pin.first + " )");
    }
  }
  out << "\nNETS " << nets.size() << " ;\n";
  for (const std::string& net : nets) {
    out << "- " << net;
    if (input_set.count(net) != 0) {
      out << " ( PIN " << net << " )";
    } else {
      const std::string& driver = net_driver[net];
      out << " ( " << driver << " " << gate_output_pin[driver]
          << " )";
    }
    for (const std::string& load : loads[net]) {
      out << " " << load;
    }
    if (output_set.count(net) != 0) {
      out << " ( PIN " << net << " )";
    }
    out << " + USE SIGNAL ;\n";
  }
  out << "END NETS\n";
  out << "\nEND DESIGN\n";
  out.flush();
  if (!out) {
    return Status::Error(ErrorCode::kIoError,
                         "could not write file: " + path);
  }
  DefWriteResult result;
  result.die_area = die;
  result.nets = static_cast<int>(nets.size());
  return result;
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
// value, never omitted. Each net line also carries width=<n>,
// the wire's bit width, so the schematic can label a bus.
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
           " terminals=" + terminals +
           " width=" + std::to_string(net.width) + "\n";
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

// Renders the elaboration issues in the same key=value line
// style as the other GUI payloads, so the GUI model parses it
// with the same rules. The first line names what was checked,
// so the empty state can state it; the second line names the
// netlist the issues describe (elaborated or generic, from
// whether synth_generic has run), because after synth_generic
// the live design is the generic netlist, not the elaborated
// one. Each issue is one line in a fixed field order with the
// message last, so the message runs to end of line and may
// contain spaces: "issue severity=<s> code=<c> module=<m>
// path=<p> target=<t> file=<f> line=<n> message=<free text>".
// An empty target or file renders as an empty value, never
// omitted; a line of 0 means the design records no source
// location for the issue. The issues arrive severity ordered
// from the facade, and the renderer preserves that order.
std::string IssuesText(
    const std::vector<synth::YosysEngine::YosysIssue>& issues,
    const std::string& netlist) {
  std::string out =
      "checked=hierarchy resolution, multiple drivers, undriven "
      "nets, combinational loops, inferred latches, unknown cell "
      "types, unconnected ports, top ports, empty modules\n";
  out += "netlist=" + netlist + "\n";
  for (const auto& issue : issues) {
    out += "issue severity=" + issue.severity + " code=" + issue.code +
           " module=" + issue.module + " path=" + issue.path +
           " target=" + issue.target + " file=" + issue.file +
           " line=" + std::to_string(issue.line) +
           " message=" + issue.message + "\n";
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

// ---- SDC command family (Immediate unit 3) -------------------------
// Shared parsing for the SDC commands. Flags are exact tokens; a
// value is the next token unless it exactly matches a known flag,
// so negative numbers like -0.5 parse as values while a missing
// value before the next flag is an error. Unknown dash-led tokens
// are reported, never ignored, matching the core discipline above.

bool ParseDouble(const std::string& token, double& out) {
  const char* first = token.data();
  const char* last = first + token.size();
  const auto result = std::from_chars(first, last, out);
  return result.ec == std::errc() && result.ptr == last &&
         std::isfinite(out);
}

bool ParseInt(const std::string& token, long& out) {
  const char* first = token.data();
  const char* last = first + token.size();
  const auto result = std::from_chars(first, last, out);
  return result.ec == std::errc() && result.ptr == last;
}

// Formats a double for storage and display: std::to_string's six
// decimals with trailing zeros trimmed, so 10.5 stores as "10.5"
// and 0.853 as "0.853". Values with at most six decimals round-trip
// through ParseDouble exactly, which covers SDC magnitudes.
std::string FormatNum(double value) {
  std::string text = std::to_string(value);
  while (!text.empty() && text.back() == '0') text.pop_back();
  if (!text.empty() && text.back() == '.') text.pop_back();
  return text;
}

struct SdcFlagSpec {
  std::string name;
  int values;       // 0 = boolean, 1 = single value, 2 = pair
  bool repeatable;  // list flags accumulate across occurrences
};

class SdcOptions {
 public:
  // Parses args against specs. On success returns nullopt and the
  // parsed form is readable through Has/Value/Values/Positionals.
  // On failure returns the diagnostic to fail with.
  static std::optional<CommandResult> Parse(
      const std::vector<std::string>& args,
      const std::vector<SdcFlagSpec>& specs, const std::string& cmd,
      SdcOptions& out) {
    std::map<std::string, const SdcFlagSpec*> by_name;
    for (const SdcFlagSpec& spec : specs) by_name[spec.name] = &spec;
    const auto fail = [&cmd](const std::string& message) {
      return CommandResult::Fail(Diag::kInvalidConfiguration,
                                 cmd + ": " + message);
    };
    std::size_t i = 0;
    while (i < args.size()) {
      const std::string& tok = args[i];
      auto it = by_name.find(tok);
      if (it == by_name.end()) {
        if (!tok.empty() && tok[0] == '-') {
          return fail("unknown option: " + tok);
        }
        out.positionals_.push_back(tok);
        ++i;
        continue;
      }
      const SdcFlagSpec* spec = it->second;
      if (!spec->repeatable && out.seen_.count(spec->name) != 0) {
        return fail("option " + spec->name + " given twice");
      }
      out.seen_.insert(spec->name);
      if (spec->values == 0) {
        out.flags_.insert(spec->name);
        ++i;
        continue;
      }
      for (int k = 0; k < spec->values; ++k) {
        if (i + 1 + k >= args.size() ||
            by_name.count(args[i + 1 + k]) != 0) {
          return fail("option " + spec->name + " needs " +
                      std::to_string(spec->values) + " value(s)");
        }
        out.values_[spec->name].push_back(args[i + 1 + k]);
      }
      i += 1 + static_cast<std::size_t>(spec->values);
    }
    return std::nullopt;
  }

  bool Has(const std::string& flag) const {
    return flags_.count(flag) != 0;
  }
  bool Seen(const std::string& flag) const {
    return seen_.count(flag) != 0;
  }
  std::optional<std::string> Value(const std::string& flag) const {
    auto it = values_.find(flag);
    if (it == values_.end() || it->second.empty()) return std::nullopt;
    return it->second.front();
  }
  std::vector<std::string> Values(const std::string& flag) const {
    auto it = values_.find(flag);
    if (it == values_.end()) return {};
    return it->second;
  }
  const std::vector<std::string>& Positionals() const {
    return positionals_;
  }

 private:
  std::set<std::string> seen_;
  std::set<std::string> flags_;
  std::map<std::string, std::vector<std::string>> values_;
  std::vector<std::string> positionals_;
};

// Parses one option value as a double, failing with a message that
// names the option.
std::optional<CommandResult> ParseOptDouble(
    const SdcOptions& opts, const std::string& flag, const std::string& cmd,
    double& out) {
  const std::optional<std::string> text = opts.Value(flag);
  if (!text.has_value()) return std::nullopt;
  if (!ParseDouble(*text, out)) {
    return CommandResult::Fail(
        Diag::kInvalidConfiguration,
        cmd + ": option " + flag + " needs a number, got: " + *text);
  }
  return std::nullopt;
}

// Trims surrounding whitespace.
std::string TrimStr(const std::string& s) {
  const std::size_t first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const std::size_t last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

// Normalizes one target address to the object name the constraint
// store resolves: a bare name, a pin path "<instance>/<pin>", or a
// single-object get_ports/get_pins/get_nets/get_cells wrapper passed
// as one quoted argument. A wrapper naming several objects fails:
// one stored target per wrapper, so each binding stays honest.
std::optional<CommandResult> NormalizeTarget(const std::string& raw,
                                             const std::string& cmd,
                                             std::string& out) {
  std::string s = TrimStr(raw);
  if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
    s = TrimStr(s.substr(1, s.size() - 2));
  }
  static const char* kGets[] = {"get_ports ", "get_pins ", "get_nets ",
                                "get_cells "};
  for (const char* get : kGets) {
    const std::string prefix(get);
    if (s.rfind(prefix, 0) == 0) {
      s = TrimStr(s.substr(prefix.size()));
      break;
    }
  }
  if (s.empty()) {
    return CommandResult::Fail(Diag::kInvalidConfiguration,
                               cmd + ": empty target");
  }
  if (s.find_first_of(" \t{}") != std::string::npos) {
    return CommandResult::Fail(
        Diag::kInvalidConfiguration,
        cmd + ": one object per target; a list names several objects, "
              "pass them as separate arguments");
  }
  out = s;
  return std::nullopt;
}

// Normalizes every positional into target names, failing on the
// first bad one.
std::optional<CommandResult> NormalizeTargets(
    const std::vector<std::string>& raw, const std::string& cmd,
    std::vector<std::string>& out) {
  for (const std::string& token : raw) {
    std::string target;
    if (auto bad = NormalizeTarget(token, cmd, target)) return bad;
    out.push_back(target);
  }
  return std::nullopt;
}

// Stores one constraint per target, all sharing kind, names, and
// params, and renders one payload line per constraint in the
// established "constraint=<id> status=<word> target=<target>"
// shape, with bound objects and stored params appended.
std::string StoreTargets(
    CommandContext& ctx, db::ConstraintKind kind,
    const std::vector<std::string>& targets,
    const std::vector<std::string>& names,
    const std::vector<std::pair<std::string, std::string>>& params) {
  std::string payload;
  for (std::size_t i = 0; i < targets.size(); ++i) {
    const db::ConstraintId id = ctx.project.constraints.Add(
        kind, names[i], targets[i], *ctx.project.design, params);
    const db::Constraint* c = ctx.project.constraints.Find(id);
    if (!payload.empty()) payload += "\n";
    payload += "constraint=" + std::to_string(id) +
               " status=" + StatusWord(c->status) +
               " target=" + targets[i];
    if (c->status == db::BindingStatus::kBound) {
      payload += " object=" + IdStr(c->bound_object);
    }
    if (!params.empty()) {
      payload += " params=";
      for (std::size_t k = 0; k < params.size(); ++k) {
        if (k != 0) payload += ";";
        payload += params[k].first + "=" + params[k].second;
      }
    }
  }
  return payload;
}

// Drops every stored constraint of one kind carrying one name.
// Used by the clock commands' replace-by-default semantics and by
// group_path, where the name is the group's identity.
void ReplaceNamed(CommandContext& ctx, db::ConstraintKind kind,
                  const std::string& name) {
  std::vector<db::ConstraintId> dead;
  for (const db::Constraint& c : ctx.project.constraints.all()) {
    if (c.kind == kind && c.name == name) dead.push_back(c.id);
  }
  for (db::ConstraintId id : dead) ctx.project.constraints.Remove(id);
}

// ---- write_sdc (Immediate unit 8, review item I5) --------------------
// The writer emits the session constraint set as one SDC file, the
// single-scenario form: one file, the active scenario's view. It is
// a FROM-SCRATCH writer over ctx.project.constraints, not an exposure
// of the engine's WriteSdc.cc: the engine writer runs on file-origin
// constraints and drops positive set_clock_gating_check rows and the
// ideal commands (docs/read-sdc-audit.md section 8), while this one
// emits every row the session can express. File-origin constraints
// that were never authored in the session stay in their source file;
// that boundary is stated in docs/write-sdc.md, not hidden.
//   - One SDC command per stored row, in constraint-id order,
//     except clocks and group_path, which merge by name (upstream
//     replace semantics: a second create_clock or group_path with
//     the same name replaces the first unless -add is given).
//   - Targets render as explicit get_* collections for their bound
//     kind, so no wildcard or -of_objects form survives into the
//     file. A row whose target cannot be rendered is skipped and
//     reported, never guessed.
//   - No timestamp is written, so repeated writes are
//     byte-identical (repeatable-reports discipline, unit 1).

// One bound target rendered as an explicit SDC object expression.
struct WriteSdcTarget {
  std::string expr;  // e.g. [get_ports clk]
  std::string name;  // the bare object name
  ObjectKind kind = ObjectKind::kNone;
};

// A name that cannot be addressed unambiguously in SDC: glob
// characters stay active inside a get_* pattern even when quoted,
// and list-structure characters would split the argument. Such a
// row is skipped, never guessed.
bool WriteSdcNameIsAddressable(const std::string& name) {
  return name.find_first_of("*?[]{}$;\"\\ \t") == std::string::npos;
}

// Resolves one stored name to its design object, in the store's
// resolution order (ports, then nets, then instances, then pin
// paths), and renders it as an explicit get_* expression. Returns
// nullopt when the name resolves to nothing or cannot be addressed.
std::optional<WriteSdcTarget> WriteSdcResolveName(
    const db::Design& design, const std::string& name) {
  if (!WriteSdcNameIsAddressable(name)) return std::nullopt;
  WriteSdcTarget out;
  out.name = name;
  ObjectId id = design.Find(ObjectKind::kPort, name);
  if (!id.is_null()) {
    out.kind = ObjectKind::kPort;
    out.expr = "[get_ports " + name + "]";
    return out;
  }
  id = design.Find(ObjectKind::kNet, name);
  if (!id.is_null()) {
    out.kind = ObjectKind::kNet;
    out.expr = "[get_nets " + name + "]";
    return out;
  }
  id = design.Find(ObjectKind::kInstance, name);
  if (!id.is_null()) {
    out.kind = ObjectKind::kInstance;
    out.expr = "[get_cells " + name + "]";
    return out;
  }
  id = design.Find(ObjectKind::kPin, name);
  if (!id.is_null()) {
    out.kind = ObjectKind::kPin;
    out.expr = "[get_pins " + name + "]";
    return out;
  }
  return std::nullopt;
}

// Renders one stored row's target from its bound object id, so a
// name shared by two kinds still addresses the object the row bound
// to. Returns nullopt with the reason in `why` when the row cannot
// be emitted as an unambiguous object.
std::optional<WriteSdcTarget> WriteSdcTargetOf(const db::Design& design,
                                               const db::Constraint& c,
                                               std::string& why) {
  if (c.status == db::BindingStatus::kEmpty) {
    why = "empty target";
    return std::nullopt;
  }
  if (c.status != db::BindingStatus::kBound) {
    why = "unresolved target";
    return std::nullopt;
  }
  const ObjectId id = c.bound_object;
  WriteSdcTarget out;
  if (const db::PortRecord* port = design.FindPort(id)) {
    out.kind = ObjectKind::kPort;
    out.name = port->name;
    out.expr = "[get_ports " + port->name + "]";
  } else if (const db::PinRecord* pin = design.FindPin(id)) {
    out.kind = ObjectKind::kPin;
    out.name = pin->name;
    out.expr = "[get_pins " + pin->name + "]";
  } else if (const db::NetRecord* net = design.FindNet(id)) {
    out.kind = ObjectKind::kNet;
    out.name = net->name;
    out.expr = "[get_nets " + net->name + "]";
  } else if (const db::InstanceRecord* inst = design.FindInstance(id)) {
    out.kind = ObjectKind::kInstance;
    out.name = inst->name;
    out.expr = "[get_cells " + inst->name + "]";
  } else {
    why = "bound object has no readable kind";
    return std::nullopt;
  }
  if (!WriteSdcNameIsAddressable(out.name)) {
    why = "name cannot be addressed unambiguously";
    return std::nullopt;
  }
  return out;
}

// Renders several targets as one SDC object argument. One object:
// its collection. Several of one kind: one get_* call over a braced
// name list. Several of mixed kinds: a bare braced name list, which
// the upstream resolvers expand per name (a repeated option key
// overwrites upstream, so a flag per kind is not expressible).
std::string WriteSdcObjectList(
    const std::vector<WriteSdcTarget>& targets) {
  if (targets.empty()) return "";
  if (targets.size() == 1) return targets[0].expr;
  bool same_kind = true;
  for (std::size_t i = 1; i < targets.size(); ++i) {
    if (targets[i].kind != targets[0].kind) same_kind = false;
  }
  if (same_kind) {
    const char* getter = targets[0].kind == ObjectKind::kPort
                             ? "get_ports"
                             : (targets[0].kind == ObjectKind::kPin
                                    ? "get_pins"
                                    : (targets[0].kind == ObjectKind::kNet
                                           ? "get_nets"
                                           : "get_cells"));
    std::string out = "[" + std::string(getter) + " {";
    for (std::size_t i = 0; i < targets.size(); ++i) {
      if (i != 0) out += " ";
      out += targets[i].name;
    }
    out += "}]";
    return out;
  }
  std::string out = "{";
  for (std::size_t i = 0; i < targets.size(); ++i) {
    if (i != 0) out += " ";
    out += targets[i].name;
  }
  out += "}";
  return out;
}

// Splits a space-joined parameter list (group_path -from/-through,
// set_sense -clock) back into names.
std::vector<std::string> WriteSdcSplitList(const std::string& joined) {
  std::vector<std::string> out;
  std::istringstream in(joined);
  std::string item;
  while (in >> item) out.push_back(item);
  return out;
}

// The parameter signature: rows group together for merging only
// when kind, name, and the full ordered parameter list match.
std::string WriteSdcParamsSig(const db::Constraint& c) {
  std::string sig;
  for (const auto& pair : c.params) {
    if (!sig.empty()) sig += ";";
    sig += pair.first + "=" + pair.second;
  }
  return sig;
}

// One emitted SDC line, tagged with the first constraint id it
// carries so lines sort back into constraint order after the
// per-kind passes.
struct WriteSdcLine {
  std::size_t first_id = 0;
  std::string text;
};

// One skipped row, with the reason named.
struct WriteSdcSkip {
  std::size_t id = 0;
  std::string kind;
  std::string name;
  std::string target;
  std::string reason;
};

struct WriteSdcPlan {
  std::vector<WriteSdcLine> lines;
  std::vector<WriteSdcSkip> skipped;
  // Rows carried into the file (total rows minus skipped rows).
  std::size_t emitted = 0;
};

// One clock group: rows sharing kind, name, and parameters, merged
// into one create_clock / create_generated_clock line.
struct WriteSdcClockGroup {
  db::ConstraintKind kind = db::ConstraintKind::kClock;
  std::string name;
  std::vector<std::pair<std::string, std::string>> params;
  std::optional<WriteSdcTarget> source;
  std::vector<WriteSdcTarget> targets;
  std::size_t first_id = 0;
  bool add = false;
};

// One group_path group: rows sharing the group name, merged into
// one group_path line. The from/through lists and the endpoint role
// come from the first row's parameters.
struct WriteSdcPathGroup {
  std::vector<std::pair<std::string, std::string>> params;
  std::vector<WriteSdcTarget> targets;
  std::size_t first_id = 0;
};

// Renders one clock group as a single SDC line.
std::string WriteSdcClockLine(const WriteSdcClockGroup& group) {
  const auto param = [&group](const std::string& key) {
    for (const auto& pair : group.params) {
      if (pair.first == key) return pair.second;
    }
    return std::string();
  };
  std::string line;
  if (group.kind == db::ConstraintKind::kClock) {
    line = "create_clock -period " + param("period_ns");
    if (!param("waveform_rise_ns").empty() &&
        !param("waveform_fall_ns").empty()) {
      line += " -waveform {" + param("waveform_rise_ns") + " " +
              param("waveform_fall_ns") + "}";
    }
  } else {
    line = "create_generated_clock -source " + group.source->expr;
    if (!param("master_clock").empty()) {
      line += " -master_clock " + param("master_clock");
    }
    if (!param("divide_by").empty()) {
      line += " -divide_by " + param("divide_by");
    }
    if (!param("multiply_by").empty()) {
      line += " -multiply_by " + param("multiply_by");
    }
    if (param("invert") == "1") line += " -invert";
    if (param("combinational") == "1") line += " -combinational";
  }
  const bool need_name =
      group.add || group.targets.size() > 1 ||
      group.name != group.targets[0].name;
  if (need_name) line += " -name " + group.name;
  if (group.add) line += " -add";
  line += " " + WriteSdcObjectList(group.targets);
  return line;
}

// Plans the file: which rows emit, which lines they form, and which
// rows skip with what reason. The design may be null (no design
// open): then every row skips as "no design is open".
WriteSdcPlan WriteSdcPlanFor(const db::Design* design,
                             const db::ConstraintSet& constraints) {
  WriteSdcPlan plan;
  const std::vector<db::Constraint>& rows = constraints.all();
  auto skip = [&plan](const db::Constraint& c, const std::string& reason) {
    plan.skipped.push_back(WriteSdcSkip{
        c.id, ConstraintKindWord(c.kind), c.name, c.target_spec, reason});
  };
  if (design == nullptr) {
    for (const db::Constraint& c : rows) {
      skip(c, "no design is open");
    }
    return plan;
  }
  // Clocks and generated clocks merge by (kind, name, parameters).
  std::map<std::string, WriteSdcClockGroup> clock_groups;
  std::set<std::string> clock_names_seen;
  // group_path merges by name.
  std::map<std::string, WriteSdcPathGroup> path_groups;
  for (const db::Constraint& c : rows) {
    if (c.kind != db::ConstraintKind::kClock &&
        c.kind != db::ConstraintKind::kGeneratedClock &&
        c.kind != db::ConstraintKind::kGroupPath) {
      continue;
    }
    if (c.kind == db::ConstraintKind::kClock &&
        !c.Param("period_ns").has_value()) {
      skip(c, "construction record, not an SDC clock");
      continue;
    }
    std::string why;
    std::optional<WriteSdcTarget> target =
        WriteSdcTargetOf(*design, c, why);
    if (!target.has_value()) {
      skip(c, why);
      continue;
    }
    if (c.kind == db::ConstraintKind::kGroupPath) {
      WriteSdcPathGroup& group = path_groups[c.name];
      if (group.targets.empty()) {
        group.params = c.params;
        group.first_id = c.id;
      }
      group.targets.push_back(*target);
      continue;
    }
    // Clock targets are ports or pins only; upstream create_clock
    // takes pins and ports, never a net or a cell.
    if (target->kind != ObjectKind::kPort &&
        target->kind != ObjectKind::kPin) {
      skip(c, "clock target is not a port or pin");
      continue;
    }
    std::optional<WriteSdcTarget> source;
    if (c.kind == db::ConstraintKind::kGeneratedClock) {
      const std::optional<std::string> src = c.Param("source");
      if (!src.has_value()) {
        skip(c, "generated clock has no source");
        continue;
      }
      source = WriteSdcResolveName(*design, *src);
      if (!source.has_value()) {
        skip(c, "unresolvable source");
        continue;
      }
      if (source->kind != ObjectKind::kPort &&
          source->kind != ObjectKind::kPin) {
        skip(c, "source is not a pin or port");
        continue;
      }
    }
    const std::string key = std::string(ConstraintKindWord(c.kind)) + "|" +
                            c.name + "|" + WriteSdcParamsSig(c);
    auto it = clock_groups.find(key);
    if (it == clock_groups.end()) {
      WriteSdcClockGroup group;
      group.kind = c.kind;
      group.name = c.name;
      group.params = c.params;
      group.source = source;
      group.first_id = c.id;
      group.add = clock_names_seen.count(c.name) != 0;
      clock_names_seen.insert(c.name);
      group.targets.push_back(*target);
      clock_groups.emplace(key, std::move(group));
    } else {
      it->second.targets.push_back(*target);
    }
  }
  // Render the clock groups in first-row order.
  std::vector<const WriteSdcClockGroup*> ordered_clocks;
  for (const auto& pair : clock_groups) {
    ordered_clocks.push_back(&pair.second);
  }
  std::sort(ordered_clocks.begin(), ordered_clocks.end(),
            [](const WriteSdcClockGroup* a, const WriteSdcClockGroup* b) {
              return a->first_id < b->first_id;
            });
  for (const WriteSdcClockGroup* group : ordered_clocks) {
    plan.lines.push_back(
        WriteSdcLine{group->first_id, WriteSdcClockLine(*group)});
  }
  // Render the group_path groups in first-row order.
  std::vector<const WriteSdcPathGroup*> ordered_paths;
  for (const auto& pair : path_groups) {
    ordered_paths.push_back(&pair.second);
  }
  std::sort(ordered_paths.begin(), ordered_paths.end(),
            [](const WriteSdcPathGroup* a, const WriteSdcPathGroup* b) {
              return a->first_id < b->first_id;
            });
  for (const WriteSdcPathGroup* group : ordered_paths) {
    const auto param = [&group](const std::string& key) {
      for (const auto& pair : group->params) {
        if (pair.first == key) return pair.second;
      }
      return std::string();
    };
    // The endpoint role: stored since unit 8; older rows derive it
    // from the lists (targets equal the from list, the through list,
    // or neither, in which case they are the -to list).
    std::string role = param("endpoint");
    if (role.empty()) {
      const std::vector<std::string> from_names =
          WriteSdcSplitList(param("from"));
      const std::vector<std::string> through_names =
          WriteSdcSplitList(param("through"));
      const std::set<std::string> target_names = [&group] {
        std::set<std::string> names;
        for (const WriteSdcTarget& t : group->targets) {
          names.insert(t.name);
        }
        return names;
      }();
      const auto same_set = [](const std::vector<std::string>& list,
                               const std::set<std::string>& set) {
        if (list.size() != set.size()) return false;
        for (const std::string& item : list) {
          if (set.count(item) == 0) return false;
        }
        return true;
      };
      role = same_set(from_names, target_names)
                 ? "from"
                 : (same_set(through_names, target_names) ? "through"
                                                          : "to");
    }
    // Resolve the recorded lists; a name that no longer resolves
    // skips the whole group rather than emitting a line the reader
    // would fail on.
    const auto resolve_list =
        [&design](const std::string& joined,
                  std::vector<WriteSdcTarget>& out, std::string& bad) {
          for (const std::string& name : WriteSdcSplitList(joined)) {
            std::optional<WriteSdcTarget> resolved =
                WriteSdcResolveName(*design, name);
            if (!resolved.has_value()) {
              bad = name;
              return false;
            }
            out.push_back(*resolved);
          }
          return true;
        };
    std::vector<WriteSdcTarget> from_targets;
    std::vector<WriteSdcTarget> through_targets;
    std::string bad;
    bool ok = true;
    if (role != "from" && !param("from").empty()) {
      ok = resolve_list(param("from"), from_targets, bad);
    }
    if (ok && role != "through" && !param("through").empty()) {
      ok = resolve_list(param("through"), through_targets, bad);
    }
    if (!ok) {
      for (const db::Constraint& c : rows) {
        if (c.kind == db::ConstraintKind::kGroupPath &&
            c.name == param("group")) {
          skip(c, "unresolvable object " + bad);
        }
      }
      continue;
    }
    std::string line = "group_path -name " + param("group");
    if (role == "from") {
      line += " -from " + WriteSdcObjectList(group->targets);
    } else if (!from_targets.empty()) {
      line += " -from " + WriteSdcObjectList(from_targets);
    }
    if (role == "through") {
      line += " -through " + WriteSdcObjectList(group->targets);
    } else if (!through_targets.empty()) {
      line += " -through " + WriteSdcObjectList(through_targets);
    }
    if (role == "to") {
      line += " -to " + WriteSdcObjectList(group->targets);
    }
    if (!param("weight").empty()) {
      line += " -weight " + param("weight");
    }
    if (!param("critical_range").empty()) {
      line += " -critical_range " + param("critical_range");
    }
    plan.lines.push_back(WriteSdcLine{group->first_id, line});
  }
  // Every other emittable kind: one line per row, in id order.
  for (const db::Constraint& c : rows) {
    const auto param = [&c](const std::string& key) {
      return c.Param(key);
    };
    switch (c.kind) {
      case db::ConstraintKind::kClock:
      case db::ConstraintKind::kGeneratedClock:
      case db::ConstraintKind::kGroupPath:
        break;
      case db::ConstraintKind::kClockGatingCheck: {
        if (!param("setup_ns").has_value() &&
            !param("hold_ns").has_value()) {
          skip(c, "no stored parameters");
          break;
        }
        std::string why;
        std::optional<WriteSdcTarget> target =
            WriteSdcTargetOf(*design, c, why);
        if (!target.has_value()) {
          skip(c, why);
          break;
        }
        std::string line = "set_clock_gating_check";
        if (param("setup_ns").has_value()) {
          line += " -setup " + *param("setup_ns");
        }
        if (param("hold_ns").has_value()) {
          line += " -hold " + *param("hold_ns");
        }
        const std::string edge = param("edge").value_or("both");
        if (edge == "high") {
          line += " -high";
        } else if (edge == "low") {
          line += " -low";
        }
        line += " " + target->expr;
        plan.lines.push_back(WriteSdcLine{c.id, line});
        break;
      }
      case db::ConstraintKind::kDataCheck: {
        if (!param("from").has_value() ||
            (!param("setup_ns").has_value() &&
             !param("hold_ns").has_value())) {
          skip(c, "no stored parameters");
          break;
        }
        std::string why;
        std::optional<WriteSdcTarget> target =
            WriteSdcTargetOf(*design, c, why);
        if (!target.has_value()) {
          skip(c, why);
          break;
        }
        std::optional<WriteSdcTarget> from =
            WriteSdcResolveName(*design, *param("from"));
        if (!from.has_value()) {
          skip(c, "unresolvable from");
          break;
        }
        // Upstream takes one margin with -setup/-hold as flags, so
        // a row with both margins at different values emits two
        // lines; equal values emit one line with both flags.
        const bool has_setup = param("setup_ns").has_value();
        const bool has_hold = param("hold_ns").has_value();
        const bool split = has_setup && has_hold &&
                           *param("setup_ns") != *param("hold_ns");
        const auto emit = [&](bool setup_flag, bool hold_flag,
                              const std::string& margin) {
          std::string line = "set_data_check -from " + from->expr +
                             " -to " + target->expr;
          if (setup_flag) line += " -setup";
          if (hold_flag) line += " -hold";
          line += " " + margin;
          plan.lines.push_back(WriteSdcLine{c.id, line});
        };
        if (split) {
          emit(true, false, *param("setup_ns"));
          emit(false, true, *param("hold_ns"));
        } else if (has_setup) {
          emit(true, has_hold, *param("setup_ns"));
        } else {
          emit(false, true, *param("hold_ns"));
        }
        break;
      }
      case db::ConstraintKind::kMinPulseWidth: {
        if (!param("value_ns").has_value()) {
          skip(c, "no stored parameters");
          break;
        }
        std::string why;
        std::optional<WriteSdcTarget> target =
            WriteSdcTargetOf(*design, c, why);
        if (!target.has_value()) {
          skip(c, why);
          break;
        }
        std::string line = "set_min_pulse_width";
        const std::string edge = param("edge").value_or("both");
        if (edge == "high") {
          line += " -high";
        } else if (edge == "low") {
          line += " -low";
        }
        line += " " + *param("value_ns") + " " + target->expr;
        plan.lines.push_back(WriteSdcLine{c.id, line});
        break;
      }
      case db::ConstraintKind::kSense: {
        if (!param("sense").has_value()) {
          skip(c, "no stored parameters");
          break;
        }
        std::string why;
        std::optional<WriteSdcTarget> target =
            WriteSdcTargetOf(*design, c, why);
        if (!target.has_value()) {
          skip(c, why);
          break;
        }
        // Clock names are recorded, not bound: they must name a
        // clock row in this set, or the reader would warn and apply
        // the sense to no clock.
        std::vector<std::string> clocks;
        if (param("sense_clocks").has_value()) {
          clocks = WriteSdcSplitList(*param("sense_clocks"));
        }
        bool unknown_clock = false;
        for (const std::string& clock : clocks) {
          bool found = false;
          for (const db::Constraint& other : rows) {
            if ((other.kind == db::ConstraintKind::kClock ||
                 other.kind == db::ConstraintKind::kGeneratedClock) &&
                other.name == clock) {
              found = true;
              break;
            }
          }
          if (!found) {
            skip(c, "unknown clock " + clock);
            unknown_clock = true;
            break;
          }
        }
        if (unknown_clock) break;
        std::string line = "set_sense -type clock";
        const std::string sense = *param("sense");
        if (sense == "positive") {
          line += " -positive";
        } else if (sense == "negative") {
          line += " -negative";
        } else {
          line += " -stop_propagation";
        }
        if (!clocks.empty()) {
          line += " -clock {";
          for (std::size_t i = 0; i < clocks.size(); ++i) {
            if (i != 0) line += " ";
            line += clocks[i];
          }
          line += "}";
        }
        line += " " + target->expr;
        plan.lines.push_back(WriteSdcLine{c.id, line});
        break;
      }
      case db::ConstraintKind::kIdealNetwork: {
        std::string why;
        std::optional<WriteSdcTarget> target =
            WriteSdcTargetOf(*design, c, why);
        if (!target.has_value()) {
          skip(c, why);
          break;
        }
        std::string line = "set_ideal_network";
        if (param("no_propagate").value_or("") == "1") {
          line += " -no_propagate";
        }
        line += " " + target->expr;
        plan.lines.push_back(WriteSdcLine{c.id, line});
        break;
      }
      case db::ConstraintKind::kIdealLatency:
      case db::ConstraintKind::kIdealTransition: {
        if (!param("value_ns").has_value()) {
          skip(c, "no stored parameters");
          break;
        }
        std::string why;
        std::optional<WriteSdcTarget> target =
            WriteSdcTargetOf(*design, c, why);
        if (!target.has_value()) {
          skip(c, why);
          break;
        }
        std::string line =
            c.kind == db::ConstraintKind::kIdealLatency
                ? "set_ideal_latency"
                : "set_ideal_transition";
        const std::string edge = param("edge").value_or("both");
        if (edge == "rise") {
          line += " -rise";
        } else if (edge == "fall") {
          line += " -fall";
        }
        const std::string delay = param("delay").value_or("both");
        if (delay == "min") {
          line += " -min";
        } else if (delay == "max") {
          line += " -max";
        }
        line += " " + *param("value_ns") + " " + target->expr;
        plan.lines.push_back(WriteSdcLine{c.id, line});
        break;
      }
      case db::ConstraintKind::kMaxTimeBorrow: {
        if (!param("value_ns").has_value()) {
          skip(c, "no stored parameters");
          break;
        }
        std::string why;
        std::optional<WriteSdcTarget> target =
            WriteSdcTargetOf(*design, c, why);
        if (!target.has_value()) {
          skip(c, why);
          break;
        }
        plan.lines.push_back(WriteSdcLine{
            c.id, "set_max_time_borrow " + *param("value_ns") + " " +
                      target->expr});
        break;
      }
      default:
        // Kinds no command stores parameters for (input/output
        // delay, false path, multicycle, max/min delay, case
        // analysis): the row carries no semantics to emit, and
        // fabricating a command from a name and a target would be
        // a lie.
        skip(c, "no stored parameters");
        break;
    }
  }
  std::sort(plan.lines.begin(), plan.lines.end(),
            [](const WriteSdcLine& a, const WriteSdcLine& b) {
              return a.first_id < b.first_id;
            });
  std::sort(plan.skipped.begin(), plan.skipped.end(),
            [](const WriteSdcSkip& a, const WriteSdcSkip& b) {
              return a.id < b.id;
            });
  plan.emitted = rows.size() - plan.skipped.size();
  return plan;
}

// Renders the planned file text: header, one command per line, and
// the skip list as a footer so the file alone documents what it
// omits. No timestamp: repeated writes are byte-identical.
std::string WriteSdcRender(
    const WriteSdcPlan& plan,
    const std::vector<const db::Scenario*>& scenarios) {
  std::string text =
      "# EOS write_sdc: single-scenario form (Immediate unit 8, review "
      "item I5)\n";
  text += "# scenario: ";
  if (scenarios.empty()) {
    text += "(none)\n";
  } else {
    for (std::size_t i = 0; i < scenarios.size(); ++i) {
      if (i != 0) text += ", ";
      text += scenarios[i]->name;
    }
    text += "\n";
  }
  text += "# The constraint set is shared by all scenarios in this "
          "increment, so\n";
  text += "# this one file carries every scenario's constraints. No "
          "per-scenario\n";
  text += "# state exists to lose in a merge; per-scenario files "
          "follow the\n";
  text += "# MMMC view model (not built yet). No timestamp is written, "
          "so\n";
  text += "# repeated writes are byte-identical.\n";
  text += "# " + std::to_string(plan.emitted) + " constraints emitted, " +
          std::to_string(plan.skipped.size()) + " rows skipped.\n";
  if (!plan.lines.empty()) {
    text += "\n";
    for (const WriteSdcLine& line : plan.lines) {
      text += line.text + "\n";
    }
  }
  if (!plan.skipped.empty()) {
    text += "\n# Skipped rows (not emitted):\n";
    for (const WriteSdcSkip& skip : plan.skipped) {
      text += "#   skipped: " + skip.kind + " " + skip.name + " " +
              skip.target + " (" + skip.reason + ")\n";
    }
  }
  return text;
}

// Writes the text to path, gzipped when compress is set. Returns
// false with the reason in `why` when the file cannot be written.
bool WriteSdcWriteFile(const std::string& path, const std::string& text,
                       bool compress, std::string& why) {
  if (!compress) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
      why = "could not write file: " + path;
      return false;
    }
    out << text;
    out.flush();
    if (!out) {
      why = "could not write file: " + path;
      return false;
    }
    return true;
  }
  gzFile gz = gzopen(path.c_str(), "wb");
  if (gz == nullptr) {
    why = "could not write file: " + path;
    return false;
  }
  const int written =
      gzwrite(gz, text.data(), static_cast<unsigned int>(text.size()));
  const int closed = gzclose(gz);
  if (written < 0 ||
      static_cast<std::size_t>(written) != text.size() ||
      closed != Z_OK) {
    why = "could not write file: " + path;
    return false;
  }
  return true;
}

// ---- check_timing (Immediate unit 4, review item I1) -----------------
// The constraint-completeness gate: the check the guides put
// before trusting any violation report. It runs FROM-SCRATCH over
// the session constraint set and the session design, and it is
// the EOS replacement for the internal OpenSTA CheckTiming, not
// a duplicate of it: OpenSTA's CheckTiming stays internal and
// unexposed, and this command never calls it. That matters
// because the two check different things: OpenSTA's checks the
// SDC file under a full STA session, while this one checks the
// constraints the user authored with eos:: commands, with no
// Liberty and no STA run required, so the gate can run before
// run_timing, where the guides put it.
//
// A check reports one of three statuses, and the summary never
// hides which one it is:
//   pass     the check ran and found nothing.
//   fail     the check ran and lists its findings.
//   not_run  the session model cannot express the check, and the
//            reason names the missing piece. A not_run check is
//            never counted as passing.
// The verdict follows the same honesty rule (review item H1: a
// clean verdict on an unchecked constraint set is forbidden):
//   pass       every selected check ran and passed.
//   fail       at least one selected check failed.
//   incomplete no check failed, but at least one selected check
//            did not run.
// The command returns Ok with the verdict in the payload in all
// three cases, like report_timing returns Ok with negative
// slack: the verdict is the gate, and a script that must stop
// on a failed gate tests verdict=. Findings never make the
// command fail, so the per-check payload stays available to
// scripts. Out of scope by the plan: check_constraints rule
// checking with waivers, and check_design.

// One check's outcome: pass, fail, or not_run, with its
// findings in sorted order. A not_run outcome carries the
// reason; a pass or fail outcome leaves it empty.
struct CheckTimingOutcome {
  std::string name;
  std::string status;
  std::vector<std::string> findings;
  std::string reason;
};

// One check_timing check: its name, whether it is in the
// PrimeTime default set, its runner (null when the session
// model cannot express it), and the reason it cannot run
// (empty when it can). The table order is the output order:
// the default set in PrimeTime order, then the optional set
// alphabetically.
struct CheckTimingCheck {
  const char* name;
  bool is_default;
  CheckTimingOutcome (*run)(const db::Design&,
                            const db::ConstraintSet&);
  const char* not_run_reason;
};

// Whether a bound constraint of the given kind targets this
// object. Only bound rows cover: an unresolved row names an
// object that does not exist, so it covers nothing.
bool CheckTimingCovered(const db::ConstraintSet& constraints, ObjectId object,
                        db::ConstraintKind kind) {
  for (const db::Constraint& c : constraints.all()) {
    if (c.status == db::BindingStatus::kBound &&
        c.bound_object == object && c.kind == kind) {
      return true;
    }
  }
  return false;
}

// Whether the named object exists in the design, as a port,
// net, instance, or pin.
bool CheckTimingObjectExists(const db::Design& design,
                             const std::string& name) {
  static const ObjectKind kKinds[] = {ObjectKind::kPort, ObjectKind::kNet,
                                      ObjectKind::kInstance,
                                      ObjectKind::kPin};
  for (ObjectKind kind : kKinds) {
    if (!design.Find(kind, name).is_null()) return true;
  }
  return false;
}

// The ports that are clock ports: targets of a bound clock or
// generated-clock row. A clock port needs no input delay.
std::set<std::uint32_t> CheckTimingClockPorts(
    const db::ConstraintSet& constraints) {
  std::set<std::uint32_t> ports;
  for (const db::Constraint& c : constraints.all()) {
    if (c.status != db::BindingStatus::kBound) continue;
    if (c.kind == db::ConstraintKind::kClock ||
        c.kind == db::ConstraintKind::kGeneratedClock) {
      ports.insert(c.bound_object.raw());
    }
  }
  return ports;
}

// Every top port that takes data in (input or inout, following
// OpenSTA's isAnyInput) and is not a clock port, without a bound
// input-delay constraint.
CheckTimingOutcome CheckTimingNoInputDelay(
    const db::Design& design, const db::ConstraintSet& constraints) {
  CheckTimingOutcome outcome;
  outcome.name = "no_input_delay";
  const std::set<std::uint32_t> clocks = CheckTimingClockPorts(constraints);
  for (const ObjectId id : EnumerateKind(design, ObjectKind::kPort)) {
    const db::PortRecord* port = design.FindPort(id);
    if (port == nullptr) continue;
    if (port->direction != db::Direction::kInput &&
        port->direction != db::Direction::kInout) {
      continue;
    }
    if (clocks.count(id.raw()) != 0) continue;
    if (CheckTimingCovered(constraints, id,
                           db::ConstraintKind::kInputDelay)) {
      continue;
    }
    outcome.findings.push_back(port->name + " missing bound input_delay");
  }
  std::sort(outcome.findings.begin(), outcome.findings.end());
  outcome.status = outcome.findings.empty() ? "pass" : "fail";
  return outcome;
}

// Every top port that drives data out (output or inout,
// following OpenSTA's isAnyOutput) without a bound
// output-delay constraint.
CheckTimingOutcome CheckTimingNoOutputDelay(
    const db::Design& design, const db::ConstraintSet& constraints) {
  CheckTimingOutcome outcome;
  outcome.name = "no_output_delay";
  for (const ObjectId id : EnumerateKind(design, ObjectKind::kPort)) {
    const db::PortRecord* port = design.FindPort(id);
    if (port == nullptr) continue;
    if (port->direction != db::Direction::kOutput &&
        port->direction != db::Direction::kInout) {
      continue;
    }
    if (CheckTimingCovered(constraints, id,
                           db::ConstraintKind::kOutputDelay)) {
      continue;
    }
    outcome.findings.push_back(port->name + " missing bound output_delay");
  }
  std::sort(outcome.findings.begin(), outcome.findings.end());
  outcome.status = outcome.findings.empty() ? "pass" : "fail";
  return outcome;
}

// Every top output port with no timing requirement on it at
// all: no bound output delay, max delay, or multicycle path.
// A false path counts as a deliberate exemption, not a missing
// requirement: the user stated the endpoint has no timing, so
// it is not reported as unconstrained.
CheckTimingOutcome CheckTimingUnconstrainedEndpoints(
    const db::Design& design, const db::ConstraintSet& constraints) {
  CheckTimingOutcome outcome;
  outcome.name = "unconstrained_endpoints";
  for (const ObjectId id : EnumerateKind(design, ObjectKind::kPort)) {
    const db::PortRecord* port = design.FindPort(id);
    if (port == nullptr) continue;
    if (port->direction != db::Direction::kOutput &&
        port->direction != db::Direction::kInout) {
      continue;
    }
    if (CheckTimingCovered(constraints, id,
                           db::ConstraintKind::kOutputDelay) ||
        CheckTimingCovered(constraints, id,
                           db::ConstraintKind::kMaxDelay) ||
        CheckTimingCovered(constraints, id,
                           db::ConstraintKind::kMulticyclePath) ||
        CheckTimingCovered(constraints, id,
                           db::ConstraintKind::kFalsePath)) {
      continue;
    }
    outcome.findings.push_back(port->name + " no covering constraint");
  }
  std::sort(outcome.findings.begin(), outcome.findings.end());
  outcome.status = outcome.findings.empty() ? "pass" : "fail";
  return outcome;
}

// Every generated clock whose definition is broken: a master
// clock that names no bound SDC clock row, a source pin that
// names no design object, or a target that never bound. The
// master must be a real SDC clock row (period_ns present,
// bound): a construction record (bind_clock) names a clock net
// but carries no period, so it cannot be a master.
CheckTimingOutcome CheckTimingGeneratedClocks(
    const db::Design& design, const db::ConstraintSet& constraints) {
  CheckTimingOutcome outcome;
  outcome.name = "generated_clocks";
  for (const db::Constraint& c : constraints.all()) {
    if (c.kind != db::ConstraintKind::kGeneratedClock) continue;
    if (c.status != db::BindingStatus::kBound) {
      outcome.findings.push_back(c.name + " target " + c.target_spec +
                                 " is unresolved");
      continue;
    }
    const std::optional<std::string> master = c.Param("master_clock");
    const bool combinational =
        c.Param("combinational").has_value() &&
        *c.Param("combinational") == "1";
    if (!combinational && master.has_value()) {
      bool defined = false;
      for (const db::Constraint& m : constraints.all()) {
        if (m.kind == db::ConstraintKind::kClock &&
            m.status == db::BindingStatus::kBound && m.name == *master &&
            m.Param("period_ns").has_value()) {
          defined = true;
          break;
        }
      }
      if (!defined) {
        outcome.findings.push_back(c.name + " master clock " + *master +
                                   " is not defined");
      }
    }
    const std::optional<std::string> source = c.Param("source");
    if (source.has_value() && !CheckTimingObjectExists(design, *source)) {
      outcome.findings.push_back(c.name + " source " + *source +
                                 " does not exist in the design");
    }
  }
  std::sort(outcome.findings.begin(), outcome.findings.end());
  outcome.status = outcome.findings.empty() ? "pass" : "fail";
  return outcome;
}

// Every multicycle-path row that never bound. Multicycle rows
// carry no setup or hold values in the session model, so an
// unbound target is the only checkable finding.
CheckTimingOutcome CheckTimingMulticyclePath(
    const db::Design& design, const db::ConstraintSet& constraints) {
  (void)design;
  CheckTimingOutcome outcome;
  outcome.name = "multicycle_path";
  for (const db::Constraint& c : constraints.all()) {
    if (c.kind != db::ConstraintKind::kMulticyclePath) continue;
    if (c.status == db::BindingStatus::kBound) continue;
    outcome.findings.push_back(c.name + " target " + c.target_spec +
                               " is unresolved");
  }
  std::sort(outcome.findings.begin(), outcome.findings.end());
  outcome.status = outcome.findings.empty() ? "pass" : "fail";
  return outcome;
}

const CheckTimingCheck kCheckTimingChecks[] = {
    {"generated_clocks", true, &CheckTimingGeneratedClocks, ""},
    {"loops", true, nullptr,
     "the design model carries no pin directions, so combinational "
     "cycles cannot be traced"},
    {"no_driving_cell", true, nullptr,
     "the session model has no driving-cell constraint"},
    {"no_input_delay", true, &CheckTimingNoInputDelay, ""},
    {"no_output_delay", true, &CheckTimingNoOutputDelay, ""},
    {"partial_input_delay", true, nullptr,
     "input-delay rows carry no clock association"},
    {"pulse_clock_cell_type", true, nullptr,
     "the session model has no clock cell types"},
    {"unconstrained_endpoints", true, &CheckTimingUnconstrainedEndpoints, ""},
    {"case_analysis", false, nullptr,
     "case-analysis rows carry no value"},
    {"clock_crossing", false, nullptr,
     "the design model has no clock domains"},
    {"gated_clock", false, nullptr,
     "the design model has no clock-gating structure"},
    {"multiple_clock", false, nullptr,
     "the design model has no sequential elements"},
    {"multicycle_path", false, &CheckTimingMulticyclePath, ""},
};

const CheckTimingCheck* FindCheckTimingCheck(const std::string& name) {
  for (const CheckTimingCheck& check : kCheckTimingChecks) {
    if (name == check.name) return &check;
  }
  return nullptr;
}

// The known check names, comma separated, for failure messages.
std::string CheckTimingKnownChecks() {
  std::string out;
  for (const CheckTimingCheck& check : kCheckTimingChecks) {
    if (!out.empty()) out += ", ";
    out += check.name;
  }
  return out;
}

CheckTimingOutcome CheckTimingNotRun(const CheckTimingCheck& check) {
  CheckTimingOutcome outcome;
  outcome.name = check.name;
  outcome.status = "not_run";
  outcome.reason = check.not_run_reason;
  return outcome;
}

// ---- report_clock (Immediate unit 5, review item I2) ----------------
// The clock inventory, PrimeTime-adjacent: the Clock, Period,
// Waveform, Attrs, Sources table plus the generated-clock table
// (Timing Constraints U-2022.12 ch. 2, PrimeTime T-2022.03 ch. 7).
// It runs FROM-SCRATCH over the session constraint set: SDC clocks
// are kClock rows carrying period_ns, generated clocks are
// kGeneratedClock rows, and construction records (bind_clock) are
// kClock rows without a period. Nothing here reads the SDC file
// recorded by read_sdc: that path stores the file path only, so
// clocks defined in an SDC file do not appear until a later unit
// wires file constraints into the session (see
// docs/sdc-commands.md). The report never invents a number: a
// period or waveform that cannot be derived from stored data
// renders as "-", and the -skew column reports the stored clock
// uncertainty, which is "none" for every clock today because EOS
// has no set_clock_uncertainty command yet. Does NOT include
// report_clock_timing (clock-network summary and skew); that is
// a separate item.

// One clock entry in the report_clock main table: stored rows that
// render identically (same name, same kind, same effective period
// and waveform), with their sources merged. Rows that differ in
// any displayed value stay separate rows, so a name stacked with
// -add under two periods is never collapsed into one number.
struct ReportClockEntry {
  std::string name;
  bool generated = false;
  bool unresolved = false;  // any row in the group never bound
  std::vector<std::string> sources;  // target specs, insertion order
  std::optional<double> period_ns;
  std::optional<double> rise_ns;
  std::optional<double> fall_ns;
};

// One row of the generated-clock table: a generated-clock
// definition with its master and its waveform modification.
// Generated sources merge within a group; the master source is
// looked up from the master clock's own rows.
struct ReportGeneratedEntry {
  std::string name;
  std::string master_clock;  // empty for a combinational clock
  std::string master_source;  // "-" when the master is not defined
  std::vector<std::string> sources;  // generated targets, insertion order
  std::string modification;  // divide_by(n), multiply_by(n), invert,
                             // combinational, in that order
};

// The effective period and waveform of a clock by name.
struct ReportClockTiming {
  double period_ns = 0;
  double rise_ns = 0;
  double fall_ns = 0;
};

// The effective waveform of an SDC clock row: the stored waveform
// when given, else the SDC default {0 period/2}. The default is
// SDC semantics, not an invented value: create_clock without
// -waveform defines a clock whose first edge is at 0 and whose
// second edge is at half the period.
bool ReportClockWaveform(const db::Constraint& clock, double period_ns,
                         double& rise_ns, double& fall_ns) {
  const std::optional<std::string> rise = clock.Param("waveform_rise_ns");
  const std::optional<std::string> fall = clock.Param("waveform_fall_ns");
  if (!rise.has_value() && !fall.has_value()) {
    rise_ns = 0.0;
    fall_ns = period_ns / 2.0;
    return true;
  }
  if (!rise.has_value() || !fall.has_value()) return false;
  return ParseDouble(*rise, rise_ns) && ParseDouble(*fall, fall_ns);
}

std::optional<ReportClockTiming> ReportClockGeneratedTiming(
    const db::ConstraintSet& constraints, const db::Constraint& c,
    std::set<db::ConstraintId>& seen);

// The effective period and waveform of a clock by name: the stored
// values for an SDC clock, the derived values for a generated
// clock. Returns nullopt when the clock is not defined, its
// master is not defined, or its waveform cannot be represented
// as one {rise fall} pair. `seen` holds the generated rows
// already on the derivation path, by constraint id: a cycle
// yields unknown, never a loop. An SDC row wins over a generated
// row sharing the name: the direct definition governs. The
// cycle set is by id, not by name, because one name can carry
// both an SDC row and a generated row: a generated clock whose
// master shares its name derives from the SDC row, which is a
// different row and not a cycle.
std::optional<ReportClockTiming> ReportClockTimingOf(
    const db::ConstraintSet& constraints, const std::string& name,
    std::set<db::ConstraintId>& seen) {
  for (const db::Constraint& c : constraints.all()) {
    if (c.kind != db::ConstraintKind::kClock || c.name != name) continue;
    const std::optional<std::string> period = c.Param("period_ns");
    if (!period.has_value()) continue;
    double period_ns = 0.0;
    if (!ParseDouble(*period, period_ns)) return std::nullopt;
    ReportClockTiming timing;
    timing.period_ns = period_ns;
    if (!ReportClockWaveform(c, period_ns, timing.rise_ns,
                             timing.fall_ns)) {
      return std::nullopt;
    }
    return timing;
  }
  for (const db::Constraint& c : constraints.all()) {
    if (c.kind != db::ConstraintKind::kGeneratedClock || c.name != name) {
      continue;
    }
    return ReportClockGeneratedTiming(constraints, c, seen);
  }
  return std::nullopt;
}

// The effective period and waveform of one generated-clock row,
// derived from its own master and waveform modification. The
// period scales by divide_by over multiply_by (1 when absent);
// a combinational clock keeps the master period. The waveform
// scales by the same factor. Inversion moves the high interval
// [rise, fall) to [fall, rise + period), which is one {rise
// fall} pair only when the master rise is 0 and the master fall
// is below the period, which the default waveform satisfies.
std::optional<ReportClockTiming> ReportClockGeneratedTiming(
    const db::ConstraintSet& constraints, const db::Constraint& c,
    std::set<db::ConstraintId>& seen) {
  // A generated row deriving from itself is a cycle: unknown,
  // never a loop. The check is by constraint id, so a master
  // that shares this row's name but is a different row (an SDC
  // row, or another generated row) still resolves.
  if (seen.count(c.id) != 0) return std::nullopt;
  seen.insert(c.id);
  const std::optional<std::string> master = c.Param("master_clock");
  if (!master.has_value()) return std::nullopt;
  const std::optional<ReportClockTiming> master_timing =
      ReportClockTimingOf(constraints, *master, seen);
  if (!master_timing.has_value()) return std::nullopt;
  double factor = 1.0;
  const std::optional<std::string> divide = c.Param("divide_by");
  const std::optional<std::string> multiply = c.Param("multiply_by");
  long divide_by = 0;
  long multiply_by = 0;
  if (divide.has_value() && !ParseInt(*divide, divide_by)) {
    return std::nullopt;
  }
  if (multiply.has_value() && !ParseInt(*multiply, multiply_by)) {
    return std::nullopt;
  }
  if (divide_by != 0) factor *= static_cast<double>(divide_by);
  if (multiply_by != 0) factor /= static_cast<double>(multiply_by);
  ReportClockTiming timing;
  timing.period_ns = master_timing->period_ns * factor;
  timing.rise_ns = master_timing->rise_ns * factor;
  timing.fall_ns = master_timing->fall_ns * factor;
  if (c.Param("invert").has_value()) {
    if (timing.rise_ns != 0.0 || timing.fall_ns >= timing.period_ns) {
      return std::nullopt;
    }
    const double rise = timing.fall_ns;
    timing.fall_ns = timing.period_ns;
    timing.rise_ns = rise;
  }
  return timing;
}

// The source objects of a clock by name: the target specs of its
// stored rows, joined with spaces, or "-" when no row names it.
std::string ReportClockSourcesOf(const db::ConstraintSet& constraints,
                                 const std::string& name) {
  std::string out;
  for (const db::Constraint& c : constraints.all()) {
    if (c.name != name) continue;
    if (c.kind != db::ConstraintKind::kClock &&
        c.kind != db::ConstraintKind::kGeneratedClock) {
      continue;
    }
    if (!out.empty()) out += " ";
    out += c.target_spec;
  }
  return out.empty() ? "-" : out;
}

// Appends a target to a source list once, keeping first-seen order.
void ReportClockAddSource(std::vector<std::string>& sources,
                          const std::string& target) {
  if (std::find(sources.begin(), sources.end(), target) ==
      sources.end()) {
    sources.push_back(target);
  }
}

// The main-table entries, grouped so rows that render identically
// merge and rows that differ stay separate, sorted by name with an
// SDC entry before a generated entry sharing the name.
std::vector<ReportClockEntry> ReportClockEntries(
    const db::ConstraintSet& constraints) {
  std::vector<ReportClockEntry> entries;
  const auto find_entry = [&entries](const std::string& name,
                                     bool generated,
                                     std::optional<double> period_ns,
                                     std::optional<double> rise_ns,
                                     std::optional<double> fall_ns) {
    for (ReportClockEntry& entry : entries) {
      if (entry.name == name && entry.generated == generated &&
          entry.period_ns == period_ns && entry.rise_ns == rise_ns &&
          entry.fall_ns == fall_ns) {
        return &entry;
      }
    }
    return static_cast<ReportClockEntry*>(nullptr);
  };
  for (const db::Constraint& c : constraints.all()) {
    if (c.kind == db::ConstraintKind::kClock) {
      const std::optional<std::string> period = c.Param("period_ns");
      if (!period.has_value()) continue;  // construction record
      double period_ns = 0.0;
      if (!ParseDouble(*period, period_ns)) continue;
      double rise_ns = 0.0;
      double fall_ns = 0.0;
      if (!ReportClockWaveform(c, period_ns, rise_ns, fall_ns)) continue;
      ReportClockEntry* entry = find_entry(
          c.name, false, period_ns, rise_ns, fall_ns);
      if (entry == nullptr) {
        entries.push_back(ReportClockEntry());
        entry = &entries.back();
        entry->name = c.name;
        entry->generated = false;
        entry->period_ns = period_ns;
        entry->rise_ns = rise_ns;
        entry->fall_ns = fall_ns;
      }
      if (c.status != db::BindingStatus::kBound) entry->unresolved = true;
      ReportClockAddSource(entry->sources, c.target_spec);
    } else if (c.kind == db::ConstraintKind::kGeneratedClock) {
      // Derive from this row's own definition, not from the name:
      // a name carrying both an SDC row and a generated row
      // derives the generated entry from its master.
      std::set<db::ConstraintId> seen;
      const std::optional<ReportClockTiming> timing =
          ReportClockGeneratedTiming(constraints, c, seen);
      std::optional<double> period_ns;
      std::optional<double> rise_ns;
      std::optional<double> fall_ns;
      if (timing.has_value()) {
        period_ns = timing->period_ns;
        rise_ns = timing->rise_ns;
        fall_ns = timing->fall_ns;
      }
      ReportClockEntry* entry =
          find_entry(c.name, true, period_ns, rise_ns, fall_ns);
      if (entry == nullptr) {
        entries.push_back(ReportClockEntry());
        entry = &entries.back();
        entry->name = c.name;
        entry->generated = true;
        entry->period_ns = period_ns;
        entry->rise_ns = rise_ns;
        entry->fall_ns = fall_ns;
      }
      if (c.status != db::BindingStatus::kBound) entry->unresolved = true;
      ReportClockAddSource(entry->sources, c.target_spec);
    }
  }
  std::sort(entries.begin(), entries.end(),
            [](const ReportClockEntry& a, const ReportClockEntry& b) {
              if (a.name != b.name) return a.name < b.name;
              return !a.generated && b.generated;
            });
  return entries;
}

// The waveform modification of one generated-clock row, in the
// fixed order divide_by, multiply_by, invert, combinational.
std::string ReportClockModification(const db::Constraint& c) {
  std::string out;
  const std::optional<std::string> divide = c.Param("divide_by");
  if (divide.has_value()) out += "divide_by(" + *divide + ")";
  const std::optional<std::string> multiply = c.Param("multiply_by");
  if (multiply.has_value()) {
    if (!out.empty()) out += " ";
    out += "multiply_by(" + *multiply + ")";
  }
  if (c.Param("invert").has_value()) {
    if (!out.empty()) out += " ";
    out += "invert";
  }
  if (c.Param("combinational").has_value()) {
    if (!out.empty()) out += " ";
    out += "combinational";
  }
  return out;
}

// The generated-clock table entries, grouped so identical rows
// merge and rows that differ stay separate, sorted by name.
std::vector<ReportGeneratedEntry> ReportGeneratedEntries(
    const db::ConstraintSet& constraints) {
  std::vector<ReportGeneratedEntry> entries;
  for (const db::Constraint& c : constraints.all()) {
    if (c.kind != db::ConstraintKind::kGeneratedClock) continue;
    const std::string master =
        c.Param("master_clock").value_or("");
    const std::string modification = ReportClockModification(c);
    ReportGeneratedEntry* entry = nullptr;
    for (ReportGeneratedEntry& candidate : entries) {
      if (candidate.name == c.name &&
          candidate.master_clock == master &&
          candidate.modification == modification) {
        entry = &candidate;
        break;
      }
    }
    if (entry == nullptr) {
      entries.push_back(ReportGeneratedEntry());
      entry = &entries.back();
      entry->name = c.name;
      entry->master_clock = master;
      entry->master_source =
          master.empty() ? "-"
                         : ReportClockSourcesOf(constraints, master);
      entry->modification = modification;
    }
    ReportClockAddSource(entry->sources, c.target_spec);
  }
  std::sort(entries.begin(), entries.end(),
            [](const ReportGeneratedEntry& a,
               const ReportGeneratedEntry& b) {
              if (a.name != b.name) return a.name < b.name;
              return a.master_clock < b.master_clock;
            });
  return entries;
}

// Renders one table: a header row, a dash line, the data rows,
// and a closing dash line. Columns pad to the widest cell, two
// spaces between columns, left aligned, following the
// report_timing table shape.
std::string ReportClockRenderTable(
    std::vector<std::vector<std::string>> rows) {
  if (rows.empty()) return "";
  const std::size_t cols = rows[0].size();
  std::vector<std::size_t> widths(cols, 0);
  for (const auto& row : rows) {
    for (std::size_t i = 0; i < row.size(); ++i) {
      widths[i] = std::max(widths[i], row[i].size());
    }
  }
  std::size_t width = 0;
  for (std::size_t i = 0; i < cols; ++i) width += widths[i];
  width += 2 * (cols - 1);
  std::string out;
  for (std::size_t r = 0; r < rows.size(); ++r) {
    for (std::size_t i = 0; i < cols; ++i) {
      if (i != 0) out += "  ";
      out += rows[r][i];
      out += std::string(widths[i] - rows[r][i].size(), ' ');
    }
    out += "\n";
    if (r == 0) out += std::string(width, '-') + "\n";
  }
  out += std::string(width, '-') + "\n";
  return out;
}

// Joins sources into the vendor "{a b}" form.
std::string ReportClockBrace(const std::vector<std::string>& sources) {
  std::string out = "{";
  for (std::size_t i = 0; i < sources.size(); ++i) {
    if (i != 0) out += " ";
    out += sources[i];
  }
  out += "}";
  return out;
}

  // Selects the scenes for a check-specific report and runs
  // the analysis for them: BuildTimingScenes plus the -scene
  // filter, then one AnalyzeCheckReports call. Returns the
  // failing CommandResult on any error, or nullopt with scenes
  // and analyzed filled in.
  std::optional<CommandResult> AnalyzeCheckReportsScenes(
      CommandContext& ctx, const char* command,
      const SdcOptions& opts,
      std::vector<analysis::SceneAnalysis>& scenes,
      std::vector<analysis::CheckReportsResult>& analyzed) {
    bool from_odb = false;
    if (auto bad = BuildTimingScenes(ctx, from_odb, scenes)) {
      return bad;
    }
    if (opts.Seen("-scene")) {
      const std::string wanted = *opts.Value("-scene");
      std::vector<analysis::SceneAnalysis> selected;
      for (const analysis::SceneAnalysis& scene : scenes) {
        if (scene.scenario.name == wanted) {
          selected.push_back(scene);
        }
      }
      if (selected.empty()) {
        return CommandResult::Fail(
            Diag::kInvalidConfiguration,
            std::string(command) + ": unknown scenario: " + wanted);
      }
      scenes = std::move(selected);
    }
    analysis::TimingService service;
    const Result<std::vector<analysis::CheckReportsResult>> result =
        from_odb
            ? service.AnalyzeCheckReports(*ctx.timing.odb,
                                          ctx.timing.sdc_path, scenes)
            : service.AnalyzeCheckReports(
                  analysis::VerilogDesign{ctx.timing.verilog_path,
                                          ctx.timing.top_cell,
                                          ctx.timing.sdc_path},
                  scenes);
    if (!result.ok()) return FromStatus(result.status());
    analyzed = std::move(result).value();
    return std::nullopt;
  }

  // Parses -delay_type for the check-specific reports that take
  // it: max selects the setup (max) kind, min the hold (min)
  // kind, and absence selects both. Returns nullopt for both.
  std::optional<CommandResult> ParseCheckDelayType(
      const SdcOptions& opts, const char* command,
      std::optional<analysis::CheckKind>& kind) {
    kind = std::nullopt;
    if (!opts.Seen("-delay_type")) return std::nullopt;
    const std::string delay_type = *opts.Value("-delay_type");
    if (delay_type == "max") {
      kind = analysis::CheckKind::kSetup;
    } else if (delay_type == "min") {
      kind = analysis::CheckKind::kHold;
    } else {
      return CommandResult::Fail(
          Diag::kInvalidConfiguration,
          std::string(command) +
              ": -delay_type must be max or min, got: " + delay_type);
    }
    return std::nullopt;
  }

  // Writes a check-specific report's text to -file when the
  // option was given. Returns the failing CommandResult when
  // the file cannot be written, or nullopt.
  std::optional<CommandResult> WriteCheckReportFile(
      const SdcOptions& opts, const std::string& text) {
    if (!opts.Seen("-file")) return std::nullopt;
    const std::string file = *opts.Value("-file");
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
    return std::nullopt;
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

  // The design-construction clock record. It binds a clock name to
  // a design target and carries no timing semantics: no period, no
  // waveform. It used to be called create_clock; that name is now
  // the SDC command below, so scripts using the old form must move
  // to bind_clock (see docs/sdc-commands.md).
  add("bind_clock", "bind_clock <name> <target>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad =
                ExpectCount(args, 2, "bind_clock <name> <target>")) {
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
          if (!c.params.empty()) {
            out += " params=";
            for (std::size_t k = 0; k < c.params.size(); ++k) {
              if (k != 0) out += ";";
              out += c.params[k].first + "=" + c.params[k].second;
            }
          }
        }
        if (out.empty()) out = "constraints=0";
        return CommandResult::Ok(out);
      });

  // ---- SDC command family (Immediate unit 3) -----------------------
  // Each command stores real constraints in the session's constraint
  // set, with its semantics as parameters, so list_constraints,
  // constraint_coverage, checkpoints, and later check commands see
  // them. Nothing here is an accept-and-ignore stub: a semantic the
  // session model cannot represent fails fast with the gap named.
  // Boundary, stated plainly: run_timing still reads its SDC from
  // the file recorded by read_sdc; these registry constraints are
  // session state, not STA input, until a later unit wires session
  // constraints into the timing service (see docs/sdc-commands.md).

  add("create_clock",
      "create_clock -period <ns> [-waveform <rise> <fall>] "
      "[-name <name>] [-add] <target>...",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-period", 1, false},
                 {"-waveform", 2, false},
                 {"-name", 1, false},
                 {"-add", 0, false}},
                "create_clock", opts)) {
          return *bad;
        }
        // The old construction form carried no flags and exactly
        // two positionals. It no longer parses as anything else, so
        // name the migration instead of reporting a bare arity error.
        if (!opts.Seen("-period") && !opts.Seen("-waveform") &&
            !opts.Seen("-name") && !opts.Seen("-add") &&
            opts.Positionals().size() == 2) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "create_clock is now the SDC command; the "
              "design-construction record moved to bind_clock "
              "<name> <target>");
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        if (!opts.Seen("-period")) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "create_clock: option -period is "
                                     "required");
        }
        double period = 0.0;
        if (auto bad = ParseOptDouble(opts, "-period", "create_clock",
                                      period)) {
          return *bad;
        }
        if (period <= 0.0) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "create_clock: -period must be positive, got: " +
                  FormatNum(period));
        }
        std::vector<std::pair<std::string, std::string>> params;
        params.emplace_back("period_ns", FormatNum(period));
        if (opts.Seen("-waveform")) {
          const std::vector<std::string> wave = opts.Values("-waveform");
          double rise = 0.0;
          double fall = 0.0;
          if (!ParseDouble(wave[0], rise) ||
              !ParseDouble(wave[1], fall)) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "create_clock: -waveform needs two numbers, got: " +
                    wave[0] + " " + wave[1]);
          }
          if (rise < 0.0 || fall < 0.0 || rise >= fall ||
              fall > period) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "create_clock: -waveform needs 0 <= rise < fall <= "
                "period, got: " +
                    FormatNum(rise) + " " + FormatNum(fall) +
                    " with period " + FormatNum(period));
          }
          params.emplace_back("waveform_rise_ns", FormatNum(rise));
          params.emplace_back("waveform_fall_ns", FormatNum(fall));
        }
        std::vector<std::string> targets;
        if (auto bad = NormalizeTargets(opts.Positionals(), "create_clock",
                                        targets)) {
          return *bad;
        }
        if (targets.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "create_clock: at least one target is required");
        }
        std::string name;
        if (opts.Value("-name").has_value()) {
          name = *opts.Value("-name");
        } else if (targets.size() == 1) {
          name = targets[0];
        } else {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "create_clock: -name is required for several targets");
        }
        if (name.empty()) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "create_clock: -name must not be "
                                     "empty");
        }
        // Replace-by-default, scoped to SDC clock rows: construction
        // records (bind_clock) sharing the name are untouched.
        if (!opts.Has("-add")) {
          std::vector<db::ConstraintId> dead;
          for (const db::Constraint& c :
               ctx.project.constraints.all()) {
            if (c.kind == db::ConstraintKind::kClock &&
                c.name == name &&
                c.Param("period_ns").has_value()) {
              dead.push_back(c.id);
            }
          }
          for (db::ConstraintId id : dead) {
            ctx.project.constraints.Remove(id);
          }
        }
        return CommandResult::Ok(StoreTargets(
            ctx, db::ConstraintKind::kClock, targets,
            std::vector<std::string>(targets.size(), name), params));
      });

  add("create_generated_clock",
      "create_generated_clock -source <pin> [-master_clock <name>] "
      "[-divide_by <n>] [-multiply_by <n>] [-invert] [-add] "
      "[-combinational] [-name <name>] <target>...",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-source", 1, false},
                 {"-master_clock", 1, false},
                 {"-divide_by", 1, false},
                 {"-multiply_by", 1, false},
                 {"-invert", 0, false},
                 {"-add", 0, false},
                 {"-combinational", 0, false},
                 {"-name", 1, false}},
                "create_generated_clock", opts)) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        if (!opts.Seen("-source")) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "create_generated_clock: option -source is required");
        }
        std::string source;
        if (auto bad = NormalizeTarget(*opts.Value("-source"),
                                       "create_generated_clock", source)) {
          return *bad;
        }
        const bool combinational = opts.Has("-combinational");
        if (!combinational && !opts.Seen("-master_clock")) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "create_generated_clock: -master_clock or "
              "-combinational is required");
        }
        long divide_by = 0;
        long multiply_by = 0;
        if (opts.Seen("-divide_by")) {
          if (!ParseInt(*opts.Value("-divide_by"), divide_by) ||
              divide_by <= 0) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "create_generated_clock: -divide_by needs a positive "
                "integer, got: " +
                    *opts.Value("-divide_by"));
          }
        }
        if (opts.Seen("-multiply_by")) {
          if (!ParseInt(*opts.Value("-multiply_by"), multiply_by) ||
              multiply_by <= 0) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "create_generated_clock: -multiply_by needs a "
                "positive integer, got: " +
                    *opts.Value("-multiply_by"));
          }
        }
        if (!combinational && divide_by == 0 && multiply_by == 0) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "create_generated_clock: at least one of -divide_by, "
              "-multiply_by is required (without -combinational)");
        }
        std::vector<std::string> targets;
        if (auto bad = NormalizeTargets(opts.Positionals(),
                                        "create_generated_clock",
                                        targets)) {
          return *bad;
        }
        if (targets.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "create_generated_clock: at least one target is "
              "required");
        }
        std::string name;
        if (opts.Value("-name").has_value()) {
          name = *opts.Value("-name");
        } else if (targets.size() == 1) {
          name = targets[0];
        } else {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "create_generated_clock: -name is required for "
              "several targets");
        }
        if (name.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "create_generated_clock: -name must not be empty");
        }
        std::vector<std::pair<std::string, std::string>> params;
        params.emplace_back("source", source);
        if (opts.Seen("-master_clock")) {
          params.emplace_back("master_clock",
                              *opts.Value("-master_clock"));
        }
        if (divide_by != 0) {
          params.emplace_back("divide_by", std::to_string(divide_by));
        }
        if (multiply_by != 0) {
          params.emplace_back("multiply_by",
                              std::to_string(multiply_by));
        }
        if (opts.Has("-invert")) params.emplace_back("invert", "1");
        if (combinational) params.emplace_back("combinational", "1");
        if (!opts.Has("-add")) {
          ReplaceNamed(ctx, db::ConstraintKind::kGeneratedClock, name);
        }
        return CommandResult::Ok(StoreTargets(
            ctx, db::ConstraintKind::kGeneratedClock, targets,
            std::vector<std::string>(targets.size(), name), params));
      });

  add("set_clock_gating_check",
      "set_clock_gating_check [-setup <ns>] [-hold <ns>] "
      "[-high | -low] <target>...",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-setup", 1, false},
                 {"-hold", 1, false},
                 {"-high", 0, false},
                 {"-low", 0, false}},
                "set_clock_gating_check", opts)) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        if (!opts.Seen("-setup") && !opts.Seen("-hold")) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_clock_gating_check: at least one of -setup, "
              "-hold is required");
        }
        if (opts.Has("-high") && opts.Has("-low")) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_clock_gating_check: -high and -low conflict");
        }
        std::vector<std::pair<std::string, std::string>> params;
        double value = 0.0;
        if (opts.Seen("-setup")) {
          if (auto bad = ParseOptDouble(opts, "-setup",
                                        "set_clock_gating_check",
                                        value)) {
            return *bad;
          }
          params.emplace_back("setup_ns", FormatNum(value));
        }
        if (opts.Seen("-hold")) {
          if (auto bad = ParseOptDouble(opts, "-hold",
                                        "set_clock_gating_check",
                                        value)) {
            return *bad;
          }
          params.emplace_back("hold_ns", FormatNum(value));
        }
        params.emplace_back(
            "edge",
            opts.Has("-high") ? "high" : (opts.Has("-low") ? "low" : "both"));
        std::vector<std::string> targets;
        if (auto bad = NormalizeTargets(opts.Positionals(),
                                        "set_clock_gating_check",
                                        targets)) {
          return *bad;
        }
        if (targets.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_clock_gating_check: at least one target is "
              "required");
        }
        std::vector<std::string> names;
        for (const std::string& t : targets) {
          names.push_back("clock_gating_check_" + t);
        }
        return CommandResult::Ok(StoreTargets(
            ctx, db::ConstraintKind::kClockGatingCheck, targets, names,
            params));
      });

  add("set_data_check",
      "set_data_check -from <obj> [-from <obj>...] -to <obj> "
      "[-to <obj>...] [-setup <ns>] [-hold <ns>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-from", 1, true},
                 {"-to", 1, true},
                 {"-setup", 1, false},
                 {"-hold", 1, false}},
                "set_data_check", opts)) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        if (!opts.Seen("-from") || !opts.Seen("-to")) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_data_check: -from and -to are both required");
        }
        if (!opts.Seen("-setup") && !opts.Seen("-hold")) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_data_check: at least one of -setup, -hold is "
              "required");
        }
        if (!opts.Positionals().empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_data_check: unexpected positional argument: " +
                  opts.Positionals()[0]);
        }
        std::vector<std::pair<std::string, std::string>> base;
        double value = 0.0;
        if (opts.Seen("-setup")) {
          if (auto bad = ParseOptDouble(opts, "-setup", "set_data_check",
                                        value)) {
            return *bad;
          }
          base.emplace_back("setup_ns", FormatNum(value));
        }
        if (opts.Seen("-hold")) {
          if (auto bad = ParseOptDouble(opts, "-hold", "set_data_check",
                                        value)) {
            return *bad;
          }
          base.emplace_back("hold_ns", FormatNum(value));
        }
        std::vector<std::string> froms;
        std::vector<std::string> tos;
        if (auto bad = NormalizeTargets(opts.Values("-from"),
                                        "set_data_check", froms)) {
          return *bad;
        }
        if (auto bad = NormalizeTargets(opts.Values("-to"),
                                        "set_data_check", tos)) {
          return *bad;
        }
        // One row per (from, to) pair, bound to the constrained
        // endpoint. The from endpoint is recorded, not bound, in
        // this increment; a later unit can bind it.
        std::string payload;
        for (const std::string& to : tos) {
          for (const std::string& from : froms) {
            std::vector<std::pair<std::string, std::string>> params =
                base;
            params.emplace_back("from", from);
            const db::ConstraintId id = ctx.project.constraints.Add(
                db::ConstraintKind::kDataCheck,
                "data_check_" + from + "_to_" + to, to,
                *ctx.project.design, params);
            const db::Constraint* c =
                ctx.project.constraints.Find(id);
            if (!payload.empty()) payload += "\n";
            payload += "constraint=" + std::to_string(id) +
                       " status=" + StatusWord(c->status) +
                       " target=" + to;
            if (c->status == db::BindingStatus::kBound) {
              payload += " object=" + IdStr(c->bound_object);
            }
            payload += " params=";
            for (std::size_t k = 0; k < params.size(); ++k) {
              if (k != 0) payload += ";";
              payload += params[k].first + "=" + params[k].second;
            }
          }
        }
        return CommandResult::Ok(payload);
      });

  add("set_min_pulse_width",
      "set_min_pulse_width <ns> [-high | -low] <target>...",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-high", 0, false}, {"-low", 0, false}},
                "set_min_pulse_width", opts)) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        if (opts.Has("-high") && opts.Has("-low")) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_min_pulse_width: -high and -low conflict");
        }
        if (opts.Positionals().size() < 2) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "usage: eos::set_min_pulse_width <ns> [-high | -low] "
              "<target>...");
        }
        double value = 0.0;
        if (!ParseDouble(opts.Positionals()[0], value)) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_min_pulse_width: width needs a number, got: " +
                  opts.Positionals()[0]);
        }
        if (value <= 0.0) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_min_pulse_width: width must be positive, got: " +
                  FormatNum(value));
        }
        std::vector<std::string> raw_targets(
            opts.Positionals().begin() + 1, opts.Positionals().end());
        std::vector<std::string> targets;
        if (auto bad = NormalizeTargets(raw_targets, "set_min_pulse_width",
                                        targets)) {
          return *bad;
        }
        std::vector<std::pair<std::string, std::string>> params;
        params.emplace_back("value_ns", FormatNum(value));
        params.emplace_back(
            "edge",
            opts.Has("-high") ? "high" : (opts.Has("-low") ? "low" : "both"));
        std::vector<std::string> names;
        for (const std::string& t : targets) {
          names.push_back("min_pulse_width_" + t);
        }
        return CommandResult::Ok(StoreTargets(
            ctx, db::ConstraintKind::kMinPulseWidth, targets, names,
            params));
      });

  add("set_sense",
      "set_sense (-positive | -negative | -stop_propagation) "
      "[-clock <name>...] <pin>...",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-positive", 0, false},
                 {"-negative", 0, false},
                 {"-stop_propagation", 0, false},
                 {"-clock", 1, true}},
                "set_sense", opts)) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        const int types = (opts.Has("-positive") ? 1 : 0) +
                          (opts.Has("-negative") ? 1 : 0) +
                          (opts.Has("-stop_propagation") ? 1 : 0);
        if (types != 1) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_sense: exactly one of -positive, -negative, "
              "-stop_propagation is required");
        }
        std::vector<std::string> targets;
        if (auto bad = NormalizeTargets(opts.Positionals(), "set_sense",
                                        targets)) {
          return *bad;
        }
        if (targets.empty()) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "set_sense: at least one pin is "
                                     "required");
        }
        std::vector<std::pair<std::string, std::string>> params;
        params.emplace_back("sense",
                            opts.Has("-positive")
                                ? "positive"
                                : (opts.Has("-negative")
                                       ? "negative"
                                       : "stop_propagation"));
        // Clock names are recorded, not bound, in this increment.
        if (opts.Seen("-clock")) {
          std::string clocks;
          for (const std::string& clock : opts.Values("-clock")) {
            if (!clocks.empty()) clocks += " ";
            clocks += clock;
          }
          params.emplace_back("sense_clocks", clocks);
        }
        std::vector<std::string> names;
        for (const std::string& t : targets) {
          names.push_back("sense_" + t);
        }
        return CommandResult::Ok(StoreTargets(
            ctx, db::ConstraintKind::kSense, targets, names, params));
      });

  add("set_ideal_network", "set_ideal_network [-no_propagate] <target>...",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(args, {{"-no_propagate", 0, false}},
                                         "set_ideal_network", opts)) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        std::vector<std::string> targets;
        if (auto bad = NormalizeTargets(opts.Positionals(),
                                        "set_ideal_network", targets)) {
          return *bad;
        }
        if (targets.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_ideal_network: at least one target is required");
        }
        std::vector<std::pair<std::string, std::string>> params;
        if (opts.Has("-no_propagate")) {
          params.emplace_back("no_propagate", "1");
        }
        std::vector<std::string> names;
        for (const std::string& t : targets) {
          names.push_back("ideal_network_" + t);
        }
        return CommandResult::Ok(StoreTargets(
            ctx, db::ConstraintKind::kIdealNetwork, targets, names,
            params));
      });

  add("set_ideal_latency",
      "set_ideal_latency <ns> [-rise | -fall] [-min | -max] "
      "<target>...",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-rise", 0, false},
                 {"-fall", 0, false},
                 {"-min", 0, false},
                 {"-max", 0, false}},
                "set_ideal_latency", opts)) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        if (opts.Positionals().size() < 2) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "usage: eos::set_ideal_latency <ns> [-rise | -fall] "
              "[-min | -max] <target>...");
        }
        double value = 0.0;
        if (!ParseDouble(opts.Positionals()[0], value)) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_ideal_latency: latency needs a number, got: " +
                  opts.Positionals()[0]);
        }
        if (value < 0.0) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_ideal_latency: latency must not be negative, "
              "got: " +
                  FormatNum(value));
        }
        std::vector<std::string> raw_targets(
            opts.Positionals().begin() + 1, opts.Positionals().end());
        std::vector<std::string> targets;
        if (auto bad = NormalizeTargets(raw_targets, "set_ideal_latency",
                                        targets)) {
          return *bad;
        }
        std::vector<std::pair<std::string, std::string>> params;
        params.emplace_back("value_ns", FormatNum(value));
        params.emplace_back(
            "edge",
            opts.Has("-rise") && !opts.Has("-fall")
                ? "rise"
                : (!opts.Has("-rise") && opts.Has("-fall") ? "fall"
                                                            : "both"));
        params.emplace_back(
            "delay",
            opts.Has("-min") && !opts.Has("-max")
                ? "min"
                : (!opts.Has("-min") && opts.Has("-max") ? "max" : "both"));
        std::vector<std::string> names;
        for (const std::string& t : targets) {
          names.push_back("ideal_latency_" + t);
        }
        return CommandResult::Ok(StoreTargets(
            ctx, db::ConstraintKind::kIdealLatency, targets, names,
            params));
      });

  add("set_ideal_transition",
      "set_ideal_transition <ns> [-rise | -fall] [-min | -max] "
      "<target>...",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-rise", 0, false},
                 {"-fall", 0, false},
                 {"-min", 0, false},
                 {"-max", 0, false}},
                "set_ideal_transition", opts)) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        if (opts.Positionals().size() < 2) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "usage: eos::set_ideal_transition <ns> [-rise | -fall] "
              "[-min | -max] <target>...");
        }
        double value = 0.0;
        if (!ParseDouble(opts.Positionals()[0], value)) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_ideal_transition: transition needs a number, "
              "got: " +
                  opts.Positionals()[0]);
        }
        if (value < 0.0) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_ideal_transition: transition must not be "
              "negative, got: " +
                  FormatNum(value));
        }
        std::vector<std::string> raw_targets(
            opts.Positionals().begin() + 1, opts.Positionals().end());
        std::vector<std::string> targets;
        if (auto bad = NormalizeTargets(raw_targets, "set_ideal_transition",
                                        targets)) {
          return *bad;
        }
        std::vector<std::pair<std::string, std::string>> params;
        params.emplace_back("value_ns", FormatNum(value));
        params.emplace_back(
            "edge",
            opts.Has("-rise") && !opts.Has("-fall")
                ? "rise"
                : (!opts.Has("-rise") && opts.Has("-fall") ? "fall"
                                                            : "both"));
        params.emplace_back(
            "delay",
            opts.Has("-min") && !opts.Has("-max")
                ? "min"
                : (!opts.Has("-min") && opts.Has("-max") ? "max" : "both"));
        std::vector<std::string> names;
        for (const std::string& t : targets) {
          names.push_back("ideal_transition_" + t);
        }
        return CommandResult::Ok(StoreTargets(
            ctx, db::ConstraintKind::kIdealTransition, targets, names,
            params));
      });

  add("group_path",
      "group_path -name <group> [-from <obj>...] [-to <obj>...] "
      "[-through <obj>...] [-weight <w>] [-critical_range <ns>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-name", 1, false},
                 {"-from", 1, true},
                 {"-to", 1, true},
                 {"-through", 1, true},
                 {"-weight", 1, false},
                 {"-critical_range", 1, false}},
                "group_path", opts)) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        if (!opts.Seen("-name") || opts.Value("-name")->empty()) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "group_path: option -name is "
                                     "required");
        }
        if (!opts.Seen("-from") && !opts.Seen("-to") &&
            !opts.Seen("-through")) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "group_path: at least one of -from, -to, -through is "
              "required");
        }
        if (!opts.Positionals().empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "group_path: unexpected positional argument: " +
                  opts.Positionals()[0]);
        }
        double weight = 0.0;
        double critical_range = 0.0;
        if (opts.Seen("-weight")) {
          if (auto bad = ParseOptDouble(opts, "-weight", "group_path",
                                        weight)) {
            return *bad;
          }
        }
        if (opts.Seen("-critical_range")) {
          if (auto bad = ParseOptDouble(opts, "-critical_range",
                                        "group_path", critical_range)) {
            return *bad;
          }
          if (critical_range < 0.0) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "group_path: -critical_range must not be negative, "
                "got: " +
                    FormatNum(critical_range));
          }
        }
        std::vector<std::string> froms;
        std::vector<std::string> tos;
        std::vector<std::string> throughs;
        if (auto bad = NormalizeTargets(opts.Values("-from"), "group_path",
                                        froms)) {
          return *bad;
        }
        if (auto bad = NormalizeTargets(opts.Values("-to"), "group_path",
                                        tos)) {
          return *bad;
        }
        if (auto bad = NormalizeTargets(opts.Values("-through"),
                                        "group_path", throughs)) {
          return *bad;
        }
        const auto join = [](const std::vector<std::string>& items) {
          std::string out;
          for (const std::string& item : items) {
            if (!out.empty()) out += " ";
            out += item;
          }
          return out;
        };
        std::vector<std::pair<std::string, std::string>> params;
        params.emplace_back("group", *opts.Value("-name"));
        // The endpoint role is stored so write_sdc can rebuild the
        // group_path line: the rows' targets are the -to, -through,
        // or -from list, and the role is not always recoverable from
        // the lists alone (a -from list can equal the -to list).
        params.emplace_back(
            "endpoint",
            !tos.empty()
                ? "to"
                : (!throughs.empty() ? "through" : "from"));
        if (!froms.empty()) params.emplace_back("from", join(froms));
        if (!throughs.empty()) {
          params.emplace_back("through", join(throughs));
        }
        if (opts.Seen("-weight")) {
          params.emplace_back("weight", FormatNum(weight));
        }
        if (opts.Seen("-critical_range")) {
          params.emplace_back("critical_range", FormatNum(critical_range));
        }
        // Rows bind to the endpoints: -to first, else -through,
        // else -from. The other lists are recorded, not bound.
        const std::vector<std::string>& endpoints =
            !tos.empty() ? tos : (!throughs.empty() ? throughs : froms);
        const std::string group = *opts.Value("-name");
        ReplaceNamed(ctx, db::ConstraintKind::kGroupPath, group);
        return CommandResult::Ok(StoreTargets(
            ctx, db::ConstraintKind::kGroupPath, endpoints,
            std::vector<std::string>(endpoints.size(), group), params));
      });

  add("set_max_time_borrow", "set_max_time_borrow <ns> <target>...",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(args, {}, "set_max_time_borrow",
                                         opts)) {
          return *bad;
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        if (opts.Positionals().size() < 2) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "usage: eos::set_max_time_borrow <ns> <target>...");
        }
        double value = 0.0;
        if (!ParseDouble(opts.Positionals()[0], value)) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_max_time_borrow: borrow needs a number, got: " +
                  opts.Positionals()[0]);
        }
        if (value < 0.0) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_max_time_borrow: borrow must not be negative, "
              "got: " +
                  FormatNum(value));
        }
        std::vector<std::string> raw_targets(
            opts.Positionals().begin() + 1, opts.Positionals().end());
        std::vector<std::string> targets;
        if (auto bad = NormalizeTargets(raw_targets, "set_max_time_borrow",
                                        targets)) {
          return *bad;
        }
        std::vector<std::pair<std::string, std::string>> params;
        params.emplace_back("value_ns", FormatNum(value));
        std::vector<std::string> names;
        for (const std::string& t : targets) {
          names.push_back("max_time_borrow_" + t);
        }
        return CommandResult::Ok(StoreTargets(
            ctx, db::ConstraintKind::kMaxTimeBorrow, targets, names,
            params));
      });

  // write_sdc [-compress] <path>
  //
  // Immediate unit 8, review item I5: the single-scenario form.
  // Emits the session constraint set as one SDC file: one line per
  // stored row (clocks and group_path merge by name, upstream
  // replace semantics), explicit get_* collections for every target,
  // no wildcards, no -of_objects. Needs no Liberty and no STA run:
  // it reads ctx.project.constraints, like check_timing. Never
  // mutates session state. A row the file cannot express is skipped
  // and reported, in the payload and in the file's own footer, never
  // dropped silently. With no design open every row skips as "no
  // design is open" and the file is the header plus the skip list.
  add("write_sdc",
      "write_sdc [-compress] <path>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args, {{"-compress", 0, false}}, "write_sdc", opts)) {
          return *bad;
        }
        if (opts.Positionals().size() != 1) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "write_sdc: expected exactly one output path; usage: "
              "write_sdc [-compress] <path>");
        }
        const std::string path = opts.Positionals()[0];
        const db::Design* design = ctx.project.design.has_value()
                                        ? &*ctx.project.design
                                        : nullptr;
        const WriteSdcPlan plan =
            WriteSdcPlanFor(design, ctx.project.constraints);
        std::vector<const db::Scenario*> scenarios;
        for (const db::Scenario* s :
             ctx.project.scenarios.ActiveScenarios()) {
          scenarios.push_back(s);
        }
        const std::string text = WriteSdcRender(plan, scenarios);
        std::string why;
        if (!WriteSdcWriteFile(path, text, opts.Has("-compress"), why)) {
          return CommandResult::Fail(Diag::kIoError, why);
        }
        std::string payload = "sdc=" + path +
                              " constraints=" +
                              std::to_string(plan.emitted) +
                              " skipped=" +
                              std::to_string(plan.skipped.size());
        for (const WriteSdcSkip& skip : plan.skipped) {
          payload += "\nskipped: " + skip.kind + " " + skip.name + " " +
                     skip.target + " (" + skip.reason + ")";
        }
        return CommandResult::Ok(payload);
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

  // ---- check_timing (Immediate unit 4, review item I1) -------------
  // The constraint-completeness gate over the session constraint
  // set and the session design. The default set runs unless
  // -exclude removes a check; -include adds optional checks.
  // -verbose adds one finding line per finding; -summary
  // selects the summary form, which is also the default, and
  // conflicts with -verbose. The payload is the summary form:
  // verdict= first, then one check= line per selected check in
  // table order, plus finding= lines under -verbose. The
  // command needs an open design but no Liberty and no STA run:
  // the gate runs before run_timing, where the guides put it.
  // It reads stored binding status, so run rebind_constraints
  // first when the design changed after the constraints were
  // authored. It never mutates session state.
  add("check_timing",
      "check_timing [-include <check>]... [-exclude <check>]... "
      "[-verbose] [-summary]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-include", 1, true},
                 {"-exclude", 1, true},
                 {"-verbose", 0, false},
                 {"-summary", 0, false}},
                "check_timing", opts)) {
          return *bad;
        }
        if (!opts.Positionals().empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "check_timing: unexpected positional argument: " +
                  opts.Positionals()[0]);
        }
        if (opts.Has("-verbose") && opts.Has("-summary")) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "check_timing: -verbose and -summary conflict");
        }
        if (auto bad = NeedDesign(ctx)) return *bad;
        // Values() returns by value: bind the vectors first, so the
        // set constructors take both iterators from the same object.
        const std::vector<std::string> include_values =
            opts.Values("-include");
        const std::vector<std::string> exclude_values =
            opts.Values("-exclude");
        const std::set<std::string> included(include_values.begin(),
                                             include_values.end());
        const std::set<std::string> excluded(exclude_values.begin(),
                                             exclude_values.end());
        for (const std::string& name : included) {
          if (FindCheckTimingCheck(name) == nullptr) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "check_timing: unknown check: " + name +
                    " (known checks: " + CheckTimingKnownChecks() + ")");
          }
        }
        for (const std::string& name : excluded) {
          const CheckTimingCheck* check = FindCheckTimingCheck(name);
          if (check == nullptr) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "check_timing: unknown check: " + name +
                    " (known checks: " + CheckTimingKnownChecks() + ")");
          }
          if (!check->is_default && included.count(name) == 0) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "check_timing: -exclude names a check that is not in "
                "the run: " +
                    name + " (include it first)");
          }
        }
        std::vector<const CheckTimingCheck*> run;
        for (const CheckTimingCheck& check : kCheckTimingChecks) {
          const bool selected =
              (check.is_default || included.count(check.name) != 0) &&
              excluded.count(check.name) == 0;
          if (selected) run.push_back(&check);
        }
        if (run.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "check_timing: no checks selected");
        }
        std::vector<CheckTimingOutcome> outcomes;
        for (const CheckTimingCheck* check : run) {
          if (check->run == nullptr) {
            outcomes.push_back(CheckTimingNotRun(*check));
          } else {
            outcomes.push_back(
                check->run(*ctx.project.design, ctx.project.constraints));
          }
        }
        bool any_fail = false;
        bool any_not_run = false;
        for (const CheckTimingOutcome& outcome : outcomes) {
          if (outcome.status == "fail") any_fail = true;
          if (outcome.status == "not_run") any_not_run = true;
        }
        const std::string verdict =
            any_fail ? "fail" : (any_not_run ? "incomplete" : "pass");
        std::string payload = "verdict=" + verdict;
        for (const CheckTimingOutcome& outcome : outcomes) {
          payload += "\ncheck=" + outcome.name +
                     " status=" + outcome.status + " findings=" +
                     std::to_string(outcome.findings.size());
          if (outcome.status == "not_run") {
            payload += " reason=" + outcome.reason;
          }
          if (opts.Has("-verbose")) {
            for (const std::string& finding : outcome.findings) {
              payload += "\nfinding=" + outcome.name + " " + finding;
            }
          }
        }
        return CommandResult::Ok(payload);
      });

  // ---- report_clock (Immediate unit 5, review item I2) -------------
  // The clock inventory over the session constraint set: the
  // Clock, Period, Waveform, Attrs, Sources table plus the
  // generated-clock table (Timing Constraints U-2022.12 ch. 2).
  // -skew adds a Skew column reporting the stored clock
  // uncertainty, which is "none" for every clock today because
  // EOS has no set_clock_uncertainty command yet; the gap is
  // named in the payload instead of hidden. Construction records
  // (bind_clock) carry no period, so they are listed separately
  // after the tables instead of in them. The command needs no
  // design beyond the constraint set and never mutates session
  // state.
  add("report_clock", "report_clock [-skew]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args, {{"-skew", 0, false}}, "report_clock", opts)) {
          return *bad;
        }
        if (!opts.Positionals().empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "report_clock: unexpected positional argument: " +
                  opts.Positionals()[0]);
        }
        const bool skew = opts.Has("-skew");
        const std::vector<ReportClockEntry> entries =
            ReportClockEntries(ctx.project.constraints);
        const std::vector<ReportGeneratedEntry> generated =
            ReportGeneratedEntries(ctx.project.constraints);
        std::vector<const db::Constraint*> records;
        for (const db::Constraint& c : ctx.project.constraints.all()) {
          if (c.kind == db::ConstraintKind::kClock &&
              !c.Param("period_ns").has_value()) {
            records.push_back(&c);
          }
        }
        if (entries.empty() && records.empty()) {
          return CommandResult::Ok("clocks=0");
        }
        std::string payload;
        // The attribute legend lists only the letters this report
        // uses. G is the vendor letter for a generated clock; u
        // is EOS-only for a clock whose target never bound. The
        // vendor letters p, d, and f never appear: EOS has no
        // propagated_clock, dont_touch_network, or fix_hold
        // command.
        const bool any_generated =
            std::any_of(entries.begin(), entries.end(),
                        [](const ReportClockEntry& e) {
                          return e.generated;
                        });
        const bool any_unresolved =
            std::any_of(entries.begin(), entries.end(),
                        [](const ReportClockEntry& e) {
                          return e.unresolved;
                        });
        if (any_generated || any_unresolved) {
          payload += "Attributes:\n";
          if (any_generated) payload += "     G - generated_clock\n";
          if (any_unresolved) {
            payload += "     u - unresolved target (EOS)\n";
          }
          payload += "\n";
        }
        if (!entries.empty()) {
          std::vector<std::vector<std::string>> rows;
          std::vector<std::string> header = {"Clock", "Period",
                                             "Waveform", "Attrs",
                                             "Sources"};
          if (skew) header.push_back("Skew");
          rows.push_back(header);
          for (const ReportClockEntry& entry : entries) {
            std::vector<std::string> row;
            row.push_back(entry.name);
            row.push_back(entry.period_ns.has_value()
                              ? FormatNum(*entry.period_ns)
                              : "-");
            row.push_back(entry.rise_ns.has_value() &&
                                  entry.fall_ns.has_value()
                              ? "{" + FormatNum(*entry.rise_ns) + " " +
                                    FormatNum(*entry.fall_ns) + "}"
                              : "-");
            std::string attrs;
            if (entry.generated) attrs += "G";
            if (entry.unresolved) attrs += "u";
            row.push_back(attrs);
            row.push_back(ReportClockBrace(entry.sources));
            if (skew) row.push_back("none");
            rows.push_back(std::move(row));
          }
          payload += ReportClockRenderTable(std::move(rows));
        }
        if (!generated.empty()) {
          if (!payload.empty()) payload += "\n";
          std::vector<std::vector<std::string>> rows;
          rows.push_back({"Generated Clock", "Master Source",
                          "Generated Source", "Master Clock",
                          "Waveform Modification"});
          for (const ReportGeneratedEntry& entry : generated) {
            rows.push_back({entry.name, entry.master_source,
                            ReportClockBrace(entry.sources),
                            entry.master_clock.empty()
                                ? "-"
                                : entry.master_clock,
                            entry.modification});
          }
          payload += ReportClockRenderTable(std::move(rows));
        }
        if (!records.empty()) {
          if (!payload.empty()) payload += "\n";
          payload += "Clock construction records (bind_clock, no "
                     "period):\n";
          std::sort(records.begin(), records.end(),
                    [](const db::Constraint* a, const db::Constraint* b) {
                      if (a->name != b->name) return a->name < b->name;
                      return a->target_spec < b->target_spec;
                    });
          std::vector<std::vector<std::string>> rows;
          rows.push_back({"Name", "Target"});
          for (const db::Constraint* c : records) {
            rows.push_back({c->name, c->target_spec});
          }
          payload += ReportClockRenderTable(std::move(rows));
        }
        if (skew && !entries.empty()) {
          payload += "skew: no clock uncertainty is defined for any "
                     "clock (set_clock_uncertainty is not an EOS "
                     "command yet)\n";
        }
        return CommandResult::Ok(payload);
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

  // read_lef and read_def (unit F0, the synth-to-PD bridge):
  // the LEF/DEF half of the DEF round trip. read_lef adds one
  // LEF file to the session database, creating it on the first
  // call; read_def loads the DEF write_def wrote (or any DEF
  // whose masters the loaded LEFs provide) into that database,
  // creating the chip and top block. Together with write_def
  // they carry a synthesized design into floorplan in one
  // session, which import_odb alone cannot do: it only reopens
  // a database produced elsewhere. A second design load is a
  // configuration failure, not a silent replace.
  add("read_lef", "read_lef <path>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 1, "read_lef <path>")) {
          return *bad;
        }
        if (auto bad = CheckReadable(args[0])) return *bad;
        if (ctx.timing.odb.has_value() &&
            ctx.timing.odb->has_top_block()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "design already loaded: eos::read_def or "
              "eos::import_odb already loaded a design");
        }
        if (!ctx.timing.odb.has_value()) {
          ctx.timing.odb.emplace();
        }
        const foundation::Status status =
            ctx.timing.odb->ReadLef(args[0]);
        if (!status.ok()) return FromStatus(status);
        return CommandResult::Ok(
            "path=" + args[0] +
            " masters=" +
            std::to_string(ctx.timing.odb->MasterCount()));
      });

  add("read_def", "read_def <path>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 1, "read_def <path>")) {
          return *bad;
        }
        if (auto bad = CheckReadable(args[0])) return *bad;
        if (!ctx.timing.odb.has_value() ||
            ctx.timing.odb->MasterCount() == 0) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no technology data loaded: eos::read_lef first");
        }
        if (ctx.timing.odb->has_top_block()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "design already loaded: eos::read_def or "
              "eos::import_odb already loaded a design");
        }
        const foundation::Status status =
            ctx.timing.odb->ReadDef(args[0]);
        if (!status.ok()) return FromStatus(status);
        return CommandResult::Ok(
            "path=" + args[0] +
            " design=" + ctx.timing.odb->TopBlockName() +
            " instances=" +
            std::to_string(ctx.timing.odb->InstanceCount()) +
            " nets=" + std::to_string(ctx.timing.odb->NetCount()) +
            " masters=" +
            std::to_string(ctx.timing.odb->MasterCount()));
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

  add("place", "place [-timing_driven]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        pd::PlaceOptions options;
        bool timing_seen = false;
        for (const std::string& flag : args) {
          if (flag == "-timing_driven") {
            if (timing_seen) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -timing_driven given twice");
            }
            timing_seen = true;
            options.timing_driven = true;
            continue;
          }
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "unknown place option: " + flag);
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
        pd::PlaceReport report;
        const foundation::Status status = pd::RunPlace(
            ctx.timing.odb->RawDatabase(), options, &report);
        if (!status.ok()) return FromStatus(status);
        std::string payload =
            "placed=" + std::to_string(report.instance_placed_count) +
            " total=" + std::to_string(report.instance_total_count) +
            " hpwl=" + std::to_string(report.hpwl_dbu) +
            " overlaps=" + std::to_string(report.overlap_count);
        // Placed STA: the session timing service on the placed
        // database, with estimated parasitics (the same path the
        // synthesis loop uses). Placement itself succeeds whether
        // or not the session is configured for timing; when it is
        // not, the payload says so with the reason instead of
        // inventing a number. The timing-service path writes the
        // placed database out as a Verilog netlist, so its
        // estimated parasitics are topology-based and do not yet
        // use placed coordinates; P6 extraction (SPEF) changes
        // that.
        bool from_odb = false;
        std::vector<analysis::SceneAnalysis> scenes;
        if (auto bad = BuildTimingScenes(ctx, from_odb, scenes)) {
          payload += " sta_slack_ns=unavailable (" + bad->message + ")";
        } else if (!from_odb) {
          payload += " sta_slack_ns=unavailable (timing source is "
                     "Verilog, not the placed database)";
        } else {
          analysis::TimingService service;
          const Result<std::vector<analysis::SceneResult>> analyzed =
              service.Analyze(*ctx.timing.odb, ctx.timing.sdc_path,
                              scenes);
          if (!analyzed.ok()) {
            payload += " sta_slack_ns=unavailable (" +
                       analyzed.status().message() + ")";
          } else {
            for (const analysis::SceneResult& result :
                 analyzed.value()) {
              payload += " scene=" + result.scenario.name +
                         " worst_slack_ns=" +
                         MetricText(result.metrics, "worst_slack_ns");
            }
          }
        }
        return CommandResult::Ok(payload);
      });

  add("cts",
      "cts -clock <net> [-buffer <cell>...] [-root_buffer <cell>] "
      "[-sink_buffer <cell>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        pd::CtsOptions options;
        bool clock_seen = false;
        bool root_seen = false;
        bool sink_seen = false;
        for (size_t i = 0; i < args.size(); ++i) {
          const std::string& flag = args[i];
          auto need_value = [&](const std::string& name)
              -> std::optional<CommandResult> {
            if (i + 1 >= args.size()) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option " + name + " needs a value");
            }
            return std::nullopt;
          };
          if (flag == "-clock") {
            if (clock_seen) {
              return CommandResult::Fail(Diag::kInvalidConfiguration,
                                         "option -clock given twice");
            }
            if (auto bad = need_value(flag)) return *bad;
            clock_seen = true;
            options.clock_nets.push_back(args[++i]);
            continue;
          }
          if (flag == "-buffer") {
            if (auto bad = need_value(flag)) return *bad;
            options.buffer_list.push_back(args[++i]);
            continue;
          }
          if (flag == "-root_buffer") {
            if (root_seen) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -root_buffer given twice");
            }
            if (auto bad = need_value(flag)) return *bad;
            root_seen = true;
            options.root_buffer = args[++i];
            continue;
          }
          if (flag == "-sink_buffer") {
            if (sink_seen) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -sink_buffer given twice");
            }
            if (auto bad = need_value(flag)) return *bad;
            sink_seen = true;
            options.sink_buffer = args[++i];
            continue;
          }
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "unknown cts option: " + flag);
        }
        if (!clock_seen) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "cts needs a clock net: cts -clock <net>");
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
        if (ctx.timing.liberty_by_corner.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no Liberty loaded: eos::read_liberty first");
        }
        // One Liberty library feeds the CTS characterization; prefer
        // the tt corner when present, else the first corner in map
        // order. The chosen corner is named in the payload.
        std::string liberty_corner;
        auto liberty_it = ctx.timing.liberty_by_corner.find("tt");
        if (liberty_it == ctx.timing.liberty_by_corner.end()) {
          liberty_it = ctx.timing.liberty_by_corner.begin();
        }
        liberty_corner = liberty_it->first;
        options.liberty_path = liberty_it->second;
        pd::CtsReport report;
        const foundation::Status status = pd::RunCts(
            ctx.timing.odb->RawDatabase(), options, &report);
        if (!status.ok()) return FromStatus(status);
        std::string payload =
            "roots=" + std::to_string(report.clock_root_count) +
            " buffers=" + std::to_string(report.buffers_inserted) +
            " sinks=" + std::to_string(report.sink_count) +
            " subnets=" + std::to_string(report.clock_subnet_count) +
            " skew_ns=unavailable (TritonCTS reports no ns skew; "
            "measure via post-CTS STA)"
            " insertion_delay_ns=unavailable (TritonCTS insertion "
            "delay is a length, not a time)"
            " liberty_corner=" +
            liberty_corner;
        // Post-CTS STA: the session timing service on the CTS
        // database, with estimated parasitics (the same path the
        // synthesis loop uses). CTS itself succeeds whether or not
        // the session is configured for timing; when it is not,
        // the payload says so with the reason instead of
        // inventing a number.
        bool from_odb = false;
        std::vector<analysis::SceneAnalysis> scenes;
        if (auto bad = BuildTimingScenes(ctx, from_odb, scenes)) {
          payload += " sta_slack_ns=unavailable (" + bad->message + ")";
        } else if (!from_odb) {
          payload += " sta_slack_ns=unavailable (timing source is "
                     "Verilog, not the CTS database)";
        } else {
          analysis::TimingService service;
          const Result<std::vector<analysis::SceneResult>> analyzed =
              service.Analyze(*ctx.timing.odb, ctx.timing.sdc_path,
                              scenes);
          if (!analyzed.ok()) {
            payload += " sta_slack_ns=unavailable (" +
                       analyzed.status().message() + ")";
          } else {
            for (const analysis::SceneResult& result :
                 analyzed.value()) {
              payload += " scene=" + result.scenario.name +
                         " worst_slack_ns=" +
                         MetricText(result.metrics, "worst_slack_ns");
            }
          }
        }
        return CommandResult::Ok(payload);
      });

  add("route",
      "route [-min_layer <n>] [-max_layer <n>] "
      "[-max_iterations <n>] [-bottom_layer <name>] "
      "[-top_layer <name>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        pd::RouteOptions options;
        bool min_seen = false;
        bool max_seen = false;
        bool iter_seen = false;
        bool bottom_seen = false;
        bool top_seen = false;
        for (size_t i = 0; i < args.size(); ++i) {
          const std::string& flag = args[i];
          int* slot = nullptr;
          bool* seen = nullptr;
          std::string* name_slot = nullptr;
          if (flag == "-min_layer") {
            slot = &options.min_routing_layer;
            seen = &min_seen;
          } else if (flag == "-max_layer") {
            slot = &options.max_routing_layer;
            seen = &max_seen;
          } else if (flag == "-max_iterations") {
            slot = &options.max_detailed_iterations;
            seen = &iter_seen;
          } else if (flag == "-bottom_layer") {
            name_slot = &options.bottom_routing_layer;
            seen = &bottom_seen;
          } else if (flag == "-top_layer") {
            name_slot = &options.top_routing_layer;
            seen = &top_seen;
          } else {
            return CommandResult::Fail(Diag::kInvalidConfiguration,
                                       "unknown route option: " + flag);
          }
          if (*seen) {
            return CommandResult::Fail(Diag::kInvalidConfiguration,
                                       "option " + flag + " given twice");
          }
          if (i + 1 >= args.size()) {
            return CommandResult::Fail(Diag::kInvalidConfiguration,
                                       "option " + flag + " needs a value");
          }
          if (name_slot != nullptr) {
            *name_slot = args[i + 1];
          } else if (!ParseCoord(args[i + 1], *slot)) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "option " + flag + " value is not an integer: " +
                    args[i + 1]);
          }
          *seen = true;
          ++i;
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
        if (ctx.timing.liberty_by_corner.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no Liberty loaded: eos::read_liberty first");
        }
        // One Liberty library feeds the engine's clock-net
        // detection; prefer the tt corner when present, else the
        // first corner in map order. The chosen corner is named in
        // the payload.
        std::string liberty_corner;
        auto liberty_it = ctx.timing.liberty_by_corner.find("tt");
        if (liberty_it == ctx.timing.liberty_by_corner.end()) {
          liberty_it = ctx.timing.liberty_by_corner.begin();
        }
        liberty_corner = liberty_it->first;
        options.liberty_path = liberty_it->second;
        pd::RouteReport report;
        const foundation::Status status = pd::RunRoute(
            ctx.timing.odb->RawDatabase(), options, &report);
        if (!status.ok()) return FromStatus(status);
        std::string payload =
            "routed=" + std::to_string(report.net_routed_count) +
            " total=" + std::to_string(report.net_total_count) +
            " guides=" + std::to_string(report.guide_count) +
            " overflow=" + std::to_string(report.total_overflow) +
            " violations=" + std::to_string(report.violation_count) +
            " detailed_iterations=" +
            std::to_string(report.detailed_iterations_used) +
            " detailed_bound=" +
            std::to_string(report.detailed_iteration_bound) +
            " stopped_at_bound=" +
            std::string(report.detailed_stopped_at_bound ? "true"
                                                         : "false");
        for (const pd::LayerCongestion& layer : report.per_layer) {
          payload += " layer_" + std::to_string(layer.layer) + "=" +
                     layer.name +
                     " capacity=" + std::to_string(layer.capacity) +
                     " usage=" + std::to_string(layer.usage) +
                     " overflow=" + std::to_string(layer.overflow);
        }
        payload += " liberty_corner=" + liberty_corner;
        // Routed STA: the session timing service on the routed
        // database, with estimated parasitics (the same path the
        // synthesis loop uses). Routing itself succeeds whether or
        // not the session is configured for timing; when it is
        // not, the payload says so with the reason instead of
        // inventing a number.
        bool from_odb = false;
        std::vector<analysis::SceneAnalysis> scenes;
        if (auto bad = BuildTimingScenes(ctx, from_odb, scenes)) {
          payload += " sta_slack_ns=unavailable (" + bad->message + ")";
        } else if (!from_odb) {
          payload += " sta_slack_ns=unavailable (timing source is "
                     "Verilog, not the routed database)";
        } else {
          analysis::TimingService service;
          const Result<std::vector<analysis::SceneResult>> analyzed =
              service.Analyze(*ctx.timing.odb, ctx.timing.sdc_path,
                              scenes);
          if (!analyzed.ok()) {
            payload += " sta_slack_ns=unavailable (" +
                       analyzed.status().message() + ")";
          } else {
            for (const analysis::SceneResult& result :
                 analyzed.value()) {
              payload += " scene=" + result.scenario.name +
                         " worst_slack_ns=" +
                         MetricText(result.metrics, "worst_slack_ns");
            }
          }
        }
        return CommandResult::Ok(payload);
      });

  add("extract_parasitics",
      "extract_parasitics <spef_path> [-model <path>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        // Argument shape first, mirroring the route command: exactly
        // one positional (the SPEF path) plus at most one -model
        // value. Unknown options, a duplicate -model, a missing
        // value, and a second positional all fail here, before any
        // session state is touched, so the registry-level tests can
        // exercise them on an empty context.
        std::string spef_path;
        std::string model_path;
        bool model_seen = false;
        for (size_t i = 0; i < args.size(); ++i) {
          const std::string& flag = args[i];
          if (flag == "-model") {
            if (model_seen) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -model given twice");
            }
            if (i + 1 >= args.size()) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -model needs a value");
            }
            model_seen = true;
            model_path = args[++i];
            continue;
          }
          if (!flag.empty() && flag[0] == '-') {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "unknown extract_parasitics option: " + flag);
          }
          if (!spef_path.empty()) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "usage: eos::extract_parasitics <spef_path> "
                "[-model <path>]");
          }
          spef_path = flag;
        }
        if (spef_path.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "usage: eos::extract_parasitics <spef_path> "
              "[-model <path>]");
        }
        if (!model_seen) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no extraction model file: pass -model <rules file>");
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
        pv::ExtractOptions options;
        options.spef_path = spef_path;
        options.model_path = model_path;
        pv::ExtractReport report;
        const foundation::Status status = pv::ExtractParasitics(
            ctx.timing.odb->RawDatabase(), options, &report);
        if (!status.ok()) return FromStatus(status);
        std::string payload =
            "nets=" + std::to_string(report.nets_extracted) +
            " rsegs=" + std::to_string(report.rseg_count) +
            " cap_nodes=" + std::to_string(report.cap_node_count) +
            " cc_segs=" + std::to_string(report.cc_seg_count) +
            " spef=" + report.spef_path;
        // Estimated STA on the routed database, the same way the
        // route command reports it: extraction itself succeeds
        // whether or not the session is configured for timing;
        // when it is not, the payload says so with the reason
        // instead of inventing a number. The extracted-STA
        // comparison is demonstrated by the unit tests, not faked
        // into this payload.
        bool from_odb = false;
        std::vector<analysis::SceneAnalysis> scenes;
        if (auto bad = BuildTimingScenes(ctx, from_odb, scenes)) {
          payload += " sta_slack_ns=unavailable (" + bad->message + ")";
        } else if (!from_odb) {
          payload += " sta_slack_ns=unavailable (timing source is "
                     "Verilog, not the routed database)";
        } else {
          analysis::TimingService service;
          const Result<std::vector<analysis::SceneResult>> analyzed =
              service.Analyze(*ctx.timing.odb, ctx.timing.sdc_path,
                              scenes);
          if (!analyzed.ok()) {
            payload += " sta_slack_ns=unavailable (" +
                       analyzed.status().message() + ")";
          } else {
            for (const analysis::SceneResult& result :
                 analyzed.value()) {
              payload += " scene=" + result.scenario.name +
                         " worst_slack_ns=" +
                         MetricText(result.metrics, "worst_slack_ns");
            }
          }
        }
        return CommandResult::Ok(payload);
      });

  add("finish", "finish [-pdn] [-fill_config <path>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        // Argument shape first, mirroring extract_parasitics: -pdn
        // is a bare flag, -fill_config takes one value, duplicates
        // and unknown options fail here, before any session state
        // is touched, so the registry-level tests can exercise
        // them on an empty context.
        pd::FinishOptions options;
        bool pdn_seen = false;
        bool fill_config_seen = false;
        for (size_t i = 0; i < args.size(); ++i) {
          const std::string& flag = args[i];
          if (flag == "-pdn") {
            if (pdn_seen) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -pdn given twice");
            }
            pdn_seen = true;
            options.with_pdn = true;
            continue;
          }
          if (flag == "-fill_config") {
            if (fill_config_seen) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -fill_config given twice");
            }
            if (i + 1 >= args.size()) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -fill_config needs a value");
            }
            fill_config_seen = true;
            options.fill_config_path = args[++i];
            continue;
          }
          if (!flag.empty() && flag[0] == '-') {
            return CommandResult::Fail(Diag::kInvalidConfiguration,
                                       "unknown finish option: " + flag);
          }
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "usage: eos::finish [-pdn] [-fill_config <path>]");
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
        pd::FinishReport report;
        const foundation::Status status = pd::RunFinish(
            ctx.timing.odb->RawDatabase(), options, &report);
        if (!status.ok()) return FromStatus(status);
        return CommandResult::Ok(
            "fillers=" + std::to_string(report.filler_count) +
            " grid_shapes=" + std::to_string(report.grid_shape_count) +
            " metal_fills=" + std::to_string(report.metal_fill_count) +
            " pdn=" + std::string(report.with_pdn ? "true" : "false"));
      });

  add("write_gds", "write_gds <path>",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        // Exactly one positional, mirroring extract_parasitics:
        // unknown options, a second positional, and a missing path
        // fail here, before any session state is touched.
        std::string path;
        for (const std::string& flag : args) {
          if (!flag.empty() && flag[0] == '-') {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "unknown write_gds option: " + flag);
          }
          if (!path.empty()) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "usage: eos::write_gds <path>");
          }
          path = flag;
        }
        if (path.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration, "usage: eos::write_gds <path>");
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
        pd::WriteGdsOptions options;
        options.path = path;
        pd::WriteGdsReport report;
        const foundation::Status status = pd::WriteGds(
            ctx.timing.odb->RawDatabase(), options, &report);
        if (!status.ok()) return FromStatus(status);
        return CommandResult::Ok(
            "gds=" + report.path +
            " size=" + std::to_string(report.size_bytes) +
            " instances=" + std::to_string(report.instance_count) +
            " structures=" + std::to_string(report.structure_count));
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
          AppendEngineLog(ctx);
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
          AppendEngineLog(ctx);
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
        AppendEngineLog(ctx);
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
        if (!parsed.ok()) {
          AppendEngineLog(ctx);
          return FromStatus(parsed.status());
        }
        std::string modules;
        bool found = false;
        for (const synth::YosysModuleSummary& module :
             parsed.value().modules) {
          if (!modules.empty()) modules += ",";
          modules += module.name;
          if (module.name == args[1]) found = true;
        }
        if (!found) {
          AppendEngineLog(ctx);
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
        AppendEngineLog(ctx);
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
          AppendEngineLog(ctx);
          return FromStatus(stats.status());
        }
        ctx.synth.elaborated = true;
        ctx.synth.elaborated_cells = stats.value().cell_count;
        ctx.synth.elaborated_wires = stats.value().wire_count;
        ctx.synth.elaborated_depth = stats.value().logic_depth;
        ctx.synth.generic_optimized = false;
        ctx.synth.generic_script.clear();
        ctx.synth.generic_verilog.clear();
        AppendEngineLog(ctx);
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

  // Reports the live design's elaboration issues for the
  // session's top module, as the GUI issues panel (panel
  // rework) consumes it. Read-only like synth_hierarchy: it
  // runs the check pass, which only reads the design, and
  // changes no session state. It fails fast when no elaborated
  // design exists. The payload names the netlist the issues
  // describe: after synth_generic the live design is the
  // generic netlist, so the issues describe that, not the
  // elaborated one. The check pass's own log lines reach the
  // engine-log drain like any other engine output, so the log
  // window shows the raw check lines alongside the issues.
  add("synth_issues", "synth_issues",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad = ExpectCount(args, 0, "synth_issues")) {
          return *bad;
        }
        if (!ctx.synth.elaborated) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no elaborated design: eos::elaborate first");
        }
        synth::YosysEngine engine;
        const foundation::Result<
            std::vector<synth::YosysEngine::YosysIssue>>
            issues = engine.Issues(ctx.synth.top_cell);
        AppendEngineLog(ctx);
        if (!issues.ok()) {
          return FromStatus(issues.status());
        }
        return CommandResult::Ok(IssuesText(
            issues.value(),
            ctx.synth.generic_optimized ? "generic" : "elaborated"));
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
              AppendEngineLog(ctx);
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
        AppendEngineLog(ctx);
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
        const std::clock_t map_start = std::clock();
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
              AppendEngineLog(ctx);
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
        ctx.synth.mapped_cpu_s = CpuSecondsSince(map_start);
        ctx.timing.verilog_path = mapped;
        ctx.timing.top_cell = ctx.synth.top_cell;
        AppendEngineLog(ctx);
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
        const std::clock_t opt_start = std::clock();
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
          ctx.synth.opt_cpu_s = CpuSecondsSince(opt_start);
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
        ctx.synth.opt_cpu_s = CpuSecondsSince(opt_start);
        ctx.synth.opt_still_violating = still_violating;
        ctx.timing.verilog_path = incumbent;
        AppendEngineLog(ctx);
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

  // write_def writes the mapped netlist as DEF (unit F0, the
  // synth-to-PD bridge): the mapped Verilog synth_map wrote is
  // parsed by the narrow reader above, checked against the cell
  // LEF, and written out with an unplaced component per gate, a
  // pin per top-level port, and one net per netlist net. The
  // DEF it writes is what read_def loads into the session
  // database; floorplan then runs on that database. The die
  // area is computed from the LEF macro sizes unless -die_area
  // gives one explicitly. The source netlist is the loop winner
  // when an opt run exists, the mapped netlist otherwise, the
  // same choice write_verilog makes.
  add("write_def",
      "write_def <path> -lef <path> -tlef <path> "
      "[-die_area <lx> <ly> <ux> <uy>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        std::string dest;
        std::string lef_path;
        std::string tlef_path;
        bool lef_seen = false;
        bool tlef_seen = false;
        std::optional<std::array<int, 4>> die_area;
        bool die_seen = false;
        for (std::size_t i = 0; i < args.size();) {
          const std::string& flag = args[i];
          if (flag == "-lef" || flag == "-tlef") {
            const bool is_lef = flag == "-lef";
            if ((is_lef && lef_seen) || (!is_lef && tlef_seen)) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option " + flag + " given twice");
            }
            if (i + 1 >= args.size()) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option " + flag + " needs a value");
            }
            if (auto bad = CheckReadable(args[i + 1])) return *bad;
            if (is_lef) {
              lef_seen = true;
              lef_path = args[i + 1];
            } else {
              tlef_seen = true;
              tlef_path = args[i + 1];
            }
            i += 2;
            continue;
          }
          if (flag == "-die_area") {
            if (die_seen) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -die_area given twice");
            }
            if (i + 4 >= args.size()) {
              return CommandResult::Fail(
                  Diag::kInvalidConfiguration,
                  "option -die_area needs four coordinates");
            }
            std::array<int, 4> area = {0, 0, 0, 0};
            for (int k = 0; k < 4; ++k) {
              if (!ParseCoord(args[i + 1 + k], area[k])) {
                return CommandResult::Fail(
                    Diag::kInvalidConfiguration,
                    "option -die_area coordinate is not an integer: " +
                        args[i + 1 + k]);
              }
            }
            die_seen = true;
            die_area = area;
            i += 5;
            continue;
          }
          if (flag.empty() || flag[0] == '-') {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "unknown write_def option: " + flag);
          }
          if (!dest.empty()) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "write_def takes one path");
          }
          dest = flag;
          ++i;
        }
        if (dest.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "usage: eos::write_def <path> -lef <path> -tlef <path> "
              "[-die_area <lx> <ly> <ux> <uy>]");
        }
        if (!lef_seen || !tlef_seen) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "write_def needs -lef and -tlef: the cell LEF for "
              "macro sizes and the tech LEF for database units");
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
        const Result<MappedNetlist> parsed =
            ParseMappedVerilog(source, cell_set);
        if (!parsed.ok()) return FromStatus(parsed.status());
        if (parsed.value().top != ctx.synth.top_cell) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "mapped netlist top '" + parsed.value().top +
                  "' is not the session top '" + ctx.synth.top_cell +
                  "'");
        }
        const Result<LefData> lef = ParseLefMacros(lef_path);
        if (!lef.ok()) return FromStatus(lef.status());
        const Result<int> dbu = ParseLefDbu(tlef_path);
        if (!dbu.ok()) return FromStatus(dbu.status());
        const Result<DefWriteResult> written = WriteMappedDef(
            dest, parsed.value(), lef.value(), dbu.value(), die_area);
        if (!written.ok()) return FromStatus(written.status());
        std::map<std::string, int> by_type;
        int total_cells = 0;
        ScanMappedCells(source, cell_set, &by_type, &total_cells);
        const std::string types = CellTypesText(by_type);
        auto area_text = [](const std::array<int, 4>& area) {
          return std::to_string(area[0]) + "," +
                 std::to_string(area[1]) + "," +
                 std::to_string(area[2]) + "," +
                 std::to_string(area[3]);
        };
        return CommandResult::Ok(
            "path=" + dest + " source=" + source_name +
            " design=" + parsed.value().top +
            " cells=" + std::to_string(total_cells) +
            " types=" + types +
            " die_area=" + area_text(written.value().die_area) +
            " nets=" + std::to_string(written.value().nets) +
            " pins=" +
            std::to_string(parsed.value().inputs.size() +
                           parsed.value().outputs.size()));
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
        if (!proof.ok()) {
          AppendEngineLog(ctx);
          return FromStatus(proof.status());
        }
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
          AppendEngineLog(ctx);
          return CommandResult::Fail(
              Diag::kProofUnknown,
              std::string("equivalence proof: ") + verdict + " for " +
                  positionals[0] + " vs " + positionals[1]);
        }
        AppendEngineLog(ctx);
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
        // The bottleneck capture (Immediate unit 9): when the
        // setting is on, this update saves the worst path of
        // every violating endpoint, one per endpoint, setup and
        // hold separately, so report_bottleneck reads saved data
        // instead of re-running timing. When the setting is off
        // the saved lists are cleared, so stale paths can never
        // serve a later report. The saved paths are the design
        // as of this update, exactly as last_results is.
        ctx.timing.last_capture_enabled = ctx.timing.bottleneck_capture;
        ctx.timing.last_bottleneck_setup_paths.clear();
        ctx.timing.last_bottleneck_hold_paths.clear();
        if (ctx.timing.bottleneck_capture) {
          const auto capture = [&](analysis::CheckKind kind,
                                   std::vector<analysis::ScenePathReports>&
                                       stored)
              -> std::optional<CommandResult> {
            std::size_t worst_count = 0;
            for (const analysis::SceneResult& result : analyzed.value()) {
              const std::vector<analysis::EndpointSlack>& violators =
                  kind == analysis::CheckKind::kSetup
                      ? result.setup_violating_endpoints
                      : result.hold_violating_endpoints;
              worst_count = std::max(worst_count, violators.size());
            }
            if (worst_count == 0) return std::nullopt;
            analysis::PathReportRequest request;
            request.kind = kind;
            request.max_paths = worst_count;
            request.slack_lesser_than = 0.0;
            const Result<std::vector<analysis::ScenePathReports>>
                captured =
                    from_odb
                        ? service.ReportPaths(*ctx.timing.odb,
                                              ctx.timing.sdc_path, scenes,
                                              request)
                        : service.ReportPaths(
                              analysis::VerilogDesign{ctx.timing.verilog_path,
                                                      ctx.timing.top_cell,
                                                      ctx.timing.sdc_path},
                              scenes, request);
            if (!captured.ok()) return FromStatus(captured.status());
            stored = captured.value();
            return std::nullopt;
          };
          if (auto bad = capture(analysis::CheckKind::kSetup,
                                 ctx.timing.last_bottleneck_setup_paths)) {
            return *bad;
          }
          if (auto bad = capture(analysis::CheckKind::kHold,
                                 ctx.timing.last_bottleneck_hold_paths)) {
            return *bad;
          }
        }
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

  // ---- report_constraint (Immediate unit 6, review item I3) ---------
  // The verdict-shaped constraint report, worst-violator tier
  // (Timing Constraints U-2022.12 ch. 2, PrimeTime report_constraint
  // in the singular DC spelling, chosen deliberately over the ICC2
  // plural). One row per constraint class (max_delay/setup,
  // min_delay/hold, max_transition, max_capacitance, max_fanout):
  // the worst violator, its slack, the cost, the weight and the
  // violating-pin count, plus the critical-range table of every
  // violating endpoint or pin per class, worst first. The cost is
  // the exact sum of the violations: -TNS for the timing classes
  // and the sum of -slack over the violating pins for the DRC
  // classes, so a DRC cost is never a worst magnitude wearing a
  // cost label. The weight is 1 for every class: EOS has no
  // constraint-weight command yet, and the gap is named in the
  // report instead of hidden. -verbose adds the full worst path
  // for the worst timing violator of each timing class and expands
  // the DRC critical-range rows to pin, value, limit and slack.
  // -all_violators is the tier 2 form (the full violator list with
  // required and actual path delays per violator) and waits on the
  // I8 path enumeration: passing it fails fast instead of silently
  // reporting the tier 1 form. The DRC classes are evaluated from
  // the timing engine's electrical checks (unit F5), not from EOS
  // constraint rows: there is no max_transition, max_fanout or
  // max_capacitance constraint kind, so a class with no limit at
  // all is reported as "no limit", never as clean. The command
  // runs the analysis itself for the selected scenes (it does not
  // reuse a previous run_timing: the session keeps no live
  // engine), and registers one report binding per scene through
  // session/reports, like run_timing and report_timing.
  add("report_constraint",
      "report_constraint [-verbose] [-all_violators] [-scene <name>] "
      "[-file <path>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-verbose", 0, false},
                 {"-all_violators", 0, false},
                 {"-scene", 1, false},
                 {"-file", 1, false}},
                "report_constraint", opts)) {
          return *bad;
        }
        if (!opts.Positionals().empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "report_constraint: unexpected positional argument: " +
                  opts.Positionals()[0]);
        }
        if (opts.Has("-all_violators")) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "report_constraint: -all_violators is the tier 2 form: "
              "it lists every violator with its required and actual "
              "path delays, and waits on the I8 path enumeration");
        }
        const bool verbose = opts.Has("-verbose");
        bool from_odb = false;
        std::vector<analysis::SceneAnalysis> scenes;
        if (auto bad = BuildTimingScenes(ctx, from_odb, scenes)) {
          return *bad;
        }
        if (opts.Seen("-scene")) {
          const std::string wanted = *opts.Value("-scene");
          std::vector<analysis::SceneAnalysis> selected;
          for (const analysis::SceneAnalysis& scene : scenes) {
            if (scene.scenario.name == wanted) {
              selected.push_back(scene);
            }
          }
          if (selected.empty()) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "unknown scenario: " + wanted);
          }
          scenes = std::move(selected);
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
        std::vector<ReportColumn> columns;
        for (const ReportColumn& candidate : kReportColumns) {
          columns.push_back(candidate);
        }
        // One class row: the class name, the worst violator's pin
        // ("-" when none violates), the worst slack and the cost
        // as display text, the violating-pin count, and whether
        // the class carries a limit at all (a DRC class with no
        // limit is reported as "no limit", never as clean).
        struct ClassRow {
          std::string name;
          std::string violator;
          std::string slack;
          std::string cost;
          std::size_t violations = 0;
          bool has_limit = true;
        };
        const auto slack_text = [](double slack) {
          return std::isfinite(slack) ? FormatNum(slack)
                                      : std::string("-");
        };
        const auto timing_row =
            [&slack_text](
                const char* name,
                const std::vector<analysis::EndpointSlack>& violators,
                const std::optional<double>& worst_slack,
                const std::optional<double>& total_negative_slack) {
              ClassRow row;
              row.name = name;
              row.violations = violators.size();
              row.violator = violators.empty() ? "-" : violators[0].pin;
              row.slack = violators.empty()
                              ? (worst_slack.has_value()
                                     ? slack_text(*worst_slack)
                                     : std::string("-"))
                              : FormatNum(violators[0].slack_ns);
              // -TNS is the cost and is non-negative, but negating
              // a zero TNS gives -0.0, which FormatNum renders as
              // "-0": normalize it to positive zero.
              double cost = 0.0;
              if (total_negative_slack.has_value()) {
                cost = -*total_negative_slack;
                if (cost == 0.0) cost = 0.0;
              }
              row.cost = total_negative_slack.has_value()
                             ? FormatNum(cost)
                             : std::string("-");
              return row;
            };
        const auto drc_row =
            [](const char* name,
               const analysis::ElectricalCheck& check,
               const std::vector<analysis::ElectricalViolation>&
                   violators) {
              ClassRow row;
              row.name = name;
              row.has_limit = check.has_limit;
              row.violations = violators.size();
              if (!check.has_limit) {
                row.violator = "-";
                row.slack = "-";
                row.cost = "-";
                return row;
              }
              row.violator = violators.empty() ? "-" : violators[0].pin;
              row.slack = violators.empty()
                              ? FormatNum(check.worst_slack)
                              : FormatNum(violators[0].slack);
              double cost = 0.0;
              for (const analysis::ElectricalViolation& violation :
                   violators) {
                cost += -violation.slack;
              }
              row.cost = FormatNum(cost);
              return row;
            };
        // One report binding per scene, registered through
        // session/reports at the current ledger revision. The
        // analysis completed, so the report's proof is pass: the
        // class rows are the measured result, whatever their sign.
        std::string payload = std::string("source=") +
                              (from_odb ? "odb" : "verilog") +
                              " scenes=" +
                              std::to_string(analyzed.value().size()) +
                              " revision=" +
                              std::to_string(
                                  ctx.project.ledger.current_revision());
        std::string text;
        for (std::size_t i = 0; i < analyzed.value().size(); ++i) {
          const analysis::SceneAnalysis& scene = scenes[i];
          const analysis::SceneResult& result = analyzed.value()[i];
          const std::string report =
              "constraint_report_" + result.scenario.name;
          const Result<session::ArtifactId> artifact =
              ctx.reports.RegisterReport(ctx.project, report,
                                         session::ProofStatus::kPass,
                                         "NLDM");
          if (!artifact.ok()) return FromStatus(artifact.status());
          const auto metric = [&result](const char* name) {
            const auto it = result.metrics.find(name);
            return it == result.metrics.end()
                       ? std::optional<double>{}
                       : std::optional<double>{it->second.value};
          };
          const std::optional<double> setup_worst =
              metric("worst_slack_ns");
          const std::optional<double> setup_tns =
              metric("total_negative_slack_ns");
          const std::optional<double> hold_worst =
              metric("worst_hold_slack_ns");
          const std::optional<double> hold_tns =
              metric("total_negative_hold_slack_ns");
          std::vector<ClassRow> rows;
          rows.push_back(timing_row("max_delay",
                                    result.setup_violating_endpoints,
                                    setup_worst, setup_tns));
          rows.push_back(timing_row("min_delay",
                                    result.hold_violating_endpoints,
                                    hold_worst, hold_tns));
          rows.push_back(drc_row("max_transition",
                                 result.max_transition_check,
                                 result.max_transition_violators));
          rows.push_back(drc_row("max_capacitance",
                                 result.max_capacitance_check,
                                 result.max_capacitance_violators));
          rows.push_back(drc_row("max_fanout", result.max_fanout_check,
                                 result.max_fanout_violators));
          const std::size_t total_violations =
              result.setup_violating_endpoints.size() +
              result.hold_violating_endpoints.size() +
              result.max_transition_violators.size() +
              result.max_capacitance_violators.size() +
              result.max_fanout_violators.size();
          payload += "\nscene=" + result.scenario.name +
                     " violations=" + std::to_string(total_violations) +
                     " worst_slack_ns=" +
                     (setup_worst.has_value() &&
                              std::isfinite(*setup_worst)
                          ? FormatNum(*setup_worst)
                          : std::string("none")) +
                     " report=" + report + " artifact=" +
                     std::to_string(artifact.value());
          for (const ClassRow& row : rows) {
            payload += "\nscene=" + result.scenario.name +
                       " class=" + row.name +
                       " violator=" + row.violator;
            if (row.has_limit) {
              payload += " slack=" +
                         (row.slack == "-" ? std::string("none")
                                           : row.slack) +
                         " cost=" + row.cost;
            } else {
              payload += " limit=none";
            }
            payload += " weight=1 violations=" +
                       std::to_string(row.violations);
          }
          text += "\nScene: " + result.scenario.name + "\n";
          std::vector<std::vector<std::string>> summary;
          summary.push_back({"Constraint", "Worst Violator", "Slack",
                             "Cost", "Weight", "Violations"});
          for (const ClassRow& row : rows) {
            summary.push_back({row.name, row.violator, row.slack,
                               row.cost, "1",
                               std::to_string(row.violations)});
          }
          text += ReportClockRenderTable(std::move(summary));
          text += "Units: slack and cost in ns for max_delay, "
                  "min_delay and max_transition; in pF for "
                  "max_capacitance; in fanout load for max_fanout. "
                  "Weight is 1 for every class: EOS has no "
                  "constraint-weight command yet.\n";
          for (const ClassRow& row : rows) {
            if (!row.has_limit) {
              text += row.name + ": no limit defined for this design.\n";
            }
          }
          if (setup_worst.has_value() && !std::isfinite(*setup_worst)) {
            text += "max_delay: no constrained endpoints in this "
                    "scene.\n";
          }
          if (hold_worst.has_value() && !std::isfinite(*hold_worst)) {
            text += "min_delay: no constrained endpoints in this "
                    "scene.\n";
          }
          const auto timing_range =
              [&text](const char* name,
                      const std::vector<analysis::EndpointSlack>&
                          violators) {
                if (violators.empty()) return;
                text += "\nCritical range (" + std::string(name) +
                        "): endpoints with slack below 0, worst "
                        "first:\n";
                std::vector<std::vector<std::string>> table;
                table.push_back({"Endpoint", "Slack (ns)"});
                for (const analysis::EndpointSlack& endpoint :
                     violators) {
                  table.push_back(
                      {endpoint.pin, FormatNum(endpoint.slack_ns)});
                }
                text += ReportClockRenderTable(std::move(table));
              };
          timing_range("max_delay", result.setup_violating_endpoints);
          timing_range("min_delay", result.hold_violating_endpoints);
          const auto drc_range =
              [&text, verbose](
                  const char* name, const char* unit,
                  const std::vector<analysis::ElectricalViolation>&
                      violators) {
                if (violators.empty()) return;
                text += "\nCritical range (" + std::string(name) +
                        "): pins with slack below 0, worst first:\n";
                std::vector<std::vector<std::string>> table;
                if (verbose) {
                  table.push_back({"Pin",
                                   std::string("Value (") + unit + ")",
                                   std::string("Limit (") + unit + ")",
                                   std::string("Slack (") + unit + ")"});
                  for (const analysis::ElectricalViolation& violation :
                       violators) {
                    table.push_back({violation.pin,
                                     FormatNum(violation.value),
                                     FormatNum(violation.limit),
                                     FormatNum(violation.slack)});
                  }
                } else {
                  table.push_back(
                      {"Pin", std::string("Slack (") + unit + ")"});
                  for (const analysis::ElectricalViolation& violation :
                       violators) {
                    table.push_back(
                        {violation.pin, FormatNum(violation.slack)});
                  }
                }
                text += ReportClockRenderTable(std::move(table));
              };
          drc_range("max_transition", "ns",
                    result.max_transition_violators);
          drc_range("max_capacitance", "pF",
                    result.max_capacitance_violators);
          drc_range("max_fanout", "fanout", result.max_fanout_violators);
          if (verbose) {
            const auto render_worst_path =
                [&](analysis::CheckKind kind,
                    const std::vector<analysis::EndpointSlack>& violators,
                    const char* class_name)
                -> std::optional<CommandResult> {
              if (violators.empty()) return std::nullopt;
              analysis::PathReportRequest request;
              request.kind = kind;
              request.endpoint = violators[0].pin;
              request.max_paths = 1;
              const std::vector<analysis::SceneAnalysis> one_scene{
                  scene};
              const Result<std::vector<analysis::ScenePathReports>>
                  reported =
                      from_odb
                          ? service.ReportPaths(
                                *ctx.timing.odb, ctx.timing.sdc_path,
                                one_scene, request)
                          : service.ReportPaths(
                                analysis::VerilogDesign{
                                    ctx.timing.verilog_path,
                                    ctx.timing.top_cell,
                                    ctx.timing.sdc_path},
                                one_scene, request);
              if (!reported.ok()) return FromStatus(reported.status());
              if (!reported.value().empty() &&
                  !reported.value()[0].paths.empty()) {
                text += "\nWorst " + std::string(class_name) +
                        " path (" + violators[0].pin + "):\n";
                text += RenderPathReport(reported.value()[0].paths[0],
                                         columns, "full", 3);
              }
              return std::nullopt;
            };
            if (auto bad = render_worst_path(
                    analysis::CheckKind::kSetup,
                    result.setup_violating_endpoints, "max_delay")) {
              return *bad;
            }
            if (auto bad = render_worst_path(
                    analysis::CheckKind::kHold,
                    result.hold_violating_endpoints, "min_delay")) {
              return *bad;
            }
          }
        }
        if (opts.Seen("-file")) {
          const std::string file = *opts.Value("-file");
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

  // ---- report_analysis_coverage (Immediate unit 7, review item I4)
  // ------
  // The coverage report: per check type, how many constrained pins
  // the timing engine actually tested and how many it left
  // untested, plus the per-pin detail form (PrimeTime T-2022.03
  // ch. 18/19 shape: tested/untested counts and percents per check
  // type, and the untested reason per pin). The check types EOS
  // can express and the engine evaluates: setup, hold, out_setup,
  // out_hold, recovery, removal, clock_gating_setup,
  // clock_gating_hold, no-change, min_pulse_width. A type EOS
  // cannot express (min_period, max_skew, clock_separation) is
  // never reported as untested: it is named on the "no checks
  // defined" line instead, and the full included/excluded table
  // lives in docs/report-analysis-coverage.md. Tested means a
  // constrained path end (or a min-pulse-width check) exists for
  // the pin; untested reasons are constant_disabled,
  // no_clock, no_startpoint_clock and no_paths, in that
  // precedence. -status_details tested|untested and -check_type
  // filter the detail table; -exclude_untested drops the untested
  // rows from the detail table only, the summary still counts
  // them. The command runs the analysis itself for the selected
  // scenes (it does not reuse a previous run_timing: the session
  // keeps no live engine), and registers one report binding per
  // scene through session/reports, like report_constraint.
  add("report_analysis_coverage",
      "report_analysis_coverage [-status_details tested|untested] "
      "[-check_type <type>] [-exclude_untested] [-scene <name>] "
      "[-file <path>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        static const char* kCheckTypes[] = {
            "setup",           "hold",
            "out_setup",       "out_hold",
            "recovery",        "removal",
            "clock_gating_setup", "clock_gating_hold",
            "no-change",       "min_pulse_width",
        };
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-status_details", 1, false},
                 {"-check_type", 1, false},
                 {"-exclude_untested", 0, false},
                 {"-scene", 1, false},
                 {"-file", 1, false}},
                "report_analysis_coverage", opts)) {
          return *bad;
        }
        if (!opts.Positionals().empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "report_analysis_coverage: unexpected positional "
              "argument: " +
                  opts.Positionals()[0]);
        }
        std::optional<std::string> status_details;
        if (opts.Seen("-status_details")) {
          status_details = *opts.Value("-status_details");
          if (*status_details != "tested" &&
              *status_details != "untested") {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "report_analysis_coverage: -status_details must be "
                "tested or untested, got: " +
                    *status_details);
          }
        }
        std::optional<std::string> check_type;
        if (opts.Seen("-check_type")) {
          check_type = *opts.Value("-check_type");
          bool known = false;
          for (const char* type : kCheckTypes) {
            if (*check_type == type) known = true;
          }
          if (!known) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "report_analysis_coverage: unknown check type: " +
                    *check_type +
                    " (setup, hold, out_setup, out_hold, recovery, "
                    "removal, clock_gating_setup, clock_gating_hold, "
                    "no-change, min_pulse_width)");
          }
        }
        const bool exclude_untested = opts.Has("-exclude_untested");
        bool from_odb = false;
        std::vector<analysis::SceneAnalysis> scenes;
        if (auto bad = BuildTimingScenes(ctx, from_odb, scenes)) {
          return *bad;
        }
        if (opts.Seen("-scene")) {
          const std::string wanted = *opts.Value("-scene");
          std::vector<analysis::SceneAnalysis> selected;
          for (const analysis::SceneAnalysis& scene : scenes) {
            if (scene.scenario.name == wanted) {
              selected.push_back(scene);
            }
          }
          if (selected.empty()) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "unknown scenario: " + wanted);
          }
          scenes = std::move(selected);
        }
        analysis::TimingService service;
        const Result<std::vector<analysis::CoverageResult>> analyzed =
            from_odb
                ? service.AnalyzeCoverage(*ctx.timing.odb,
                                          ctx.timing.sdc_path, scenes)
                : service.AnalyzeCoverage(
                      analysis::VerilogDesign{ctx.timing.verilog_path,
                                              ctx.timing.top_cell,
                                              ctx.timing.sdc_path},
                      scenes);
        if (!analyzed.ok()) return FromStatus(analyzed.status());
        // One report binding per scene, registered through
        // session/reports at the current ledger revision. The
        // analysis completed, so the report's proof is pass: the
        // coverage counts are the measured result, whatever their
        // split.
        std::string payload = std::string("source=") +
                              (from_odb ? "odb" : "verilog") +
                              " scenes=" +
                              std::to_string(analyzed.value().size()) +
                              " revision=" +
                              std::to_string(
                                  ctx.project.ledger.current_revision());
        std::string text;
        for (std::size_t i = 0; i < analyzed.value().size(); ++i) {
          const analysis::SceneAnalysis& scene = scenes[i];
          const analysis::CoverageResult& result = analyzed.value()[i];
          const std::string report =
              "coverage_report_" + result.scenario.name;
          const Result<session::ArtifactId> artifact =
              ctx.reports.RegisterReport(ctx.project, report,
                                         session::ProofStatus::kPass,
                                         "NLDM");
          if (!artifact.ok()) return FromStatus(artifact.status());
          // Per-type counts in the fixed type order. The detail
          // rows arrive sorted by (pin, check type), so a row's
          // type index is found by a linear scan of ten names.
          struct TypeCounts {
            std::size_t tested = 0;
            std::size_t untested = 0;
          };
          std::vector<TypeCounts> counts(
              sizeof(kCheckTypes) / sizeof(kCheckTypes[0]));
          std::size_t total_tested = 0;
          std::size_t total_untested = 0;
          for (const analysis::CoveragePin& pin : result.pins) {
            for (std::size_t t = 0; t < counts.size(); ++t) {
              if (pin.check_type == kCheckTypes[t]) {
                if (pin.tested) {
                  ++counts[t].tested;
                  ++total_tested;
                } else {
                  ++counts[t].untested;
                  ++total_untested;
                }
                break;
              }
            }
          }
          payload += "\nscene=" + result.scenario.name +
                     " checks=" +
                     std::to_string(result.pins.size()) +
                     " tested=" + std::to_string(total_tested) +
                     " untested=" + std::to_string(total_untested) +
                     " report=" + report + " artifact=" +
                     std::to_string(artifact.value());
          for (std::size_t t = 0; t < counts.size(); ++t) {
            const std::size_t total =
                counts[t].tested + counts[t].untested;
            if (total == 0) continue;
            const double percent =
                static_cast<double>(counts[t].tested) * 100.0 /
                static_cast<double>(total);
            payload += "\nscene=" + result.scenario.name +
                       " type=" + kCheckTypes[t] +
                       " tested=" + std::to_string(counts[t].tested) +
                       " untested=" +
                       std::to_string(counts[t].untested) +
                       " percent=" + FormatFixed(percent, 1);
          }
          text += "\nScene: " + result.scenario.name + "\n";
          std::vector<std::vector<std::string>> summary;
          summary.push_back({"Check Type", "Tested", "Untested",
                             "Tested %"});
          for (std::size_t t = 0; t < counts.size(); ++t) {
            const std::size_t total =
                counts[t].tested + counts[t].untested;
            if (total == 0) continue;
            const double percent =
                static_cast<double>(counts[t].tested) * 100.0 /
                static_cast<double>(total);
            summary.push_back({kCheckTypes[t],
                               std::to_string(counts[t].tested),
                               std::to_string(counts[t].untested),
                               FormatFixed(percent, 1)});
          }
          text += ReportClockRenderTable(std::move(summary));
          std::string no_checks;
          for (std::size_t t = 0; t < counts.size(); ++t) {
            if (counts[t].tested + counts[t].untested == 0) {
              if (!no_checks.empty()) no_checks += ", ";
              no_checks += kCheckTypes[t];
            }
          }
          if (!no_checks.empty()) {
            text += "No checks defined: " + no_checks + ".\n";
          }
          // The detail table honors the filters; the summary
          // above still counts every defined check.
          const auto detail_shown =
              [&](const analysis::CoveragePin& pin) {
                if (check_type.has_value() &&
                    pin.check_type != *check_type) {
                  return false;
                }
                if (status_details.has_value() &&
                    ((pin.tested && *status_details != "tested") ||
                     (!pin.tested && *status_details != "untested"))) {
                  return false;
                }
                if (exclude_untested && !pin.tested) return false;
                return true;
              };
          std::vector<std::vector<std::string>> detail;
          detail.push_back({"Constrained Pin", "Related Pin",
                            "Check Type", "Untested Reason"});
          for (const analysis::CoveragePin& pin : result.pins) {
            if (!detail_shown(pin)) continue;
            detail.push_back({pin.pin, pin.related_pin,
                              pin.check_type,
                              pin.tested ? "-"
                                         : pin.untested_reason});
            payload += "\nscene=" + result.scenario.name +
                       " pin=" + pin.pin +
                       " related=" + pin.related_pin +
                       " type=" + pin.check_type +
                       " status=" + (pin.tested ? "tested" : "untested") +
                       " reason=" +
                       (pin.tested ? "-" : pin.untested_reason);
          }
          text += ReportClockRenderTable(std::move(detail));
        }
        if (opts.Seen("-file")) {
          const std::string file = *opts.Value("-file");
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

  // ---- set_bottleneck_capture (Immediate unit 9, review item I9)
  // The bottleneck capture setting: PrimeTime's
  // timing_save_pin_arrival_and_slack as an EOS command, because
  // EOS has no variable mechanism. It must be set before the
  // timing update: run_timing saves the violating-endpoint worst
  // paths only while it is on, and report_bottleneck reads the
  // saved paths. Setting it after run_timing has no effect until
  // run_timing runs again.
  add("set_bottleneck_capture", "set_bottleneck_capture on|off",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (auto bad =
                ExpectCount(args, 1, "set_bottleneck_capture on|off")) {
          return *bad;
        }
        if (args[0] != "on" && args[0] != "off") {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "set_bottleneck_capture takes on or off, got: " + args[0]);
        }
        ctx.timing.bottleneck_capture = args[0] == "on";
        return CommandResult::Ok(std::string("bottleneck_capture=") +
                                 args[0]);
      });

  // ---- report_qor (Immediate unit 9, review item I9)
  // The quality-of-results report: per-group WNS/TNS and
  // violating-path counts for setup and hold, the cell census and
  // area, the DRC summary, and the compile CPU. TNS is reported
  // both ways and both numbers are shown, because they differ
  // when an endpoint belongs to several groups: the union TNS
  // counts each violating endpoint once (and equals the
  // run_timing total_negative_slack_ns by construction), the
  // every-group TNS counts it once per group it belongs to.
  // Compile CPU is the process CPU time of the synthesis runs in
  // this session (synth_map plus synth_opt); it is n/a when
  // neither has run.
  add("report_qor",
      "report_qor [-scene <name>] [-file <path>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-scene", 1, false}, {"-file", 1, false}},
                "report_qor", opts)) {
          return *bad;
        }
        if (!opts.Positionals().empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "report_qor: unexpected positional argument: " +
                  opts.Positionals()[0]);
        }
        bool from_odb = false;
        std::vector<analysis::SceneAnalysis> scenes;
        if (auto bad = BuildTimingScenes(ctx, from_odb, scenes)) {
          return *bad;
        }
        std::vector<analysis::SceneAnalysis> selected = scenes;
        if (opts.Seen("-scene")) {
          const std::string name = *opts.Value("-scene");
          std::vector<analysis::SceneAnalysis> picked;
          for (const analysis::SceneAnalysis& scene : scenes) {
            if (scene.scenario.name == name) picked.push_back(scene);
          }
          if (picked.empty()) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "report_qor: unknown scenario: " + name);
          }
          selected = std::move(picked);
        }
        analysis::TimingService service;
        const Result<std::vector<analysis::QorResult>> analyzed =
            from_odb
                ? service.AnalyzeQor(*ctx.timing.odb, ctx.timing.sdc_path,
                                     selected)
                : service.AnalyzeQor(
                      analysis::VerilogDesign{ctx.timing.verilog_path,
                                              ctx.timing.top_cell,
                                              ctx.timing.sdc_path},
                      selected);
        if (!analyzed.ok()) return FromStatus(analyzed.status());
        std::string text;
        std::string payload;
        for (const analysis::QorResult& result : analyzed.value()) {
          const std::string report = "qor_report_" + result.scenario.name;
          const Result<session::ArtifactId> artifact = ctx.reports.RegisterReport(
              ctx.project, report, session::ProofStatus::kPass, "NLDM");
          if (!artifact.ok()) return FromStatus(artifact.status());
          // Per-group WNS/TNS and violating-path count: WNS is the
          // worst slack over every endpoint in the group, TNS the
          // sum of the negative ones.
          const auto group_tns = [](const analysis::PathGroupResult& group) {
            double wns = 0.0;
            double tns = 0.0;
            std::size_t violating = 0;
            bool first = true;
            for (const analysis::EndpointSlack& endpoint :
                 group.endpoints) {
              if (first || endpoint.slack_ns < wns) {
                wns = endpoint.slack_ns;
                first = false;
              }
              if (endpoint.slack_ns < 0.0) {
                tns += endpoint.slack_ns;
                ++violating;
              }
            }
            return std::make_tuple(wns, tns, violating);
          };
          // The union TNS counts each violating endpoint once,
          // however many groups it belongs to; the slack is a
          // property of the endpoint, so it is the same in every
          // group, and the minimum guards against any drift.
          const auto union_tns =
              [](const std::vector<analysis::PathGroupResult>& groups) {
                std::map<std::string, double> by_pin;
                for (const analysis::PathGroupResult& group : groups) {
                  for (const analysis::EndpointSlack& endpoint :
                       group.endpoints) {
                    const auto [it, inserted] = by_pin.try_emplace(
                        endpoint.pin, endpoint.slack_ns);
                    if (!inserted) {
                      it->second =
                          std::min(it->second, endpoint.slack_ns);
                    }
                  }
                }
                double tns = 0.0;
                std::size_t violating = 0;
                for (const auto& entry : by_pin) {
                  const double slack = entry.second;
                  if (slack < 0.0) {
                    tns += slack;
                    ++violating;
                  }
                }
                return std::make_pair(tns, violating);
              };
          const auto [setup_union_tns, setup_union_violating] =
              union_tns(result.setup_groups);
          const auto [hold_union_tns, hold_union_violating] =
              union_tns(result.hold_groups);
          double setup_every_tns = 0.0;
          std::size_t setup_every_violating = 0;
          for (const analysis::PathGroupResult& group :
               result.setup_groups) {
            const auto [wns, tns, violating] = group_tns(group);
            setup_every_tns += tns;
            setup_every_violating += violating;
          }
          double hold_every_tns = 0.0;
          std::size_t hold_every_violating = 0;
          for (const analysis::PathGroupResult& group : result.hold_groups) {
            const auto [wns, tns, violating] = group_tns(group);
            hold_every_tns += tns;
            hold_every_violating += violating;
          }
          const double compile_cpu =
              (ctx.synth.mapped_cpu_s >= 0.0 ? ctx.synth.mapped_cpu_s
                                             : 0.0) +
              (ctx.synth.opt_cpu_s >= 0.0 ? ctx.synth.opt_cpu_s : 0.0);
          const bool compile_recorded =
              ctx.synth.mapped_cpu_s >= 0.0 || ctx.synth.opt_cpu_s >= 0.0;
          payload += "\nscene=" + result.scenario.name +
                     " setup_groups=" +
                     std::to_string(result.setup_groups.size()) +
                     " hold_groups=" +
                     std::to_string(result.hold_groups.size()) +
                     " union_tns=" + FormatNum(setup_union_tns) +
                     " union_endpoints=" +
                     std::to_string(setup_union_violating) +
                     " every_group_tns=" + FormatNum(setup_every_tns) +
                     " every_group_endpoints=" +
                     std::to_string(setup_every_violating) +
                     " hold_union_tns=" + FormatNum(hold_union_tns) +
                     " hold_union_endpoints=" +
                     std::to_string(hold_union_violating) +
                     " hold_every_group_tns=" +
                     FormatNum(hold_every_tns) +
                     " hold_every_group_endpoints=" +
                     std::to_string(hold_every_violating) +
                     " cells=" + std::to_string(result.total_cells) +
                     " area=" + FormatNum(result.total_area) +
                     " compile_cpu_s=" +
                     (compile_recorded ? FormatNum(compile_cpu) : "n/a") +
                     " report=" + report +
                     " artifact=" + std::to_string(artifact.value());
          for (const analysis::PathGroupResult& group :
               result.setup_groups) {
            const auto [wns, tns, violating] = group_tns(group);
            payload += "\nscene=" + result.scenario.name +
                       " group=" + group.name +
                       " check=setup wns=" + FormatNum(wns) +
                       " tns=" + FormatNum(tns) +
                       " violating_paths=" + std::to_string(violating);
          }
          for (const analysis::PathGroupResult& group : result.hold_groups) {
            const auto [wns, tns, violating] = group_tns(group);
            payload += "\nscene=" + result.scenario.name +
                       " group=" + group.name +
                       " check=hold wns=" + FormatNum(wns) +
                       " tns=" + FormatNum(tns) +
                       " violating_paths=" + std::to_string(violating);
          }
          for (const analysis::CellTypeRow& row : result.cells) {
            payload += "\nscene=" + result.scenario.name +
                       " cell=" + row.cell +
                       " count=" + std::to_string(row.count) +
                       " area=" + FormatNum(row.area);
          }
          const auto drc_row = [&](const char* name,
                                   const analysis::ElectricalCheck& check) {
            payload += "\nscene=" + result.scenario.name +
                       " drc=" + name +
                       " violations=" +
                       std::to_string(check.violation_count) +
                       " worst_slack=" +
                       (check.has_limit ? FormatNum(check.worst_slack)
                                        : "no limit");
          };
          drc_row("max_transition", result.max_transition_check);
          drc_row("max_capacitance", result.max_capacitance_check);
          drc_row("max_fanout", result.max_fanout_check);
          text += "\nScene: " + result.scenario.name + "\n";
          const auto group_table = [&](const char* title,
                                       const std::vector<
                                           analysis::PathGroupResult>& groups,
                                       double union_tns_value,
                                       std::size_t union_violating,
                                       double every_tns_value,
                                       std::size_t every_violating) {
            text += title;
            std::vector<std::vector<std::string>> table;
            table.push_back(
                {"Group", "WNS (ns)", "TNS (ns)", "Violating paths"});
            for (const analysis::PathGroupResult& group : groups) {
              const auto [wns, tns, violating] = group_tns(group);
              table.push_back({group.name, FormatNum(wns), FormatNum(tns),
                               std::to_string(violating)});
            }
            if (groups.empty()) {
              table.push_back({"(no path groups)", "-", "-", "-"});
            }
            text += ReportClockRenderTable(std::move(table));
            text += "TNS (union over " +
                    std::to_string(union_violating) +
                    " endpoints): " + FormatNum(union_tns_value) + "\n";
            text += "TNS (every group, " +
                    std::to_string(every_violating) +
                    " endpoints): " + FormatNum(every_tns_value) + "\n";
          };
          group_table("Path groups (setup):\n", result.setup_groups,
                      setup_union_tns, setup_union_violating,
                      setup_every_tns, setup_every_violating);
          group_table("Path groups (hold):\n", result.hold_groups,
                      hold_union_tns, hold_union_violating,
                      hold_every_tns, hold_every_violating);
          text += "Cells: " + std::to_string(result.total_cells) +
                  ", total area " + FormatNum(result.total_area) +
                  " (Liberty area units)\n";
          std::vector<std::vector<std::string>> cells;
          cells.push_back({"Cell Type", "Count", "Area"});
          for (const analysis::CellTypeRow& row : result.cells) {
            cells.push_back({row.cell, std::to_string(row.count),
                             FormatNum(row.area)});
          }
          if (result.cells.empty()) {
            cells.push_back({"(no cells)", "-", "-"});
          }
          text += ReportClockRenderTable(std::move(cells));
          text += "DRC summary:\n";
          std::vector<std::vector<std::string>> drc;
          drc.push_back({"Check", "Violations", "Worst Slack"});
          const auto drc_text_row = [&](const char* name,
                                        const analysis::ElectricalCheck&
                                            check) {
            drc.push_back({name, std::to_string(check.violation_count),
                           check.has_limit ? FormatNum(check.worst_slack)
                                           : "no limit"});
          };
          drc_text_row("max_transition", result.max_transition_check);
          drc_text_row("max_capacitance", result.max_capacitance_check);
          drc_text_row("max_fanout", result.max_fanout_check);
          text += ReportClockRenderTable(std::move(drc));
          if (compile_recorded) {
            text += "Compile CPU: " + FormatNum(compile_cpu) +
                    " s (synth_map plus synth_opt)\n";
          } else {
            text += "Compile CPU: n/a (no synthesis run in this "
                    "session)\n";
          }
        }
        if (opts.Seen("-file")) {
          const std::string file = *opts.Value("-file");
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

  // ---- report_bottleneck (Immediate unit 9, review item I9)
  // The bottleneck report: the cell instances ranked by the
  // number of violating worst paths passing through them. It
  // reads the paths the last timing update saved while the
  // capture was on (set_bottleneck_capture on, then run_timing):
  // it never re-runs timing, so the ranking is the design as of
  // that update. A cell counts once per path it lies on, however
  // many of the path's pins touch it; ties rank by worst slack,
  // then instance name. Without -scene the ranking pools the
  // captured paths of every scene.
  add("report_bottleneck",
      "report_bottleneck [-delay_type max|min] [-max_cells <n>] "
      "[-scene <name>] [-file <path>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-delay_type", 1, false},
                 {"-max_cells", 1, false},
                 {"-scene", 1, false},
                 {"-file", 1, false}},
                "report_bottleneck", opts)) {
          return *bad;
        }
        if (!opts.Positionals().empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "report_bottleneck: unexpected positional argument: " +
                  opts.Positionals()[0]);
        }
        analysis::CheckKind kind = analysis::CheckKind::kSetup;
        if (opts.Seen("-delay_type")) {
          const std::string delay_type = *opts.Value("-delay_type");
          if (delay_type == "max") {
            kind = analysis::CheckKind::kSetup;
          } else if (delay_type == "min") {
            kind = analysis::CheckKind::kHold;
          } else {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "report_bottleneck: -delay_type must be max or min, "
                "got: " + delay_type);
          }
        }
        std::size_t max_cells = 10;
        if (opts.Seen("-max_cells")) {
          std::uint64_t parsed = 0;
          if (!ParseU64(*opts.Value("-max_cells"), parsed) ||
              parsed == 0) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "report_bottleneck: -max_cells must be a positive "
                "integer, got: " + *opts.Value("-max_cells"));
          }
          max_cells = static_cast<std::size_t>(parsed);
        }
        if (ctx.timing.last_results.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no timing results: run eos::run_timing first");
        }
        if (!ctx.timing.last_capture_enabled) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "bottleneck capture was off at the last timing update: "
              "run eos::set_bottleneck_capture on, then eos::run_timing "
              "again");
        }
        // The scene is resolved against the last timing results:
        // a scene that ran with no violations has no stored entry,
        // which ranks as empty, not as missing data.
        std::string report_scene = "all";
        if (opts.Seen("-scene")) {
          const std::string name = *opts.Value("-scene");
          const auto found = std::find_if(
              ctx.timing.last_results.begin(),
              ctx.timing.last_results.end(),
              [&](const analysis::SceneResult& result) {
                return result.scenario.name == name;
              });
          if (found == ctx.timing.last_results.end()) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "report_bottleneck: unknown scene: " + name);
          }
          report_scene = name;
        }
        const std::vector<analysis::ScenePathReports>& stored =
            kind == analysis::CheckKind::kSetup
                ? ctx.timing.last_bottleneck_setup_paths
                : ctx.timing.last_bottleneck_hold_paths;
        struct RankedCell {
          std::string instance;
          std::string cell;
          std::size_t paths = 0;
          double worst_slack = 0.0;
        };
        std::map<std::string, RankedCell> by_instance;
        std::size_t path_count = 0;
        for (const analysis::ScenePathReports& scene_paths : stored) {
          if (report_scene != "all" &&
              scene_paths.scenario.name != report_scene) {
            continue;
          }
          for (const analysis::PathReport& path : scene_paths.paths) {
            ++path_count;
            std::set<std::string> seen;
            for (const analysis::PathPoint& point : path.points) {
              if (point.instance.empty() ||
                  !seen.insert(point.instance).second) {
                continue;
              }
              RankedCell& cell = by_instance[point.instance];
              const bool first = cell.paths == 0;
              cell.instance = point.instance;
              if (cell.cell.empty()) cell.cell = point.cell;
              ++cell.paths;
              cell.worst_slack = first ? path.slack_ns
                                       : std::min(cell.worst_slack,
                                                  path.slack_ns);
            }
          }
        }
        std::vector<RankedCell> ranked;
        for (auto& entry : by_instance) ranked.push_back(entry.second);
        std::sort(ranked.begin(), ranked.end(),
                  [](const RankedCell& a, const RankedCell& b) {
                    if (a.paths != b.paths) return a.paths > b.paths;
                    if (a.worst_slack != b.worst_slack) {
                      return a.worst_slack < b.worst_slack;
                    }
                    return a.instance < b.instance;
                  });
        if (ranked.size() > max_cells) ranked.resize(max_cells);
        const std::string report = "bottleneck_report_" + report_scene;
        const Result<session::ArtifactId> artifact = ctx.reports.RegisterReport(
            ctx.project, report, session::ProofStatus::kPass, "NLDM");
        if (!artifact.ok()) return FromStatus(artifact.status());
        std::string payload =
            "\nscene=" + report_scene +
            " delay_type=" +
            (kind == analysis::CheckKind::kSetup ? "max" : "min") +
            " paths=" + std::to_string(path_count) +
            " cells=" + std::to_string(ranked.size()) +
            " report=" + report +
            " artifact=" + std::to_string(artifact.value());
        std::string text = "\nBottleneck (";
        text += kind == analysis::CheckKind::kSetup ? "setup" : "hold";
        text += ", " + std::to_string(path_count) +
                " violating paths, scene " + report_scene + "):\n";
        text += "Ranking from the paths saved by the last run_timing "
                "with bottleneck capture on.\n";
        std::vector<std::vector<std::string>> table;
        table.push_back({"Rank", "Cell Instance", "Cell Type", "Paths",
                         "Worst Slack (ns)"});
        for (std::size_t i = 0; i < ranked.size(); ++i) {
          const RankedCell& cell = ranked[i];
          table.push_back({std::to_string(i + 1), cell.instance, cell.cell,
                           std::to_string(cell.paths),
                           FormatNum(cell.worst_slack)});
          payload += "\nscene=" + report_scene +
                     " rank=" + std::to_string(i + 1) +
                     " cell=" + cell.instance +
                     " type=" + cell.cell +
                     " paths=" + std::to_string(cell.paths) +
                     " worst_slack=" + FormatNum(cell.worst_slack);
        }
        if (ranked.empty()) {
          table.push_back({"-", "(no violating paths captured)", "-", "-",
                           "-"});
        }
        text += ReportClockRenderTable(std::move(table));
        if (opts.Seen("-file")) {
          const std::string file = *opts.Value("-file");
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

  // ---- Check-specific reports (Immediate unit 10, review item
  // T8) ---------------------------------------------------------
  // Five from-scratch reports over the timing session, all run
  // through one TimingService::AnalyzeCheckReports call per
  // scene (one engine session per scene, exactly as report_qor):
  // report_global_timing (the worst path of every path group),
  // report_global_slack (the worst slack of every constrained
  // endpoint pin), report_min_period (the smallest period each
  // clock can close at), report_min_pulse_width (the measured
  // pulse widths against their minimums), and
  // report_clock_gating_check (the setup and hold slacks of
  // every gated-clock enable). Every value is measured by the
  // engine under the SDC file the session read: nothing is
  // invented, and a check the engine did not evaluate is
  // reported as not evaluated, never as a pass. The margins the
  // gating and pulse-width checks are measured against come
  // from that SDC file; EOS-native set_clock_gating_check and
  // set_min_pulse_width rows live in the session constraint set
  // and reach the engine only after write_sdc and a fresh
  // read_sdc, exactly as report_clock documents for clocks.

  // ---- report_global_timing (Immediate unit 10, review item
  // T8) ---------------------------------------------------------
  // The global timing summary: the design-wide worst slack and
  // total negative slack (the union over every constrained
  // endpoint, so they equal the run_timing scalars), then the
  // worst path of every path group for the selected check kinds.
  // The worst path of a group is the worst path to the group's
  // worst endpoint: the startpoint names the launch, the slack
  // is the group's WNS.
  add("report_global_timing",
      "report_global_timing [-delay_type max|min] [-scene <name>] "
      "[-file <path>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-delay_type", 1, false},
                 {"-scene", 1, false},
                 {"-file", 1, false}},
                "report_global_timing", opts)) {
          return *bad;
        }
        if (!opts.Positionals().empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "report_global_timing: unexpected positional argument: " +
                  opts.Positionals()[0]);
        }
        std::optional<analysis::CheckKind> kind;
        if (auto bad = ParseCheckDelayType(opts, "report_global_timing",
                                           kind)) {
          return *bad;
        }
        std::vector<analysis::SceneAnalysis> scenes;
        std::vector<analysis::CheckReportsResult> analyzed;
        if (auto bad = AnalyzeCheckReportsScenes(
                ctx, "report_global_timing", opts, scenes, analyzed)) {
          return *bad;
        }
        const auto union_slack =
            [](const std::vector<analysis::EndpointSlack>& endpoints) {
              std::optional<double> worst;
              double tns = 0.0;
              std::size_t violating = 0;
              for (const analysis::EndpointSlack& endpoint : endpoints) {
                if (!worst.has_value() ||
                    endpoint.slack_ns < *worst) {
                  worst = endpoint.slack_ns;
                }
                if (endpoint.slack_ns < 0.0) {
                  tns += endpoint.slack_ns;
                  ++violating;
                }
              }
              return std::make_tuple(worst, tns, violating);
            };
        const auto slack_text = [](const std::optional<double>& slack) {
          return slack.has_value() ? FormatNum(*slack)
                                   : std::string("-");
        };
        std::string payload =
            std::string("scenes=") + std::to_string(analyzed.size()) +
            " revision=" +
            std::to_string(ctx.project.ledger.current_revision());
        std::string text;
        for (const analysis::CheckReportsResult& result :
             analyzed) {
          const std::string report =
              "global_timing_report_" + result.scenario.name;
          const Result<session::ArtifactId> artifact =
              ctx.reports.RegisterReport(ctx.project, report,
                                         session::ProofStatus::kPass,
                                         "NLDM");
          if (!artifact.ok()) return FromStatus(artifact.status());
          const auto [setup_worst, setup_tns, setup_violating] =
              union_slack(result.setup_endpoints);
          const auto [hold_worst, hold_tns, hold_violating] =
              union_slack(result.hold_endpoints);
          payload += "\nscene=" + result.scenario.name +
                     " setup_wns=" + slack_text(setup_worst) +
                     " setup_tns=" + FormatNum(setup_tns) +
                     " setup_violating=" +
                     std::to_string(setup_violating) +
                     " hold_wns=" + slack_text(hold_worst) +
                     " hold_tns=" + FormatNum(hold_tns) +
                     " hold_violating=" +
                     std::to_string(hold_violating) +
                     " report=" + report +
                     " artifact=" + std::to_string(artifact.value());
          text += "\nGlobal timing (scene " + result.scenario.name +
                  "):\n";
          text += "Setup WNS " + slack_text(setup_worst) + " ns, TNS " +
                  FormatNum(setup_tns) + " ns, " +
                  std::to_string(setup_violating) +
                  " violating endpoints.\n";
          text += "Hold WNS " + slack_text(hold_worst) + " ns, TNS " +
                  FormatNum(hold_tns) + " ns, " +
                  std::to_string(hold_violating) +
                  " violating endpoints.\n";
          const auto render_kind =
              [&](const char* label, analysis::CheckKind wanted,
                  const std::vector<analysis::GroupWorstPath>& paths) {
                if (kind.has_value() && *kind != wanted) return;
                text += std::string("Worst paths (") + label + "):\n";
                std::vector<std::vector<std::string>> table;
                table.push_back({"Group", "WNS (ns)", "Worst endpoint",
                                 "Startpoint", "Slack (ns)"});
                for (const analysis::GroupWorstPath& entry : paths) {
                  table.push_back(
                      {entry.group, FormatNum(entry.path.slack_ns),
                       entry.path.endpoint, entry.path.startpoint,
                       FormatNum(entry.path.slack_ns)});
                  payload += "\nscene=" + result.scenario.name +
                             " group=" + entry.group +
                             " check=" + label +
                             " endpoint=" + entry.path.endpoint +
                             " startpoint=" + entry.path.startpoint +
                             " slack=" + FormatNum(entry.path.slack_ns);
                }
                if (paths.empty()) {
                  table.push_back({"-", "-", "(no constrained endpoints)",
                                   "-", "-"});
                }
                text += ReportClockRenderTable(std::move(table));
              };
          render_kind("setup", analysis::CheckKind::kSetup,
                      result.setup_group_paths);
          render_kind("hold", analysis::CheckKind::kHold,
                      result.hold_group_paths);
        }
        if (auto bad = WriteCheckReportFile(opts, text)) return *bad;
        if (opts.Seen("-file")) {
          payload += "\nfile=" + *opts.Value("-file");
        }
        payload += "\n" + text;
        return CommandResult::Ok(payload);
      });

  // ---- report_global_slack (Immediate unit 10, review item
  // T8) ---------------------------------------------------------
  // The per-pin slack table: every constrained endpoint pin
  // with its worst slack for the selected check kinds, worst
  // first. This is the report that names every pin that
  // violates, not just the worst one per group: an endpoint
  // that meets timing appears too, so a clean design reports
  // its margins rather than an empty table.
  add("report_global_slack",
      "report_global_slack [-delay_type max|min] "
      "[-slack_lesser_than <ns>] [-scene <name>] [-file <path>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-delay_type", 1, false},
                 {"-slack_lesser_than", 1, false},
                 {"-scene", 1, false},
                 {"-file", 1, false}},
                "report_global_slack", opts)) {
          return *bad;
        }
        if (!opts.Positionals().empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "report_global_slack: unexpected positional argument: " +
                  opts.Positionals()[0]);
        }
        std::optional<analysis::CheckKind> kind;
        if (auto bad =
                ParseCheckDelayType(opts, "report_global_slack", kind)) {
          return *bad;
        }
        std::optional<double> ceiling;
        if (opts.Seen("-slack_lesser_than")) {
          double value = 0.0;
          if (auto bad = ParseOptDouble(opts, "-slack_lesser_than",
                                        "report_global_slack", value)) {
            return *bad;
          }
          ceiling = value;
        }
        std::vector<analysis::SceneAnalysis> scenes;
        std::vector<analysis::CheckReportsResult> analyzed;
        if (auto bad = AnalyzeCheckReportsScenes(
                ctx, "report_global_slack", opts, scenes, analyzed)) {
          return *bad;
        }
        std::string payload =
            std::string("scenes=") + std::to_string(analyzed.size()) +
            " revision=" +
            std::to_string(ctx.project.ledger.current_revision());
        std::string text;
        for (const analysis::CheckReportsResult& result :
             analyzed) {
          const std::string report =
              "global_slack_report_" + result.scenario.name;
          const Result<session::ArtifactId> artifact =
              ctx.reports.RegisterReport(ctx.project, report,
                                         session::ProofStatus::kPass,
                                         "NLDM");
          if (!artifact.ok()) return FromStatus(artifact.status());
          payload += "\nscene=" + result.scenario.name +
                     " report=" + report +
                     " artifact=" + std::to_string(artifact.value());
          text += "\nGlobal slack (scene " + result.scenario.name +
                  "):\n";
          const auto render_kind =
              [&](const char* label, analysis::CheckKind wanted,
                  const std::vector<analysis::EndpointSlack>& endpoints) {
                if (kind.has_value() && *kind != wanted) return;
                std::size_t violating = 0;
                for (const analysis::EndpointSlack& endpoint :
                     endpoints) {
                  if (endpoint.slack_ns < 0.0) ++violating;
                }
                text += std::string("Endpoints (") + label + "): " +
                        std::to_string(endpoints.size()) +
                        " endpoints, " + std::to_string(violating) +
                        " violating";
                if (ceiling.has_value()) {
                  text += ", slack below " + FormatNum(*ceiling) + " ns";
                }
                text += ".\n";
                std::vector<std::vector<std::string>> table;
                table.push_back({"Endpoint pin", "Slack (ns)"});
                std::size_t shown = 0;
                for (const analysis::EndpointSlack& endpoint :
                     endpoints) {
                  if (ceiling.has_value() &&
                      endpoint.slack_ns >= *ceiling) {
                    continue;
                  }
                  table.push_back(
                      {endpoint.pin, FormatNum(endpoint.slack_ns)});
                  payload += "\nscene=" + result.scenario.name +
                             " check=" + label +
                             " pin=" + endpoint.pin +
                             " slack=" + FormatNum(endpoint.slack_ns);
                  ++shown;
                }
                if (shown == 0) {
                  table.push_back(
                      {"-", "(no endpoints pass the selection)"});
                }
                text += ReportClockRenderTable(std::move(table));
                payload += "\nscene=" + result.scenario.name +
                           " check=" + label +
                           " endpoints=" + std::to_string(shown) +
                           " violating=" + std::to_string(violating);
              };
          render_kind("setup", analysis::CheckKind::kSetup,
                      result.setup_endpoints);
          render_kind("hold", analysis::CheckKind::kHold,
                      result.hold_endpoints);
        }
        if (auto bad = WriteCheckReportFile(opts, text)) return *bad;
        if (opts.Seen("-file")) {
          payload += "\nfile=" + *opts.Value("-file");
        }
        payload += "\n" + text;
        return CommandResult::Ok(payload);
      });

  // ---- report_min_period (Immediate unit 10, review item T8)
  // ------------------------------------------------------------
  // The minimum period each clock can close at: for every
  // clock, period minus the worst setup slack of the clock's
  // setup group. One row per clock (its worst endpoint) by
  // default; -verbose lists every endpoint of each clock's
  // group. A clock with no setup group (no constrained
  // endpoints) yields no row: the report says so instead of
  // inventing a period. Endpoints reassigned to a named
  // group_path are not in their clock group, so they do not
  // contribute to the clock's minimum period. Setup only:
  // hold slack does not depend on the period.
  add("report_min_period",
      "report_min_period [-verbose] [-scene <name>] [-file <path>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-verbose", 0, false},
                 {"-scene", 1, false},
                 {"-file", 1, false}},
                "report_min_period", opts)) {
          return *bad;
        }
        if (!opts.Positionals().empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "report_min_period: unexpected positional argument: " +
                  opts.Positionals()[0]);
        }
        const bool verbose = opts.Has("-verbose");
        std::vector<analysis::SceneAnalysis> scenes;
        std::vector<analysis::CheckReportsResult> analyzed;
        if (auto bad = AnalyzeCheckReportsScenes(
                ctx, "report_min_period", opts, scenes, analyzed)) {
          return *bad;
        }
        std::string payload =
            std::string("scenes=") + std::to_string(analyzed.size()) +
            " revision=" +
            std::to_string(ctx.project.ledger.current_revision());
        std::string text;
        for (const analysis::CheckReportsResult& result :
             analyzed) {
          const std::string report =
              "min_period_report_" + result.scenario.name;
          const Result<session::ArtifactId> artifact =
              ctx.reports.RegisterReport(ctx.project, report,
                                         session::ProofStatus::kPass,
                                         "NLDM");
          if (!artifact.ok()) return FromStatus(artifact.status());
          std::size_t violating = 0;
          std::optional<double> worst_slack;
          for (const analysis::MinPeriodClockRow& row :
               result.min_period_clocks) {
            if (row.slack_ns < 0.0) ++violating;
            if (!worst_slack.has_value() ||
                row.slack_ns < *worst_slack) {
              worst_slack = row.slack_ns;
            }
          }
          payload += "\nscene=" + result.scenario.name +
                     " checks=" +
                     std::to_string(result.min_period_clocks.size()) +
                     " violating=" + std::to_string(violating) +
                     " worst_slack=" +
                     (worst_slack.has_value() ? FormatNum(*worst_slack)
                                              : std::string("-")) +
                     " report=" + report +
                     " artifact=" + std::to_string(artifact.value());
          text += "\nMinimum period (scene " + result.scenario.name +
                  "): " +
                  std::to_string(result.min_period_clocks.size()) +
                  " checks, " + std::to_string(violating) +
                  " violating";
          if (worst_slack.has_value()) {
            text += ", worst slack " + FormatNum(*worst_slack) + " ns";
          }
          text += ".\n";
          if (result.min_period_clocks.empty()) {
            text += "No min-period checks evaluated: no clock has a "
                    "setup group with constrained endpoints.\n";
          } else if (verbose) {
            std::vector<std::vector<std::string>> table;
            table.push_back({"Pin", "Clock", "Period (ns)",
                             "Min period (ns)", "Slack (ns)"});
            for (const analysis::MinPeriodEndpointRow& row :
                 result.min_period_endpoints) {
              table.push_back({row.pin, row.clock,
                               FormatNum(row.period_ns),
                               FormatNum(row.min_period_ns),
                               FormatNum(row.slack_ns)});
              payload += "\nscene=" + result.scenario.name +
                         " pin=" + row.pin +
                         " clock=" + row.clock +
                         " period=" + FormatNum(row.period_ns) +
                         " min_period=" +
                         FormatNum(row.min_period_ns) +
                         " slack=" + FormatNum(row.slack_ns);
            }
            text += ReportClockRenderTable(std::move(table));
          } else {
            // One row per clock: its worst endpoint. The rows
            // arrive worst slack first, already per clock.
            std::vector<std::vector<std::string>> table;
            table.push_back({"Clock", "Period (ns)",
                             "Min period (ns)", "Slack (ns)",
                             "Worst pin"});
            for (const analysis::MinPeriodClockRow& row :
                 result.min_period_clocks) {
              table.push_back({row.clock,
                               FormatNum(row.period_ns),
                               FormatNum(row.min_period_ns),
                               FormatNum(row.slack_ns), row.worst_pin});
              payload += "\nscene=" + result.scenario.name +
                         " clock=" + row.clock +
                         " period=" + FormatNum(row.period_ns) +
                         " min_period=" +
                         FormatNum(row.min_period_ns) +
                         " slack=" + FormatNum(row.slack_ns) +
                         " worst_pin=" + row.worst_pin;
            }
            text += ReportClockRenderTable(std::move(table));
          }
        }
        if (auto bad = WriteCheckReportFile(opts, text)) return *bad;
        if (opts.Seen("-file")) {
          payload += "\nfile=" + *opts.Value("-file");
        }
        payload += "\n" + text;
        return CommandResult::Ok(payload);
      });

  // ---- report_min_pulse_width (Immediate unit 10, review item
  // T8) ---------------------------------------------------------
  // The minimum-pulse-width checks: every evaluated clock
  // pulse, its measured width against the required minimum,
  // worst slack first. A pin with a minimum-width constraint
  // but no evaluated pulse (no clock path, or no close path)
  // is listed separately as defined but not evaluated, with
  // the coverage reason: a missing row is never presented as
  // a pass.
  add("report_min_pulse_width",
      "report_min_pulse_width [-violators] [-scene <name>] "
      "[-file <path>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-violators", 0, false},
                 {"-scene", 1, false},
                 {"-file", 1, false}},
                "report_min_pulse_width", opts)) {
          return *bad;
        }
        if (!opts.Positionals().empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "report_min_pulse_width: unexpected positional "
              "argument: " + opts.Positionals()[0]);
        }
        const bool violators_only = opts.Has("-violators");
        std::vector<analysis::SceneAnalysis> scenes;
        std::vector<analysis::CheckReportsResult> analyzed;
        if (auto bad = AnalyzeCheckReportsScenes(
                ctx, "report_min_pulse_width", opts, scenes, analyzed)) {
          return *bad;
        }
        std::string payload =
            std::string("scenes=") + std::to_string(analyzed.size()) +
            " revision=" +
            std::to_string(ctx.project.ledger.current_revision());
        std::string text;
        for (const analysis::CheckReportsResult& result :
             analyzed) {
          const std::string report =
              "min_pulse_width_report_" + result.scenario.name;
          const Result<session::ArtifactId> artifact =
              ctx.reports.RegisterReport(ctx.project, report,
                                         session::ProofStatus::kPass,
                                         "NLDM");
          if (!artifact.ok()) return FromStatus(artifact.status());
          std::size_t violating = 0;
          std::optional<double> worst_slack;
          for (const analysis::MinPulseWidthCheck& check :
               result.min_pulse_width_checks) {
            if (check.slack_ns < 0.0) ++violating;
            if (!worst_slack.has_value() ||
                check.slack_ns < *worst_slack) {
              worst_slack = check.slack_ns;
            }
          }
          // Defined but not evaluated: a min-pulse-width
          // coverage row that was never tested, with its
          // reason. Evaluated checks are exactly the tested
          // rows, so this is the complement.
          std::vector<const analysis::CoveragePin*> unevaluated;
          for (const analysis::CoveragePin& row :
               result.coverage_pins) {
            if (row.check_type == "min_pulse_width" && !row.tested) {
              unevaluated.push_back(&row);
            }
          }
          payload += "\nscene=" + result.scenario.name +
                     " checks=" +
                     std::to_string(
                         result.min_pulse_width_checks.size()) +
                     " violating=" + std::to_string(violating) +
                     " worst_slack=" +
                     (worst_slack.has_value() ? FormatNum(*worst_slack)
                                              : std::string("-")) +
                     " unevaluated=" +
                     std::to_string(unevaluated.size()) +
                     " report=" + report +
                     " artifact=" + std::to_string(artifact.value());
          text += "\nMinimum pulse width (scene " +
                  result.scenario.name + "): " +
                  std::to_string(result.min_pulse_width_checks.size()) +
                  " checks, " + std::to_string(violating) +
                  " violating";
          if (worst_slack.has_value()) {
            text += ", worst slack " + FormatNum(*worst_slack) + " ns";
          }
          text += ".\n";
          std::vector<std::vector<std::string>> table;
          table.push_back({"Pin", "Transition", "Width (ns)",
                           "Min width (ns)", "Slack (ns)"});
          std::size_t shown = 0;
          for (const analysis::MinPulseWidthCheck& check :
               result.min_pulse_width_checks) {
            if (violators_only && check.slack_ns >= 0.0) continue;
            table.push_back({check.pin, check.transition,
                             FormatNum(check.width_ns),
                             FormatNum(check.min_width_ns),
                             FormatNum(check.slack_ns)});
            payload += "\nscene=" + result.scenario.name +
                       " pin=" + check.pin +
                       " transition=" + check.transition +
                       " width=" + FormatNum(check.width_ns) +
                       " min_width=" + FormatNum(check.min_width_ns) +
                       " slack=" + FormatNum(check.slack_ns);
            ++shown;
          }
          if (shown == 0) {
            table.push_back({"-", "-", "(no checks pass the selection)",
                             "-", "-"});
          }
          text += ReportClockRenderTable(std::move(table));
          if (!unevaluated.empty()) {
            text += "Defined but not evaluated:\n";
            std::vector<std::vector<std::string>> unevaluated_table;
            unevaluated_table.push_back({"Pin", "Reason"});
            for (const analysis::CoveragePin* row : unevaluated) {
              unevaluated_table.push_back(
                  {row->pin, row->untested_reason});
              payload += "\nscene=" + result.scenario.name +
                         " unevaluated_pin=" + row->pin +
                         " reason=" + row->untested_reason;
            }
            text += ReportClockRenderTable(std::move(unevaluated_table));
          }
        }
        if (auto bad = WriteCheckReportFile(opts, text)) return *bad;
        if (opts.Seen("-file")) {
          payload += "\nfile=" + *opts.Value("-file");
        }
        payload += "\n" + text;
        return CommandResult::Ok(payload);
      });

  // ---- report_clock_gating_check (Immediate unit 10, review
  // item T8) ----------------------------------------------------
  // The clock-gating checks: every gated-clock enable the
  // engine recognizes, with the setup and hold slacks the
  // engine measured for it. The enable pin and its gated clock
  // pin come from the analysis coverage (the coverage row
  // names both); the slacks come from the gated-clock path
  // group, whose endpoint is the enable pin. A defined check
  // the engine never evaluated (no clocked path reaches the
  // enable) is listed with its coverage reason and no slack,
  // never as a pass.
  add("report_clock_gating_check",
      "report_clock_gating_check [-scene <name>] [-file <path>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-scene", 1, false}, {"-file", 1, false}},
                "report_clock_gating_check", opts)) {
          return *bad;
        }
        if (!opts.Positionals().empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "report_clock_gating_check: unexpected positional "
              "argument: " + opts.Positionals()[0]);
        }
        std::vector<analysis::SceneAnalysis> scenes;
        std::vector<analysis::CheckReportsResult> analyzed;
        if (auto bad = AnalyzeCheckReportsScenes(
                ctx, "report_clock_gating_check", opts, scenes,
                analyzed)) {
          return *bad;
        }
        std::string payload =
            std::string("scenes=") + std::to_string(analyzed.size()) +
            " revision=" +
            std::to_string(ctx.project.ledger.current_revision());
        std::string text;
        for (const analysis::CheckReportsResult& result :
             analyzed) {
          const std::string report =
              "clock_gating_check_report_" + result.scenario.name;
          const Result<session::ArtifactId> artifact =
              ctx.reports.RegisterReport(ctx.project, report,
                                         session::ProofStatus::kPass,
                                         "NLDM");
          if (!artifact.ok()) return FromStatus(artifact.status());
          // The defined checks: one coverage row per (enable,
          // kind); the setup and hold rows of one enable share
          // the pin, the gated clock pin, and the tested state.
          struct GatingCheck {
            std::string enable;
            std::string clock = "-";
            bool tested = false;
            std::string reason;
            std::optional<double> setup_slack;
            std::optional<double> hold_slack;
          };
          std::map<std::string, GatingCheck> by_enable;
          for (const analysis::CoveragePin& row :
               result.coverage_pins) {
            if (row.check_type != "clock_gating_setup" &&
                row.check_type != "clock_gating_hold") {
              continue;
            }
            GatingCheck& check = by_enable[row.pin];
            check.enable = row.pin;
            check.clock = row.related_pin;
            check.tested = row.tested;
            if (!row.tested) check.reason = row.untested_reason;
          }
          // The measured slacks: the gated-clock path group,
          // whose endpoint is the enable pin, per check kind.
          const auto add_group_slack =
              [&by_enable](
                  const std::vector<analysis::PathGroupResult>& groups,
                  bool setup) {
                for (const analysis::PathGroupResult& group : groups) {
                  if (group.name != "gated clock") continue;
                  for (const analysis::EndpointSlack& endpoint :
                       group.endpoints) {
                    GatingCheck& check = by_enable[endpoint.pin];
                    check.enable = endpoint.pin;
                    if (setup) {
                      check.setup_slack = endpoint.slack_ns;
                    } else {
                      check.hold_slack = endpoint.slack_ns;
                    }
                  }
                }
              };
          add_group_slack(result.setup_groups, true);
          add_group_slack(result.hold_groups, false);
          std::size_t tested = 0;
          std::size_t violating = 0;
          for (const auto& entry : by_enable) {
            const GatingCheck& check = entry.second;
            if (check.tested) ++tested;
            if ((check.setup_slack.has_value() &&
                 *check.setup_slack < 0.0) ||
                (check.hold_slack.has_value() &&
                 *check.hold_slack < 0.0)) {
              ++violating;
            }
          }
          payload += "\nscene=" + result.scenario.name +
                     " checks=" + std::to_string(by_enable.size()) +
                     " tested=" + std::to_string(tested) +
                     " violating=" + std::to_string(violating) +
                     " report=" + report +
                     " artifact=" + std::to_string(artifact.value());
          text += "\nClock gating checks (scene " +
                  result.scenario.name + "): " +
                  std::to_string(by_enable.size()) + " checks, " +
                  std::to_string(tested) + " evaluated, " +
                  std::to_string(violating) + " violating.\n";
          if (by_enable.empty()) {
            text += "No clock-gating checks defined: no gated-clock "
                    "enable in the design.\n";
          } else {
            std::vector<std::vector<std::string>> table;
            table.push_back({"Enable pin", "Gated clock pin",
                             "Setup slack (ns)", "Hold slack (ns)",
                             "Status"});
            for (const auto& entry : by_enable) {
              const GatingCheck& check = entry.second;
              const std::string setup =
                  check.setup_slack.has_value()
                      ? FormatNum(*check.setup_slack)
                      : std::string("-");
              const std::string hold =
                  check.hold_slack.has_value()
                      ? FormatNum(*check.hold_slack)
                      : std::string("-");
              const std::string status =
                  check.tested ? "tested"
                               : "untested: " + check.reason;
              table.push_back({check.enable, check.clock, setup, hold,
                               status});
              payload += "\nscene=" + result.scenario.name +
                         " enable=" + check.enable +
                         " clock=" + check.clock +
                         " setup_slack=" + setup +
                         " hold_slack=" + hold + " status=" + status;
            }
            text += ReportClockRenderTable(std::move(table));
            // A tested check with no measured slack is
            // unmeasured, not passing: the engine produced no
            // gated-clock path end for this session, so there
            // is no slack to report. Say so plainly instead of
            // letting the dash read as a pass.
            std::size_t unmeasured = 0;
            for (const auto& entry : by_enable) {
              const GatingCheck& check = entry.second;
              if (check.tested &&
                  (!check.setup_slack.has_value() ||
                   !check.hold_slack.has_value())) {
                ++unmeasured;
              }
            }
            if (unmeasured > 0) {
              text += "Note: " + std::to_string(unmeasured) +
                      " tested check(s) have no measured slack: the "
                      "engine produced no gated-clock path end for "
                      "this session.\n";
            }
          }
        }
        if (auto bad = WriteCheckReportFile(opts, text)) return *bad;
        if (opts.Seen("-file")) {
          payload += "\nfile=" + *opts.Value("-file");
        }
        payload += "\n" + text;
        return CommandResult::Ok(payload);
      });

  // ---- timing_summary (full-flow GUI unit F2) --------------------
  // The structured timing query the GUI Timing panel reads: one
  // payload with the per-scene slack summary from the last
  // run_timing, the clock table from the session constraint set,
  // the worst-violator table from the same results, the worst
  // paths across endpoints (worst first) from the timing
  // service, the coverage summary, and the QoR numbers. It is a
  // query, not a report: it runs no report binding, writes no
  // file, and changes no session state. The worst paths come
  // from the TimingService analysis (ReportPaths), never from
  // report_timing text; the coverage and QoR rows come from the
  // same service (AnalyzeCoverage, AnalyzeQor). Like its sibling
  // reports it runs the analysis itself for the selected scenes:
  // the session keeps no live engine, so a query cannot reuse
  // one. That costs one engine session per scene for the paths,
  // one for coverage, and one for QoR, on top of the stored
  // last_results the summary reads; the GUI refreshes the panel
  // only when the timing inputs change, so a plain refresh never
  // pays it twice. The scenes reported are the scenes of the
  // last run_timing: a scenario added after that run is not
  // shown until run_timing runs again. -max_paths caps the
  // worst paths per check per scene (default 5); -scene limits
  // the payload to one scene.
  add("timing_summary", "timing_summary [-max_paths N] [-scene <name>]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        SdcOptions opts;
        if (auto bad = SdcOptions::Parse(
                args,
                {{"-max_paths", 1, false}, {"-scene", 1, false}},
                "timing_summary", opts)) {
          return *bad;
        }
        if (!opts.Positionals().empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "timing_summary: unexpected positional argument: " +
                  opts.Positionals()[0]);
        }
        std::size_t max_paths = 5;
        if (opts.Seen("-max_paths")) {
          std::uint64_t parsed = 0;
          if (!ParseU64(*opts.Value("-max_paths"), parsed) ||
              parsed == 0) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "timing_summary: -max_paths must be a positive "
                "integer, got: " +
                    *opts.Value("-max_paths"));
          }
          max_paths = static_cast<std::size_t>(parsed);
        }
        if (ctx.timing.last_results.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "no timing results: run eos::run_timing first");
        }
        bool from_odb = false;
        std::vector<analysis::SceneAnalysis> scenes;
        if (auto bad = BuildTimingScenes(ctx, from_odb, scenes)) {
          return *bad;
        }
        // The reported scenes are the last run's scenes, in run
        // order; -scene picks one of them. A scene the last run
        // never covered has no summary to show, so it fails
        // fast instead of reporting analysis rows with no
        // summary row.
        std::vector<const analysis::SceneResult*> reported;
        for (const analysis::SceneResult& result :
             ctx.timing.last_results) {
          if (opts.Seen("-scene") &&
              result.scenario.name != *opts.Value("-scene")) {
            continue;
          }
          reported.push_back(&result);
        }
        if (reported.empty()) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              opts.Seen("-scene")
                  ? "timing_summary: scene has no timing results: " +
                        *opts.Value("-scene")
                  : "timing_summary: no timing results");
        }
        const auto scene_selected = [&reported](const std::string& name) {
          for (const analysis::SceneResult* result : reported) {
            if (result->scenario.name == name) return true;
          }
          return false;
        };
        std::vector<analysis::SceneAnalysis> analyzed;
        for (const analysis::SceneAnalysis& scene : scenes) {
          if (scene_selected(scene.scenario.name)) {
            analyzed.push_back(scene);
          }
        }
        analysis::TimingService service;
        const auto paths_for = [&](analysis::CheckKind kind) {
          analysis::PathReportRequest request;
          request.kind = kind;
          request.max_paths = max_paths;
          return from_odb
                     ? service.ReportPaths(*ctx.timing.odb,
                                           ctx.timing.sdc_path, analyzed,
                                           request)
                     : service.ReportPaths(
                           analysis::VerilogDesign{
                               ctx.timing.verilog_path, ctx.timing.top_cell,
                               ctx.timing.sdc_path},
                           analyzed, request);
        };
        const Result<std::vector<analysis::ScenePathReports>> setup_paths =
            paths_for(analysis::CheckKind::kSetup);
        if (!setup_paths.ok()) return FromStatus(setup_paths.status());
        const Result<std::vector<analysis::ScenePathReports>> hold_paths =
            paths_for(analysis::CheckKind::kHold);
        if (!hold_paths.ok()) return FromStatus(hold_paths.status());
        const Result<std::vector<analysis::CoverageResult>> coverage =
            from_odb
                ? service.AnalyzeCoverage(*ctx.timing.odb,
                                          ctx.timing.sdc_path, analyzed)
                : service.AnalyzeCoverage(
                      analysis::VerilogDesign{ctx.timing.verilog_path,
                                              ctx.timing.top_cell,
                                              ctx.timing.sdc_path},
                      analyzed);
        if (!coverage.ok()) return FromStatus(coverage.status());
        const Result<std::vector<analysis::QorResult>> qor =
            from_odb
                ? service.AnalyzeQor(*ctx.timing.odb, ctx.timing.sdc_path,
                                     analyzed)
                : service.AnalyzeQor(
                      analysis::VerilogDesign{ctx.timing.verilog_path,
                                              ctx.timing.top_cell,
                                              ctx.timing.sdc_path},
                      analyzed);
        if (!qor.ok()) return FromStatus(qor.status());
        const auto find_paths = [](
            const std::vector<analysis::ScenePathReports>& all,
            const std::string& name) {
          for (const analysis::ScenePathReports& scene : all) {
            if (scene.scenario.name == name) return &scene.paths;
          }
          return static_cast<const std::vector<analysis::PathReport>*>(
              nullptr);
        };
        const auto find_coverage = [](
            const std::vector<analysis::CoverageResult>& all,
            const std::string& name) {
          for (const analysis::CoverageResult& scene : all) {
            if (scene.scenario.name == name) return &scene;
          }
          return static_cast<const analysis::CoverageResult*>(nullptr);
        };
        const auto find_qor = [](
            const std::vector<analysis::QorResult>& all,
            const std::string& name) {
          for (const analysis::QorResult& scene : all) {
            if (scene.scenario.name == name) return &scene;
          }
          return static_cast<const analysis::QorResult*>(nullptr);
        };
        std::string payload = std::string("source=") +
                              (from_odb ? "odb" : "verilog") +
                              " scenes=" +
                              std::to_string(reported.size()) +
                              " revision=" +
                              std::to_string(
                                  ctx.project.ledger.current_revision());
        // The clock table, from the session constraint set: the
        // same entries report_clock renders, as machine-readable
        // rows. Skew reads none on every clock: no clock
        // uncertainty is defined yet
        // (set_clock_uncertainty is not an EOS command).
        const std::vector<ReportClockEntry> clocks =
            ReportClockEntries(ctx.project.constraints);
        for (const ReportClockEntry& entry : clocks) {
          std::string attrs;
          if (entry.generated) attrs += "G";
          if (entry.unresolved) attrs += "u";
          payload += "\nclock name=" + entry.name +
                     " generated=" + (entry.generated ? "1" : "0") +
                     " period=" +
                     (entry.period_ns.has_value()
                          ? FormatNum(*entry.period_ns)
                          : std::string("-")) +
                     " rise=" +
                     (entry.rise_ns.has_value()
                          ? FormatNum(*entry.rise_ns)
                          : std::string("-")) +
                     " fall=" +
                     (entry.fall_ns.has_value()
                          ? FormatNum(*entry.fall_ns)
                          : std::string("-")) +
                     " attrs=" + attrs + " skew=none sources=" +
                     ReportClockBrace(entry.sources);
        }
        const std::vector<ReportGeneratedEntry> generated =
            ReportGeneratedEntries(ctx.project.constraints);
        for (const ReportGeneratedEntry& entry : generated) {
          // The brace groups carry the source lists and the
          // modification: both may contain spaces, so they ride
          // as one brace-valued token each.
          payload += "\ngenclock name=" + entry.name +
                     " master_clock=" +
                     (entry.master_clock.empty() ? std::string("-")
                                                 : entry.master_clock) +
                     " modification=" +
                     (entry.modification.empty()
                          ? std::string("-")
                          : "{" + entry.modification + "}") +
                     " master_source={" + entry.master_source + "}" +
                     " sources=" + ReportClockBrace(entry.sources);
        }
        for (const db::Constraint& c : ctx.project.constraints.all()) {
          if (c.kind != db::ConstraintKind::kClock ||
              c.Param("period_ns").has_value()) {
            continue;
          }
          payload += "\nbindclock name=" + c.name +
                     " target={" + c.target_spec + "}";
        }
        // One class row per constraint class, mirroring
        // report_constraint's worst-violator tier: the worst
        // violator, its slack, the exact cost, weight 1, and
        // the violating-pin count. A DRC class with no limit
        // at all reports limit=none, never clean.
        const auto timing_row = [](
            const char* name,
            const std::vector<analysis::EndpointSlack>& violators,
            const std::optional<double>& worst_slack,
            const std::optional<double>& total_negative_slack,
            std::string& out) {
          out += " class=";
          out += name;
          out += " violator=";
          out += violators.empty() ? "-" : violators[0].pin;
          out += " slack=";
          if (!violators.empty()) {
            out += FormatNum(violators[0].slack_ns);
          } else if (worst_slack.has_value() &&
                     std::isfinite(*worst_slack)) {
            out += FormatNum(*worst_slack);
          } else {
            out += "none";
          }
          out += " cost=";
          if (total_negative_slack.has_value()) {
            double cost = -*total_negative_slack;
            if (cost == 0.0) cost = 0.0;
            out += FormatNum(cost);
          } else {
            out += "none";
          }
          out += " weight=1 violations=" +
                 std::to_string(violators.size());
        };
        const auto drc_row = [](
            const char* name, const analysis::ElectricalCheck& check,
            const std::vector<analysis::ElectricalViolation>& violators,
            std::string& out) {
          out += " class=";
          out += name;
          if (!check.has_limit) {
            out += " violator=- limit=none weight=1 violations=" +
                   std::to_string(violators.size());
            return;
          }
          out += " violator=";
          out += violators.empty() ? "-" : violators[0].pin;
          out += " slack=";
          out += FormatNum(violators.empty() ? check.worst_slack
                                             : violators[0].slack);
          double cost = 0.0;
          for (const analysis::ElectricalViolation& violation :
               violators) {
            cost += -violation.slack;
          }
          out += " cost=" + FormatNum(cost);
          out += " weight=1 violations=" +
                 std::to_string(violators.size());
        };
        // The per-group WNS/TNS helpers mirror report_qor's:
        // WNS is the worst slack over every endpoint in the
        // group, TNS the sum of the negative ones.
        const auto group_tns = [](const analysis::PathGroupResult& group) {
          double wns = 0.0;
          double tns = 0.0;
          std::size_t violating = 0;
          bool first = true;
          for (const analysis::EndpointSlack& endpoint : group.endpoints) {
            if (first || endpoint.slack_ns < wns) {
              wns = endpoint.slack_ns;
              first = false;
            }
            if (endpoint.slack_ns < 0.0) {
              tns += endpoint.slack_ns;
              ++violating;
            }
          }
          return std::make_tuple(wns, tns, violating);
        };
        const auto union_tns =
            [](const std::vector<analysis::PathGroupResult>& groups) {
              std::map<std::string, double> by_pin;
              for (const analysis::PathGroupResult& group : groups) {
                for (const analysis::EndpointSlack& endpoint :
                     group.endpoints) {
                  const auto [it, inserted] = by_pin.try_emplace(
                      endpoint.pin, endpoint.slack_ns);
                  if (!inserted) {
                    it->second = std::min(it->second, endpoint.slack_ns);
                  }
                }
              }
              double tns = 0.0;
              std::size_t violating = 0;
              for (const auto& entry : by_pin) {
                if (entry.second < 0.0) {
                  tns += entry.second;
                  ++violating;
                }
              }
              return std::make_pair(tns, violating);
            };
        static const char* kCoverageTypes[] = {
            "setup",           "hold",
            "out_setup",       "out_hold",
            "recovery",        "removal",
            "clock_gating_setup", "clock_gating_hold",
            "no-change",       "min_pulse_width",
        };
        for (const analysis::SceneResult* result : reported) {
          const std::string& scene = result->scenario.name;
          const auto metric = [&result](const char* name) {
            const auto it = result->metrics.find(name);
            return it == result->metrics.end()
                       ? std::optional<double>{}
                       : std::optional<double>{it->second.value};
          };
          payload += "\nscene=" + scene +
                     " mode=" + result->scenario.mode +
                     " corner=" + result->scenario.corner +
                     " parasitics=" +
                     std::string(result->scenario.parasitics ==
                                         db::ParasiticsLevel::kExtracted
                                     ? "extracted"
                                     : "estimate") +
                     " worst_slack_ns=" +
                     MetricText(result->metrics, "worst_slack_ns") +
                     " total_negative_slack_ns=" +
                     MetricText(result->metrics,
                                "total_negative_slack_ns") +
                     " worst_hold_slack_ns=" +
                     MetricText(result->metrics, "worst_hold_slack_ns") +
                     " total_negative_hold_slack_ns=" +
                     MetricText(result->metrics,
                                "total_negative_hold_slack_ns") +
                     " max_transition_ns=" +
                     MetricText(result->metrics, "max_transition_ns") +
                     " max_transition_violations=" +
                     MetricText(result->metrics,
                                "max_transition_violations") +
                     " max_capacitance_pf=" +
                     MetricText(result->metrics, "max_capacitance_pf") +
                     " max_capacitance_violations=" +
                     MetricText(result->metrics,
                                "max_capacitance_violations") +
                     " max_fanout=" +
                     MetricText(result->metrics, "max_fanout") +
                     " max_fanout_violations=" +
                     MetricText(result->metrics, "max_fanout_violations");
          std::string violators = "\nscene=" + scene;
          timing_row("max_delay", result->setup_violating_endpoints,
                     metric("worst_slack_ns"),
                     metric("total_negative_slack_ns"), violators);
          payload += violators;
          violators = "\nscene=" + scene;
          timing_row("min_delay", result->hold_violating_endpoints,
                     metric("worst_hold_slack_ns"),
                     metric("total_negative_hold_slack_ns"), violators);
          payload += violators;
          violators = "\nscene=" + scene;
          drc_row("max_transition", result->max_transition_check,
                  result->max_transition_violators, violators);
          payload += violators;
          violators = "\nscene=" + scene;
          drc_row("max_capacitance", result->max_capacitance_check,
                  result->max_capacitance_violators, violators);
          payload += violators;
          violators = "\nscene=" + scene;
          drc_row("max_fanout", result->max_fanout_check,
                  result->max_fanout_violators, violators);
          payload += violators;
          const auto emit_paths = [&](const char* check,
                                      const std::vector<
                                          analysis::ScenePathReports>& all) {
            const std::vector<analysis::PathReport>* paths =
                find_paths(all, scene);
            if (paths == nullptr) return;
            for (const analysis::PathReport& path : *paths) {
              payload += "\npath scene=" + scene + " check=" + check +
                         " endpoint=" + path.endpoint +
                         " startpoint=" + path.startpoint +
                         " slack=" + FormatNum(path.slack_ns) +
                         " arrival=" + FormatNum(path.arrival_ns) +
                         " required=" + FormatNum(path.required_ns);
            }
          };
          emit_paths("setup", setup_paths.value());
          emit_paths("hold", hold_paths.value());
          const analysis::CoverageResult* scene_coverage =
              find_coverage(coverage.value(), scene);
          if (scene_coverage != nullptr) {
            std::size_t total_tested = 0;
            std::size_t total_untested = 0;
            for (const analysis::CoveragePin& pin : scene_coverage->pins) {
              if (pin.tested) {
                ++total_tested;
              } else {
                ++total_untested;
              }
            }
            payload += "\ncoverage scene=" + scene +
                       " checks=" +
                       std::to_string(scene_coverage->pins.size()) +
                       " tested=" + std::to_string(total_tested) +
                       " untested=" + std::to_string(total_untested);
            for (const char* type : kCoverageTypes) {
              std::size_t tested = 0;
              std::size_t untested = 0;
              for (const analysis::CoveragePin& pin :
                   scene_coverage->pins) {
                if (pin.check_type != type) continue;
                if (pin.tested) {
                  ++tested;
                } else {
                  ++untested;
                }
              }
              const std::size_t total = tested + untested;
              if (total == 0) continue;
              const double percent =
                  static_cast<double>(tested) * 100.0 /
                  static_cast<double>(total);
              payload += "\ncoverage scene=" + scene + " type=" + type +
                         " tested=" + std::to_string(tested) +
                         " untested=" + std::to_string(untested) +
                         " percent=" + FormatFixed(percent, 1);
            }
          }
          const analysis::QorResult* scene_qor =
              find_qor(qor.value(), scene);
          if (scene_qor != nullptr) {
            const auto [setup_union_tns, setup_union_violating] =
                union_tns(scene_qor->setup_groups);
            const auto [hold_union_tns, hold_union_violating] =
                union_tns(scene_qor->hold_groups);
            double setup_every_tns = 0.0;
            std::size_t setup_every_violating = 0;
            for (const analysis::PathGroupResult& group :
                 scene_qor->setup_groups) {
              const auto [wns, tns, violating] = group_tns(group);
              setup_every_tns += tns;
              setup_every_violating += violating;
            }
            double hold_every_tns = 0.0;
            std::size_t hold_every_violating = 0;
            for (const analysis::PathGroupResult& group :
                 scene_qor->hold_groups) {
              const auto [wns, tns, violating] = group_tns(group);
              hold_every_tns += tns;
              hold_every_violating += violating;
            }
            const double compile_cpu =
                (ctx.synth.mapped_cpu_s >= 0.0 ? ctx.synth.mapped_cpu_s
                                               : 0.0) +
                (ctx.synth.opt_cpu_s >= 0.0 ? ctx.synth.opt_cpu_s : 0.0);
            const bool compile_recorded =
                ctx.synth.mapped_cpu_s >= 0.0 || ctx.synth.opt_cpu_s >= 0.0;
            payload += "\nqor scene=" + scene +
                       " setup_groups=" +
                       std::to_string(scene_qor->setup_groups.size()) +
                       " hold_groups=" +
                       std::to_string(scene_qor->hold_groups.size()) +
                       " union_tns=" + FormatNum(setup_union_tns) +
                       " union_endpoints=" +
                       std::to_string(setup_union_violating) +
                       " every_group_tns=" + FormatNum(setup_every_tns) +
                       " every_group_endpoints=" +
                       std::to_string(setup_every_violating) +
                       " hold_union_tns=" + FormatNum(hold_union_tns) +
                       " hold_union_endpoints=" +
                       std::to_string(hold_union_violating) +
                       " hold_every_group_tns=" +
                       FormatNum(hold_every_tns) +
                       " hold_every_group_endpoints=" +
                       std::to_string(hold_every_violating) +
                       " cells=" + std::to_string(scene_qor->total_cells) +
                       " area=" + FormatNum(scene_qor->total_area) +
                       " compile_cpu_s=" +
                       (compile_recorded ? FormatNum(compile_cpu)
                                         : "n/a");
            for (const analysis::PathGroupResult& group :
                 scene_qor->setup_groups) {
              const auto [wns, tns, violating] = group_tns(group);
              payload += "\nqor scene=" + scene + " group=" + group.name +
                         " check=setup wns=" + FormatNum(wns) +
                         " tns=" + FormatNum(tns) +
                         " violating_paths=" + std::to_string(violating);
            }
            for (const analysis::PathGroupResult& group :
                 scene_qor->hold_groups) {
              const auto [wns, tns, violating] = group_tns(group);
              payload += "\nqor scene=" + scene + " group=" + group.name +
                         " check=hold wns=" + FormatNum(wns) +
                         " tns=" + FormatNum(tns) +
                         " violating_paths=" + std::to_string(violating);
            }
            for (const analysis::CellTypeRow& row : scene_qor->cells) {
              payload += "\nqor scene=" + scene + " cell=" + row.cell +
                         " count=" + std::to_string(row.count) +
                         " area=" + FormatNum(row.area);
            }
            const auto qor_drc = [&](const char* name,
                                     const analysis::ElectricalCheck& check) {
              // "none" is the no-limit value: a worst_slack of
              // "no limit" would be two tokens in this payload.
              payload += "\nqor scene=" + scene + " drc=" + name +
                         " violations=" +
                         std::to_string(check.violation_count) +
                         " worst_slack=" +
                         (check.has_limit ? FormatNum(check.worst_slack)
                                          : "none");
            };
            qor_drc("max_transition", scene_qor->max_transition_check);
            qor_drc("max_capacitance", scene_qor->max_capacitance_check);
            qor_drc("max_fanout", scene_qor->max_fanout_check);
          }
        }
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
  // name it prints just that command's usage; with a glob pattern
  // ('*' for any run of characters, '?' for one character) it
  // prints the usage line for every matching command, in registry
  // order. The "eos::" namespace prefix is accepted and stripped.
  // This is the discoverability entry point the shell's tab
  // completion points at: list_commands stays as the script-friendly
  // full listing.
  add("help", "help [command|pattern]",
      [&registry](CommandContext&, const std::vector<std::string>& args)
          -> CommandResult {
        if (args.size() > 1) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "usage: eos::help [command|pattern]");
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
        const bool wildcard =
            name.find('*') != std::string::npos ||
            name.find('?') != std::string::npos;
        if (!wildcard) {
          const CommandInfo* info = registry.Find(name);
          if (info == nullptr) {
            return CommandResult::Fail(Diag::kInvalidCommand,
                                       "unknown command: " + args[0]);
          }
          return CommandResult::Ok(info->name + " " + info->usage);
        }
        std::string out;
        for (const std::string& candidate : registry.Names()) {
          if (!GlobMatches(name, candidate)) continue;
          if (!out.empty()) out += "\n";
          out += candidate + " " + registry.Find(candidate)->usage;
        }
        if (out.empty()) {
          return CommandResult::Fail(Diag::kInvalidCommand,
                                     "no commands match: " + args[0]);
        }
        return CommandResult::Ok(out);
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
  RegisterGuiShowCommand(registry);
}

}  // namespace eos::ui
