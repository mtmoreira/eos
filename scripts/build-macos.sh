#!/usr/bin/env bash
# build-macos.sh: build the EOS GUI on macOS with a live progress display.
#
# Usage:
#   ./scripts/build-macos.sh [--log FILE] [--dry-run] [--no-color] [--help]
#
# What it does, in order:
#   1. Prerequisites: checks Homebrew, CMake, the Xcode Command Line
#      Tools, Python 3, Qt 6, Tcl headers and objcopy. Nothing is
#      installed and nothing is deleted. A missing piece fails fast
#      with the exact command that provides it, or a pointer to
#      scripts/setup-macos.sh.
#   2. Configure: cmake -S . -B build-fresh
#      -DCMAKE_PREFIX_PATH=$(brew --prefix), then verifies eos-gui-bundle
#      is a known target of the configured build.
#   3. Build eos-gui: the GUI application binary.
#   4. Build eos-gui-bundle: the .app bundle that carries the Dock icon.
#
# Every command's stdout and stderr is written to the log file
# (default gui-build.log in the repository root, overridable with
# --log). The log ends with an exit= line. On failure the script prints
# the failing stage and the log path, then exits non-zero.
#
# The script never runs git pull, never asks for sudo, and never deletes
# anything. Re-running it reuses the existing build-fresh directory;
# configure is always re-run, so a stale configuration is refreshed.
#
# macOS only. --dry-run walks the whole display without running any
# build command, which is how the interface is exercised on other
# platforms.

set -uo pipefail

# --- defaults ---
BUILD_DIR="build-fresh"
LOG_FILE="gui-build.log"
DRY_RUN=0
USE_COLOR=1
STAGE_TOTAL=4
STAGE_INDEX=0
MISSING_FILE=""

usage() {
  echo "Usage: $0 [--log FILE] [--dry-run] [--no-color] [--help]"
  echo
  echo "Build the EOS GUI on macOS into build-fresh, with a live"
  echo "progress display. The full build log goes to gui-build.log"
  echo "in the repository root unless --log names another file."
  echo
  echo "  --log FILE   write the build log to FILE (default gui-build.log)"
  echo "  --dry-run    walk the display without running any build command"
  echo "  --no-color   plain output, no ANSI colors"
  echo "  --help       show this help and exit"
}

