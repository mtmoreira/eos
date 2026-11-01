// panels: the GUI's right panels model (L6 interface, Qt-free).
#include "eos/ui/panels.h"

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
}

}  // namespace eos::ui
