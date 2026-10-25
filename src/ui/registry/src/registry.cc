// registry: the one command registry (L6 interface).
#include "eos/ui/registry.h"

#include <algorithm>
#include <utility>

namespace eos::ui {

const char* DiagName(Diag diag) {
  switch (diag) {
    case Diag::kOk: return "ok";
    case Diag::kInvalidCommand: return "invalid_command";
    case Diag::kInvalidConfiguration: return "invalid_configuration";
    case Diag::kUnsupportedFeature: return "unsupported_feature";
    case Diag::kUnclosedDesign: return "unclosed_design";
    case Diag::kProofUnknown: return "proof_unknown";
    case Diag::kVerifiedPass: return "verified_pass";
    case Diag::kToolCrash: return "tool_crash";
    case Diag::kIoError: return "io_error";
  }
  return "unknown";
}

CommandResult CommandResult::Ok(std::string payload) {
  CommandResult result;
  result.ok = true;
  result.diag = Diag::kOk;
  result.payload = std::move(payload);
  return result;
}

CommandResult CommandResult::Fail(Diag diag, std::string message) {
  CommandResult result;
  result.ok = false;
  result.diag = diag;
  result.message = std::move(message);
  return result;
}

Status CommandRegistry::Register(std::string name, std::string usage,
                                 CommandHandler handler) {
  if (name.empty() || commands_.count(name) != 0) {
    return Status::Error(foundation::ErrorCode::kInvalidArgument,
                         "duplicate or empty command name: " + name);
  }
  CommandInfo info;
  info.name = name;
  info.usage = std::move(usage);
  info.handler = std::move(handler);
  commands_[name] = std::move(info);
  return Status::Ok();
}

const CommandInfo* CommandRegistry::Find(const std::string& name) const {
  const auto it = commands_.find(name);
  return it == commands_.end() ? nullptr : &it->second;
}

std::vector<std::string> CommandRegistry::Names() const {
  std::vector<std::string> names;
  for (const auto& entry : commands_) {
    names.push_back(entry.first);
  }
  std::sort(names.begin(), names.end());
  return names;
}

CommandResult CommandRegistry::Dispatch(
    CommandContext& context, const std::string& name,
    const std::vector<std::string>& args) const {
  const CommandInfo* info = Find(name);
  if (info == nullptr) {
    return CommandResult::Fail(Diag::kInvalidCommand,
                               "unknown command: eos::" + name);
  }
  return info->handler(context, args);
}

}  // namespace eos::ui
