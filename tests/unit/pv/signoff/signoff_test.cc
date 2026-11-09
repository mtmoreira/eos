// Unit tests: pv/signoff, the external PV evidence parsers.
#include "eos/pv/signoff.h"

#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace eos::pv {
namespace {

// A minimal PV evidence document in the docs/gcd-pv-results.md
// shape: same headings, result sentences, discrepancy
// paragraphs, and summary table, smaller numbers.
const char kPvDoc[] =
    "# PV results (test fixture)\n"
    "\n"
    "## 2. DRC\n"
    "\n"
    "Result: the report database holds 3 rule categories and 0 "
    "violations.\n"
    "Coverage by rule family (categories per family): metal layers "
    "m1 2, m2 1; every remaining family is a single category.\n"
    "\n"
    "## 3. LVS\n"
    "\n"
    "### 3.4 Results\n"
    "\n"
    "68 cell classes compared. Every stdcell class gcd uses is "
    "topologically equivalent to its CDL definition except "
    "`sky130_fd_sc_hd__a21oi_2` (Section 3.5).\n"
    "\n"
    "### 3.5 The named discrepancies\n"
    "\n"
    "D1. Substrate net model (extraction artifact, named, not a "
    "layout error): the runset forces the substrate to a global "
    "net.\n"
    "\n"
    "D2. `sky130_fd_sc_hd__a21oi_2` (a real extraction-level "
    "difference, cause not fully resolved): inside the extracted "
    "cell the VGND net appears as two nets.\n"
    "\n"
    "### 3.6 LVS verdict\n"
    "\n"
    "LVS does not pass clean on gcd at this pin.\n"
    "\n"
    "## 4. Antenna\n"
    "\n"
    "Result: 0 net violations, 0 pin violations.\n"
    "\n"
    "## 5. Summary and tier position\n"
    "\n"
    "| Check | Verdict | Evidence |\n"
    "|---|---|---|\n"
    "| DRC (KLayout deck, 3 categories) | PASS, 0 violations | "
    "Section 2 |\n"
    "| Antenna (final geometry) | PASS, 0 net and 0 pin violations | "
    "Section 4 |\n"
    "| LVS (KLayout extraction + Netgen) | NOT CLEAN | Sections 3.4 "
    "to 3.6 |\n";

// A minimal LVS evidence document in the docs/gcd-lvs-magic.md
// shape: same headings, results, decision, residual, verdict,
// and summary table, smaller numbers.
const char kLvsDoc[] =
    "# LVS by the Magic extraction path (test fixture)\n"
    "\n"
    "## 4. Results\n"
    "\n"
    "Netgen compared 68 standard cell classes and the gcd top "
    "level. 67 of the 68 classes are topologically equivalent "
    "to their CDL definitions, with cell pin lists equivalent "
    "in every class. The single exception is "
    "`sky130_fd_sc_hd__a21oi_2` (Section 5).\n"
    "\n"
    "Top level: the flat totals agree exactly: 3,676 fingers "
    "extracted against 3,676 in the reference.\n"
    "\n"
    "## 5. D2 resolved at the cell level, and the successor residual\n"
    "\n"
    "### 5.2 Decision on D2\n"
    "\n"
    "D2 as recorded in unit 3.1 does not exist in this cell's "
    "geometry under the Magic extraction model. By the judging "
    "rule of Section 2, D2 is decided a modeling artifact of the "
    "KLayout extraction path.\n"
    "\n"
    "## 6. D1, and the LVS verdict for this path\n"
    "\n"
    "D1 is closed under this extraction model.\n"
    "\n"
    "LVS verdict for the Magic extraction path, as measured: "
    "NOT CLEAN, with one named residual. 67 of 68 standard "
    "cell classes are equivalent, and there are no property "
    "errors. The single residual is the a21oi_2 series midpoint "
    "representation of Section 5.3 (4 transistors in 2 instances, "
    "cause proven by controlled diagnostic).\n"
    "\n"
    "## 7. Summary\n"
    "\n"
    "| Item | Unit 3.1 (KLayout path) | Unit F3 (Magic path) |\n"
    "|---|---|---|\n"
    "| Cell classes equivalent | 67 of 68 (a21oi_2 excepted) | 67 of "
    "68 (a21oi_2 excepted) |\n"
    "| Flat devices (extracted vs reference) | 2,941 vs 2,937 | 3,676 "
    "vs 3,676 fingers |\n"
    "| D1 substrate split | Present (extraction artifact) | Closed "
    "(no split) |\n"
    "| D2 | VGND split in a21oi_2, cause unresolved | VGND split "
    "absent; decided KLayout modeling artifact |\n"
    "| Remaining residual | D1 and D2 | a21oi_2 series midpoint "
    "representation, cause proven |\n"
    "| Verdict | NOT CLEAN | NOT CLEAN, one named residual |\n";

bool ReadFile(const std::string& path, std::string* text) {
  std::ifstream in(path);
  if (!in) return false;
  std::stringstream buffer;
  buffer << in.rdbuf();
  *text = buffer.str();
  return true;
}

}  // namespace