# --- flags ---
while [[ $# -gt 0 ]]; do
  case "$1" in
    --log)
      if [[ $# -lt 2 ]]; then
        echo "build-macos.sh: --log needs a file argument" >&2
        exit 2
      fi
      LOG_FILE="$2"
      shift 2
      ;;
    --log=*)
      LOG_FILE="${1#--log=}"
      shift
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    --no-color)
      USE_COLOR=0
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "build-macos.sh: unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

# --- terminal setup ---
TTY=0
if [[ -t 1 ]]; then
  TTY=1
fi
COLOR=0
if [[ $USE_COLOR -eq 1 && $TTY -eq 1 && "${TERM:-}" != "dumb" && -z "${NO_COLOR:-}" ]]; then
  COLOR=1
fi
C_RESET=""
C_CYAN=""
C_GREEN=""
C_RED=""
C_YELLOW=""
C_DIM=""
C_BOLD=""
if [[ $COLOR -eq 1 ]]; then
  C_RESET=$'\033[0m'
  C_CYAN=$'\033[36m'
  C_GREEN=$'\033[32m'
  C_RED=$'\033[31m'
  C_YELLOW=$'\033[33m'
  C_DIM=$'\033[2m'
  C_BOLD=$'\033[1m'
fi

# Braille spinner frames, one per array element so no multibyte
# substring math is needed (macOS ships bash 3.2).
SPINNER=( '⠋' '⠙' '⠹' '⠸' '⠼' '⠴' '⠦' '⠧' '⠇' '⠏' )

# --- helpers ---
fmt_time() {
  local s="$1"
  if [[ $s -lt 60 ]]; then
    printf '%ds' "$s"
  else
    printf '%dm %02ds' $(( s / 60 )) $(( s % 60 ))
  fi
}

# bar <done> <total>: a 28 cell progress bar with percent and count.
bar() {
  local done="$1" total="$2" width=28 filled=0 i=0 cells=""
  filled=$(( done * width / total ))
  while [[ $i -lt $filled ]]; do cells="${cells}█"; i=$(( i + 1 )); done
  while [[ $i -lt $width ]]; do cells="${cells}░"; i=$(( i + 1 )); done
  printf '[%s] %d%% (%d/%d)' "$cells" $(( done * 100 / total )) "$done" "$total"
}

print_logo() {
  printf '%s' "$C_CYAN"
  cat <<'EOF'
███████╗ ██████╗ ███████╗
██╔════╝██╔═══██╗██╔════╝
█████╗  ██║   ██║███████╗
██╔══╝  ██║   ██║╚════██║
███████╗╚██████╔╝███████║
╚══════╝ ╚═════╝ ╚══════╝
EOF
  printf '%s' "$C_RESET"
  printf '  %s═══════════════════════%s\n' "$C_CYAN" "$C_RESET"
  printf '  %sEDA OPEN SYSTEM%s\n' "$C_BOLD" "$C_RESET"
  printf '  %sThe dawn of open-source EDA.%s\n' "$C_DIM" "$C_RESET"
  printf '\n'
}

# run_stage <name> <description> <command...>: run one build stage with
# a live spinner, a PASS/FAIL line with elapsed time, and a progress
# bar. The command's output goes to the log file only. Returns the
# command's exit code. In dry-run mode the command is replaced by a
# short sleep so the whole display can be exercised anywhere.
run_stage() {
  local name="$1" desc="$2"
  shift 2
  local start=$SECONDS rc=0 pid=0 i=0 elapsed=0
  STAGE_INDEX=$(( STAGE_INDEX + 1 ))
  printf '%s [%d/%d] %s\n' "▶" "$STAGE_INDEX" "$STAGE_TOTAL" "$name"
  if [[ $DRY_RUN -eq 1 ]]; then
    sleep 1 &
    pid=$!
  else
    "$@" >>"$LOG_FILE" 2>&1 &
    pid=$!
  fi
  trap 'kill "$pid" 2>/dev/null' INT TERM
  while kill -0 "$pid" 2>/dev/null; do
    if [[ $TTY -eq 1 ]]; then
      printf '\r\033[K  %s %s' "${SPINNER[$i]}" "$desc"
      i=$(( (i + 1) % 10 ))
    fi
    sleep 0.2
  done
  wait "$pid"
  rc=$?
  trap - INT TERM
  elapsed=$(( SECONDS - start ))
  if [[ $rc -eq 0 ]]; then
    printf '\r\033[K  %sPASS (%s)%s\n' "$C_GREEN" "$(fmt_time "$elapsed")" "$C_RESET"
    printf '[%s] STAGE PASS: %s (%ss)\n' "$(date '+%H:%M:%S')" "$name" "$elapsed" >>"$LOG_FILE"
  else
    printf '\r\033[K  %sFAIL (%s)%s\n' "$C_RED" "$(fmt_time "$elapsed")" "$C_RESET"
    printf '[%s] STAGE FAIL: %s (%ss) exit=%s\n' "$(date '+%H:%M:%S')" "$name" "$elapsed" "$rc" >>"$LOG_FILE"
  fi
  printf '%s\n' "$(bar "$STAGE_INDEX" "$STAGE_TOTAL")"
  return $rc
}

# --- stage 1: prerequisites ---
# check_cmd <label> <command...> appends a missing item to MISSING_FILE
# and returns 1 when the command is not found.
check_cmd() {
  local label="$1"
  shift
  if "$@" >/dev/null 2>&1; then
    printf '  ok: %s\n' "$label" >>"$LOG_FILE"
    return 0
  fi
  printf '  MISSING: %s\n' "$label" >>"$LOG_FILE"
  printf '%s\n' "$label" >>"$MISSING_FILE"
  return 1
}

do_prerequisites() {
  local missing=0
  : > "$MISSING_FILE"

  if ! check_cmd "Homebrew (brew)" command -v brew; then
    printf 'Install Homebrew from https://brew.sh, then re-run this script.\n' >>"$MISSING_FILE"
    missing=1
  fi
  if ! check_cmd "CMake (cmake)" command -v cmake; then
    printf 'Install CMake: brew install cmake (or run scripts/setup-macos.sh).\n' >>"$MISSING_FILE"
    missing=1
  fi
  if ! check_cmd "Xcode Command Line Tools (xcode-select -p)" xcode-select -p; then
    printf 'Install the Xcode Command Line Tools: xcode-select --install, then re-run this script.\n' >>"$MISSING_FILE"
    missing=1
  fi
  if ! check_cmd "Python 3 (python3)" command -v python3; then
    printf 'Install Python 3: brew install python3 (or run scripts/setup-macos.sh).\n' >>"$MISSING_FILE"
    missing=1
  fi
  if ! check_cmd "Qt 6 (brew --prefix qt)" brew --prefix qt; then
    printf 'Qt 6 is not installed. Run: brew install qt (if a qt@5 conflict appears: brew unlink qt@5, then brew install qt), or run scripts/setup-macos.sh.\n' >>"$MISSING_FILE"
    missing=1
  fi
  if ! check_cmd "Tcl headers (tcl.h under the Homebrew tcl-tk prefix)" bash -c 'p="$(brew --prefix tcl-tk)"; [[ -f "$p/include/tcl-tk/tcl.h" || -f "$p/include/tcl.h" ]]'; then
    printf 'Tcl headers not found. Run: brew install tcl-tk (or run scripts/setup-macos.sh).\n' >>"$MISSING_FILE"
    missing=1
  fi
  if ! check_cmd "objcopy (LLVM objcopy on PATH)" command -v objcopy; then
    printf 'objcopy not found. Run: brew install llvm (or run scripts/setup-macos.sh), which links LLVM objcopy as objcopy.\n' >>"$MISSING_FILE"
    missing=1
  fi
  return $missing
}

# --- stage 2: configure ---
do_configure() {
  local prefix=""
  prefix="$(brew --prefix)"
  cmake -S . -B "$BUILD_DIR" -DCMAKE_PREFIX_PATH="$prefix"
  local rc=$?
  if [[ $rc -ne 0 ]]; then
    return $rc
  fi
  echo "Verifying eos-gui-bundle is a known target of the configured build..."
  if ! cmake --build "$BUILD_DIR" --target help 2>/dev/null | grep -q "eos-gui-bundle"; then
    echo "eos-gui-bundle is not a known target of the configured build." >&2
    echo "The build directory configuration is stale. Re-run this script to re-configure; if the target is still unknown, remove $BUILD_DIR by hand and re-run." >&2
    return 1
  fi
  echo "eos-gui-bundle is a known target."
  return 0
}

# --- stages 3 and 4: build ---
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 2)"

