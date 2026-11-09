// signoff: the signoff verdict record and the parsers for the
// external PV evidence reports (L4 engines, pure text processing).
#include "eos/pv/signoff.h"

#include <cstdint>

namespace eos::pv {
namespace {

// Splits text on newlines, stripping a trailing carriage return
// per line. No line carries a trailing newline.
std::vector<std::string> SplitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (true) {
    const std::size_t newline = text.find('\n', start);
    if (newline == std::string::npos) {
      lines.push_back(text.substr(start));
      break;
    }
    lines.push_back(text.substr(start, newline - start));
    start = newline + 1;
  }
  for (std::string& line : lines) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
  }
  return lines;
}

std::string Trim(const std::string& text) {
  const std::size_t first = text.find_first_not_of(" \t");
  if (first == std::string::npos) return "";
  const std::size_t last = text.find_last_not_of(" \t");
  return text.substr(first, last - first + 1);
}

// Joins lines [begin, end) with single spaces, skipping empty
// lines: the documents wrap prose, so a sentence spans lines.
std::string JoinLines(const std::vector<std::string>& lines,
                      std::size_t begin, std::size_t end) {
  std::string out;
  for (std::size_t i = begin; i < end; ++i) {
    const std::string line = Trim(lines[i]);
    if (line.empty()) continue;
    if (!out.empty()) out += ' ';
    out += line;
  }
  return out;
}

// The first line starting with the heading prefix, or npos.
std::size_t FindHeading(const std::vector<std::string>& lines,
                        const std::string& prefix) {
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (lines[i].rfind(prefix, 0) == 0) return i;
  }
  return std::string::npos;
}

// The end of the section starting at begin: the next line
// starting with '#', or the end of the document.
std::size_t SectionEnd(const std::vector<std::string>& lines,
                       std::size_t begin) {
  for (std::size_t i = begin + 1; i < lines.size(); ++i) {
    if (!lines[i].empty() && lines[i][0] == '#') return i;
  }
  return lines.size();
}

// Parses the unsigned decimal starting at pos. Returns the
// position just past the digits, or npos when no digit starts
// there. Overflow past size_t fails.
std::size_t ParseUnsigned(const std::string& text, std::size_t pos,
                          std::size_t* value) {
  if (pos >= text.size() || text[pos] < '0' || text[pos] > '9') {
    return std::string::npos;
  }
  std::size_t parsed = 0;
  while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
    const std::size_t digit = static_cast<std::size_t>(text[pos] - '0');
    if (parsed > (static_cast<std::size_t>(-1) - digit) / 10) {
      return std::string::npos;
    }
    parsed = parsed * 10 + digit;
    ++pos;
  }
  *value = parsed;
  return pos;
}

// Parses the unsigned decimal ending just before pos (the
// number immediately to the left of pos). Returns false when
// no digit ends there.
bool ParseUnsignedBefore(const std::string& text, std::size_t pos,
                         std::size_t* value) {
  if (pos == 0 || text[pos - 1] < '0' || text[pos - 1] > '9') {
    return false;
  }
  std::size_t begin = pos;
  while (begin > 0 && text[begin - 1] >= '0' && text[begin - 1] <= '9') {
    --begin;
  }
  return ParseUnsigned(text, begin, value) != std::string::npos;
}

// Parses a comma-grouped decimal ("3,676") starting at pos.
std::size_t ParseCommaNumber(const std::string& text, std::size_t pos,
                             std::size_t* value) {
  if (pos >= text.size() || text[pos] < '0' || text[pos] > '9') {
    return std::string::npos;
  }
  std::size_t parsed = 0;
  bool saw_digit = false;
  while (pos < text.size() &&
         ((text[pos] >= '0' && text[pos] <= '9') || text[pos] == ',')) {
    if (text[pos] != ',') {
      const std::size_t digit = static_cast<std::size_t>(text[pos] - '0');
      if (parsed > (static_cast<std::size_t>(-1) - digit) / 10) {
        return std::string::npos;
      }
      parsed = parsed * 10 + digit;
      saw_digit = true;
    }
    ++pos;
  }
  if (!saw_digit) return std::string::npos;
  *value = parsed;
  return pos;
}

