# cmake/EosDependencies.cmake
#
# Configure-time dependency check for the EOS build.
#
# Included from the top-level CMakeLists before any module
# subdirectory, so a missing tool or library fails configure with
# one actionable report instead of failing late inside some
# module's build. That late failure is what produced a Python
# traceback for a missing objcopy on macOS: the ABC namespacing
# step calls GNU objcopy, which macOS does not ship.
#
# Every hard build dependency is checked here:
#   Tcl headers and library (the real shell needs them; without
#     them the build succeeds but eda reports the shell is
#     unavailable), BISON, FLEX, SWIG, Eigen3, ZLIB, spdlog,
#     the Python3 interpreter, and GNU objcopy.
# GTest is deliberately not a hard dependency: the test tree falls
# back to FetchContent when it is not found. Threads come from the
# system toolchain.
#
# option EOS_AUTO_INSTALL_DEPS (default OFF): when ON, missing
# dependencies are installed automatically before failing:
#   macOS: scripts/setup-macos.sh (Homebrew; it also links
#     Homebrew's LLVM objcopy as objcopy).
#   Debian/Ubuntu: apt-get install (requires passwordless sudo;
#     otherwise the report prints the exact command to run).
# After an install attempt the checks re-run; anything still
# missing fails configure with the same report. Auto-install is
# opt-in: installing system packages without being asked is not
# a default a build system should take.

option(EOS_AUTO_INSTALL_DEPS
  "Automatically install missing build dependencies at configure time"
  OFF)

