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

#include <optional>
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

// Splits one submitted console text into the command segments it
// contains, in order. Newlines separate segments, and inside one
// line a segment starts at a token of the form eos::<name> where
// <name> is a known registry command: a multi-command paste
// arrives in the single-line entry with its line breaks stripped,
// so the commands run together and only the eos:: marker shows
// where one ends and the next begins. A marker inside double
// quotes is an argument, not a command, and is never split on.
// A marker naming an unknown command is left in its segment, so
// dispatch reports it as an unknown command instead of splitting
// on it. Blank segments are dropped, so a trailing newline or a
// blank line contributes nothing. Bare command names never start
// a segment: an argument may legitimately equal a command name
// (a top module named like one), so only the eos:: marker splits.
std::vector<std::string> SplitConsoleSegments(
    const std::string& text, const std::vector<std::string>& names);

// Up and down history for the GUI console entry (Qt-free, so it
// is unit-testable; the widget only forwards keys to it). The
// history is in-session only: the console takes registry
// commands, not Tcl, so its lines are never written to the
// terminal shell's .eos_history file, where a bare name would
// fail as Tcl. Add records a dispatched line; empty lines and
// consecutive duplicates are ignored. Up returns the older
// entry, clamped at the oldest; Down returns the newer entry,
// and past the newest it returns the draft text that was in
// the entry when navigation started, ending navigation. Down
// while not navigating returns nullopt, and Up on empty
// history returns nullopt. Either way, nullopt means the widget
// leaves the entry text unchanged.
class ConsoleHistory {
 public:
  void Add(const std::string& line);
  std::optional<std::string> Up(const std::string& current);
  std::optional<std::string> Down();

 private:
  std::vector<std::string> entries_;
  int index_ = -1;  // -1: showing the draft, not an entry.
  std::string draft_;
};

// Completion candidates for a console line: registry command
// names matching the command word being typed. Only the first
// word completes; once the line has an argument, there is
// nothing to complete, so the result is empty. A word with an
// eos:: prefix completes with the prefix kept (eos::ela gives
// eos::elaborate); a bare word completes bare (ela gives
// elaborate). An empty word matches every name, in registry
// order. Names arrive sorted from the registry.
std::vector<std::string> CompleteConsoleCommand(
    const std::vector<std::string>& names, const std::string& line);

}  // namespace eos::ui

#endif  // EOS_UI_CONSOLE_ENTRY_H_
