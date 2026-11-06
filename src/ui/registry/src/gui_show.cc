// registry: eos::gui_show (open the GUI viewer from the CLI).
//
// Two modes, decided by whether the process installed a viewer
// handler with SetGuiShowHandler:
// - GUI mode (eos-gui, installed by RunGui): the command shows
//   and raises the running viewer window, including reopening
//   it after it was closed.
// - Plain CLI mode (eos, no handler): the command launches the
//   eos-gui binary as a detached process and returns at once;
//   the CLI never waits for the viewer, and the viewer keeps
//   running after the CLI session ends.
//
// The spawned viewer must not share the parent CLI's stdin: two
// processes reading one terminal break both REPLs. The child
// gets its own session (setsid) and stdin, stdout, and stderr
// redirected to /dev/null. Its terminal REPL therefore sees EOF
// at once and ends, which is exactly the U6 stdin-closed state:
// closing such a viewer quits the application instead of hiding
// it, since no terminal is left to reopen it from.
#include "eos/ui/registry.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace eos::ui {

namespace fs = std::filesystem;

namespace {

std::function<void()> g_gui_show_handler;

bool IsExecutableFile(const fs::path& path) {
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) return false;
  return ::access(path.c_str(), X_OK) == 0;
}

std::string CurrentExecutableDir() {
#if defined(__APPLE__)
  char buffer[PATH_MAX];
  std::uint32_t size = sizeof(buffer);
  if (_NSGetExecutablePath(buffer, &size) != 0) return "";
  std::error_code ec;
  const fs::path resolved = fs::canonical(buffer, ec);
  if (ec) return fs::path(buffer).parent_path().string();
  return resolved.parent_path().string();
#elif defined(__linux__)
  std::error_code ec;
  const fs::path target = fs::read_symlink("/proc/self/exe", ec);
  if (ec) return "";
  return target.parent_path().string();
#else
  return "";
#endif
}

std::string PathEnvironment() {
  const char* path = std::getenv("PATH");
  return path == nullptr ? "" : std::string(path);
}

// Writes one errno value to the error pipe (best effort: the
// parent treats a closed pipe with no data as a successful exec).
void ReportErrno(int fd, int value) {
  const char* bytes = reinterpret_cast<const char*>(&value);
  std::size_t left = sizeof(value);
  while (left > 0) {
    const ssize_t written = ::write(fd, bytes, left);
    if (written <= 0) break;
    bytes += written;
    left -= static_cast<std::size_t>(written);
  }
}

}  // namespace

std::string FindGuiExecutable(const std::string& exe_dir,
                              const std::string& path_env) {
  if (!exe_dir.empty()) {
    const fs::path dir(exe_dir);
    // Installed layout: eos-gui sits next to eos in bin/.
    // Build layout: eos is build/apps/eos/eos, so the viewer is
    // one level up and down into the sibling eos-gui directory.
    // A build that runs eos from build/apps directly (exe at
    // build/apps/eos) looks in the sibling eos-gui directory
    // under the same parent instead.
    for (const fs::path raw :
         {dir / "eos-gui", dir / ".." / "eos-gui" / "eos-gui",
          dir / "eos-gui" / "eos-gui"}) {
      // Normalize first: the build-layout candidate walks through
      // the eos directory (..), which may not exist on disk in a
      // fixture even though the normalized path does.
      const fs::path candidate = raw.lexically_normal();
      if (IsExecutableFile(candidate)) {
        return candidate.string();
      }
    }
  }
  std::size_t start = 0;
  while (start <= path_env.size()) {
    const std::size_t sep = path_env.find(':', start);
    const std::string entry = path_env.substr(
        start, sep == std::string::npos ? std::string::npos : sep - start);
    const fs::path candidate =
        fs::path(entry.empty() ? "." : entry) / "eos-gui";
    if (IsExecutableFile(candidate)) {
      return candidate.lexically_normal().string();
    }
    if (sep == std::string::npos) break;
    start = sep + 1;
  }
  return "";
}

