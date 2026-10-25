// registry: the one command registry (L6 interface).
//
// Every EOS operation exists exactly once, here. The Tcl dialect, the
// Python API and the GUI adapter bind to this registry and inherit the
// same semantics; no interface keeps design state of its own or
// duplicates an operation (Revised Plan, Section 12).
//
// Commands live in the EOS product namespace: interfaces expose them
// as eos::<name>, so orchestration commands can never silently change
// the meaning of a native engine command. A command returns a
// CommandResult: a machine-readable payload (key=value lines, units
// explicit), a diagnostic from the taxonomy below, and a message.
// Unsupported options are rejected, never silently ignored.
//
// Diagnostics (Plan Section 12): the taxonomy distinguishes at least
// tool crash, invalid configuration, unsupported feature, unclosed
// design, proof unknown, and verified pass. Command failures use the
// matching code; kVerifiedPass and kProofUnknown are how verification
// style commands report an outcome that is a result, not an error.
//
// Collections are a defined type here, distinct from plain lists: a
// named, live handle over persistent object IDs. Listing a collection
// resolves names at call time; an object that no longer exists reads
// as <deleted>. Wildcard rules: '*' matches any run of characters,
// '?' exactly one; matching is anchored at both ends; the hierarchy
// separator is '/'; backslash escapes the next character literally.
//
// This header is a stable surface.
#ifndef EOS_UI_REGISTRY_H_
#define EOS_UI_REGISTRY_H_

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "eos/db/constraints.h"
#include "eos/db/design.h"
#include "eos/db/scenarios.h"
#include "eos/foundation/errors.h"
#include "eos/foundation/ids.h"
#include "eos/session/ledger.h"
#include "eos/session/project.h"

namespace eos::ui {

using foundation::ObjectId;
using foundation::Status;

enum class Diag : std::int32_t {
  kOk = 0,
  kInvalidCommand = 1,
  kInvalidConfiguration = 2,
  kUnsupportedFeature = 3,
  kUnclosedDesign = 4,
  kProofUnknown = 5,
  kVerifiedPass = 6,
  kToolCrash = 7,
  kIoError = 8,
};

const char* DiagName(Diag diag);

struct CommandResult {
  bool ok = false;
  Diag diag = Diag::kOk;
  std::string message;
  std::string payload;

  static CommandResult Ok(std::string payload);
  static CommandResult Fail(Diag diag, std::string message);
};

// The state commands operate on: the L5 Project (design, scenarios,
// constraints and ledger as one owned unit), plus the collections
// this interface session has minted. Owned by whoever hosts the
// session; interfaces borrow it. There is at most one open design,
// the "current design" of traditional tools.
struct CommandContext {
  session::Project project;

  // Collections by handle ("eos_coll_1", ...). Handles are never
  // reused within a context.
  std::map<std::string, std::vector<ObjectId>> collections;
  std::uint32_t next_collection = 1;

  bool HasDesign() const { return project.HasDesign(); }
};

using CommandHandler = std::function<CommandResult(
    CommandContext&, const std::vector<std::string>& args)>;

struct CommandInfo {
  std::string name;   // bare name, exposed by interfaces as eos::<name>
  std::string usage;  // one line, shown by help
  CommandHandler handler;
};

class CommandRegistry {
 public:
  // Registers a command. Fails with kInvalidArgument on a duplicate
  // name or an empty one.
  Status Register(std::string name, std::string usage,
                  CommandHandler handler);

  const CommandInfo* Find(const std::string& name) const;
  std::vector<std::string> Names() const;  // sorted

  // Runs a command by bare name. Unknown names fail with
  // kInvalidCommand; handler results pass through unchanged.
  CommandResult Dispatch(CommandContext& context, const std::string& name,
                         const std::vector<std::string>& args) const;

 private:
  std::map<std::string, CommandInfo> commands_;
};

// Registers the core command set: design lifecycle and construction,
// scenarios, constraints and coverage, collections, ledger
// transactions and artifacts, and the project-level checkpoint and
// restore commands (the full project file: design, scenarios,
// constraints and ledger).
void RegisterCoreCommands(CommandRegistry& registry);

}  // namespace eos::ui

#endif  // EOS_UI_REGISTRY_H_
