// gui: the Qt application entry point (Qt only).
//
// U6 session model: one process, one session, two command
// entries. The GUI console dispatches registry commands on the
// GUI thread. The terminal REPL reads stdin on its own thread
// and posts every line to the GUI thread, where it is evaluated
// through the Tcl shell and the window refreshes; the REPL
// thread waits for each evaluation, so terminal output stays in
// order and no two commands ever run at once. Closing the
// window hides it (quit-on-last-window-closed is off); typing
// show in the terminal shows it again on the same session. The
// terminal REPL uses plain line input: line editing, history,
// and tab completion stay in the eos CLI, whose linenoise reader
// is private to the tcl module.
#include <QApplication>
#include <QMetaObject>

#include <cstdio>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <unistd.h>

#include "eos/ui/gui.h"
#include "eos/ui/tcl_shell.h"
#include "main_window.h"

namespace eos::ui {

namespace {

// Trims ASCII whitespace from both ends for reopen-command
// matching only; the evaluated line itself is untrimmed Tcl.
std::string Trim(const std::string& line) {
  const std::size_t first = line.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const std::size_t last = line.find_last_not_of(" \t\r\n");
  return line.substr(first, last - first + 1);
}

}  // namespace

int RunGui(int argc, char** argv, CommandRegistry& registry,
           CommandContext& context, TclShell* shell) {
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  SessionView view(registry, context);
  MainWindow window(view);
  window.show();

  std::thread repl;
  if (shell != nullptr) {
    repl = std::thread([shell, &window]() {
      const bool tty = isatty(STDIN_FILENO) != 0;
      std::string line;
      for (;;) {
        if (tty) {
          std::fputs("eos> ", stdout);
          std::fflush(stdout);
        }
        if (!std::getline(std::cin, line)) break;
        const std::string trimmed = Trim(line);
        if (trimmed == "show") {
          QMetaObject::invokeMethod(
              &window, [&window]() { window.ShowAgain(); },
              Qt::QueuedConnection);
          continue;
        }
        if (trimmed.empty() || trimmed[0] == '#') continue;
        auto done = std::make_shared<std::promise<std::string>>();
        std::future<std::string> result = done->get_future();
        QMetaObject::invokeMethod(
            &window,
            [shell, &window, line, done]() {
              const foundation::Result<std::string> evaluated =
                  shell->Eval(line);
              std::string out;
              if (!evaluated.ok()) {
                out = "error: " + evaluated.status().message();
              } else {
                out = evaluated.value();
              }
              window.Refresh();
              done->set_value(out);
            },
            Qt::QueuedConnection);
        const std::string out = result.get();
        if (!out.empty()) std::printf("%s\n", out.c_str());
      }
      QMetaObject::invokeMethod(
          &window, [&window]() { window.NoteStdinClosed(); },
          Qt::QueuedConnection);
    });
  }

  const int rc = app.exec();
  if (repl.joinable()) repl.join();
  return rc;
}

}  // namespace eos::ui
