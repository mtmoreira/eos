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

std::vector<std::string> SplitConsoleSegments(
    const std::string& text, const std::vector<std::string>& names) {
  std::vector<std::string> out;
  std::string current;
  auto flush = [&]() {
    const std::size_t first = current.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
      current.clear();
      return;
    }
    const std::size_t last = current.find_last_not_of(" \t\r\n");
    out.push_back(current.substr(first, last - first + 1));
    current.clear();
  };
  bool in_quotes = false;
  std::size_t i = 0;
  const std::size_t n = text.size();
  while (i < n) {
    const char c = text[i];
    if (in_quotes) {
      current += c;
      if (c == '\\' && i + 1 < n) {
        current += text[i + 1];
        i += 2;
        continue;
      }
      if (c == '"') in_quotes = false;
      ++i;
      continue;
    }
    if (c == '"') {
      in_quotes = true;
      current += c;
      ++i;
      continue;
    }
    if (c == '\n' || c == '\r') {
      flush();
      ++i;
      continue;
    }
    // Outside quotes, an eos::<known-name> marker starts a new
    // segment, unless the current segment holds nothing yet
    // (the marker is then the first command, not a split).
    if (c == 'e' && i + 5 <= n && text.compare(i, 5, "eos::") == 0) {
      std::size_t j = i + 5;
      while (j < n && !IsSpace(text[j]) && text[j] != '"' &&
             text[j] != '\n' && text[j] != '\r') {
        ++j;
      }
      const std::string name = text.substr(i + 5, j - (i + 5));
      bool known = false;
      for (const std::string& known_name : names) {
        if (known_name == name) {
          known = true;
          break;
        }
      }
      if (known &&
          current.find_first_not_of(" \t\r\n") != std::string::npos) {
        flush();
      }
    }
    current += c;
    ++i;
  }
  flush();
  return out;
}

void ConsoleHistory::Add(const std::string& line) {
  if (line.empty()) return;
  if (!entries_.empty() && entries_.back() == line) return;
  entries_.push_back(line);
  index_ = -1;
  draft_.clear();
}

std::optional<std::string> ConsoleHistory::Up(const std::string& current) {
  if (entries_.empty()) return std::nullopt;
  if (index_ == -1) {
    draft_ = current;
    index_ = static_cast<int>(entries_.size()) - 1;
  } else if (index_ > 0) {
    --index_;
  }
  return entries_[static_cast<std::size_t>(index_)];
}

std::optional<std::string> ConsoleHistory::Down() {
  if (index_ == -1) return std::nullopt;
  if (index_ < static_cast<int>(entries_.size()) - 1) {
    ++index_;
    return entries_[static_cast<std::size_t>(index_)];
  }
  index_ = -1;
  return draft_;
}

std::vector<std::string> CompleteConsoleCommand(
    const std::vector<std::string>& names, const std::string& line) {
  // Only the first word completes: any whitespace means the
  // user is already typing an argument.
  for (const char c : line) {
    if (IsSpace(c)) return {};
  }
  const std::string marker = "eos::";
  const bool prefixed = line.rfind(marker, 0) == 0;
  const std::string stem = prefixed ? line.substr(marker.size()) : line;
  std::vector<std::string> out;
  for (const std::string& name : names) {
    if (name.rfind(stem, 0) == 0) {
      out.push_back(prefixed ? marker + name : name);
    }
  }
  return out;
}

}  // namespace eos::ui
