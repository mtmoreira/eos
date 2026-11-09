#!/usr/bin/env python3
"""Process-level regression test for the GUI exit hang.

Launches the real eos-gui binary with stdin on a pty held open
(the terminal case: the REPL thread sits in its read loop) and
QT_QPA_PLATFORM=offscreen, then triggers QApplication::quit
through the EOS_GUI_QUIT_AFTER_MS test hook, which is exactly
the Cmd-Q path. Asserts the process exits with code 0 within
the timeout.

Before the fix, quit returned from the event loop but the main
thread blocked forever joining the REPL thread, which only
leaves its read loop at stdin EOF; the process never exited.
"""

import os
import pty
import signal
import subprocess
import sys
import tempfile


def main(argv):
    if len(argv) < 2:
        print("usage: quit_gui_test.py <path-to-eos-gui>", file=sys.stderr)
        return 2
    binary = argv[1]
    timeout = float(os.environ.get("EOS_GUI_QUIT_TEST_TIMEOUT", "15"))
    quit_after_ms = os.environ.get("EOS_GUI_QUIT_AFTER_MS", "3000")
    if not os.path.isfile(binary):
        print(f"eos-gui binary not found: {binary}", file=sys.stderr)
        return 2

    master_fd, slave_fd = pty.openpty()
    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["EOS_GUI_QUIT_AFTER_MS"] = quit_after_ms
    # Keep the test hermetic: the REPL reads the shared history
    # file only for tty stdin, and it must never touch the real
    # one.
    home = tempfile.mkdtemp(prefix="eos-gui-quit-test-home-")
    env["HOME"] = home
    proc = subprocess.Popen(
        [binary],
        stdin=slave_fd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        close_fds=True,
    )
    os.close(slave_fd)
    try:
        out, err = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        out, err = proc.communicate()
        print(
            f"eos-gui did not exit within {timeout}s after "
            f"QApplication::quit was requested; killed it",
            file=sys.stderr,
        )
        print("--- stdout tail ---", file=sys.stderr)
        print(out.decode(errors="replace")[-2000:], file=sys.stderr)
        print("--- stderr tail ---", file=sys.stderr)
        print(err.decode(errors="replace")[-2000:], file=sys.stderr)
        return 1
    finally:
        os.close(master_fd)
    if proc.returncode != 0:
        print(
            f"eos-gui exited with code {proc.returncode}, "
            f"expected 0",
            file=sys.stderr,
        )
        print("--- stdout tail ---", file=sys.stderr)
        print(out.decode(errors="replace")[-2000:], file=sys.stderr)
        print("--- stderr tail ---", file=sys.stderr)
        print(err.decode(errors="replace")[-2000:], file=sys.stderr)
        return 1
    print(
        f"eos-gui exited cleanly with code 0 "
        f"({quit_after_ms} ms quit delay, {timeout}s timeout)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
