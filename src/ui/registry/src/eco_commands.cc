// eco_commands: the per-cell ECO edit commands (eos::size_cell,
// eos::swap_cell, eos::insert_buffer, eos::remove_buffer).
//
// Four direct manual edits on the live session database
// (ctx.timing.odb), through the timing ECO adapters
// (src/eco/timing-eco). The session posture is close_timing's,
// not repair_timing's: an edit command that loaded its own copy
// of the design would edit a copy nobody else sees, so these
// handlers require the session's physical database and act on it
// in place.
//
// A successful edit applies immediately and unconditionally once
// its validation passes: no ECO transaction, no acceptance
// judging, no ledger entry. The transaction protocol is the
// acceptance path for edits the tool proposes (the closure
// loop); a command the user typed names the exact edit they
// want. The safety net is the project checkpoint and restore
// commands, and re-import.
//
// On success each command clears the timing session's stored
// results and both bottleneck path lists: after an edit, those
// records describe a design that no longer exists, and stale
// paths must never serve a later report. The commands do not
// re-run STA; re-analysis is one command away (run_timing, or
// close_timing for the judged loop), and timing_summary answers
// with its no-results failure until then, which is the honest
// state. Signoff and DRC records are left alone: the design
// fingerprint changes with every edit, so their stored verdicts
// flag themselves stale by their designed mechanism.
#include "eos/ui/registry.h"
#include "command_helpers.h"

#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "eos/eco/timing_eco.h"

