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
#include <optional>
#include <string>
#include <vector>

#include "eos/foundation/errors.h"
#include "eos/ui/registry.h"

namespace eos::ui {

// The shared interactive history file: $HOME/.eos_history, or
// .eos_history in the launch directory when HOME is unset. One
// file serves the user from every working directory.
std::string HistoryFilePath();

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
  // status carries the interpreter's message, and its code is the
  // code the failed command produced, mapped back from the
  // "EOS <diag>: ..." prefix (a non-EOS Tcl error keeps
  // kInvalidArgument).
  foundation::Result<std::string> Eval(const std::string& script);

  // The engine log lines the last failed Eval (or RunFiles file)
  // drained, in order: the lines the failed command appended to
  // the context's engine log. Empty after a success, and empty
  // when the build has no Tcl.
  const std::vector<std::string>& LastFailedEngineLog() const;

  // Runs script files in order; prints each file's final result when
  // non-empty, errors to stderr. Returns 0 when every file succeeded,
  // 1 at the first failure.
  int RunFiles(const std::vector<std::string>& files);

  // Interactive loop with an "eos> " prompt: line editing with
  // history (arrow keys) and tab completion of ::eos:: command
  // names, backed by a persistent history file shared with the
  // GUI console ($HOME/.eos_history): a command typed in either
  // entry is recallable with up-arrow in the other, and the file
  // is the same from every working directory. The file is saved
  // after every command, so history survives any exit path.
  // Piped stdin reads plainly with no prompt and touches no
  // history. Returns 0 at EOF; Ctrl-C cancels the current line
  // without leaving the shell.
  int RunInteractive();

  // One interactive line for callers that run their own loop,
  // such as the GUI-mode terminal REPL (U6): the same linenoise
  // editing, history, and tab completion as RunInteractive, on
  // the same shared history file. Call InitInteractiveHistory
  // once before the first read: it installs the completion
  // callback and loads the history file when stdin is a
  // terminal. Before each prompt, lines other entries appended
  // to the file since the last read are loaded, so a command
  // typed in the GUI console is recallable here with up-arrow.
  // ReadInteractiveLine returns nullopt at EOF (Ctrl-D on an
  // empty line, or closed stdin); Ctrl-C cancels the current
  // line and re-prompts instead of returning. Every non-empty
  // line read is appended to history and saved. Piped stdin
  // reads plainly with no prompt and touches no history.
  void InitInteractiveHistory();
  std::optional<std::string> ReadInteractiveLine(const char* prompt);

  // Records a line typed in another entry (the GUI console) in
  // the shared history file, so the terminal REPL can recall it
  // with up-arrow. The line must already be in "eos::<name> ..."
  // form, valid Tcl: the console feed normalizes it before
  // calling. Appends to the file only; the REPL loads the new
  // line at its next prompt and while it sits idle at a prompt.
  // The caller installs this feed only for interactive sessions
  // (stdin a terminal), so piped stdin never writes history.
  // Empty lines are ignored, and a failed file write is silently
  // ignored: history must never break the shell.
  void AddHistoryLine(const std::string& line);

 private:
  TclShell() = default;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace eos::ui

#endif  // EOS_UI_TCL_SHELL_H_
