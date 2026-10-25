// registry: the core command set (L6 interface).
//
// Every handler follows the same discipline: validate the argument
// shape first (unsupported options are rejected, never ignored), then
// the session state (an unclosed design is its own diagnostic), then
// run the operation and map its Status onto the diagnostic taxonomy.
#include "eos/ui/registry.h"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>


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

  add("register_report", "register_report <name> [pass|fail|unknown]",
      [](CommandContext& ctx, const std::vector<std::string>& args)
          -> CommandResult {
        if (args.empty() || args.size() > 2) {
          return CommandResult::Fail(
              Diag::kInvalidConfiguration,
              "usage: eos::register_report <name> [pass|fail|unknown]");
        }
        session::ProofStatus proof = session::ProofStatus::kUnknown;
        if (args.size() == 2) {
          const auto parsed = ParseProof(args[1]);
          if (!parsed.has_value()) {
            return CommandResult::Fail(
                Diag::kInvalidConfiguration,
                "proof must be pass, fail or unknown, got: " + args[1]);
          }
          proof = *parsed;
        }
        const session::ArtifactId id = ctx.project.ledger.RegisterArtifact(
            session::ArtifactKind::kReport, args[0], proof);
        return CommandResult::Ok(
            "artifact=" + std::to_string(id) + " revision=" +
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
        const Result<db::RestoreInfo> info =
            ctx.project.LoadProject(args[0]);
        if (!info.ok()) return FromStatus(info.status());
        // Collections named objects of the previous project state;
        // they do not survive a restore.
        ctx.collections.clear();
        ctx.next_collection = 1;
        return CommandResult::Ok(
            "restored=1 design=" + ctx.project.design->name() +
            " revision=" +
            std::to_string(ctx.project.ledger.current_revision()) +
            " unresolved=" +
            std::to_string(info.value().constraints_marked_unresolved));
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