do_build_gui() {
  cmake --build "$BUILD_DIR" --target eos-gui -j"$JOBS"
}

do_build_bundle() {
  cmake --build "$BUILD_DIR" --target eos-gui-bundle -j"$JOBS"
  local rc=$?
  if [[ $rc -ne 0 ]]; then
    return $rc
  fi
  if [[ ! -x "$BUILD_DIR/apps/eos-gui/eos-gui" ]]; then
    echo "eos-gui binary not found after a successful build: $BUILD_DIR/apps/eos-gui/eos-gui" >&2
    return 1
  fi
  if [[ ! -d "$BUILD_DIR/apps/eos-gui/eos-gui.app" ]]; then
    echo "eos-gui.app bundle not found after a successful build: $BUILD_DIR/apps/eos-gui/eos-gui.app" >&2
    return 1
  fi
  return 0
}

# --- failure and success summaries ---
fail_summary() {
  local stage="$1"
  printf '\n%sBUILD FAILED%s at stage: %s\n' "$C_RED" "$C_RESET" "$stage"
  printf 'Log: %s\n' "$LOG_FILE"
  if [[ -s "$MISSING_FILE" ]]; then
    printf 'Missing prerequisites:\n'
    while IFS= read -r line; do
      printf '  - %s\n' "$line"
    done < "$MISSING_FILE"
  fi
  printf 'Last lines of the log:\n'
  tail -20 "$LOG_FILE" | sed 's/^/  /'
  printf 'Open the full log with: less %s\n' "$LOG_FILE"
}

