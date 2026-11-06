// issues: the GUI's elaboration issues model (L6 interface,
// Qt-free).
#include "eos/ui/issues.h"

#include <algorithm>
#include <string>
#include <vector>

namespace eos::ui {

namespace {

// Parses a non-negative decimal integer covering the whole
// token: no sign, no trailing junk, no overflow past int.
// Same rule as the panels model: a line number is never
// negative, and 0 means the design records no location.
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

// Severity order for sorting: errors first, then warnings,
// then info.
int SeverityRank(const std::string& severity) {
  if (severity == "error") return 0;
  if (severity == "warning") return 1;
  return 2;
}

// Parses one issue line in fixed field order, with the
// message last and running to end of line. Every field
// before the message is a single space-free token; a double
// space anywhere makes the word fail its key prefix and the
// line malformed.
bool ParseIssueLine(const std::string& line, GuiIssue& issue) {
  if (line.rfind("issue ", 0) != 0) return false;
  std::size_t pos = 6;  // after "issue "
  const char* keys[7] = {"severity=", "code=", "module=", "path=",
                         "target=", "file=", "line="};
  std::string values[7];
  for (int i = 0; i < 7; ++i) {
    const std::size_t space = line.find(' ', pos);
    if (space == std::string::npos) return false;
    const std::string word = line.substr(pos, space - pos);
    const std::string key(keys[i]);
    if (word.rfind(key, 0) != 0) return false;
    values[i] = word.substr(key.size());
    pos = space + 1;
  }
  if (values[0] != "error" && values[0] != "warning" &&
      values[0] != "info") {
    return false;
  }
  if (values[1].empty() || values[2].empty() || values[3].empty()) {
    return false;
  }
  int line_number = 0;
  if (!ParseCount(values[6], line_number)) return false;
  if (line.rfind("message=", pos) != pos) return false;
  const std::string message = line.substr(pos + 8);
  if (message.empty()) return false;
  issue.severity = values[0];
  issue.code = values[1];
  issue.module = values[2];
  issue.path = values[3];
  issue.target = values[4];
  issue.file = values[5];
  issue.line = line_number;
  issue.message = message;
  return true;
}

}  // namespace

bool IssuesModel::LoadPayload(const std::string& payload) {
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

  std::string checked;
  {
    std::string key, value;
    if (!SplitKeyValue(lines[0], key, value) || key != "checked") {
      return false;
    }
    checked = value;
  }

  std::string netlist;
  {
    std::string key, value;
    if (!SplitKeyValue(lines[1], key, value) || key != "netlist" ||
        (value != "elaborated" && value != "generic")) {
      return false;
    }
    netlist = value;
  }

  std::vector<GuiIssue> issues;
  for (std::size_t i = 2; i < lines.size(); ++i) {
    GuiIssue issue;
    if (!ParseIssueLine(lines[i], issue)) return false;
    issues.push_back(std::move(issue));
  }

  std::sort(issues.begin(), issues.end(),
            [](const GuiIssue& a, const GuiIssue& b) {
              const int rank_a = SeverityRank(a.severity);
              const int rank_b = SeverityRank(b.severity);
              if (rank_a != rank_b) return rank_a < rank_b;
              const std::string path_a = DisplayPath(a);
              const std::string path_b = DisplayPath(b);
              if (path_a != path_b) return path_a < path_b;
              if (a.code != b.code) return a.code < b.code;
              return a.target < b.target;
            });

  loaded_ = true;
  checked_ = checked;
  netlist_ = netlist;
  issues_ = issues;
  return true;
}

void IssuesModel::Clear() {
  loaded_ = false;
  checked_.clear();
  netlist_.clear();
  issues_.clear();
}

int IssuesModel::ErrorCount() const {
  int count = 0;
  for (const GuiIssue& issue : issues_) {
    if (issue.severity == "error") ++count;
  }
  return count;
}

int IssuesModel::WarningCount() const {
  int count = 0;
  for (const GuiIssue& issue : issues_) {
    if (issue.severity == "warning") ++count;
  }
  return count;
}

int IssuesModel::InfoCount() const {
  int count = 0;
  for (const GuiIssue& issue : issues_) {
    if (issue.severity == "info") ++count;
  }
  return count;
}

std::string IssuesModel::DisplayPath(const GuiIssue& issue) {
  if (issue.target.empty()) return issue.path;
  return issue.path + "/" + issue.target;
}

}  // namespace eos::ui
