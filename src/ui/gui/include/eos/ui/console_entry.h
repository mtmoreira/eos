// console_entry: parsing for the GUI console command line (U6,
// Qt-free). A console line is one registry command, not Tcl: the
// name, optionally prefixed with eos::, followed by whitespace
// separated arguments. Double quotes group an argument that
// contains whitespace; inside quotes, a backslash escapes a double
// quote or a backslash, and any other backslash stays literal, so
// parsing is the exact inverse of SessionView echo quoting. A
// blank line and a line starting with # are no-ops, not errors.
#ifndef EOS_UI_CONSOLE_ENTRY_H_
#define EOS_UI_CONSOLE_ENTRY_H_

#include <string>
#include <vector>

namespace eos::ui {

struct ConsoleLine {
  bool ok = true;
  std::string error;
  std::string name;
  std::vector<std::string> args;
};

// Parses one console line. Never throws: a malformed line reports
// ok=false with an error string instead.
ConsoleLine ParseConsoleLine(const std::string& line);

}  // namespace eos::ui

#endif  // EOS_UI_CONSOLE_ENTRY_H_
