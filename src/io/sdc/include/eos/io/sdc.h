// Copyright 2026 The EOS Authors
// SPDX-License-Identifier: Apache-2.0
//
// L1 SDC reader: a scoped parser for the clock statements of an
// SDC file. Only create_clock, create_generated_clock, and
// set_clock_uncertainty are understood; every other statement is
// ignored entirely (not counted, not reported), so this subset
// deliberately does not implement Tcl. See the clock-network
// design, section 2: the parser exists so read_sdc can wire file
// clocks into the session constraint set (the F2 fix); the STA
// engine keeps receiving the same file for its own analysis.
//
// Lexical subset: statements split on newlines and semicolons;
// {...} groups nest and protect their contents; [...] groups
// are kept whole as one token, wrappers and all; double quotes
// group words; backslash-newline continues a statement; a '#'
// at the start of a token comments out the rest of the line.
// No variable or command substitution is performed: a clock
// statement that would need either lands in the unwired list
// with its line number and a plain reason, contained and
// reported, never silently dropped. The parse never fails on
// file content: a garbage file yields zero statements, exactly
// like a file with no clock statements.

#ifndef EOS_IO_SDC_H_
#define EOS_IO_SDC_H_

#include <string>
#include <vector>

#include "eos/foundation/errors.h"

namespace eos::io {

// One parsed create_clock statement. Targets are kept as
// written, collection wrappers and all ([get_ports clk]); the
// wiring normalizes them with the registry's own target
// normalization, so a file clock and an interactive clock store
// identical rows.
struct SdcClockDef {
  std::string name;  // -name, or the single target as written.
  double period_ns = 0;
  bool has_waveform = false;
  double waveform_rise_ns = 0;
  double waveform_fall_ns = 0;
  bool add = false;
  std::vector<std::string> targets;
};

// One parsed create_generated_clock statement. divide_by and
// multiply_by are 0 when absent. master_clock is as written;
// resolving it against the session clock set is the wiring's
// job (design section 2.3, rule 4).
struct SdcGeneratedClockDef {
  std::string name;  // -name, or the single target as written.
  std::string source;
  bool has_master_clock = false;
  std::string master_clock;
  int divide_by = 0;
  int multiply_by = 0;
  bool invert = false;
  bool combinational = false;
  bool add = false;
  std::vector<std::string> targets;
};

// One parsed set_clock_uncertainty statement. Targets are as
// written: [all_clocks], a [get_clocks ...] list, or clock
// names. An empty target list means the statement names no
// object, which SDC reads as all clocks.
struct SdcUncertaintyDef {
  bool has_setup = false;
  double setup_ns = 0;
  bool has_hold = false;
  double hold_ns = 0;
  std::vector<std::string> targets;
};

// A clock statement the subset cannot wire, with the line it
// starts on and the reason, in plain words.
struct SdcUnwired {
  int line = 0;
  std::string statement;  // The command word, e.g. create_clock.
  std::string reason;
};

// Everything one SDC file's clock statements say.
struct SdcClockFile {
  std::vector<SdcClockDef> clocks;
  std::vector<SdcGeneratedClockDef> generated;
  std::vector<SdcUncertaintyDef> uncertainties;
  std::vector<SdcUnwired> unwired;
};

// Parses the clock statements out of SDC file text. Never
// fails on content; the Result only fails if a future variant
// needs it, so callers can treat content problems as data.
foundation::Result<SdcClockFile> ParseSdcClocks(const std::string& text);

}  // namespace eos::io

#endif  // EOS_IO_SDC_H_
