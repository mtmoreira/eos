// tcl: the EOS Tcl dialect over the command registry (L6 interface).
#include "eos/ui/tcl_shell.h"

#include <cstdio>
#include <iostream>
#include <utility>

#ifdef EOS_HAS_TCL
#include <tcl.h>
#endif

namespace eos::ui {

#ifdef EOS_HAS_TCL

namespace {

struct Binding {
  CommandRegistry* registry;
  CommandContext* context;
  std::string name;
};

int CommandProc(ClientData client_data, Tcl_Interp* interp, int objc,
                Tcl_Obj* const objv[]) {
  const auto* binding = static_cast<const Binding*>(client_data);
  std::vector<std::string> args;
  for (int i = 1; i < objc; ++i) {
    args.emplace_back(Tcl_GetString(objv[i]));
  }
  const CommandResult result =
      binding->registry->Dispatch(*binding->context, binding->name, args);
  if (result.ok) {
    Tcl_SetObjResult(interp,
                     Tcl_NewStringObj(result.payload.data(),
                                      static_cast<int>(result.payload.size())));
    return TCL_OK;
  }
  const std::string message =
      "EOS " + std::string(DiagName(result.diag)) + ": " + result.message;
  Tcl_SetObjResult(
      interp, Tcl_NewStringObj(message.data(), static_cast<int>(message.size())));
  return TCL_ERROR;
}

}  // namespace

struct TclShell::Impl {
  Tcl_Interp* interp = nullptr;
  std::vector<std::unique_ptr<Binding>> bindings;
  ~Impl() {
    if (interp != nullptr) {
      Tcl_DeleteInterp(interp);
    }
  }
};

foundation::Result<std::unique_ptr<TclShell>> TclShell::Create(
    CommandRegistry& registry, CommandContext& context) {
  std::unique_ptr<TclShell> shell(new TclShell());
  shell->impl_ = std::make_unique<Impl>();
  shell->impl_->interp = Tcl_CreateInterp();
  if (shell->impl_->interp == nullptr) {
    return foundation::Status::Error(foundation::ErrorCode::kIoError,
                                     "cannot create a Tcl interpreter");
  }
  if (Tcl_Init(shell->impl_->interp) != TCL_OK) {
    return foundation::Status::Error(
        foundation::ErrorCode::kIoError,
        std::string("Tcl_Init failed: ") +
            Tcl_GetStringResult(shell->impl_->interp));
  }
  for (const std::string& name : registry.Names()) {
    auto binding = std::make_unique<Binding>();
    binding->registry = &registry;
    binding->context = &context;
    binding->name = name;
    const std::string tcl_name = "::eos::" + name;
    Tcl_CreateObjCommand(shell->impl_->interp, tcl_name.c_str(), &CommandProc,
                         static_cast<ClientData>(binding.get()), nullptr);
    shell->impl_->bindings.push_back(std::move(binding));
  }
  return shell;
}

TclShell::~TclShell() = default;

foundation::Result<std::string> TclShell::Eval(const std::string& script) {
  if (Tcl_Eval(impl_->interp, script.c_str()) != TCL_OK) {
    return foundation::Status::Error(
        foundation::ErrorCode::kInvalidArgument,
        Tcl_GetStringResult(impl_->interp));
  }
  return std::string(Tcl_GetStringResult(impl_->interp));
}

int TclShell::RunFiles(const std::vector<std::string>& files) {
  for (const std::string& file : files) {
    if (Tcl_EvalFile(impl_->interp, file.c_str()) != TCL_OK) {
      std::fprintf(stderr, "%s: %s\n", file.c_str(),
                   Tcl_GetStringResult(impl_->interp));
      return 1;
    }
    const char* result = Tcl_GetStringResult(impl_->interp);
    if (result != nullptr && result[0] != '\0') {
      std::printf("%s\n", result);
    }
  }
  return 0;
}

int TclShell::RunInteractive() {
  std::string line;
  for (;;) {
    std::printf("eos> ");
    std::fflush(stdout);
    if (!std::getline(std::cin, line)) {
      std::printf("\n");
      return 0;
    }
    const foundation::Result<std::string> result = Eval(line);
    if (!result.ok()) {
      std::fprintf(stderr, "error: %s\n", result.status().message().c_str());
    } else if (!result.value().empty()) {
      std::printf("%s\n", result.value().c_str());
    }
  }
}

#else  // EOS_HAS_TCL

struct TclShell::Impl {};

foundation::Result<std::unique_ptr<TclShell>> TclShell::Create(
    CommandRegistry&, CommandContext&) {
  return foundation::Status::Error(
      foundation::ErrorCode::kUnsupported,
      "this build has no Tcl: the EOS shell is unavailable");
}

TclShell::~TclShell() = default;

foundation::Result<std::string> TclShell::Eval(const std::string&) {
  return foundation::Status::Error(foundation::ErrorCode::kUnsupported,
                                   "this build has no Tcl");
}

int TclShell::RunFiles(const std::vector<std::string>&) { return 1; }

int TclShell::RunInteractive() { return 1; }

#endif  // EOS_HAS_TCL

}  // namespace eos::ui
