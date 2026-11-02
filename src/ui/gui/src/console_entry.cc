// console_entry: parsing for the GUI console command line (U6,
// Qt-free). See console_entry.h for the grammar.
#include "eos/ui/console_entry.h"

namespace eos::ui {

namespace {

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

}  // namespace

ConsoleLine ParseConsoleLine(const std::string& line) {
  ConsoleLine out;
  std::size_t i = 0;
  const std::size_t n = line.size();
  while (i < n && IsSpace(line[i])) ++i;
  if (i >= n || line[i] == '#') return out;
  std::vector<std::string> tokens;
  while (i < n) {
    while (i < n && IsSpace(line[i])) ++i;
    if (i >= n) break;
    std::string token;
    if (line[i] == '"') {
      ++i;
      bool closed = false;
      while (i < n) {
        const char c = line[i++];
        if (c == '"') {
          closed = true;
          break;
        }
        if (c == '\\' && i < n && (line[i] == '"' || line[i] == '\\')) {
          token += line[i++];
        } else {
          token += c;
        }
      }
      if (!closed) {
        out.ok = false;
        out.error = "unterminated quote";
        return out;
      }
    } else {
      while (i < n && !IsSpace(line[i])) token += line[i++];
    }
    tokens.push_back(token);
  }
  if (tokens.empty()) return out;
  out.name = tokens[0];
  if (out.name.rfind("eos::", 0) == 0) out.name = out.name.substr(5);
  out.args.assign(tokens.begin() + 1, tokens.end());
  return out;
}

}  // namespace eos::ui
