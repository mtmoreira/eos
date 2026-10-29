// tcl: the EOS Tcl dialect (L6 interface).
//
// A thin binding: every command in the registry becomes a Tcl command
// in the ::eos namespace, with identical semantics, because both call
// the same registry dispatch. A command failure becomes a Tcl error
// whose message is "EOS <diag_name>: <message>", so scripts can catch
// failures and still see the diagnostic taxonomy. Plain Tcl remains
// fully available around the EOS commands.
//
// The shell is only as real as the Tcl it links: when the build found
// no Tcl development package, Create reports kUnsupportedFeature.
//
// This header is a stable surface.
#ifndef EOS_UI_TCL_SHELL_H_
#define EOS_UI_TCL_SHELL_H_

#include <memory>
#include <string>
#include <vector>

#include "eos/foundation/errors.h"
#include "eos/ui/registry.h"

namespace eos::ui {

class TclShell {
 public:
  // Creates an interpreter and registers every command in the
  // registry as ::eos::<name>, bound to the given context.
  static foundation::Result<std::unique_ptr<TclShell>> Create(
      CommandRegistry& registry, CommandContext& context);

  ~TclShell();
  TclShell(const TclShell&) = delete;
  TclShell& operator=(const TclShell&) = delete;

  // Evaluates a script. On success, the value is the script's result
  // string. On a Tcl error (including a failed EOS command), the
  // status carries the interpreter's message.
  foundation::Result<std::string> Eval(const std::string& script);

  // Runs script files in order; prints each file's final result when
  // non-empty, errors to stderr. Returns 0 when every file succeeded,
  // 1 at the first failure.
  int RunFiles(const std::vector<std::string>& files);

  // Interactive loop with an "eos> " prompt: line editing with
  // history (arrow keys) and tab completion of ::eos:: command
  // names, backed by a persistent history file (.eos_history in
  // the launch directory) when stdin is a terminal. The file is
  // saved after every command, so history survives any exit path.
  // Piped stdin reads plainly with no prompt and touches no
  // history. Returns 0 at EOF; Ctrl-C cancels the current line
  // without leaving the shell.
  int RunInteractive();

 private:
  TclShell() = default;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace eos::ui

#endif  // EOS_UI_TCL_SHELL_H_
