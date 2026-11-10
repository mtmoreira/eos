// command_helpers: shared fail helpers for the registry commands.
//
// core_commands.cc keeps the CommandResult::Fail(Diag::...) idiom for
// its hand-written sites; repair_commands.cc adopts these helpers.
// A helper names the failure kind in the call, so the diag code a
// script matches on follows from the helper chosen, not from a
// token the reader must check: FailArgument for bad arguments,
// FailNotFound for a name that resolves to nothing, FailState for
// the wrong session state, FailStale for a stale revision,
// FailRange for an out-of-range index, FailConfig for a genuine
// configuration failure, FailIo for a file or IO failure, FailUsage
// for a wrong argument count or usage line.
#ifndef EOS_UI_REGISTRY_COMMAND_HELPERS_H_
#define EOS_UI_REGISTRY_COMMAND_HELPERS_H_

#include <string>

#include "eos/ui/registry.h"

namespace eos::ui {
namespace {

inline CommandResult FailArgument(const std::string& message) {
  return CommandResult::Fail(Diag::kInvalidArgument, message);
}

inline CommandResult FailNotFound(const std::string& message) {
  return CommandResult::Fail(Diag::kNotFound, message);
}

inline CommandResult FailState(const std::string& message) {
  return CommandResult::Fail(Diag::kInvalidState, message);
}

inline CommandResult FailStale(const std::string& message) {
  return CommandResult::Fail(Diag::kStaleRevision, message);
}

inline CommandResult FailRange(const std::string& message) {
  return CommandResult::Fail(Diag::kOutOfRange, message);
}

inline CommandResult FailConfig(const std::string& message) {
  return CommandResult::Fail(Diag::kInvalidConfiguration, message);
}

inline CommandResult FailIo(const std::string& message) {
  return CommandResult::Fail(Diag::kIoError, message);
}

inline CommandResult FailUsage(const std::string& message) {
  return CommandResult::Fail(Diag::kInvalidArgument, message);
}

}  // namespace
}  // namespace eos::ui

#endif  // EOS_UI_REGISTRY_COMMAND_HELPERS_H_
