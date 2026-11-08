// tcl: the EOS Tcl dialect over the command registry (L6 interface).
#include "eos/ui/tcl_shell.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

#ifdef EOS_HAS_TCL
#include <tcl.h>

#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <poll.h>
#include <unistd.h>
#include <vector>

#include "linenoise.h"
#endif

namespace eos::ui {

// The shared interactive history file: $HOME/.eos_history, so one
// file serves the user, not one per working directory. A command
// typed in the GUI console in demo/synth must be recallable with
// up-arrow in the terminal REPL launched from demo/hier, and vice
// versa. When HOME is unset, fall back to .eos_history in the
// launch directory.
std::string HistoryFilePath() {
  const char* home = std::getenv("HOME");
  if (home != nullptr && home[0] != '\0') {
    return std::string(home) + "/.eos_history";
  }
  return ".eos_history";
}

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

// The command registry the completion callback completes against.
// linenoise takes a plain function pointer, so the callback reads
// this file-static, set for the duration of the interactive loop.
// Only one shell runs per process, and the loop is single-threaded.
const CommandRegistry* g_completion_registry = nullptr;

// Tab completion for ::eos:: command names. An empty or partial
// namespace prefix completes to "eos::"; a partial command name
// completes to every command it prefixes. linenoise replaces the
// whole line with the completion, so completions carry the full
// "eos::<name>" text. Option names are not completed: that is a
// later step, once commands expose their options to the shell.
void CompleteCommand(const char* buffer, linenoiseCompletions* completions) {
  if (g_completion_registry == nullptr) return;
  const std::string line = buffer;
  const std::string marker = "eos::";
  if (line.size() < marker.size() && marker.rfind(line, 0) == 0) {
    linenoiseAddCompletion(completions, "eos::");
    return;
  }
  if (line.rfind(marker, 0) == 0) {
    const std::string rest = line.substr(marker.size());
    for (const std::string& name : g_completion_registry->Names()) {
      if (name.rfind(rest, 0) == 0) {
        linenoiseAddCompletion(completions, ("eos::" + name).c_str());
      }
    }
    return;
  }
  // A bare first word completes to the eos:: form, like the GUI
  // console: the terminal is a Tcl shell, so a bare name would
  // fail as a command, and completing it to eos::<name> is the
  // helpful form. Only a single word completes; once arguments
  // follow, there is nothing to complete.
  if (line.find(' ') != std::string::npos ||
      line.find('\t') != std::string::npos) {
    return;
  }
  for (const std::string& name : g_completion_registry->Names()) {
    if (name.rfind(line, 0) == 0) {
      linenoiseAddCompletion(completions, ("eos::" + name).c_str());
    }
  }
}

// Lines of the history file already reflected in the in-memory
// linenoise history, and the mutex serializing every history file
// operation. The terminal REPL and the GUI console both append to
// the same file: the console appends on the GUI thread
// (AddHistoryLine), the REPL rewrites the file on save and loads
// the tail it has not seen yet before each prompt and while it
// sits idle at a prompt (SyncHistoryFromFile, called from the
// poll loop in ReadInteractiveLine). The mutex is held only for
// those short file operations, never across the line read, so the
// console feed never waits on the terminal and the terminal never
// waits on the console. History must never break the shell, so a
// failed file operation is silently ignored.
std::mutex g_history_file_mutex;
std::size_t g_history_synced_lines = 0;

// The history file is saved after every command, not only at
// exit, so the session's history survives any exit path: Ctrl-D,
// Tcl's exit command, or the terminal window closing.
std::size_t CountHistoryFileLines(const std::string& path) {
  std::ifstream in(path);
  if (!in) return 0;
  std::size_t count = 0;
  std::string line;
  while (std::getline(in, line)) ++count;
  return count;
}

// Loads the tail of the history file the in-memory history has
// not seen yet: lines another entry (the GUI console) appended
// since the last sync. Runs before each prompt and while the
// terminal sits idle at a prompt (from the poll loop in
// ReadInteractiveLine), so a GUI-typed command is recallable
// with up-arrow without submitting a line first. The caller is
// responsible for only syncing while the editor is not walking
// history and not completing: appending mid-navigation would
// shift the indices the editor walks. Returns how many lines
// were added to the in-memory history. The file only grows by
// appends in this process, so a smaller file means an external
// edit: adopt its size without reloading, which avoids re-adding
// entries already in memory.
std::size_t SyncHistoryFromFile() {
  std::lock_guard<std::mutex> lock(g_history_file_mutex);
  const std::string path = HistoryFilePath();
  std::ifstream in(path);
  if (!in) return 0;
  std::vector<std::string> lines;
  std::string raw;
  while (std::getline(in, raw)) {
    for (char& c : raw) {
      if (c == '\r') c = '\n';
    }
    lines.push_back(raw);
  }
  std::size_t added = 0;
  if (lines.size() > g_history_synced_lines) {
    for (std::size_t i = g_history_synced_lines; i < lines.size(); ++i) {
      added += static_cast<std::size_t>(linenoiseHistoryAdd(lines[i].c_str()));
    }
  }
  g_history_synced_lines = lines.size();
  return added;
}

}  // namespace