namespace eos::ui {
namespace {

using foundation::ErrorCode;

// The foundation-to-Diag map, identical to core_commands.cc's
// file-local FromStatus: each foundation code maps to its Diag
// twin, kUnsupported to kUnsupportedFeature.
CommandResult FromStatus(const foundation::Status& status) {
  Diag diag = Diag::kInvalidConfiguration;
  switch (status.code()) {
    case ErrorCode::kOk: diag = Diag::kOk; break;
    case ErrorCode::kInvalidArgument: diag = Diag::kInvalidArgument; break;
    case ErrorCode::kNotFound: diag = Diag::kNotFound; break;
    case ErrorCode::kInvalidState: diag = Diag::kInvalidState; break;
    case ErrorCode::kStaleRevision: diag = Diag::kStaleRevision; break;
    case ErrorCode::kOutOfRange: diag = Diag::kOutOfRange; break;
    case ErrorCode::kIoError: diag = Diag::kIoError; break;
    case ErrorCode::kUnsupported: diag = Diag::kUnsupportedFeature; break;
    case ErrorCode::kInvalidConfiguration:
      diag = Diag::kInvalidConfiguration;
      break;
  }
  return CommandResult::Fail(diag, status.message());
}

// The session precondition shared by all four commands, checked
// after argument parsing, before any lookup: the edits act on
// the physical database, so the session must hold one with a top
// block (eos::import_odb, or eos::read_lef plus eos::read_def).
// No SDC, scenario, or Liberty precondition: the edits are
// structural and need masters and connectivity, which the
// database holds.
std::optional<CommandResult> NeedEcoDesign(const CommandContext& ctx) {
  if (!ctx.timing.odb.has_value() || !ctx.timing.odb->has_top_block()) {
    return CommandResult::Fail(
        Diag::kUnclosedDesign,
        "no ODB design: eos::import_odb first (per-cell edits act "
        "on the physical database; a Verilog source has no "
        "instances to edit)");
  }
  return std::nullopt;
}

// What a successful edit does to the timing session (see the
// banner): the stored results and captured paths describe the
// pre-edit design, so they are cleared, never left to be read as
// current.
void ClearTimingAfterEdit(CommandContext& ctx) {
  ctx.timing.last_results.clear();
  ctx.timing.last_bottleneck_setup_paths.clear();
  ctx.timing.last_bottleneck_hold_paths.clear();
}

bool ParseDoubleStrict(const std::string& token, double& out) {
  const char* first = token.data();
  const char* last = first + token.size();
  const auto result = std::from_chars(first, last, out);
  return result.ec == std::errc() && result.ptr == last &&
         std::isfinite(out);
}

std::size_t CountLoads(const db::OdbDatabase& database,
                       const std::string& net) {
  std::size_t loads = 0;
  for (const db::OdbNetConnection& connection :
       database.NetConnections(net)) {
    if (!connection.is_driver) ++loads;
  }
  return loads;
}

CommandResult SizeCellHandler(CommandContext& ctx,
                              const std::vector<std::string>& args) {
  if (args.size() != 2) {
    return FailUsage("usage: eos::size_cell <instance> <new_master>");
  }
  if (auto bad = NeedEcoDesign(ctx)) return *bad;
  db::OdbDatabase& database = *ctx.timing.odb;
  const std::string& instance = args[0];
  const std::string& target = args[1];
  db::OdbInstanceInfo info;
  if (!database.FindInstance(instance, &info)) {
    return FailNotFound("instance not found: " + instance);
  }
  if (target != info.master) {
    if (database.MasterPins(target).empty()) {
      return FailNotFound("master not found: " + target);
    }
    // The command-layer gate: the target must be a size variant
    // of the instance's master under the shared family definition
    // (same name stem, identical pin signature, family of at
    // least two), so the failure says "not a size variant" rather
    // than the adapter's pin wording. The same-master case skips
    // the gate; the adapter names it below.
    bool same_family = false;
    for (const std::vector<std::string>& family :
         eco::SizingFamilies(database)) {
      bool has_current = false;
      bool has_target = false;
      for (const std::string& member : family) {
        if (member == info.master) has_current = true;
        if (member == target) has_target = true;
      }
      if (has_current && has_target) {
        same_family = true;
        break;
      }
    }
    if (!same_family) {
      return FailArgument(
          "master " + target + " is not a size variant of " +
          info.master +
          ": size_cell swaps within one sizing family (same name "
          "stem, identical signal pins)");
    }
  }
  const foundation::Status status =
      eco::ResizeInstance(database, instance, target);
  if (!status.ok()) return FromStatus(status);
  ClearTimingAfterEdit(ctx);
  return CommandResult::Ok("instance=" + instance +
                           " from_master=" + info.master +
                           " to_master=" + target +
                           " timing_results=cleared");
}

CommandResult SwapCellHandler(CommandContext& ctx,
                              const std::vector<std::string>& args) {
  if (args.size() != 2) {
    return FailUsage("usage: eos::swap_cell <instance> <new_master>");
  }
  if (auto bad = NeedEcoDesign(ctx)) return *bad;
  db::OdbDatabase& database = *ctx.timing.odb;
  const std::string& instance = args[0];
  const std::string& target = args[1];
  db::OdbInstanceInfo info;
  if (!database.FindInstance(instance, &info)) {
    return FailNotFound("instance not found: " + instance);
  }
  // No family restriction: the adapter's pin check is the only
  // structural gate, so connectivity is preserved by construction.
  // Logic function is explicitly not checked; preserving it is
  // the caller's responsibility (see the design record).
  const foundation::Status status =
      eco::ResizeInstance(database, instance, target);
  if (!status.ok()) return FromStatus(status);
  ClearTimingAfterEdit(ctx);
  return CommandResult::Ok("instance=" + instance +
                           " from_master=" + info.master +
                           " to_master=" + target +
                           " timing_results=cleared");
}

const char kInsertUsage[] =
    "insert_buffer <net> -buffer_cell <master> [-instance_name "
    "<name>] [-net_name <name>] [-position <0..1>]";

struct InsertOptions {
  std::string net;
  std::optional<std::string> buffer_cell;
  std::optional<std::string> instance_name;
  std::optional<std::string> net_name;
  double position = 0.0;
};

// Parses the insert_buffer arguments in the ParseRepairArgs
// idiom: one positional (the net), then -key value options.
// Unknown options, missing values, extra positionals, and a
// non-numeric -position fail kInvalidArgument with the usage
// line; an out-of-range -position fails kOutOfRange.
std::optional<CommandResult> ParseInsertArgs(
    const std::vector<std::string>& args, InsertOptions& options) {
  const std::string usage = " (usage: eos::" + std::string(kInsertUsage) + ")";
  bool position_seen = false;
  std::string position_text;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& token = args[i];
    if (token == "-buffer_cell" || token == "-instance_name" ||
        token == "-net_name" || token == "-position") {
      if (i + 1 >= args.size()) {
        return FailArgument("missing value for " + token + usage);
      }
      const std::string& value = args[++i];
      if (token == "-buffer_cell") {
        options.buffer_cell = value;
      } else if (token == "-instance_name") {
        options.instance_name = value;
      } else if (token == "-net_name") {
        options.net_name = value;
      } else {
        position_seen = true;
        position_text = value;
        if (!ParseDoubleStrict(value, options.position)) {
          return FailArgument("bad numeric value for -position: " +
                              value + usage);
        }
      }
    } else if (!token.empty() && token[0] == '-') {
      return FailArgument("unknown option: " + token + usage);
    } else if (options.net.empty()) {
      options.net = token;
    } else {
      return FailArgument("unexpected argument: " + token + usage);
    }
  }
  if (options.net.empty()) {
    return FailUsage("usage: eos::" + std::string(kInsertUsage));
  }
  if (!options.buffer_cell.has_value()) {
    return FailArgument("missing required option -buffer_cell" + usage);
  }
  if (position_seen &&
      !(options.position >= 0.0 && options.position <= 1.0)) {
    return FailRange("-position must be between 0 and 1, got: " +
                     position_text);
  }
  return std::nullopt;
}

// The generated name for an inserted buffer or its net: the base,
// or the base with a numeric suffix (_1, _2, ...) when taken.
// Deterministic for a given database state; explicit names are
// never suffixed (the adapter refuses a taken explicit name).
std::string GeneratedEcoName(const db::OdbDatabase& database,
                             const std::string& base, bool instance) {
  const auto taken = [&database, instance](const std::string& name) {
    return instance ? database.HasInstance(name) : database.HasNet(name);
  };
  if (!taken(base)) return base;
  for (int suffix = 1;; ++suffix) {
    const std::string candidate =
        base + "_" + std::to_string(suffix);
    if (!taken(candidate)) return candidate;
  }
}

CommandResult InsertBufferHandler(CommandContext& ctx,
                                  const std::vector<std::string>& args) {
  InsertOptions options;
  if (auto bad = ParseInsertArgs(args, options)) return *bad;
  if (auto bad = NeedEcoDesign(ctx)) return *bad;
  db::OdbDatabase& database = *ctx.timing.odb;
  const std::string instance_name =
      options.instance_name.has_value()
          ? *options.instance_name
          : GeneratedEcoName(database, options.net + "_eco_buf",
                             /*instance=*/true);
  const std::string new_net_name =
      options.net_name.has_value()
          ? *options.net_name
          : GeneratedEcoName(database, options.net + "_eco_buf_net",
                             /*instance=*/false);
  const std::size_t loads_moved = CountLoads(database, options.net);
  const foundation::Status status = eco::InsertBuffer(
      database, options.net, *options.buffer_cell, instance_name,
      new_net_name, options.position);
  if (!status.ok()) return FromStatus(status);
  ClearTimingAfterEdit(ctx);
  return CommandResult::Ok(
      "net=" + options.net + " buffer_instance=" + instance_name +
      " buffer_master=" + *options.buffer_cell +
      " new_net=" + new_net_name +
      " loads_moved=" + std::to_string(loads_moved) +
      " timing_results=cleared");
}

// The payload facts of a remove_buffer call, read from the
// database before the edit: the nets on the buffer's two pins
// and the load count on the output net. Meaningful only when the
// instance's master is pin-shaped like a buffer (exactly one
// input and one output pin); any other shape is the adapter's
// refusal to report, and the facts stay at their defaults.
struct RemoveFacts {
  std::string in_net;
  std::string out_net;
  std::size_t loads_moved = 0;
};

RemoveFacts ScanRemoveFacts(const db::OdbDatabase& database,
                            const std::string& instance,
                            const std::string& master) {
  RemoveFacts facts;
  std::string input_pin;
  std::string output_pin;
  std::size_t inputs = 0;
  std::size_t outputs = 0;
  for (const db::OdbMasterPin& pin : database.MasterPins(master)) {
    if (pin.is_input && !pin.is_output) {
      input_pin = pin.name;
      ++inputs;
    }
    if (pin.is_output && !pin.is_input) {
      output_pin = pin.name;
      ++outputs;
    }
  }
  if (inputs != 1 || outputs != 1) return facts;
  for (const std::string& net : database.NetNames()) {
    for (const db::OdbNetConnection& connection :
         database.NetConnections(net)) {
      if (connection.is_port || connection.instance != instance) {
        continue;
      }
      if (connection.pin == input_pin) facts.in_net = net;
      if (connection.pin == output_pin) facts.out_net = net;
    }
  }
  if (!facts.out_net.empty()) {
    facts.loads_moved = CountLoads(database, facts.out_net);
  }
  return facts;
}

CommandResult RemoveBufferHandler(CommandContext& ctx,
                                  const std::vector<std::string>& args) {
  if (args.size() != 1) {
    return FailUsage("usage: eos::remove_buffer <instance>");
  }
  if (auto bad = NeedEcoDesign(ctx)) return *bad;
  db::OdbDatabase& database = *ctx.timing.odb;
  const std::string& instance = args[0];
  db::OdbInstanceInfo info;
  if (!database.FindInstance(instance, &info)) {
    return FailNotFound("instance not found: " + instance);
  }
  const RemoveFacts facts = ScanRemoveFacts(database, instance, info.master);
  const foundation::Status status = eco::RemoveBuffer(database, instance);
  if (!status.ok()) return FromStatus(status);
  ClearTimingAfterEdit(ctx);
  return CommandResult::Ok(
      "instance=" + instance + " master=" + info.master +
      " net_restored=" + (facts.in_net.empty() ? "none" : facts.in_net) +
      " net_removed=" + (facts.out_net.empty() ? "none" : facts.out_net) +
      " loads_moved=" + std::to_string(facts.loads_moved) +
      " timing_results=cleared");
}

}  // namespace
}  // namespace eos::ui

namespace eos::ui {

void RegisterEcoCommands(CommandRegistry& registry) {
  (void)registry.Register("size_cell",
                          "size_cell <instance> <new_master>",
                          SizeCellHandler);
  (void)registry.Register("swap_cell",
                          "swap_cell <instance> <new_master>",
                          SwapCellHandler);
  (void)registry.Register("insert_buffer", kInsertUsage,
                          InsertBufferHandler);
  (void)registry.Register("remove_buffer", "remove_buffer <instance>",
                          RemoveBufferHandler);
}

}  // namespace eos::ui