// Parses the comma-grouped decimal ending just before pos.
bool ParseCommaNumberBefore(const std::string& text, std::size_t pos,
                            std::size_t* value) {
  if (pos == 0 || !((text[pos - 1] >= '0' && text[pos - 1] <= '9') ||
                    text[pos - 1] == ',')) {
    return false;
  }
  std::size_t begin = pos;
  while (begin > 0 && ((text[begin - 1] >= '0' && text[begin - 1] <= '9') ||
                       text[begin - 1] == ',')) {
    --begin;
  }
  return ParseCommaNumber(text, begin, value) != std::string::npos;
}

bool IsNameToken(const std::string& token) {
  if (token.empty() || token[0] < 'a' || token[0] > 'z') return false;
  for (char c : token) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
  }
  return true;
}

bool IsDigitsToken(const std::string& token) {
  if (token.empty()) return false;
  for (char c : token) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

// The fields of the summary-table row whose first cell starts
// with the given prefix (the prefix plus a space, a paren, or
// the end of the cell). Returns false when no such row exists
// in lines [begin, end).
bool TableRow(const std::vector<std::string>& lines, std::size_t begin,
              std::size_t end, const std::string& prefix,
              std::vector<std::string>* fields) {
  for (std::size_t i = begin; i < end; ++i) {
    const std::string line = Trim(lines[i]);
    if (line.empty() || line[0] != '|') continue;
    std::vector<std::string> cells;
    std::size_t start = 0;
    while (true) {
      const std::size_t bar = line.find('|', start);
      if (bar == std::string::npos) break;
      cells.push_back(Trim(line.substr(start, bar - start)));
      start = bar + 1;
    }
    cells.push_back(Trim(line.substr(start)));
    while (!cells.empty() && cells.front().empty()) cells.erase(cells.begin());
    while (!cells.empty() && cells.back().empty()) cells.pop_back();
    if (cells.empty()) continue;
    const std::string& first = cells[0];
    if (first != prefix && first.rfind(prefix + " ", 0) != 0 &&
        first.rfind(prefix + " (", 0) != 0) {
      continue;
    }
    *fields = cells;
    return true;
  }
  return false;
}

// A verdict word from a summary-table verdict field: "PASS"
// (possibly with a trailing comma and counts) means pass,
// "NOT CLEAN" or "FAIL" means fail. Returns false for anything
// else.
bool ParseTableVerdict(const std::string& field, SignoffVerdict* verdict) {
  if (field.rfind("PASS", 0) == 0) {
    *verdict = SignoffVerdict::kPass;
    return true;
  }
  if (field.find("NOT CLEAN") != std::string::npos ||
      field.rfind("FAIL", 0) == 0) {
    *verdict = SignoffVerdict::kFail;
    return true;
  }
  return false;
}

std::string Fail(const std::string& path, const std::string& what,
                 std::string* error) {
  *error = path + ": " + what;
  return "";
}

}  // namespace