# On macOS, Homebrew's keg-only formulae live outside the default
# search paths. Add their prefixes so find_package and find_program
# can see them without the user passing CMAKE_PREFIX_PATH by hand.
if(APPLE)
  find_program(EOS_BREW_PROGRAM brew)
  if(EOS_BREW_PROGRAM)
    execute_process(
      COMMAND ${EOS_BREW_PROGRAM} --prefix tcl-tk bison flex zlib eigen spdlog llvm
      OUTPUT_VARIABLE _eos_brew_prefixes
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    string(REPLACE "\n" ";" _eos_brew_prefixes "${_eos_brew_prefixes}")
    foreach(_prefix IN LISTS _eos_brew_prefixes)
      if(IS_DIRECTORY "${_prefix}")
        list(APPEND CMAKE_PREFIX_PATH "${_prefix}")
      endif()
      # Homebrew's tcl-tk formula installs headers under
      # include/tcl-tk/, which CMake's FindTCL never searches (its
      # suffixes are the versioned include/tcl8.x dirs). Point
      # FindTCL at the real header dir as a normal variable, so the
      # cache-clearing in eos_check_dependencies below does not
      # wipe it, and a user-supplied TCL_INCLUDE_PATH still wins.
      if(NOT TCL_INCLUDE_PATH AND EXISTS "${_prefix}/include/tcl-tk/tcl.h")
        set(TCL_INCLUDE_PATH "${_prefix}/include/tcl-tk")
      endif()
    endforeach()
  endif()
endif()

# One check pass. Results are returned in EOS_DEPS_MISSING (display
# names), EOS_DEPS_BREW (Homebrew formulae) and EOS_DEPS_APT
# (Debian/Ubuntu packages). Cache entries from a previous pass are
# cleared first so a re-check after an install attempt searches
# again instead of returning stale NOTFOUND values.
function(eos_check_dependencies)
  unset(TCL_LIBRARY CACHE)
  unset(TCL_INCLUDE_PATH CACHE)
  unset(BISON_EXECUTABLE CACHE)
  unset(FLEX_EXECUTABLE CACHE)
  unset(FLEX_INCLUDE_DIR CACHE)
  unset(SWIG_EXECUTABLE CACHE)
  unset(SWIG_DIR CACHE)
  unset(EIGEN3_INCLUDE_DIR CACHE)
  unset(ZLIB_LIBRARY CACHE)
  unset(ZLIB_INCLUDE_DIR CACHE)
  unset(spdlog_DIR CACHE)
  unset(Python3_EXECUTABLE CACHE)
  unset(EOS_OBJCOPY_PATH CACHE)

  set(_missing "")
  set(_brew "")
  set(_apt "")

  find_package(TCL QUIET)
  if(NOT TCL_FOUND)
    list(APPEND _missing "Tcl development files (tcl.h and libtcl)")
    list(APPEND _brew "tcl-tk")
    list(APPEND _apt "tcl-dev")
  endif()

  find_package(BISON QUIET)
  if(NOT BISON_FOUND)
    list(APPEND _missing "BISON parser generator")
    list(APPEND _brew "bison")
    list(APPEND _apt "bison")
  endif()

  find_package(FLEX QUIET)
  if(NOT FLEX_FOUND)
    list(APPEND _missing "FLEX lexer generator")
    list(APPEND _brew "flex")
    list(APPEND _apt "flex" "libfl-dev")
  endif()

  find_package(SWIG 3.0 QUIET)
  if(NOT SWIG_FOUND)
    list(APPEND _missing "SWIG 3.0 or newer")
    list(APPEND _brew "swig")
    list(APPEND _apt "swig")
  endif()

  find_package(Eigen3 QUIET)
  if(NOT Eigen3_FOUND AND NOT EIGEN3_FOUND)
    list(APPEND _missing "Eigen3 headers")
    list(APPEND _brew "eigen")
    list(APPEND _apt "libeigen3-dev")
  endif()

  find_package(ZLIB QUIET)
  if(NOT ZLIB_FOUND)
    list(APPEND _missing "zlib development files")
    list(APPEND _brew "zlib")
    list(APPEND _apt "zlib1g-dev")
  endif()

  find_package(spdlog QUIET)
  if(NOT spdlog_FOUND)
    list(APPEND _missing "spdlog development files")
    list(APPEND _brew "spdlog")
    list(APPEND _apt "libspdlog-dev")
  endif()

  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(NOT Python3_Interpreter_FOUND)
    list(APPEND _missing "Python3 interpreter")
    list(APPEND _brew "python3")
    list(APPEND _apt "python3")
  endif()

  find_program(EOS_OBJCOPY_PATH objcopy)
  if(NOT EOS_OBJCOPY_PATH)
    list(APPEND _missing "objcopy (ABC CUDD namespacing step; LLVM objcopy on macOS)")
    list(APPEND _brew "llvm")
    list(APPEND _apt "binutils")
  endif()

  list(REMOVE_DUPLICATES _missing)
  list(REMOVE_DUPLICATES _brew)
  list(REMOVE_DUPLICATES _apt)
  set(EOS_DEPS_MISSING "${_missing}" PARENT_SCOPE)
  set(EOS_DEPS_BREW "${_brew}" PARENT_SCOPE)
  set(EOS_DEPS_APT "${_apt}" PARENT_SCOPE)
endfunction()

function(eos_missing_report)
  set(_lines "EOS build dependencies missing:")
  foreach(_item IN LISTS EOS_DEPS_MISSING)
    string(APPEND _lines "\n  - ${_item}")
  endforeach()
  string(APPEND _lines "\nInstall them, then re-run cmake configure.")
  if(APPLE)
    string(APPEND _lines "\nmacOS: brew install ${EOS_DEPS_BREW}")
    string(APPEND _lines "\n  (or run scripts/setup-macos.sh, which also links gobjcopy as objcopy)")
  elseif(EXISTS "/etc/debian_version")
    string(APPEND _lines "\nDebian/Ubuntu: sudo apt-get install -y ${EOS_DEPS_APT}")
  endif()
  if(NOT EOS_AUTO_INSTALL_DEPS)
    string(APPEND _lines "\nOr configure with -DEOS_AUTO_INSTALL_DEPS=ON to install automatically.")
  endif()
  set(EOS_DEPS_REPORT "${_lines}" PARENT_SCOPE)
endfunction()

eos_check_dependencies()

if(EOS_DEPS_MISSING AND EOS_AUTO_INSTALL_DEPS)
  if(APPLE)
    if(EXISTS "${CMAKE_SOURCE_DIR}/scripts/setup-macos.sh")
      message(STATUS "EOS: installing missing dependencies via scripts/setup-macos.sh")
      execute_process(
        COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/setup-macos.sh"
        RESULT_VARIABLE _eos_install_rc
        OUTPUT_VARIABLE _eos_install_out
        ERROR_VARIABLE _eos_install_err)
      if(NOT _eos_install_rc EQUAL 0)
        eos_missing_report()
        message(FATAL_ERROR "${_eos_install_out}\n${_eos_install_err}\n${EOS_DEPS_REPORT}")
      endif()
    else()
      eos_missing_report()
      message(FATAL_ERROR "scripts/setup-macos.sh not found in the source tree.\n${EOS_DEPS_REPORT}")
    endif()
  elseif(EXISTS "/etc/debian_version")
    execute_process(COMMAND sudo -n true RESULT_VARIABLE _eos_sudo_rc
      OUTPUT_QUIET ERROR_QUIET)
    if(NOT _eos_sudo_rc EQUAL 0)
      eos_missing_report()
      message(FATAL_ERROR "Passwordless sudo is unavailable, so automatic install cannot run.\n${EOS_DEPS_REPORT}")
    endif()
    message(STATUS "EOS: installing missing dependencies via apt-get: ${EOS_DEPS_APT}")
    execute_process(COMMAND sudo apt-get install -y ${EOS_DEPS_APT}
      RESULT_VARIABLE _eos_install_rc)
    if(NOT _eos_install_rc EQUAL 0)
      eos_missing_report()
      message(FATAL_ERROR "apt-get install failed.\n${EOS_DEPS_REPORT}")
    endif()
  else()
    eos_missing_report()
    message(FATAL_ERROR "Automatic install is supported on macOS and Debian/Ubuntu only.\n${EOS_DEPS_REPORT}")
  endif()
  eos_check_dependencies()
endif()

if(EOS_DEPS_MISSING)
  eos_missing_report()
  message(FATAL_ERROR "${EOS_DEPS_REPORT}")
endif()

message(STATUS "EOS dependencies: all found")
