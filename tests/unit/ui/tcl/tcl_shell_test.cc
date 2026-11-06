// Unit tests: ui/tcl shell binding. Built only when the build found a
// Tcl development package (TCL_FOUND), so a pass here always means a
// real interpreter ran.
#include "eos/ui/tcl_shell.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <gtest/gtest.h>

namespace eos::ui {
namespace {

struct Fixture {
  CommandRegistry registry;
  CommandContext ctx;
  Fixture() { RegisterCoreCommands(registry); }
};

TEST(TclShell, EosCommandsRunInTheEosNamespace) {
  Fixture f;
  auto shell = TclShell::Create(f.registry, f.ctx);
  ASSERT_TRUE(shell.ok());
  const auto made = shell.value()->Eval("eos::create_design top");
  ASSERT_TRUE(made.ok());
  EXPECT_NE(made.value().find("design=top"), std::string::npos);
  const auto net = shell.value()->Eval("eos::create_net n1");
  ASSERT_TRUE(net.ok());
  const auto current = shell.value()->Eval("eos::current_design");
  ASSERT_TRUE(current.ok());
  EXPECT_NE(current.value().find("objects=1"), std::string::npos);
}

TEST(TclShell, PlainTclStillWorksAroundEosCommands) {
  Fixture f;
  auto shell = TclShell::Create(f.registry, f.ctx);
  ASSERT_TRUE(shell.ok());
  const auto value = shell.value()->Eval("expr {6 * 7}");
  ASSERT_TRUE(value.ok());
  EXPECT_EQ(value.value(), "42");
  const auto scripted = shell.value()->Eval(
      "eos::create_design d\n"
      "set nets [eos::get_objects net *]\n"
      "return $nets");
  ASSERT_TRUE(scripted.ok());
  EXPECT_NE(scripted.value().find("collection=eos_coll_1"),
            std::string::npos);
}

TEST(TclShell, CommandFailureIsACatchableTclErrorWithTheDiagnostic) {
  Fixture f;
  auto shell = TclShell::Create(f.registry, f.ctx);
  ASSERT_TRUE(shell.ok());
  const auto failed = shell.value()->Eval("eos::create_net n1");
  ASSERT_FALSE(failed.ok());
  EXPECT_NE(failed.status().message().find("EOS unclosed_design"),
            std::string::npos);
  const auto caught = shell.value()->Eval(
      "catch {eos::create_net n1} msg\n"
      "return $msg");
  ASSERT_TRUE(caught.ok());
  EXPECT_NE(caught.value().find("unclosed_design"), std::string::npos);
}

// S6 backfill: RunFiles executes a script file through the same
// command set, and a missing file is a nonzero exit, not a crash.
TEST(TclShell, RunFilesExecutesScriptFilesAndRejectsMissingFiles) {
  Fixture f;
  auto shell = TclShell::Create(f.registry, f.ctx);
  ASSERT_TRUE(shell.ok());
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "eos_tcl_runfiles_test";
  std::filesystem::create_directories(dir);
  const std::filesystem::path script = dir / "setup.tcl";
  {
    std::ofstream out(script);
    out << "eos::create_design top\n";
    out << "eos::create_net n1\n";
  }
  EXPECT_EQ(shell.value()->RunFiles({script.string()}), 0);
  EXPECT_TRUE(f.ctx.project.HasDesign());
  EXPECT_EQ(shell.value()->RunFiles({(dir / "missing.tcl").string()}), 1);
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

namespace {

// Restores HOME on destruction, so a test that redirects it cannot
// leak the change into later tests.
struct HomeGuard {
  std::string saved;
  bool had = false;
  HomeGuard() {
    const char* home = std::getenv("HOME");
    had = home != nullptr;
    if (had) saved = home;
  }
  ~HomeGuard() {
    if (had) {
      ::setenv("HOME", saved.c_str(), 1);
    } else {
      ::unsetenv("HOME");
    }
  }
};

}  // namespace

// The shared history file lives under HOME, not in the launch
// directory, so one file serves the user from every working
// directory; without HOME it falls back to the launch directory.
TEST(TclShell, HistoryFilePathLivesUnderHome) {
  HomeGuard guard;
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "eos_tcl_home_test";
  std::filesystem::create_directories(dir);
  ::setenv("HOME", dir.string().c_str(), 1);
  EXPECT_EQ(HistoryFilePath(), (dir / ".eos_history").string());
  ::unsetenv("HOME");
  EXPECT_EQ(HistoryFilePath(), ".eos_history");
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

// A line fed from another entry (the GUI console) is appended to
// the shared history file, so the terminal REPL can recall it;
// empty lines are ignored.
TEST(TclShell, AddHistoryLineAppendsToTheSharedFile) {
  HomeGuard guard;
  Fixture f;
  auto shell = TclShell::Create(f.registry, f.ctx);
  ASSERT_TRUE(shell.ok());
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "eos_tcl_addhist_test";
  std::filesystem::create_directories(dir);
  ::setenv("HOME", dir.string().c_str(), 1);
  shell.value()->AddHistoryLine("eos::elaborate");
  shell.value()->AddHistoryLine("eos::read_verilog optloop.v optloop");
  shell.value()->AddHistoryLine("");
  std::ifstream in(dir / ".eos_history");
  ASSERT_TRUE(in.good());
  std::string first, second, extra;
  ASSERT_TRUE(std::getline(in, first));
  ASSERT_TRUE(std::getline(in, second));
  EXPECT_EQ(first, "eos::elaborate");
  EXPECT_EQ(second, "eos::read_verilog optloop.v optloop");
  EXPECT_FALSE(std::getline(in, extra));
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

// Drives the real terminal REPL on a pty: the child loops
// ReadInteractiveLine and prints each submitted line as
// RESULT<n>:<line>; the parent plays the terminal. Used to prove
// the two behaviors a unit test without a terminal cannot see:
// a history line appended while the prompt sits idle is
// recallable with up-arrow, and Tab completes an eos:: name.
class PtyRepl {
 public:
  PtyRepl() = default;
  PtyRepl(const PtyRepl&) = delete;
  PtyRepl& operator=(const PtyRepl&) = delete;
  ~PtyRepl() {
    if (child_ > 0) {
      ::kill(child_, SIGKILL);
      int status = 0;
      ::waitpid(child_, &status, 0);
    }
    if (master_ >= 0) ::close(master_);
  }

  bool Start() {
    master_ = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master_ < 0) return false;
    if (::grantpt(master_) != 0 || ::unlockpt(master_) != 0) return false;
    // A real terminal reports a size. A fresh pty reports 0x0, and
    // linenoise then queries the cursor position (ESC[6n) and blocks
    // waiting for a reply the test driver never sends, so the prompt
    // never appears. Give the pty a size first.
    struct winsize ws;
    ws.ws_row = 24;
    ws.ws_col = 80;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    if (::ioctl(master_, TIOCSWINSZ, &ws) != 0) return false;
    char* slave_name = ::ptsname(master_);
    if (slave_name == nullptr) return false;
    const std::string slave_path(slave_name);
    const pid_t pid = ::fork();
    if (pid < 0) return false;
    if (pid == 0) {
      // Child: the terminal REPL on the pty slave. _exit only,
      // never return into the test framework.
      ::close(master_);
      if (::setsid() < 0) ::_exit(2);
      const int slave = ::open(slave_path.c_str(), O_RDWR);
      if (slave < 0) ::_exit(2);
      if (::ioctl(slave, TIOCSCTTY, 0) != 0) ::_exit(2);
      ::dup2(slave, STDIN_FILENO);
      ::dup2(slave, STDOUT_FILENO);
      ::dup2(slave, STDERR_FILENO);
      if (slave > STDERR_FILENO) ::close(slave);
      Fixture f;
      auto shell = TclShell::Create(f.registry, f.ctx);
      if (!shell.ok()) ::_exit(2);
      shell.value()->InitInteractiveHistory();
      for (int i = 1;; ++i) {
        auto line = shell.value()->ReadInteractiveLine("eos> ");
        if (!line.has_value()) ::_exit(0);
        const std::string out =
            "RESULT" + std::to_string(i) + ":" + *line + "\n";
        const char* p = out.data();
        std::size_t left = out.size();
        while (left > 0) {
          const ssize_t n = ::write(STDOUT_FILENO, p, left);
          if (n <= 0) ::_exit(3);
          p += n;
          left -= static_cast<std::size_t>(n);
        }
      }
    }
    child_ = pid;
    return true;
  }

  void Send(const std::string& bytes) {
    const char* p = bytes.data();
    std::size_t left = bytes.size();
    while (left > 0) {
      const ssize_t n = ::write(master_, p, left);
      if (n <= 0) return;
      p += n;
      left -= static_cast<std::size_t>(n);
    }
  }

  // Reads until needle appears or the deadline passes. The child
  // exiting first is a failure, not a timeout.
  bool Expect(const std::string& needle, int timeout_ms = 10000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      const std::size_t at = pending_.find(needle);
      if (at != std::string::npos) {
        pending_.erase(0, at + needle.size());
        return true;
      }
      struct pollfd pfd;
      pfd.fd = master_;
      pfd.events = POLLIN;
      pfd.revents = 0;
      const int ready = ::poll(&pfd, 1, 100);
      if (ready < 0) {
        if (errno == EINTR) continue;
        return false;
      }
      if (ready == 0) continue;
      if ((pfd.revents & (POLLIN | POLLHUP | POLLERR)) == 0) continue;
      char buf[4096];
      const ssize_t n = ::read(master_, buf, sizeof(buf));
      if (n <= 0) return false;
      pending_.append(buf, static_cast<std::size_t>(n));
    }
    return false;
  }

  bool WaitExit(int timeout_ms = 10000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      int status = 0;
      const pid_t done = ::waitpid(child_, &status, WNOHANG);
      if (done == child_) {
        child_ = -1;
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
  }

 private:
  int master_ = -1;
  pid_t child_ = -1;
  std::string pending_;
};

// A line appended to the shared history file while the terminal
// prompt sits idle (as the GUI console appends it) is recallable
// with up-arrow at that same prompt, with no line submitted first.
TEST(TclShell, ReadInteractiveLineRecallsHistoryAppendedWhileIdle) {
  HomeGuard guard;
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "eos_tcl_pty_idle_test";
  std::filesystem::create_directories(dir);
  ::setenv("HOME", dir.string().c_str(), 1);
  PtyRepl repl;
  ASSERT_TRUE(repl.Start());
  ASSERT_TRUE(repl.Expect("eos> "));
  {
    std::ofstream out(dir / ".eos_history", std::ios::app);
    out << "eos::elaborate\n";
  }
  // The poll loop re-checks the file every 50 ms; give it six
  // cycles to pick the line up.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  repl.Send("\x1b[A");
  ASSERT_TRUE(repl.Expect("eos::elaborate"));
  repl.Send("\r");  // Enter is CR (13) in raw mode, not LF.
  ASSERT_TRUE(repl.Expect("RESULT1:eos::elaborate"));
  // Wait for the next prompt before Ctrl-D: the prompt is only
  // written once raw mode is set again, so a Ctrl-D sent earlier
  // lands in the canonical-mode window and is swallowed as VEOF.
  ASSERT_TRUE(repl.Expect("eos> "));
  repl.Send("\x04");  // Ctrl-D on an empty line ends input.
  ASSERT_TRUE(repl.WaitExit());
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

// Tab in the terminal REPL completes a registry command name:
// "eos::elaborat" plus Tab yields "eos::elaborate", and a bare
// first word completes to the eos:: form, like the GUI console.
TEST(TclShell, ReadInteractiveLineCompletesEosNamesOnTab) {
  HomeGuard guard;
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "eos_tcl_pty_tab_test";
  std::filesystem::create_directories(dir);
  ::setenv("HOME", dir.string().c_str(), 1);
  PtyRepl repl;
  ASSERT_TRUE(repl.Start());
  ASSERT_TRUE(repl.Expect("eos> "));
  repl.Send("eos::elaborat");
  repl.Send("\t");
  ASSERT_TRUE(repl.Expect("eos::elaborate"));
  repl.Send("\r");  // Enter is CR (13) in raw mode, not LF.
  ASSERT_TRUE(repl.Expect("RESULT1:eos::elaborate"));
  ASSERT_TRUE(repl.Expect("eos> "));
  repl.Send("elaborat");
  repl.Send("\t");
  ASSERT_TRUE(repl.Expect("eos::elaborate"));
  repl.Send("\r");  // Enter is CR (13) in raw mode, not LF.
  ASSERT_TRUE(repl.Expect("RESULT2:eos::elaborate"));
  // Wait for the next prompt before Ctrl-D (same canonical-mode
  // window as the idle test).
  ASSERT_TRUE(repl.Expect("eos> "));
  repl.Send("\x04");  // Ctrl-D on an empty line ends input.
  ASSERT_TRUE(repl.WaitExit());
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

}  // namespace
}  // namespace eos::ui