bool ParsePvText(const std::string& text, const std::string& path,
                 SignoffRecord* record, std::string* error) {
  const std::vector<std::string> lines = SplitLines(text);

  const std::size_t drc_begin = FindHeading(lines, "## 2. DRC");
  if (drc_begin == std::string::npos) {
    Fail(path, "no '## 2. DRC' section", error);
    return false;
  }
  const std::string drc_text =
      JoinLines(lines, drc_begin, SectionEnd(lines, drc_begin));
  std::size_t pos = drc_text.find("holds ");
  if (pos == std::string::npos) {
    Fail(path, "DRC section has no 'holds N rule categories and M "
               "violations' result sentence",
         error);
    return false;
  }
  pos += 6;
  std::size_t categories = 0;
  std::size_t violations = 0;
  pos = ParseUnsigned(drc_text, pos, &categories);
  if (pos == std::string::npos ||
      drc_text.compare(pos, 21, " rule categories and ") != 0) {
    Fail(path, "DRC result sentence is not 'holds N rule categories "
               "and M violations'",
         error);
    return false;
  }
  pos = ParseUnsigned(drc_text, pos + 21, &violations);
  if (pos == std::string::npos ||
      drc_text.compare(pos, 11, " violations") != 0) {
    Fail(path, "DRC result sentence is not 'holds N rule categories "
               "and M violations'",
         error);
    return false;
  }

  const std::size_t coverage = drc_text.find("Coverage by rule family");
  if (coverage == std::string::npos) {
    Fail(path, "DRC section has no 'Coverage by rule family' sentence",
         error);
    return false;
  }
  std::string family_text = drc_text.substr(coverage);
  const std::size_t remaining = family_text.find("every remaining");
  if (remaining == std::string::npos) {
    Fail(path, "DRC coverage sentence has no 'every remaining' tail",
         error);
    return false;
  }
  family_text = family_text.substr(0, remaining);
  std::vector<std::string> tokens;
  {
    std::size_t start = 0;
    while (start < family_text.size()) {
      const std::size_t end =
          family_text.find_first_of(" ,;", start);
      const std::size_t stop =
          end == std::string::npos ? family_text.size() : end;
      if (stop > start) tokens.push_back(family_text.substr(start, stop - start));
      start = end == std::string::npos ? family_text.size() : end + 1;
    }
  }
  std::vector<SignoffDrcFamily> families;
  std::size_t family_total = 0;
  for (std::size_t i = 0; i + 1 < tokens.size();) {
    if (IsNameToken(tokens[i]) && IsDigitsToken(tokens[i + 1])) {
      for (const SignoffDrcFamily& seen : families) {
        if (seen.family == tokens[i]) {
          Fail(path, "DRC coverage lists family '" + tokens[i] + "' twice",
               error);
          return false;
        }
      }
      SignoffDrcFamily family;
      family.family = tokens[i];
      family.categories = static_cast<std::size_t>(std::stoul(tokens[i + 1]));
      family.violations = 0;
      if (family.categories == 0) {
        Fail(path, "DRC coverage lists family '" + tokens[i] +
                       "' with zero categories",
             error);
        return false;
      }
      family_total += family.categories;
      families.push_back(family);
      i += 2;
    } else {
      ++i;
    }
  }
  if (families.empty()) {
    Fail(path, "DRC coverage sentence names no family with a count",
         error);
    return false;
  }
  if (family_total > categories) {
    Fail(path, "DRC coverage families sum to " +
                   std::to_string(family_total) +
                   " categories, more than the reported total " +
                   std::to_string(categories),
         error);
    return false;
  }

  const std::size_t ant_begin = FindHeading(lines, "## 4. Antenna");
  if (ant_begin == std::string::npos) {
    Fail(path, "no '## 4. Antenna' section", error);
    return false;
  }
  const std::string ant_text =
      JoinLines(lines, ant_begin, SectionEnd(lines, ant_begin));
  pos = ant_text.find("Result: ");
  if (pos == std::string::npos) {
    Fail(path, "antenna section has no 'Result: N net violations, M "
               "pin violations' sentence",
         error);
    return false;
  }
  pos += 8;
  std::size_t net_violations = 0;
  std::size_t pin_violations = 0;
  pos = ParseUnsigned(ant_text, pos, &net_violations);
  if (pos == std::string::npos ||
      ant_text.compare(pos, 17, " net violations, ") != 0) {
    Fail(path, "antenna result sentence is not 'Result: N net "
               "violations, M pin violations'",
         error);
    return false;
  }
  pos = ParseUnsigned(ant_text, pos + 17, &pin_violations);
  if (pos == std::string::npos ||
      ant_text.compare(pos, 15, " pin violations") != 0) {
    Fail(path, "antenna result sentence is not 'Result: N net "
               "violations, M pin violations'",
         error);
    return false;
  }

  const std::size_t s34_begin = FindHeading(lines, "### 3.4");
  if (s34_begin == std::string::npos) {
    Fail(path, "no '### 3.4' LVS results section", error);
    return false;
  }
  const std::string s34_text =
      JoinLines(lines, s34_begin, SectionEnd(lines, s34_begin));
  pos = s34_text.find(" cell classes compared");
  if (pos == std::string::npos) {
    Fail(path, "LVS results have no 'N cell classes compared' sentence",
         error);
    return false;
  }
  std::size_t compared = 0;
  if (!ParseUnsignedBefore(s34_text, pos, &compared) || compared == 0) {
    Fail(path, "LVS results name no class count before "
               "'cell classes compared'",
         error);
    return false;
  }
  std::string exception_cell;
  {
    std::size_t at = 0;
    int exceptions = 0;
    while (true) {
      const std::size_t found = s34_text.find("except `", at);
      if (found == std::string::npos) break;
      const std::size_t open = found + 8;
      const std::size_t close = s34_text.find('`', open);
      if (close == std::string::npos) {
        Fail(path, "LVS results have an unterminated excepted cell name",
             error);
        return false;
      }
      exception_cell = s34_text.substr(open, close - open);
      ++exceptions;
      at = close + 1;
    }
    if (exceptions != 1 || exception_cell.empty()) {
      Fail(path, "LVS results must name exactly one excepted cell class",
           error);
      return false;
    }
  }

  const std::size_t s35_begin = FindHeading(lines, "### 3.5");
  if (s35_begin == std::string::npos) {
    Fail(path, "no '### 3.5' named-discrepancies section", error);
    return false;
  }
  const std::size_t s35_end = SectionEnd(lines, s35_begin);
  std::vector<SignoffLvsIssue> issues;
  for (std::size_t i = s35_begin; i < s35_end; ++i) {
    const std::string& line = lines[i];
    if (line.size() < 4 || line[0] != 'D' || line[1] < '0' ||
        line[1] > '9' || line[2] != '.' || line[3] != ' ') {
      continue;
    }
    const std::string code = line.substr(0, 2);
    if (code != "D1" && code != "D2") {
      Fail(path, "named discrepancy '" + code +
                     "' is not one the parser knows (D1, D2)",
           error);
      return false;
    }
    for (const SignoffLvsIssue& seen : issues) {
      if (seen.issue == code) {
        Fail(path, "named discrepancy '" + code + "' appears twice",
             error);
        return false;
      }
    }
    std::size_t para_end = i + 1;
    while (para_end < s35_end && !Trim(lines[para_end]).empty()) ++para_end;
    const std::string paragraph = JoinLines(lines, i, para_end);
    SignoffLvsIssue issue;
    issue.issue = code;
    issue.path = "klayout";
    const std::string rest = paragraph.substr(4);
    if (!rest.empty() && rest[0] == '`') {
      const std::size_t close = rest.find('`', 1);
      if (close == std::string::npos) {
        Fail(path, "named discrepancy '" + code +
                       "' has an unterminated cell name",
             error);
        return false;
      }
      issue.cell = rest.substr(1, close - 1);
    } else {
      issue.cell = "none";
    }
    if (paragraph.find("extraction artifact") != std::string::npos) {
      issue.kind = "extraction_artifact";
    } else if (paragraph.find("cause not fully resolved") !=
               std::string::npos) {
      issue.kind = "unresolved";
    } else {
      Fail(path, "named discrepancy '" + code +
                     "' states no recognized kind (extraction artifact, "
                     "cause not fully resolved)",
           error);
      return false;
    }
    issues.push_back(issue);
  }
  if (issues.size() != 2) {
    Fail(path, "named discrepancies must be exactly D1 and D2", error);
    return false;
  }

  const std::size_t s36_begin = FindHeading(lines, "### 3.6");
  if (s36_begin == std::string::npos) {
    Fail(path, "no '### 3.6' LVS verdict section", error);
    return false;
  }
  const std::string s36_text =
      JoinLines(lines, s36_begin, SectionEnd(lines, s36_begin));
  SignoffVerdict lvs_verdict;
  if (s36_text.find("does not pass clean") != std::string::npos) {
    lvs_verdict = SignoffVerdict::kFail;
  } else if (s36_text.find("passes clean") != std::string::npos) {
    lvs_verdict = SignoffVerdict::kPass;
  } else {
    Fail(path, "LVS verdict section states neither 'does not pass "
               "clean' nor 'passes clean'",
         error);
    return false;
  }

  const std::size_t sum_begin = FindHeading(lines, "## 5. Summary");
  if (sum_begin == std::string::npos) {
    Fail(path, "no '## 5. Summary' table", error);
    return false;
  }
  const std::size_t sum_end = SectionEnd(lines, sum_begin);
  std::vector<std::string> row;
  if (!TableRow(lines, sum_begin, sum_end, "DRC", &row) ||
      row.size() < 3) {
    Fail(path, "summary table has no DRC row", error);
    return false;
  }
  {
    const std::size_t at = row[0].find(" categories");
    std::size_t table_categories = 0;
    if (at == std::string::npos ||
        !ParseUnsignedBefore(row[0], at, &table_categories) ||
        table_categories != categories) {
      Fail(path, "summary DRC row names a different category count "
                 "than section 2",
           error);
      return false;
    }
  }
  SignoffVerdict table_drc;
  if (!ParseTableVerdict(row[1], &table_drc)) {
    Fail(path, "summary DRC row states no recognized verdict", error);
    return false;
  }
  if ((table_drc == SignoffVerdict::kPass) != (violations == 0)) {
    Fail(path, "summary DRC verdict disagrees with the section 2 "
               "violation count",
         error);
    return false;
  }
  if (!TableRow(lines, sum_begin, sum_end, "Antenna", &row) ||
      row.size() < 3) {
    Fail(path, "summary table has no Antenna row", error);
    return false;
  }
  SignoffVerdict table_antenna;
  if (!ParseTableVerdict(row[1], &table_antenna)) {
    Fail(path, "summary Antenna row states no recognized verdict",
         error);
    return false;
  }
  if ((table_antenna == SignoffVerdict::kPass) !=
      (net_violations == 0 && pin_violations == 0)) {
    Fail(path, "summary Antenna verdict disagrees with the section 4 "
               "violation counts",
         error);
    return false;
  }
  if (!TableRow(lines, sum_begin, sum_end, "LVS", &row) ||
      row.size() < 3) {
    Fail(path, "summary table has no LVS row", error);
    return false;
  }
  SignoffVerdict table_lvs;
  if (!ParseTableVerdict(row[1], &table_lvs)) {
    Fail(path, "summary LVS row states no recognized verdict", error);
    return false;
  }
  if (table_lvs != lvs_verdict) {
    Fail(path, "summary LVS verdict disagrees with section 3.6", error);
    return false;
  }

  record->drc = table_drc;
  record->antenna = table_antenna;
  record->drc_categories = categories;
  record->drc_violations = violations;
  record->antenna_net_violations = net_violations;
  record->antenna_pin_violations = pin_violations;
  record->drc_families = families;
  SignoffLvsPath klayout;
  klayout.path = "klayout";
  klayout.classes_compared = compared;
  klayout.classes_equivalent = compared - 1;
  klayout.verdict = lvs_verdict;
  record->lvs_paths.push_back(klayout);
  record->lvs_issues = issues;
  record->lvs = lvs_verdict;
  return true;
}