TEST(SignoffPv, ParsesFixtureDocument) {
  SignoffRecord record;
  std::string error;
  ASSERT_TRUE(ParsePvText(kPvDoc, "pv.md", &record, &error)) << error;
  EXPECT_EQ(record.drc, SignoffVerdict::kPass);
  EXPECT_EQ(record.antenna, SignoffVerdict::kPass);
  EXPECT_EQ(record.lvs, SignoffVerdict::kFail);
  EXPECT_EQ(record.drc_categories, 3u);
  EXPECT_EQ(record.drc_violations, 0u);
  EXPECT_EQ(record.antenna_net_violations, 0u);
  EXPECT_EQ(record.antenna_pin_violations, 0u);
  ASSERT_EQ(record.drc_families.size(), 2u);
  EXPECT_EQ(record.drc_families[0].family, "m1");
  EXPECT_EQ(record.drc_families[0].categories, 2u);
  EXPECT_EQ(record.drc_families[1].family, "m2");
  EXPECT_EQ(record.drc_families[1].categories, 1u);
  ASSERT_EQ(record.lvs_paths.size(), 1u);
  EXPECT_EQ(record.lvs_paths[0].path, "klayout");
  EXPECT_EQ(record.lvs_paths[0].classes_compared, 68u);
  EXPECT_EQ(record.lvs_paths[0].classes_equivalent, 67u);
  EXPECT_FALSE(record.lvs_paths[0].has_fingers);
  EXPECT_EQ(record.lvs_paths[0].verdict, SignoffVerdict::kFail);
  ASSERT_EQ(record.lvs_issues.size(), 2u);
  EXPECT_EQ(record.lvs_issues[0].issue, "D1");
  EXPECT_EQ(record.lvs_issues[0].cell, "none");
  EXPECT_EQ(record.lvs_issues[0].kind, "extraction_artifact");
  EXPECT_EQ(record.lvs_issues[0].path, "klayout");
  EXPECT_EQ(record.lvs_issues[1].issue, "D2");
  EXPECT_EQ(record.lvs_issues[1].cell, "sky130_fd_sc_hd__a21oi_2");
  EXPECT_EQ(record.lvs_issues[1].kind, "unresolved");
  EXPECT_EQ(record.lvs_issues[1].path, "klayout");
  EXPECT_FALSE(record.lvs_residual.has_value());
}