bool LaunchGuiDetached(const std::string& path, std::string* error) {
  int pipefd[2];
  if (::pipe(pipefd) != 0) {
    if (error != nullptr) *error = std::strerror(errno);
    return false;
  }
  // The write end closes on a successful exec, so the parent
  // reads EOF; on a failed exec the child reports errno first.
  ::fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);
  const pid_t middle = ::fork();
  if (middle < 0) {
    if (error != nullptr) *error = std::strerror(errno);
    ::close(pipefd[0]);
    ::close(pipefd[1]);
    return false;
  }
  if (middle == 0) {
    const pid_t child = ::fork();
    if (child < 0) {
      ReportErrno(pipefd[1], errno);
      ::_exit(127);
    }
    if (child == 0) {
      ::setsid();
      ::close(pipefd[0]);
      const int nullfd = ::open("/dev/null", O_RDWR);
      if (nullfd >= 0) {
        ::dup2(nullfd, STDIN_FILENO);
        ::dup2(nullfd, STDOUT_FILENO);
        ::dup2(nullfd, STDERR_FILENO);
        if (nullfd > STDERR_FILENO) ::close(nullfd);
      }
      ::execl(path.c_str(), "eos-gui", static_cast<char*>(nullptr));
      ReportErrno(pipefd[1], errno);
      ::_exit(127);
    }
    ::_exit(0);
  }
  ::close(pipefd[1]);
  // Reap the middle child so it never becomes a zombie; the
  // viewer itself is reparented to init.
  int status = 0;
  while (::waitpid(middle, &status, 0) < 0 && errno == EINTR) {
  }
  // EOF with no data means the exec succeeded and closed the
  // write end; a full errno value means the exec failed.
  int exec_errno = 0;
  char* bytes = reinterpret_cast<char*>(&exec_errno);
  std::size_t got = 0;
  for (;;) {
    const ssize_t n =
        ::read(pipefd[0], bytes + got, sizeof(exec_errno) - got);
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (n == 0) break;
    got += static_cast<std::size_t>(n);
    if (got >= sizeof(exec_errno)) break;
  }
  ::close(pipefd[0]);
  if (got == sizeof(exec_errno)) {
    if (error != nullptr) *error = std::strerror(exec_errno);
    return false;
  }
  return true;
}

void SetGuiShowHandler(std::function<void()> handler) {
  g_gui_show_handler = std::move(handler);
}

void RegisterGuiShowCommand(CommandRegistry& registry) {
  (void)registry.Register(
      "gui_show", "gui_show",
      [](CommandContext&, const std::vector<std::string>& args)
          -> CommandResult {
        if (!args.empty()) {
          return CommandResult::Fail(Diag::kInvalidConfiguration,
                                     "usage: eos::gui_show");
        }
        if (g_gui_show_handler != nullptr) {
          g_gui_show_handler();
          return CommandResult::Ok("viewer=shown");
        }
        const std::string exe_dir = CurrentExecutableDir();
        const std::string path =
            FindGuiExecutable(exe_dir, PathEnvironment());
        if (path.empty()) {
          std::string searched;
          if (exe_dir.empty()) {
            searched = "the eos executable directory (undetermined) "
                       "and PATH";
          } else {
            searched = exe_dir + "/eos-gui, " + exe_dir +
                       "/../eos-gui/eos-gui, " + exe_dir +
                       "/eos-gui/eos-gui, and PATH";
          }
          return CommandResult::Fail(
              Diag::kIoError,
              "eos-gui not found: looked in " + searched);
        }
        std::string error;
        if (!LaunchGuiDetached(path, &error)) {
          return CommandResult::Fail(
              Diag::kIoError, "failed to launch " + path + ": " + error);
        }
        return CommandResult::Ok("viewer=launched path=" + path);
      });
}

}  // namespace eos::ui
