// Unit tests: tech/package.
#include "eos/tech/package.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace eos::tech {
namespace {

std::string HashOf(char fill) { return std::string(64, fill); }

// A small fully consistent package: two cells (one with PG
// pins), one macro with all required views, special cells,
// one scenario and required collateral with valid hashes.
TechnologyPackage ConsistentPackage() {
  TechnologyPackage pkg;
  pkg.name = "sky130hd_test";
  pkg.version = "1.0.0";
  pkg.time_unit = "ns";
  pkg.capacitance_unit = "pF";

  pkg.collateral = {
      {"liberty_tt", ViewKind::kLiberty, "lib/tt.lib", "1.0.0", HashOf('a'),
       true},
      {"tech_lef", ViewKind::kTechLef, "lef/tech.lef", "1.0.0", HashOf('b'),
       true},
      {"cell_lef", ViewKind::kCellLef, "lef/cells.lef", "1.0.0", HashOf('c'),
       true},
      {"drc_runset", ViewKind::kDrcRunset, "drc/runset.lydrc", "1.0.0",
       HashOf('d'), true},
  };

  const CellPins inv{"INV_X1", {"A", "Y"}, {"VPWR", "VGND"}};
  const CellPins buf{"BUF_X1", {"A", "Y"}, {}};
  pkg.liberty_cells = {inv, buf};
  pkg.lef_cells = {inv, buf};
  pkg.logical_cells = {inv, buf};

  MacroDef macro;
  macro.name = "SRAM_256X32";
  macro.views = {ViewKind::kCellLef, ViewKind::kGds, ViewKind::kLiberty,
                 ViewKind::kCdl};
  macro.black_box = false;
  macro.abstraction_contract = "";
  pkg.macros = {macro};

  pkg.special_cells = {{"tie_high", "INV_X1"}, {"filler", "BUF_X1"}};
  pkg.scenarios = {{"func_tt", "tt_025c", "typical"}};
  pkg.liberty_corners = {"tt_025c", "ss_125c"};
  pkg.supply_aliases = {{"VDD", "VPWR"}, {"VSS", "VGND"}};
  return pkg;
}

bool HasRule(const std::vector<Finding>& findings, const std::string& rule) {
  for (const Finding& finding : findings) {
    if (finding.rule == rule) {
      return true;
    }
  }
  return false;
}

int CountRule(const std::vector<Finding>& findings, const std::string& rule) {
  int count = 0;
  for (const Finding& finding : findings) {
    if (finding.rule == rule) {
      ++count;
    }
  }
  return count;
}

TEST(PackageChecker, ConsistentPackagePasses) {
  const TechnologyPackage pkg = ConsistentPackage();
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(findings.empty());
}

TEST(PackageChecker, MissingVersionPackage) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.version = "";
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "missing_version"));
}

TEST(PackageChecker, MissingVersionRequiredCollateral) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.collateral[0].version = "";
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "missing_version"));
  EXPECT_EQ(CountRule(findings, "missing_version"), 1);
}

TEST(PackageChecker, MissingHash) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.collateral[0].sha256 = "abc123";
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "missing_hash"));
  EXPECT_EQ(CountRule(findings, "missing_hash"), 1);
}

TEST(PackageChecker, MissingHashRejectsUppercaseHex) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.collateral[0].sha256 = std::string(64, 'A');
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "missing_hash"));
}

TEST(PackageChecker, DuplicateCollateralId) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.collateral.push_back(pkg.collateral[0]);
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "duplicate_collateral_id"));
  EXPECT_EQ(CountRule(findings, "duplicate_collateral_id"), 1);
}

TEST(PackageChecker, CellMissingInLef) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.liberty_cells.push_back(CellPins{"ONLY_LIB_X1", {"A", "Y"}, {}});
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "cell_missing_in_lef"));
  EXPECT_EQ(CountRule(findings, "cell_missing_in_lef"), 1);
}

TEST(PackageChecker, CellMissingInLiberty) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.lef_cells.push_back(CellPins{"ONLY_LEF_X1", {"A", "Y"}, {}});
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "cell_missing_in_liberty"));
  EXPECT_EQ(CountRule(findings, "cell_missing_in_liberty"), 1);
}