TEST(SignoffLvs, ParsesFixtureDocument) {
  SignoffRecord record;
  std::string error;
  ASSERT_TRUE(ParsePvText(kPvDoc, "pv.md", &record, &error)) << error;
  ASSERT_TRUE(ParseLvsText(kLvsDoc, "lvs.md", &record, &error)) << error;
  EXPECT_EQ(record.lvs, SignoffVerdict::kFail);
  ASSERT_EQ(record.lvs_paths.size(), 2u);
  EXPECT_EQ(record.lvs_paths[1].path, "magic");
  EXPECT_EQ(record.lvs_paths[1].classes_compared, 68u);
  EXPECT_EQ(record.lvs_paths[1].classes_equivalent, 67u);
  EXPECT_TRUE(record.lvs_paths[1].has_fingers);
  EXPECT_EQ(record.lvs_paths[1].fingers_extracted, 3676u);
  EXPECT_EQ(record.lvs_paths[1].fingers_reference, 3676u);
  EXPECT_EQ(record.lvs_paths[1].verdict, SignoffVerdict::kFail);
  ASSERT_EQ(record.lvs_issues.size(), 4u);
  EXPECT_EQ(record.lvs_issues[2].issue, "D1");
  EXPECT_EQ(record.lvs_issues[2].cell, "none");
  EXPECT_EQ(record.lvs_issues[2].kind, "closed");
  EXPECT_EQ(record.lvs_issues[2].path, "magic");
  EXPECT_EQ(record.lvs_issues[3].issue, "D2");
  EXPECT_EQ(record.lvs_issues[3].cell, "sky130_fd_sc_hd__a21oi_2");
  EXPECT_EQ(record.lvs_issues[3].kind, "resolved_artifact");
  EXPECT_EQ(record.lvs_issues[3].path, "magic");
  ASSERT_TRUE(record.lvs_residual.has_value());
  EXPECT_EQ(record.lvs_residual->cell, "sky130_fd_sc_hd__a21oi_2");
  EXPECT_EQ(record.lvs_residual->transistors, 4u);
  EXPECT_EQ(record.lvs_residual->instances, 2u);
  EXPECT_EQ(record.lvs_residual->cause, "proven");
}

TEST(SignoffPv, RejectsMissingSections) {
  SignoffRecord record;
  std::string error;
  EXPECT_FALSE(ParsePvText("", "pv.md", &record, &error));
  EXPECT_NE(error.find("pv.md"), std::string::npos);
  EXPECT_FALSE(ParsePvText("# PV results\n\n## 2. DRC\n", "pv.md", &record,
                          &error));
  EXPECT_NE(error.find("pv.md"), std::string::npos);
  EXPECT_FALSE(ParseLvsText("", "lvs.md", &record, &error));
  EXPECT_NE(error.find("lvs.md"), std::string::npos);
}

TEST(SignoffPv, RejectsInconsistentSummary) {
  // The table says PASS while section 2 counts 2 violations.
  std::string doc = kPvDoc;
  const std::string from =
      "holds 3 rule categories and 0 violations";
  const std::string to = "holds 3 rule categories and 2 violations";
  ASSERT_NE(doc.find(from), std::string::npos);
  doc.replace(doc.find(from), from.size(), to);
  SignoffRecord record;
  std::string error;
  EXPECT_FALSE(ParsePvText(doc, "pv.md", &record, &error));
  EXPECT_NE(error.find("pv.md"), std::string::npos);
}

TEST(SignoffPv, RejectsUnknownDiscrepancy) {
  std::string doc = kPvDoc;
  const std::string from = "D2. `sky130_fd_sc_hd__a21oi_2`";
  const std::string to = "D3. `sky130_fd_sc_hd__a21oi_2`";
  ASSERT_NE(doc.find(from), std::string::npos);
  doc.replace(doc.find(from), from.size(), to);
  SignoffRecord record;
  std::string error;
  EXPECT_FALSE(ParsePvText(doc, "pv.md", &record, &error));
}

TEST(SignoffLvs, RejectsInconsistentSummary) {
  // The table says the Magic path closed D1 while section 6 does
  // not: remove the "D1 is closed" sentence.
  std::string doc = kLvsDoc;
  const std::string from = "D1 is closed under this extraction model.\n";
  ASSERT_NE(doc.find(from), std::string::npos);
  doc.replace(doc.find(from), from.size(), "");
  SignoffRecord record;
  std::string error;
  ASSERT_TRUE(ParsePvText(kPvDoc, "pv.md", &record, &error)) << error;
  EXPECT_FALSE(ParseLvsText(doc, "lvs.md", &record, &error));
  EXPECT_NE(error.find("lvs.md"), std::string::npos);
}