bool ParseLvsText(const std::string& text, const std::string& path,
                  SignoffRecord* record, std::string* error) {
  const std::vector<std::string> lines = SplitLines(text);

  const std::size_t s4_begin = FindHeading(lines, "## 4. Results");
  if (s4_begin == std::string::npos) {
    Fail(path, "no '## 4. Results' section", error);
    return false;
  }
  const std::string s4_text =
      JoinLines(lines, s4_begin, SectionEnd(lines, s4_begin));
  const std::size_t of_the = s4_text.find(" of the ");
  if (of_the == std::string::npos) {
    Fail(path, "results have no 'N of the M classes are topologically "
               "equivalent' sentence",
         error);
    return false;
  }
  std::size_t equivalent = 0;
  std::size_t compared = 0;
  if (!ParseUnsignedBefore(s4_text, of_the, &equivalent)) {
    Fail(path, "results name no equivalent-class count before "
               "'of the'",
         error);
    return false;
  }
  std::size_t after = ParseUnsigned(s4_text, of_the + 8, &compared);
  if (after == std::string::npos ||
      s4_text.compare(after, 37,
                      " classes are topologically equivalent") != 0) {
    Fail(path, "results sentence is not 'N of the M classes are "
               "topologically equivalent'",
         error);
    return false;
  }
  if (equivalent >= compared || compared == 0) {
    Fail(path, "results claim " + std::to_string(equivalent) +
                   " of " + std::to_string(compared) +
                   " classes equivalent, which cannot hold",
         error);
    return false;
  }
  const std::size_t exc = s4_text.find("single exception is `");
  if (exc == std::string::npos) {
    Fail(path, "results name no single excepted cell class", error);
    return false;
  }
  const std::size_t open = exc + 21;
  const std::size_t close = s4_text.find('`', open);
  if (close == std::string::npos) {
    Fail(path, "results have an unterminated excepted cell name", error);
    return false;
  }
  const std::string exception_cell = s4_text.substr(open, close - open);
  if (exception_cell.empty() || equivalent != compared - 1) {
    Fail(path, "results name an excepted cell but the equivalent "
               "count is not one below the compared count",
         error);
    return false;
  }
  const std::size_t fingers_at = s4_text.find(" fingers extracted against ");
  if (fingers_at == std::string::npos) {
    Fail(path, "results have no 'N fingers extracted against M in the "
               "reference' sentence",
         error);
    return false;
  }
  std::size_t fingers_extracted = 0;
  std::size_t fingers_reference = 0;
  if (!ParseCommaNumberBefore(s4_text, fingers_at, &fingers_extracted)) {
    Fail(path, "results name no extracted finger count", error);
    return false;
  }
  std::size_t pos = ParseCommaNumber(s4_text, fingers_at + 27,
                                     &fingers_reference);
  if (pos == std::string::npos ||
      s4_text.compare(pos, 17, " in the reference") != 0) {
    Fail(path, "results sentence is not 'N fingers extracted against "
               "M in the reference'",
         error);
    return false;
  }

  const std::size_t s52_begin = FindHeading(lines, "### 5.2");
  if (s52_begin == std::string::npos) {
    Fail(path, "no '### 5.2' D2 decision section", error);
    return false;
  }
  const std::string s52_text =
      JoinLines(lines, s52_begin, SectionEnd(lines, s52_begin));
  if (s52_text.find("D2 is decided a modeling artifact") ==
      std::string::npos) {
    Fail(path, "D2 decision section does not say 'D2 is decided a "
               "modeling artifact'",
         error);
    return false;
  }

  const std::size_t s6_begin = FindHeading(lines, "## 6.");
  if (s6_begin == std::string::npos) {
    Fail(path, "no '## 6.' D1 and verdict section", error);
    return false;
  }
  const std::string s6_text =
      JoinLines(lines, s6_begin, SectionEnd(lines, s6_begin));
  if (s6_text.find("D1 is closed") == std::string::npos) {
    Fail(path, "section 6 does not say 'D1 is closed'", error);
    return false;
  }
  const std::size_t res = s6_text.find("The single residual is the ");
  if (res == std::string::npos) {
    Fail(path, "section 6 names no single residual", error);
    return false;
  }
  const std::size_t res_open = res + 27;
  const std::size_t res_close =
      s6_text.find(" series midpoint", res_open);
  if (res_close == std::string::npos) {
    Fail(path, "section 6 residual is not a series midpoint "
               "representation",
         error);
    return false;
  }
  const std::string residual_short =
      s6_text.substr(res_open, res_close - res_open);
  const std::string residual_suffix = "__" + residual_short;
  const bool residual_matches =
      !residual_short.empty() &&
      (exception_cell == residual_short ||
       (exception_cell.size() > residual_suffix.size() &&
        exception_cell.compare(
            exception_cell.size() - residual_suffix.size(),
            residual_suffix.size(), residual_suffix) == 0));
  if (!residual_matches) {
    Fail(path, "section 6 residual names '" + residual_short +
                   "', which is not the excepted cell '" + exception_cell +
                   "'",
         error);
    return false;
  }
  const std::size_t counts = s6_text.find("(", res_close);
  if (counts == std::string::npos) {
    Fail(path, "section 6 residual states no transistor and instance "
               "counts",
         error);
    return false;
  }
  std::size_t res_transistors = 0;
  std::size_t res_instances = 0;
  pos = ParseUnsigned(s6_text, counts + 1, &res_transistors);
  if (pos == std::string::npos ||
      s6_text.compare(pos, 16, " transistors in ") != 0) {
    Fail(path, "section 6 residual is not '(N transistors in M "
               "instances, cause ...)'",
         error);
    return false;
  }
  pos = ParseUnsigned(s6_text, pos + 16, &res_instances);
  if (pos == std::string::npos ||
      s6_text.compare(pos, 18, " instances, cause ") != 0) {
    Fail(path, "section 6 residual is not '(N transistors in M "
               "instances, cause ...)'",
         error);
    return false;
  }
  const std::size_t cause_begin = pos + 18;
  std::size_t cause_end = s6_text.find_first_of(",) ", cause_begin);
  if (cause_end == std::string::npos) {
    Fail(path, "section 6 residual states no cause", error);
    return false;
  }
  const std::string cause =
      s6_text.substr(cause_begin, cause_end - cause_begin);
  if (cause != "proven" && cause != "unresolved") {
    Fail(path, "section 6 residual cause '" + cause +
                   "' is neither 'proven' nor 'unresolved'",
         error);
    return false;
  }
  const std::string verdict_marker =
      "LVS verdict for the Magic extraction path, as measured:";
  const std::size_t verdict_at = s6_text.find(verdict_marker);
  if (verdict_at == std::string::npos) {
    Fail(path, "section 6 states no LVS verdict for the Magic "
               "extraction path",
         error);
    return false;
  }
  std::size_t verdict_pos = verdict_at + verdict_marker.size();
  while (verdict_pos < s6_text.size() && s6_text[verdict_pos] == ' ') {
    ++verdict_pos;
  }
  SignoffVerdict magic_verdict;
  if (s6_text.compare(verdict_pos, 9, "NOT CLEAN") == 0) {
    magic_verdict = SignoffVerdict::kFail;
  } else if (s6_text.compare(verdict_pos, 5, "CLEAN") == 0) {
    magic_verdict = SignoffVerdict::kPass;
  } else {
    Fail(path, "section 6 LVS verdict is neither 'NOT CLEAN' nor "
               "'CLEAN'",
         error);
    return false;
  }

  const std::size_t sum_begin = FindHeading(lines, "## 7. Summary");
  if (sum_begin == std::string::npos) {
    Fail(path, "no '## 7. Summary' table", error);
    return false;
  }
  const std::size_t sum_end = SectionEnd(lines, sum_begin);
  std::vector<std::string> row;
  if (!TableRow(lines, sum_begin, sum_end, "Cell classes equivalent",
                &row) ||
      row.size() < 3) {
    Fail(path, "summary table has no 'Cell classes equivalent' row",
         error);
    return false;
  }
  {
    const std::size_t of = row[2].find(" of ");
    std::size_t table_equivalent = 0;
    std::size_t table_compared = 0;
    if (of == std::string::npos ||
        !ParseUnsignedBefore(row[2], of, &table_equivalent) ||
        ParseUnsigned(row[2], of + 4, &table_compared) ==
            std::string::npos ||
        table_equivalent != equivalent || table_compared != compared) {
      Fail(path, "summary 'Cell classes equivalent' row disagrees "
                 "with section 4",
           error);
      return false;
    }
  }
  if (!TableRow(lines, sum_begin, sum_end, "D1 substrate split", &row) ||
      row.size() < 3 ||
      row[2].find("Closed") == std::string::npos) {
    Fail(path, "summary 'D1 substrate split' row does not say the "
               "Magic path closed it",
         error);
    return false;
  }
  if (!TableRow(lines, sum_begin, sum_end, "D2", &row) ||
      row.size() < 3 ||
      row[2].find("modeling artifact") == std::string::npos) {
    Fail(path, "summary 'D2' row does not say the Magic path decided "
               "it a modeling artifact",
         error);
    return false;
  }
  if (!TableRow(lines, sum_begin, sum_end, "Verdict", &row) ||
      row.size() < 3) {
    Fail(path, "summary table has no 'Verdict' row", error);
    return false;
  }
  SignoffVerdict table_verdict;
  if (!ParseTableVerdict(row[2], &table_verdict) ||
      table_verdict != magic_verdict) {
    Fail(path, "summary 'Verdict' row disagrees with section 6", error);
    return false;
  }
  if (!TableRow(lines, sum_begin, sum_end, "Remaining residual", &row) ||
      row.size() < 3 ||
      row[2].find("series midpoint") == std::string::npos ||
      row[2].find("cause " + cause) == std::string::npos) {
    Fail(path, "summary 'Remaining residual' row disagrees with "
               "section 6",
         error);
    return false;
  }

  SignoffLvsPath magic;
  magic.path = "magic";
  magic.classes_compared = compared;
  magic.classes_equivalent = equivalent;
  magic.fingers_extracted = fingers_extracted;
  magic.fingers_reference = fingers_reference;
  magic.has_fingers = true;
  magic.verdict = magic_verdict;
  record->lvs_paths.push_back(magic);
  SignoffLvsIssue d1;
  d1.issue = "D1";
  d1.cell = "none";
  d1.kind = "closed";
  d1.path = "magic";
  record->lvs_issues.push_back(d1);
  SignoffLvsIssue d2;
  d2.issue = "D2";
  d2.cell = exception_cell;
  d2.kind = "resolved_artifact";
  d2.path = "magic";
  record->lvs_issues.push_back(d2);
  SignoffLvsResidual residual;
  residual.cell = exception_cell;
  residual.transistors = res_transistors;
  residual.instances = res_instances;
  residual.cause = cause;
  record->lvs_residual = residual;
  if (magic_verdict == SignoffVerdict::kFail) {
    record->lvs = SignoffVerdict::kFail;
  }
  return true;
}

std::string FingerprintHash(const std::string& text) {
  std::uint64_t hash = 14695981039346656037ull;
  for (unsigned char c : text) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  const char* digits = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = digits[hash & 0xf];
    hash >>= 4;
  }
  return out;
}

}  // namespace eos::pv
