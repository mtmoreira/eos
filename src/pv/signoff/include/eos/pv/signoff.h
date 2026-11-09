// signoff: the signoff verdict record and the parsers for the
// external PV evidence reports (L4 engines, pure text processing).
//
// F13 panel unit: the Signoff panel renders the measured verdicts
// from the existing external KLayout and Netgen reports
// (docs/gcd-pv-results.md and docs/gcd-lvs-magic.md), never from
// hard-coded strings. This module parses those two documents
// into one verdict record. It runs no engine and reads no
// layout: the reports are the evidence, produced outside EOS by
// the pinned KLayout, Magic, and Netgen runs. A later unit owns
// the in-tree DRC engine; this module only reads what the
// external runs already measured.
//
// Both parsers are shape-based, not value-based: they anchor on
// the documents' own section headings, result sentences, and
// summary tables, and cross-check the sections against the
// tables. A re-run that changes the numbers but keeps the shape
// parses fine; a document that is missing, truncated, or
// internally inconsistent fails with the path and the anchor
// named, never with a partial verdict.
#ifndef EOS_PV_SIGNOFF_H_
#define EOS_PV_SIGNOFF_H_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace eos::pv {

// One check's verdict: pass means the check ran and found no
// violation; fail means it ran and found at least one, or the
// comparison did not come out clean.
enum class SignoffVerdict { kPass, kFail };

// One DRC rule family from the PV report's coverage sentence:
// the family name (m1, via2, li, ...) with its category count
// and its violation count. The report lists explicit counts
// for fourteen families; the remaining categories are not
// named per family in the document, so they are not listed
// here either. The header's totals are the authority.
struct SignoffDrcFamily {
  std::string family;
  std::size_t categories = 0;
  std::size_t violations = 0;
};

// One LVS comparison path: "klayout" from the PV report
// (KLayout extraction plus Netgen) or "magic" from the LVS
// report (Magic extraction plus Netgen). The finger totals
// exist only on the magic path, which counts flat transistor
// fingers; the KLayout path reports device counts in prose
// that this record does not carry.
struct SignoffLvsPath {
  std::string path;
  std::size_t classes_compared = 0;
  std::size_t classes_equivalent = 0;
  std::size_t fingers_extracted = 0;
  std::size_t fingers_reference = 0;
  bool has_fingers = false;
  SignoffVerdict verdict = SignoffVerdict::kFail;
};

// One named LVS discrepancy: D1 or D2, the cell it names
// ("none" when it names no cell, as D1 does), its kind in a
// closed vocabulary, and the path it was measured on.
// Kinds: "extraction_artifact" (a modeling limit of the
// extraction rules, not a layout error), "unresolved" (a real
// difference whose cause the run did not decide), "closed"
// (the discrepancy does not occur on this path), and
// "resolved_artifact" (decided a modeling artifact of another
// path by a cell-level proof).
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

// The parsed verdict record for one signoff run: the DRC,
// LVS, and antenna verdicts, the counts behind them, and the
// report paths they came from. The overall LVS verdict is
// fail when either path fails, pass only when both pass.
struct SignoffRecord {
  SignoffVerdict drc = SignoffVerdict::kFail;
  SignoffVerdict lvs = SignoffVerdict::kFail;
  SignoffVerdict antenna = SignoffVerdict::kFail;
  std::size_t drc_categories = 0;
  std::size_t drc_violations = 0;
  std::size_t antenna_net_violations = 0;
  std::size_t antenna_pin_violations = 0;
  std::vector<SignoffDrcFamily> drc_families;
  std::vector<SignoffLvsPath> lvs_paths;
  std::vector<SignoffLvsIssue> lvs_issues;
  std::optional<SignoffLvsResidual> lvs_residual;
  std::string pv_report;
  std::string lvs_report;
};

// Parses the PV evidence document (the docs/gcd-pv-results.md
// shape): DRC section 2 (categories, violations, per-family
// counts), antenna section 4 (net and pin violations), LVS
// sections 3.4 to 3.6 (classes compared, the named
// discrepancies D1 and D2, the verdict), and the section 5
// summary table as a cross-check. Fills the DRC, antenna, and
// KLayout-path LVS fields of the record and sets the overall
// LVS verdict to the KLayout path's verdict; the caller runs
// ParseLvsText after this to fold in the magic path. Returns
// false with error naming the path and the missing or
// inconsistent anchor when the text is not the expected shape.
bool ParsePvText(const std::string& text, const std::string& path,
                 SignoffRecord* record, std::string* error);

// Parses the LVS evidence document (the docs/gcd-lvs-magic.md
// shape): section 4 (classes compared and equivalent, finger
// totals, the excepted cell), section 5.2 (D2 decided a
// modeling artifact), sections 5.3 and 6 (the successor
// residual, D1 closed, the verdict), and the section 7 summary
// table as a cross-check. Fills the magic-path LVS fields of
// the record and folds its verdict into the overall LVS
// verdict (fail when the magic path fails, never upgrading a
// fail to a pass). Returns false with error naming the path
// and the missing or inconsistent anchor when the text is not
// the expected shape.
bool ParseLvsText(const std::string& text, const std::string& path,
                  SignoffRecord* record, std::string* error);

// FNV-1a hash of a fingerprint string, as 16 lowercase hex
// digits. Deterministic across runs, so a stored fingerprint
// compares equal after a restart. Used for the design
// fingerprint and for the report content hashes.
std::string FingerprintHash(const std::string& text);

}  // namespace eos::pv

#endif  // EOS_PV_SIGNOFF_H_