TEST(PackageChecker, PinMismatchLef) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.lef_cells[0].pins = {"A", "Y", "Z"};
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "pin_mismatch"));
}

TEST(PackageChecker, PinMismatchLogical) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.logical_cells[0].pins = {"A"};
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "pin_mismatch"));
}

TEST(PackageChecker, PinOrderDoesNotMatter) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.lef_cells[0].pins = {"Y", "A"};
  pkg.lef_cells[0].pg_pins = {"VGND", "VPWR"};
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(findings.empty());
}

TEST(PackageChecker, PgPinMismatch) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.lef_cells[0].pg_pins = {"VPWR"};
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "pg_pin_mismatch"));
  EXPECT_EQ(CountRule(findings, "pg_pin_mismatch"), 1);
  EXPECT_FALSE(HasRule(findings, "pin_mismatch"));
}

TEST(PackageChecker, SupplyAliasAmbiguousChain) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.supply_aliases["MY_VDD"] = "VDD";
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "supply_alias_ambiguous"));
}

TEST(PackageChecker, SupplyAliasAmbiguousSelfAlias) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.supply_aliases["VPWR"] = "VPWR";
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "supply_alias_ambiguous"));
}

TEST(PackageChecker, UnitMismatch) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.time_unit = "";
  std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "unit_mismatch"));
  EXPECT_EQ(CountRule(findings, "unit_mismatch"), 1);

  pkg = ConsistentPackage();
  pkg.capacitance_unit = "";
  findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "unit_mismatch"));
  EXPECT_EQ(CountRule(findings, "unit_mismatch"), 1);
}

TEST(PackageChecker, MacroMissingView) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.macros[0].views = {ViewKind::kCellLef, ViewKind::kLiberty,
                         ViewKind::kCdl};
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "macro_missing_view"));
  EXPECT_EQ(CountRule(findings, "macro_missing_view"), 1);
}

TEST(PackageChecker, MacroMissingMultipleViewsCountsEach) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.macros[0].views = {ViewKind::kCellLef};
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "macro_missing_view"));
  EXPECT_EQ(CountRule(findings, "macro_missing_view"), 3);
}

TEST(PackageChecker, BlackBoxWithoutContract) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.macros[0].black_box = true;
  pkg.macros[0].abstraction_contract = "";
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "black_box_without_contract"));
  EXPECT_EQ(CountRule(findings, "black_box_without_contract"), 1);
}

TEST(PackageChecker, BlackBoxWithContractPasses) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.macros[0].black_box = true;
  pkg.macros[0].abstraction_contract = "timing and power abstract only";
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(findings.empty());
}

TEST(PackageChecker, SpecialCellUnknown) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.special_cells.push_back(SpecialCell{"decap", "NO_SUCH_CELL"});
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "special_cell_unknown"));
  EXPECT_EQ(CountRule(findings, "special_cell_unknown"), 1);
}

TEST(PackageChecker, ScenarioUnknownCorner) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.scenarios.push_back(PackageScenario{"func_xx", "xx_999c", "typical"});
  const std::vector<Finding> findings = CheckPackage(pkg);
  EXPECT_TRUE(HasRule(findings, "scenario_unknown_corner"));
  EXPECT_EQ(CountRule(findings, "scenario_unknown_corner"), 1);
}

TEST(PackageChecker, FindingsAreSortedByRuleAndSubject) {
  TechnologyPackage pkg = ConsistentPackage();
  pkg.version = "";
  pkg.time_unit = "";
  pkg.lef_cells.push_back(CellPins{"ONLY_LEF_X1", {"A", "Y"}, {}});
  const std::vector<Finding> findings = CheckPackage(pkg);
  ASSERT_GE(findings.size(), 3u);
  for (std::size_t i = 1; i < findings.size(); ++i) {
    const bool ordered =
        findings[i - 1].rule < findings[i].rule ||
        (findings[i - 1].rule == findings[i].rule &&
         findings[i - 1].subject <= findings[i].subject);
    EXPECT_TRUE(ordered) << findings[i - 1].rule << "/" << findings[i - 1].subject
                         << " before " << findings[i].rule << "/"
                         << findings[i].subject;
  }
}

}  // namespace
}  // namespace eos::tech