TEST(SignoffFingerprint, HashIsDeterministicHex) {
  const std::string first = FingerprintHash("optloop|mapped");
  EXPECT_EQ(first, FingerprintHash("optloop|mapped"));
  EXPECT_EQ(first.size(), 16u);
  EXPECT_NE(first, FingerprintHash("optloop|placed"));
  EXPECT_NE(first, FingerprintHash(""));
  for (char c : first) {
    EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
  }
}

// The golden test: the real evidence documents parse to the
// recorded gcd verdicts. Runs under ctest with the working
// directory at the repository root; skipped when run standalone
// from elsewhere, like the ORFS-gated tests.
TEST(SignoffGolden, RealDocumentsParse) {
  std::string pv_text;
  std::string lvs_text;
  if (!ReadFile("docs/gcd-pv-results.md", &pv_text) ||
      !ReadFile("docs/gcd-lvs-magic.md", &lvs_text)) {
    GTEST_SKIP() << "evidence documents not found from the working "
                    "directory; run under ctest";
  }
  SignoffRecord record;
  std::string error;
  ASSERT_TRUE(ParsePvText(pv_text, "docs/gcd-pv-results.md", &record,
                          &error))
      << error;
  ASSERT_TRUE(ParseLvsText(lvs_text, "docs/gcd-lvs-magic.md", &record,
                           &error))
      << error;
  EXPECT_EQ(record.drc, SignoffVerdict::kPass);
  EXPECT_EQ(record.antenna, SignoffVerdict::kPass);
  EXPECT_EQ(record.lvs, SignoffVerdict::kFail);
  EXPECT_EQ(record.drc_categories, 145u);
  EXPECT_EQ(record.drc_violations, 0u);
  EXPECT_EQ(record.antenna_net_violations, 0u);
  EXPECT_EQ(record.antenna_pin_violations, 0u);
  ASSERT_EQ(record.drc_families.size(), 14u);
  std::size_t family_total = 0;
  for (const SignoffDrcFamily& family : record.drc_families) {
    EXPECT_GT(family.categories, 0u);
    EXPECT_EQ(family.violations, 0u);
    family_total += family.categories;
  }
  EXPECT_EQ(family_total, 70u);
  EXPECT_EQ(record.drc_families[0].family, "m1");
  EXPECT_EQ(record.drc_families[0].categories, 8u);
  ASSERT_EQ(record.lvs_paths.size(), 2u);
  EXPECT_EQ(record.lvs_paths[0].path, "klayout");
  EXPECT_EQ(record.lvs_paths[0].classes_compared, 68u);
  EXPECT_EQ(record.lvs_paths[0].classes_equivalent, 67u);
  EXPECT_EQ(record.lvs_paths[1].path, "magic");
  EXPECT_EQ(record.lvs_paths[1].classes_compared, 68u);
  EXPECT_EQ(record.lvs_paths[1].classes_equivalent, 67u);
  EXPECT_EQ(record.lvs_paths[1].fingers_extracted, 3676u);
  EXPECT_EQ(record.lvs_paths[1].fingers_reference, 3676u);
  ASSERT_EQ(record.lvs_issues.size(), 4u);
  EXPECT_EQ(record.lvs_issues[0].issue, "D1");
  EXPECT_EQ(record.lvs_issues[0].kind, "extraction_artifact");
  EXPECT_EQ(record.lvs_issues[1].issue, "D2");
  EXPECT_EQ(record.lvs_issues[1].cell, "sky130_fd_sc_hd__a21oi_2");
  EXPECT_EQ(record.lvs_issues[1].kind, "unresolved");
  EXPECT_EQ(record.lvs_issues[2].kind, "closed");
  EXPECT_EQ(record.lvs_issues[3].kind, "resolved_artifact");
  ASSERT_TRUE(record.lvs_residual.has_value());
  EXPECT_EQ(record.lvs_residual->cell, "sky130_fd_sc_hd__a21oi_2");
  EXPECT_EQ(record.lvs_residual->transistors, 4u);
  EXPECT_EQ(record.lvs_residual->instances, 2u);
  EXPECT_EQ(record.lvs_residual->cause, "proven");
}

}  // namespace eos::pv
