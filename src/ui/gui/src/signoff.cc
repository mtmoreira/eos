// signoff: the GUI's signoff model (L6 interface, Qt-free).
#include "eos/ui/signoff.h"

#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace eos::ui {

namespace {

// Splits one payload line into key=value pairs, in order. A
// value is a single token. Returns false on a token without
// '=', or a duplicate key.
bool SplitLine(const std::string& line,
               std::vector<std::pair<std::string, std::string>>* pairs) {
  std::size_t i = 0;
  const std::size_t n = line.size();
  std::set<std::string> seen;
  while (i < n) {
    while (i < n && line[i] == ' ') ++i;
    if (i >= n) break;
    const std::size_t key_start = i;
    while (i < n && line[i] != '=' && line[i] != ' ') ++i;
    if (i >= n || line[i] != '=') return false;
    const std::string key = line.substr(key_start, i - key_start);
    ++i;
    const std::size_t value_start = i;
    while (i < n && line[i] != ' ') ++i;
    const std::string value = line.substr(value_start, i - value_start);
    if (key.empty() || value.empty() || !seen.insert(key).second) {
      return false;
    }
    pairs->push_back({key, value});
  }
  return true;
}

// The value map of one line, or false when the line is
// malformed. Duplicate keys are malformed.
bool LineFields(const std::string& line,
                std::map<std::string, std::string>* fields) {
  std::vector<std::pair<std::string, std::string>> pairs;
  if (!SplitLine(line, &pairs)) return false;
  for (const auto& pair : pairs) (*fields)[pair.first] = pair.second;
  return true;
}

bool HasKeys(const std::map<std::string, std::string>& fields,
             const std::vector<const char*>& keys) {
  for (const char* key : keys) {
    if (fields.find(key) == fields.end()) return false;
  }
  return true;
}

bool OnlyKeys(const std::map<std::string, std::string>& fields,
              const std::vector<const char*>& keys) {
  if (fields.size() != keys.size()) return false;
  return HasKeys(fields, keys);
}

// Parses a non-negative decimal integer covering the whole
// token: no sign, no trailing junk, no overflow past
// size_t.
bool ParseCount(const std::string& text, std::size_t* value) {
  if (text.empty()) return false;
  long parsed = 0;
  for (char c : text) {
    if (c < '0' || c > '9') return false;
    parsed = parsed * 10 + (c - '0');
    if (parsed > 2147483647L) return false;
  }
  *value = static_cast<std::size_t>(parsed);
  return true;
}

// Parses a "pass" or "fail" verdict token.
bool ParseVerdict(const std::string& text, bool* pass) {
  if (text == "pass") {
    *pass = true;
    return true;
  }
  if (text == "fail") {
    *pass = false;
    return true;
  }
  return false;
}

// Parses a 16-digit lowercase hex fingerprint.
bool ParseFingerprint(const std::string& text) {
  if (text.size() != 16) return false;
  for (char c : text) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

}  // namespace

bool SignoffModel::LoadPayload(const std::string& payload) {
  std::istringstream in(payload);
  std::string line;
  if (!std::getline(in, line)) return false;
  const std::size_t space = line.find(' ');
  if (space == std::string::npos) return false;
  if (line.substr(0, space) != "signoff") return false;
  std::map<std::string, std::string> header;
  if (!LineFields(line.substr(space + 1), &header) ||
      !OnlyKeys(header,
                {"drc", "lvs", "antenna", "stale", "drc_categories",
                 "drc_violations", "antenna_net_violations",
                 "antenna_pin_violations", "pv_report", "lvs_report",
                 "fingerprint"})) {
    return false;
  }
  SignoffModel next;
  if (!ParseVerdict(header["drc"], &next.drc_pass_) ||
      !ParseVerdict(header["lvs"], &next.lvs_pass_) ||
      !ParseVerdict(header["antenna"], &next.antenna_pass_) ||
      (header["stale"] != "0" && header["stale"] != "1") ||
      !ParseCount(header["drc_categories"], &next.drc_categories_) ||
      !ParseCount(header["drc_violations"], &next.drc_violations_) ||
      !ParseCount(header["antenna_net_violations"],
                  &next.antenna_net_violations_) ||
      !ParseCount(header["antenna_pin_violations"],
                  &next.antenna_pin_violations_) ||
      !ParseFingerprint(header["fingerprint"])) {
    return false;
  }
  next.stale_ = header["stale"] == "1";
  next.pv_report_ = header["pv_report"];
  next.lvs_report_ = header["lvs_report"];
  next.fingerprint_ = header["fingerprint"];
  if ((next.drc_pass_) != (next.drc_violations_ == 0) ||
      (next.antenna_pass_) !=
          (next.antenna_net_violations_ == 0 &&
           next.antenna_pin_violations_ == 0)) {
    return false;
  }
  std::set<std::string> family_names;
  std::set<std::string> issue_keys;
  std::size_t family_categories = 0;
  std::size_t family_violations = 0;
  bool saw_klayout = false;
  bool saw_magic = false;
  bool saw_residual = false;
  while (std::getline(in, line)) {
    if (line.empty()) return false;
    const std::size_t tag_end = line.find(' ');
    if (tag_end == std::string::npos) return false;
    const std::string kind = line.substr(0, tag_end);
    std::map<std::string, std::string> fields;
    if (!LineFields(line.substr(tag_end + 1), &fields)) return false;
    if (kind == "drc") {
      if (!OnlyKeys(fields, {"family", "categories", "violations"})) {
        return false;
      }
      if (fields["family"].empty() ||
          !family_names.insert(fields["family"]).second) {
        return false;
      }
      SignoffDrcFamily family;
      family.family = fields["family"];
      if (!ParseCount(fields["categories"], &family.categories) ||
          family.categories == 0 ||
          !ParseCount(fields["violations"], &family.violations)) {
        return false;
      }
      family_categories += family.categories;
      family_violations += family.violations;
      next.drc_families_.push_back(std::move(family));
    } else if (kind == "lvs" && fields.count("classes_compared") != 0) {
      // A path row: klayout first, then magic, exactly one each.
      // The klayout row carries no finger totals; the magic row
      // carries both.
      if (saw_magic) return false;
      if (fields["path"] == "klayout") {
        if (saw_klayout) return false;
        if (!OnlyKeys(fields,
                      {"path", "classes_compared", "classes_equivalent",
                       "verdict"})) {
          return false;
        }
        saw_klayout = true;
      } else if (fields["path"] == "magic") {
        if (!OnlyKeys(fields,
                      {"path", "classes_compared", "classes_equivalent",
                       "fingers_extracted", "fingers_reference",
                       "verdict"})) {
          return false;
        }
        saw_magic = true;
      } else {
        return false;
      }
      SignoffLvsPath lvs_path;
      lvs_path.path = fields["path"];
      if (!ParseCount(fields["classes_compared"],
                      &lvs_path.classes_compared) ||
          !ParseCount(fields["classes_equivalent"],
                      &lvs_path.classes_equivalent) ||
          lvs_path.classes_equivalent > lvs_path.classes_compared ||
          !ParseVerdict(fields["verdict"], &lvs_path.pass)) {
        return false;
      }
      if (lvs_path.path == "magic") {
        lvs_path.has_fingers = true;
        if (!ParseCount(fields["fingers_extracted"],
                        &lvs_path.fingers_extracted) ||
            !ParseCount(fields["fingers_reference"],
                        &lvs_path.fingers_reference)) {
          return false;
        }
      }
      next.lvs_paths_.push_back(std::move(lvs_path));
    } else if (kind == "lvs" && fields.count("issue") != 0) {
      if (!OnlyKeys(fields, {"issue", "cell", "kind", "path"})) {
        return false;
      }
      if ((fields["issue"] != "D1" && fields["issue"] != "D2") ||
          fields["cell"].empty() ||
          (fields["kind"] != "extraction_artifact" &&
           fields["kind"] != "unresolved" &&
           fields["kind"] != "closed" &&
           fields["kind"] != "resolved_artifact") ||
          (fields["path"] != "klayout" && fields["path"] != "magic") ||
          !issue_keys.insert(fields["issue"] + "|" + fields["path"])
               .second) {
        return false;
      }
      SignoffLvsIssue issue;
      issue.issue = fields["issue"];
      issue.cell = fields["cell"];
      issue.kind = fields["kind"];
      issue.path = fields["path"];
      next.lvs_issues_.push_back(std::move(issue));
    } else if (kind == "lvs_residual") {
      if (saw_residual) return false;
      if (!OnlyKeys(fields, {"cell", "transistors", "instances",
                             "cause", "path"})) {
        return false;
      }
      if (fields["cell"].empty() ||
          (fields["cause"] != "proven" &&
           fields["cause"] != "unresolved") ||
          fields["path"] != "magic") {
        return false;
      }
      SignoffLvsResidual residual;
      residual.cell = fields["cell"];
      if (!ParseCount(fields["transistors"], &residual.transistors) ||
          !ParseCount(fields["instances"], &residual.instances)) {
        return false;
      }
      residual.cause = fields["cause"];
      next.residual_ = residual;
      saw_residual = true;
    } else {
      return false;
    }
  }
  if (next.drc_families_.empty() || !saw_klayout || !saw_magic ||
      family_categories > next.drc_categories_ ||
      family_violations != next.drc_violations_) {
    return false;
  }
  bool paths_pass = true;
  for (const SignoffLvsPath& lvs_path : next.lvs_paths_) {
    if (!lvs_path.pass) paths_pass = false;
  }
  if (next.lvs_pass_ != paths_pass) return false;
  next.loaded_ = true;
  *this = std::move(next);
  return true;
}

void SignoffModel::Clear() {
  loaded_ = false;
  drc_pass_ = false;
  lvs_pass_ = false;
  antenna_pass_ = false;
  stale_ = false;
  drc_categories_ = 0;
  drc_violations_ = 0;
  antenna_net_violations_ = 0;
  antenna_pin_violations_ = 0;
  pv_report_.clear();
  lvs_report_.clear();
  fingerprint_.clear();
  drc_families_.clear();
  lvs_paths_.clear();
  lvs_issues_.clear();
  residual_.reset();
}

}  // namespace eos::ui
