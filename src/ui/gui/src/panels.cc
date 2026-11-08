// panels: the GUI's right panels model (L6 interface, Qt-free).
#include "eos/ui/panels.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace eos::ui {

namespace {

// Parses a non-negative decimal integer covering the whole
// token: no sign, no trailing junk, no overflow past int.
bool ParseCount(const std::string& token, int& out) {
  if (token.empty()) return false;
  long value = 0;
  for (char c : token) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + (c - '0');
    if (value > 2147483647L) return false;
  }
  out = static_cast<int>(value);
  return true;
}

// Splits a "key=value" line: returns false when there is no
// '=', or when the key or the value is empty.
bool SplitKeyValue(const std::string& line, std::string& key,
                   std::string& value) {
  const std::size_t eq = line.find('=');
  if (eq == std::string::npos || eq == 0 || eq + 1 >= line.size()) {
    return false;
  }
  key = line.substr(0, eq);
  value = line.substr(eq + 1);
  return true;
}

// Splits a line on single spaces: returns false on a leading,
// trailing, or double space, so "type  $and 6" never parses.
bool SplitWords(const std::string& line, std::vector<std::string>& words) {
  if (line.empty() || line.front() == ' ' || line.back() == ' ') {
    return false;
  }
  std::size_t start = 0;
  while (start < line.size()) {
    const std::size_t space = line.find(' ', start);
    const std::string word =
        line.substr(start, space == std::string::npos
                               ? std::string::npos
                               : space - start);
    if (word.empty()) return false;
    words.push_back(word);
    if (space == std::string::npos) break;
    start = space + 1;
  }
  return true;
}

// Returns the value of a "key=value" token: false when the
// token does not start with the key or its value is empty. A
// value may itself contain '='; everything after the key is
// the value.
bool KeyValue(const std::string& token, const char* key,
              std::string& value) {
  const std::string prefix = key;
  if (token.rfind(prefix, 0) != 0 ||
      token.size() == prefix.size()) {
    return false;
  }
  value = token.substr(prefix.size());
  return true;
}

// Parses a decimal number covering the whole token: strtod with
// full consumption, finite, no trailing junk.
bool ParseDouble(const std::string& token, double& out) {
  if (token.empty()) return false;
  char* end = nullptr;
  const double value = std::strtod(token.c_str(), &end);
  if (end != token.c_str() + token.size()) return false;
  if (!std::isfinite(value)) return false;
  out = value;
  return true;
}

// Parses a "Type:count,..." list in strictly increasing type
// order, the order CellTypesText emits, or "none" for an empty
// list. Returns false on any malformed entry, unsorted or
// duplicate type, or a negative or non-numeric count.
bool ParseCellTypes(const std::string& token,
                    std::vector<PanelCellType>& out) {
  out.clear();
  if (token == "none") return true;
  std::size_t start = 0;
  while (start < token.size()) {
    const std::size_t comma = token.find(',', start);
    const std::string entry = token.substr(
        start, comma == std::string::npos ? std::string::npos
                                          : comma - start);
    const std::size_t colon = entry.find(':');
    if (colon == std::string::npos || colon == 0 ||
        colon + 1 >= entry.size()) {
      return false;
    }
    int count = 0;
    if (!ParseCount(entry.substr(colon + 1), count)) return false;
    const std::string type = entry.substr(0, colon);
    if (!out.empty() && out.back().type >= type) return false;
    out.push_back(PanelCellType{type, count});
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return !out.empty();
}

// Splits a payload into lines, dropping the single trailing
// empty segment the payload's final newline leaves. Any other
// empty line is malformed.
bool SplitPayloadLines(const std::string& payload,
                       std::vector<std::string>& lines) {
  lines.clear();
  std::size_t start = 0;
  while (start < payload.size()) {
    const std::size_t nl = payload.find('\n', start);
    if (nl == std::string::npos) {
      lines.push_back(payload.substr(start));
      break;
    }
    lines.push_back(payload.substr(start, nl - start));
    start = nl + 1;
  }
  return !lines.empty();
}

}  // namespace

