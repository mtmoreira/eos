// signoff: the GUI's signoff model (L6 interface, Qt-free).
//
// The model the Signoff panel reads: the verdict record from a
// signoff_summary payload. Parsing is all-or-nothing like the
// other models: a malformed payload leaves the previous
// signoff untouched and reports false. A payload value is a
// single token: no value in this payload contains a space.
#ifndef EOS_UI_SIGNOFF_H_
#define EOS_UI_SIGNOFF_H_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace eos::ui {

// One DRC rule family from the signoff payload: the family
// name with its category count and its violation count.
struct SignoffDrcFamily {
  std::string family;
  std::size_t categories = 0;
  std::size_t violations = 0;
};

// One LVS comparison path from the signoff payload:
// "klayout" (KLayout extraction plus Netgen) or "magic"
// (Magic extraction plus Netgen). The finger totals exist
// only on the magic path.
struct SignoffLvsPath {
  std::string path;
  std::size_t classes_compared = 0;
  std::size_t classes_equivalent = 0;
  std::size_t fingers_extracted = 0;
  std::size_t fingers_reference = 0;
  bool has_fingers = false;
  bool pass = false;
};

// One named LVS discrepancy from the signoff payload: D1 or
// D2, the cell it names ("none" when it names no cell), its
// kind, and the path it was measured on. Kinds:
// "extraction_artifact", "unresolved", "closed", and
// "resolved_artifact".
struct SignoffLvsIssue {
  std::string issue;
  std::string cell;
  std::string kind;
  std::string path;
};

// The single remaining LVS residual on the magic path: the
// cell, the transistor and instance counts it touches, and
// whether its cause is proven by a controlled diagnostic
// ("proven") or still open ("unresolved").
struct SignoffLvsResidual {
  std::string cell;
  std::size_t transistors = 0;
  std::size_t instances = 0;
  std::string cause;
};

class SignoffModel {
 public:
  // Parses a signoff_summary payload: a "signoff" header line
  // with the DRC, LVS, and antenna verdicts, the stale flag,
  // the DRC and antenna counts, the two report paths, and the
  // design fingerprint; then one "drc" line per rule family,
  // one "lvs" line per comparison path (klayout, then magic),
  // one "lvs" line per named discrepancy, and at most one
  // "lvs_residual" line. The header's DRC verdict must agree
  // with its violation count, the antenna verdict with its net
  // and pin counts, and the LVS verdict with the two path
  // verdicts; the family categories must sum to no more than
  // the header total and the family violations must sum to the
  // header total. Returns false (signoff unchanged) on any
  // malformed line, unknown key, unknown verdict, kind, or
  // cause, duplicate family or discrepancy, missing path row,
  // or count or verdict mismatch. A successful load replaces
  // the previous signoff.
  bool LoadPayload(const std::string& payload);
  void Clear();

  bool Loaded() const { return loaded_; }
  bool Empty() const { return !loaded_; }
  bool DrcPass() const { return drc_pass_; }
  bool LvsPass() const { return lvs_pass_; }
  bool AntennaPass() const { return antenna_pass_; }
  bool Stale() const { return stale_; }
  std::size_t DrcCategories() const { return drc_categories_; }
  std::size_t DrcViolations() const { return drc_violations_; }
  std::size_t AntennaNetViolations() const {
    return antenna_net_violations_;
  }
  std::size_t AntennaPinViolations() const {
    return antenna_pin_violations_;
  }
  const std::string& PvReport() const { return pv_report_; }
  const std::string& LvsReport() const { return lvs_report_; }
  const std::string& Fingerprint() const { return fingerprint_; }
  const std::vector<SignoffDrcFamily>& DrcFamilies() const {
    return drc_families_;
  }
  const std::vector<SignoffLvsPath>& LvsPaths() const {
    return lvs_paths_;
  }
  const std::vector<SignoffLvsIssue>& LvsIssues() const {
    return lvs_issues_;
  }
  bool HasResidual() const { return residual_.has_value(); }
  const SignoffLvsResidual& Residual() const { return *residual_; }

 private:
  bool loaded_ = false;
  bool drc_pass_ = false;
  bool lvs_pass_ = false;
  bool antenna_pass_ = false;
  bool stale_ = false;
  std::size_t drc_categories_ = 0;
  std::size_t drc_violations_ = 0;
  std::size_t antenna_net_violations_ = 0;
  std::size_t antenna_pin_violations_ = 0;
  std::string pv_report_;
  std::string lvs_report_;
  std::string fingerprint_;
  std::vector<SignoffDrcFamily> drc_families_;
  std::vector<SignoffLvsPath> lvs_paths_;
  std::vector<SignoffLvsIssue> lvs_issues_;
  std::optional<SignoffLvsResidual> residual_;
};

}  // namespace eos::ui

#endif  // EOS_UI_SIGNOFF_H_