success_summary() {
  local total="$1"
  printf '\n%sEOS build complete%s in %s\n' "$C_GREEN" "$C_RESET" "$(fmt_time "$total")"
  printf '  App:  %s/apps/eos-gui/eos-gui.app\n' "$BUILD_DIR"
  printf '  Log:  %s\n' "$LOG_FILE"
  printf '  Next: open %s/apps/eos-gui/eos-gui.app\n' "$BUILD_DIR"
}

# --- main ---
main() {
  # Must run from the repository root.
  if [[ ! -f CMakeLists.txt || ! -d src/ui/gui || ! -f src/ui/gui/CMakeLists.txt ]]; then
    echo "build-macos.sh: run this script from the EOS repository root." >&2
    echo "Expected CMakeLists.txt and src/ui/gui/CMakeLists.txt in the current directory." >&2
    exit 2
  fi

  MISSING_FILE="$(mktemp "${TMPDIR:-/tmp}/eos-build-missing.XXXXXX")"
  trap 'rm -f "$MISSING_FILE"' EXIT

  # Start the log.
  : > "$LOG_FILE"
  {
    echo "EOS macOS build log"
    echo "script: scripts/build-macos.sh"
    echo "date: $(date)"
    echo "host: $(uname -a)"
    if [[ "$(uname -s)" == "Darwin" ]]; then
      echo "macOS: $(sw_vers -productVersion 2>/dev/null || echo unknown)"
    fi
    if git rev-parse HEAD >/dev/null 2>&1; then
      echo "git: $(git rev-parse HEAD)"
    fi
    echo "build dir: $BUILD_DIR"
    echo "log: $LOG_FILE"
    if [[ $DRY_RUN -eq 1 ]]; then
      echo "mode: dry run (no build commands executed)"
    fi
    echo
  } >>"$LOG_FILE"

  if [[ "$(uname -s)" != "Darwin" && $DRY_RUN -eq 0 ]]; then
    echo "build-macos.sh: this script builds EOS on macOS only (uname: $(uname -s))." | tee -a "$LOG_FILE" >&2
    echo "On Linux, configure and build directly: cmake -S . -B build && cmake --build build" | tee -a "$LOG_FILE" >&2
    echo "exit=1" >>"$LOG_FILE"
    exit 1
  fi

  print_logo
  if [[ $DRY_RUN -eq 1 ]]; then
    printf '%sDry run: walking the display without running any build command.%s\n\n' "$C_YELLOW" "$C_RESET"
  fi
  printf 'Build plan: 1 Prerequisites, 2 Configure, 3 Build eos-gui, 4 Build eos-gui-bundle\n'
  printf 'Log: %s\n\n' "$LOG_FILE"

  local overall_start=$SECONDS rc=0
  if ! run_stage "Prerequisites" "Checking Homebrew, CMake, Qt and the build tools..." do_prerequisites; then
    fail_summary "Prerequisites"
    echo "exit=1" >>"$LOG_FILE"
    exit 1
  fi
  if ! run_stage "Configure" "Configuring build-fresh with the Homebrew prefix..." do_configure; then
    fail_summary "Configure"
    echo "exit=1" >>"$LOG_FILE"
    exit 1
  fi
  if ! run_stage "Build eos-gui" "Compiling the EOS GUI application..." do_build_gui; then
    fail_summary "Build eos-gui"
    echo "exit=1" >>"$LOG_FILE"
    exit 1
  fi
  if ! run_stage "Build eos-gui-bundle" "Assembling eos-gui.app with the Dock icon..." do_build_bundle; then
    fail_summary "Build eos-gui-bundle"
    echo "exit=1" >>"$LOG_FILE"
    exit 1
  fi

  rc=0
  success_summary $(( SECONDS - overall_start ))
  echo "exit=0" >>"$LOG_FILE"
  exit 0
}

main