bool LiveStatsModel::LoadPayload(const std::string& payload) {
  // Split into lines, dropping the single trailing empty
  // segment the payload's final newline leaves. Any other
  // empty line is malformed.
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start < payload.size()) {
    const std::size_t nl = payload.find('\n', start);
    if (nl == std::string::npos) {
      lines.push_back(payload.substr(start));
      break;
    }
    lines.push_back(payload.substr(start, nl - start));
    start = nl + 1;
  }
  if (lines.size() < 2) return false;

  std::string module;
  {
    std::string key, value;
    if (!SplitKeyValue(lines[0], key, value) || key != "module" ||
        value.find(' ') != std::string::npos) {
      return false;
    }
    module = value;
  }

  int cells = 0;
  int wires = 0;
  int depth = 0;
  int sequential = 0;
  {
    std::vector<std::string> words;
    if (!SplitWords(lines[1], words) || words.size() != 4) return false;
    const char* keys[4] = {"cells=", "wires=", "depth=", "sequential="};
    int* outs[4] = {&cells, &wires, &depth, &sequential};
    for (int i = 0; i < 4; ++i) {
      const std::string& word = words[i];
      if (word.rfind(keys[i], 0) != 0) return false;
      if (!ParseCount(word.substr(std::string(keys[i]).size()),
                      *outs[i])) {
        return false;
      }
    }
  }

  std::vector<PanelCellType> types;
  for (std::size_t i = 2; i < lines.size(); ++i) {
    std::vector<std::string> words;
    if (!SplitWords(lines[i], words) || words.size() != 3 ||
        words[0] != "type") {
      return false;
    }
    int count = 0;
    if (!ParseCount(words[2], count)) return false;
    if (!types.empty() && types.back().type >= words[1]) return false;
    types.push_back(PanelCellType{words[1], count});
  }

  module_ = module;
  cells_ = cells;
  wires_ = wires;
  depth_ = depth;
  sequential_ = sequential;
  types_ = types;
  return true;
}

void LiveStatsModel::Clear() {
  module_.clear();
  cells_ = 0;
  wires_ = 0;
  depth_ = 0;
  sequential_ = 0;
  types_.clear();
}

