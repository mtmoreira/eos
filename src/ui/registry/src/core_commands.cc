// registry: the core command set (L6 interface).
//
// Every handler follows the same discipline: validate the argument
// shape first (unsupported options are rejected, never ignored), then
// the session state (an unclosed design is its own diagnostic), then
// run the operation and map its Status onto the diagnostic taxonomy.
#include "eos/ui/registry.h"

#include "eos/session/flow.h"
#include "eos/synth/abc.h"
#include "eos/synth/yosys.h"

#include <algorithm>
#include <atomic>
#include <cctype>
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
        {
          std::ifstream in(mapped);
          std::string line;
          while (std::getline(in, line)) {
            const std::size_t first =
                line.find_first_not_of(" \t");
            if (first == std::string::npos) continue;
            const std::size_t end = line.find_first_of(" \t", first);
            const std::string token =
                line.substr(first, end == std::string::npos
                                     ? std::string::npos
                                     : end - first);
            if (cell_set.count(token) == 0) continue;
            ++by_type[token];
            ++total_cells;
          }
        }
        std::string types;
        for (const auto& entry : by_type) {
          if (!types.empty()) types += ",";
          types += entry.first + ":" + std::to_string(entry.second);
        }
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
}

}  // namespace eos::ui