struct TclShell::Impl {
  Tcl_Interp* interp = nullptr;
  const CommandRegistry* registry = nullptr;
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
  shell->impl_->registry = &registry;
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
  // foreach_in_collection evaluates its body in this interpreter:
  // set the loop variable to one member's display name, then eval
  // the body. The hook is empty in hosts without a Tcl interpreter
  // (the GUI console), where the command fails fast instead. The
  // interpreter outlives every use of the context: both belong to
  // the shell's process, and the hook is never called after the
  // shell is destroyed.
  context.eval_script =
      [interp = shell->impl_->interp](
          const std::string& var, const std::string& value,
          const std::string& body, std::string& error) {
        if (Tcl_SetVar(interp, var.c_str(), value.c_str(), 0) ==
            nullptr) {
          error = "cannot set variable: " + var;
          return false;
        }
        if (Tcl_Eval(interp, body.c_str()) != TCL_OK) {
          error = Tcl_GetStringResult(interp);
          return false;
        }
        return true;
      };
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
  // The loop shares the interactive reader with the GUI-mode
  // terminal REPL, so both CLIs edit, complete, and remember
  // identically. On a terminal the reader prints the prompt
  // itself and handles arrow keys, Ctrl-C (cancel the line and
  // re-prompt) and Ctrl-D on an empty line (end of input); on
  // a pipe it reads lines plainly and prints no prompt, so
  // piped runs keep working. History is a terminal-only
  // feature: piped runs neither read nor write the history file.
  InitInteractiveHistory();
  for (;;) {
    std::optional<std::string> line = ReadInteractiveLine("eos> ");
    if (!line.has_value()) {
      std::printf("\n");
      break;
    }
    const foundation::Result<std::string> result = Eval(*line);
    if (!result.ok()) {
      std::fprintf(stderr, "error: %s\n", result.status().message().c_str());
    } else if (!result.value().empty()) {
      std::printf("%s\n", result.value().c_str());
    }
  }
  g_completion_registry = nullptr;
  return 0;
}

void TclShell::InitInteractiveHistory() {
  linenoiseSetCompletionCallback(&CompleteCommand);
  linenoiseHistorySetMaxLen(1000);
  if (isatty(STDIN_FILENO) != 0) {
    const std::string path = HistoryFilePath();
    std::lock_guard<std::mutex> lock(g_history_file_mutex);
    linenoiseHistoryLoad(path.c_str());
    g_history_synced_lines = CountHistoryFileLines(path);
  }
  g_completion_registry = impl_->registry;
}