bool MappedSynthModel::LoadPayload(const std::string& payload) {
  std::vector<std::string> lines;
  if (!SplitPayloadLines(payload, lines) || lines.size() != 3) {
    return false;
  }

  std::string target;
  std::string liberty;
  std::string verilog;
  int cells = 0;
  std::vector<PanelCellType> types;
  bool judged = false;
  double worst_slack_ns = 0.0;
  double tns_ns = 0.0;
  {
    std::vector<std::string> words;
    if (!SplitWords(lines[0], words) || words.size() != 9 ||
        words[0] != "mapped") {
      return false;
    }
    std::string value;
    if (!KeyValue(words[1], "target=", value)) return false;
    target = value;
    if (!KeyValue(words[2], "liberty=", value)) return false;
    liberty = value;
    if (!KeyValue(words[3], "verilog=", value)) return false;
    verilog = value;
    if (!KeyValue(words[4], "cells=", value) ||
        !ParseCount(value, cells)) {
      return false;
    }
    if (!KeyValue(words[5], "types=", value) ||
        !ParseCellTypes(value, types)) {
      return false;
    }
    if (!KeyValue(words[6], "judged=", value)) return false;
    if (value == "true") {
      judged = true;
    } else if (value == "false") {
      judged = false;
    } else {
      return false;
    }
    if (!KeyValue(words[7], "worst_slack_ns=", value)) return false;
    if (judged) {
      if (!ParseDouble(value, worst_slack_ns)) return false;
    } else if (value != "none") {
      return false;
    }
    if (!KeyValue(words[8], "tns_ns=", value)) return false;
    if (judged) {
      if (!ParseDouble(value, tns_ns)) return false;
    } else if (value != "none") {
      return false;
    }
  }

  bool opt_run = false;
  std::string opt_winner;
  int opt_iterations = 0;
  std::string opt_verilog;
  int opt_cells = 0;
  std::vector<PanelCellType> opt_types;
  double opt_worst_slack_ns = 0.0;
  double opt_tns_ns = 0.0;
  {
    std::vector<std::string> words;
    if (!SplitWords(lines[1], words) || words.size() != 9 ||
        words[0] != "opt") {
      return false;
    }
    std::string value;
    if (!KeyValue(words[1], "run=", value)) return false;
    if (value == "true") {
      opt_run = true;
    } else if (value == "false") {
      opt_run = false;
    } else {
      return false;
    }
    if (!KeyValue(words[2], "winner=", value)) return false;
    opt_winner = value;
    if (!KeyValue(words[3], "iterations=", value) ||
        !ParseCount(value, opt_iterations)) {
      return false;
    }
    if (!KeyValue(words[4], "verilog=", value)) return false;
    opt_verilog = value;
    if (!KeyValue(words[5], "cells=", value) ||
        !ParseCount(value, opt_cells)) {
      return false;
    }
    if (!KeyValue(words[6], "types=", value) ||
        !ParseCellTypes(value, opt_types)) {
      return false;
    }
    if (!KeyValue(words[7], "worst_slack_ns=", value)) return false;
    if (opt_run) {
      if (!ParseDouble(value, opt_worst_slack_ns)) return false;
    } else if (value != "none") {
      return false;
    }
    if (!KeyValue(words[8], "tns_ns=", value)) return false;
    if (opt_run) {
      if (!ParseDouble(value, opt_tns_ns)) return false;
    } else if (value != "none") {
      return false;
    }
    // The placeholders are a fixed shape: a run that never
    // happened reports every opt field at its placeholder, and
    // a run that happened names a real winner netlist.
    if (!opt_run &&
        (opt_winner != "none" || opt_iterations != 0 ||
         opt_verilog != "none" || opt_cells != 0 ||
         !opt_types.empty())) {
      return false;
    }
    if (opt_run && opt_verilog == "none") return false;
  }

  bool equiv_run = false;
  std::string equiv_verdict;
  std::string equiv_first;
  std::string equiv_second;
  {
    std::vector<std::string> words;
    if (!SplitWords(lines[2], words) || words.size() != 5 ||
        words[0] != "equiv") {
      return false;
    }
    std::string value;
    if (!KeyValue(words[1], "run=", value)) return false;
    if (value == "true") {
      equiv_run = true;
    } else if (value == "false") {
      equiv_run = false;
    } else {
      return false;
    }
    if (!KeyValue(words[2], "verdict=", value)) return false;
    equiv_verdict = value;
    if (!KeyValue(words[3], "first=", value)) return false;
    equiv_first = value;
    if (!KeyValue(words[4], "second=", value)) return false;
    equiv_second = value;
    if (!equiv_run &&
        (equiv_verdict != "none" || equiv_first != "none" ||
         equiv_second != "none")) {
      return false;
    }
    if (equiv_run &&
        equiv_verdict != "equivalent" &&
        equiv_verdict != "not_equivalent" &&
        equiv_verdict != "unknown") {
      return false;
    }
    if (equiv_run &&
        (equiv_first == "none" || equiv_second == "none")) {
      return false;
    }
  }

  target_ = target;
  liberty_ = liberty;
  verilog_ = verilog;
  cells_ = cells;
  types_ = types;
  judged_ = judged;
  worst_slack_ns_ = worst_slack_ns;
  tns_ns_ = tns_ns;
  opt_run_ = opt_run;
  opt_winner_ = opt_winner;
  opt_iterations_ = opt_iterations;
  opt_verilog_ = opt_verilog;
  opt_cells_ = opt_cells;
  opt_types_ = opt_types;
  opt_worst_slack_ns_ = opt_worst_slack_ns;
  opt_tns_ns_ = opt_tns_ns;
  equiv_run_ = equiv_run;
  equiv_verdict_ = equiv_verdict;
  equiv_first_ = equiv_first;
  equiv_second_ = equiv_second;
  return true;
}

void MappedSynthModel::Clear() {
  target_.clear();
  liberty_.clear();
  verilog_.clear();
  cells_ = 0;
  types_.clear();
  judged_ = false;
  worst_slack_ns_ = 0.0;
  tns_ns_ = 0.0;
  opt_run_ = false;
  opt_winner_.clear();
  opt_iterations_ = 0;
  opt_verilog_.clear();
  opt_cells_ = 0;
  opt_types_.clear();
  opt_worst_slack_ns_ = 0.0;
  opt_tns_ns_ = 0.0;
  equiv_run_ = false;
  equiv_verdict_.clear();
  equiv_first_.clear();
  equiv_second_.clear();
}

void PanelsModel::Clear() {
  parsed = false;
  top.clear();
  verilog_path.clear();
  filelist_path.clear();
  filelist_files.clear();
  elaborated = false;
  elaborated_cells = 0;
  elaborated_wires = 0;
  elaborated_depth = 0;
  generic_run = false;
  generic_script.clear();
  generic_before_and = 0;
  generic_before_levels = 0;
  generic_after_and = 0;
  generic_after_levels = 0;
  generic_verilog.clear();
  live_stats.Clear();
  mapped_synth.Clear();
}

}  // namespace eos::ui
