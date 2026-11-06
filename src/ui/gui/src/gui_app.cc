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
// terminal REPL reads through the Tcl shell's interactive line
// reader, so it has the same linenoise editing, history, and
// tab completion as the plain eos CLI, sharing .eos_history in
// the launch directory.
#include <QApplication>
#include <QIcon>
#include <QMetaObject>
#include <QPixmap>

#include <cstdio>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "eos/ui/gui.h"
#include "eos/ui/tcl_shell.h"
#include "eos_icon_png.h"
#include "main_window.h"
#include "splash_screen.h"

// Qt resource initializer generated from resources/splash.qrc
// (global namespace; see the call site in RunGui).
extern void qInitResources_splash();

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
  // Window icon: the EOS logo, embedded at configure time from
  // the committed iconset PNG. This sets the window and task
  // switcher icon on every platform. It does NOT set the macOS
  // Dock icon for a bare binary launched from a terminal: the
  // Dock icon comes from the .app bundle (see apps/eos-gui),
  // which is why the bundle target exists.
  QPixmap icon_pixmap;
  if (icon_pixmap.loadFromData(kEosIconPng, kEosIconPngSize, "PNG")) {
    app.setWindowIcon(QIcon(icon_pixmap));
  }
  // The splash artwork lives in a static library, so the linker
  // would otherwise drop the resource object as unreferenced and
  // the splash would be silently skipped. This call forces it in,
  // registering :/eos/eos-logo-dark.png before it is loaded.
  // Qt generates qInitResources_splash in the global namespace,
  // so it is declared at file scope and called qualified: the
  // Q_INIT_RESOURCE macro cannot be used here, because a
  // block-scope extern inside namespace eos::ui would declare
  // eos::ui::qInitResources_splash instead, which does not exist.
  ::qInitResources_splash();
  // Splash: shown before the main window, closed once the
  // window is ready AND the banner has been visible for at
  // least 2 seconds (FinishWhenReady enforces the minimum;
  // the close never blocks the GUI thread). Each stage is a
  // real startup step below, never a timed animation. A
  // missing logo resource skips the splash entirely; startup
  // proceeds exactly as before. eos-gui always opens a window,
  // so there is no headless mode in which the splash could block.
  SplashScreen* splash = nullptr;
  const QPixmap logo(":/eos/eos-logo-dark.png");
  if (!logo.isNull()) {
    splash = new SplashScreen(logo);
    splash->show();
    app.processEvents();
  }
  auto set_stage = [&](const char* message, int percent) {
    if (splash != nullptr) {
      splash->SetStage(message, percent);
      app.processEvents();
    }
  };
  set_stage("Starting session...", 10);
  SessionView view(registry, context);
  set_stage("Building interface...", 50);
  MainWindow window(view);
  set_stage("Opening window...", 90);
  window.show();
  if (splash != nullptr) {
    // FinishWhenReady owns the close from here: it finishes
    // at once when the 2-second minimum already passed, or
    // after the remainder on the event loop, and deletes the
    // splash itself. Do not touch splash after this call.
    splash->FinishWhenReady(&window);
  }

  std::thread repl;
  if (shell != nullptr) {
    repl = std::thread([shell, &window]() {
      shell->InitInteractiveHistory();
      for (;;) {
        // Linenoise prints the prompt on a terminal and reads
        // plainly with no prompt on a pipe, so piped runs keep
        // working exactly as before.
        std::optional<std::string> maybe_line =
            shell->ReadInteractiveLine("eos> ");
        if (!maybe_line.has_value()) break;
        const std::string line = *maybe_line;
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