std::optional<std::string> TclShell::ReadInteractiveLine(
    const char* prompt) {
  if (isatty(STDIN_FILENO) == 0) {
    // Piped stdin: plain reads with no prompt and no history,
    // exactly as before. linenoise handles the non-tty case
    // itself.
    for (;;) {
      errno = 0;
      char* line = linenoise(prompt);
      if (line == nullptr) return std::nullopt;
      std::string out(line);
      linenoiseFree(line);
      return out;
    }
  }
  // Terminal: drive the multiplexed linenoise API directly
  // instead of the blocking linenoise() call, so the history
  // file can be re-checked while the user sits idle at the
  // prompt. A command typed in the GUI console is then
  // recallable with up-arrow without submitting a line first.
  // poll() waits for input with a short timeout; the read
  // itself stays blocking, so key handling, pasting, and
  // escape sequences behave exactly as in the stock blocking
  // call. The edit buffer uses the same sizes as that call
  // (linenoise.c: 4 KiB initial, 1 MiB maximum).
  for (;;) {
    SyncHistoryFromFile();
    struct linenoiseState state;
    char* buf = static_cast<char*>(std::malloc(4096));
    if (buf == nullptr) return std::nullopt;
    if (linenoiseEditStart(&state, STDIN_FILENO, STDOUT_FILENO, buf, 4096,
                           prompt) == -1) {
      std::free(buf);
      return std::nullopt;
    }
    state.buflen_max = 1024 * 1024;
    char* res = nullptr;
    int feed_errno = 0;
    for (;;) {
      struct pollfd pfd;
      pfd.fd = STDIN_FILENO;
      pfd.events = POLLIN;
      pfd.revents = 0;
      const int ready = ::poll(&pfd, 1, 50);
      if (ready == 0) {
        // No input: pick up history lines another entry
        // appended. Only while not walking history and not
        // completing: appending mid-navigation would shift
        // the indices the editor walks. The editor removes
        // the last history entry on Enter, assuming it is
        // the empty placeholder it appended at edit start,
        // so a sync that added lines is followed by
        // re-adding the placeholder to keep it last. That
        // leaves one stale empty step in the up-arrow chain
        // past a fed line; losing the fed line on Enter
        // would be worse.
        if (state.history_index == 0 && !state.in_completion &&
            SyncHistoryFromFile() > 0) {
          linenoiseHistoryAdd("");
        }
        continue;
      }
      if (ready < 0) {
        if (errno == EINTR) continue;
        break;
      }
      res = linenoiseEditFeed(&state);
      if (res == linenoiseEditMore) continue;
      feed_errno = errno;
      break;
    }
    linenoiseEditStop(&state);
    std::free(buf);
    if (res == nullptr) {
      if (feed_errno == EAGAIN) continue;  // Ctrl-C: cancel and re-prompt.
      return std::nullopt;
    }
    std::string out(res);
    linenoiseFree(res);
    if (!out.empty()) {
      std::lock_guard<std::mutex> lock(g_history_file_mutex);
      linenoiseHistoryAdd(out.c_str());
      const std::string path = HistoryFilePath();
      linenoiseHistorySave(path.c_str());
      g_history_synced_lines = CountHistoryFileLines(path);
    }
    return out;
  }
}

void TclShell::AddHistoryLine(const std::string& line) {
  if (line.empty()) return;
  // Append only: the line must not disturb the in-memory history
  // the terminal REPL may be editing. The REPL picks it up from
  // the file at its next prompt, and while it sits idle at a
  // prompt (the poll loop in ReadInteractiveLine). The caller
  // installs this feed only for interactive sessions, so piped
  // stdin never writes history.
  std::lock_guard<std::mutex> lock(g_history_file_mutex);
  const std::string path = HistoryFilePath();
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
  if (fd < 0) return;
  const std::string entry = line + "\n";
  const char* p = entry.data();
  std::size_t left = entry.size();
  while (left > 0) {
    const ssize_t n = ::write(fd, p, left);
    if (n <= 0) break;
    p += n;
    left -= static_cast<std::size_t>(n);
  }
  ::close(fd);
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

void TclShell::InitInteractiveHistory() {}

std::optional<std::string> TclShell::ReadInteractiveLine(const char*) {
  return std::nullopt;
}

void TclShell::AddHistoryLine(const std::string&) {}

#endif  // EOS_HAS_TCL

}  // namespace eos::ui
