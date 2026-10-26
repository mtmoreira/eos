// package: technology package model and consistency checker
// (L2 design-data).
#include "eos/tech/package.h"

#include <algorithm>
#include <set>

namespace eos::tech {
namespace {

bool IsValidSha256(const std::string& value) {
  if (value.size() != 64) {
    return false;
  }
  for (const char c : value) {
    const bool is_digit = c >= '0' && c <= '9';
    const bool is_lower_hex = c >= 'a' && c <= 'f';
    if (!is_digit && !is_lower_hex) {
      return false;
    }
  }
  return true;
}

std::string ViewKindName(ViewKind kind) {
  switch (kind) {
    case ViewKind::kLiberty:
      return "Liberty";
    case ViewKind::kTechLef:
      return "tech LEF";
    case ViewKind::kCellLef:
      return "cell LEF";
    case ViewKind::kGds:
      return "GDS";
    case ViewKind::kCdl:
      return "CDL";
    case ViewKind::kSpice:
      return "SPICE";
    case ViewKind::kVerilogModel:
      return "Verilog model";
    case ViewKind::kDrcRunset:
      return "DRC runset";
    case ViewKind::kLvsRunset:
      return "LVS runset";
    case ViewKind::kAntennaRunset:
      return "antenna runset";
    case ViewKind::kDensityRunset:
      return "density runset";
    case ViewKind::kRcRules:
      return "RC rules";
  }
  return "unknown";
}

std::set<std::string> ToSet(const std::vector<std::string>& values) {
  return std::set<std::string>(values.begin(), values.end());
}

std::string JoinNames(const std::set<std::string>& values) {
  std::string joined;
  for (const std::string& value : values) {
    if (!joined.empty()) {
      joined += ", ";
    }
    joined += value;
  }
  return joined;
}

// The pin names present in one set but not the other, in both
// directions, phrased so the detail names the differing pins.
std::string DescribeDifference(const std::set<std::string>& left,
                               const std::set<std::string>& right,
                               const std::string& left_label,
                               const std::string& right_label) {
  std::set<std::string> only_left;
  std::set<std::string> only_right;
  std::set_difference(left.begin(), left.end(), right.begin(), right.end(),
                      std::inserter(only_left, only_left.begin()));
  std::set_difference(right.begin(), right.end(), left.begin(), left.end(),
                      std::inserter(only_right, only_right.begin()));
  std::string detail;
  if (!only_left.empty()) {
    detail += "only in " + left_label + ": " + JoinNames(only_left);
  }
  if (!only_right.empty()) {
    if (!detail.empty()) {
      detail += "; ";
    }
    detail += "only in " + right_label + ": " + JoinNames(only_right);
  }
  return detail;
}

// Indexes cells by name. The first entry for a name wins, so a
// duplicated cell definition cannot silently change the result
// depending on iteration order.
std::map<std::string, const CellPins*> IndexCells(
    const std::vector<CellPins>& cells) {
  std::map<std::string, const CellPins*> by_name;
  for (const CellPins& cell : cells) {
    if (by_name.find(cell.name) == by_name.end()) {
      by_name[cell.name] = &cell;
    }
  }
  return by_name;
}

void AddFinding(std::vector<Finding>* findings, const std::string& rule,
                const std::string& subject, const std::string& detail) {
  findings->push_back(Finding{rule, subject, detail});
}

}  // namespace

std::vector<Finding> CheckPackage(const TechnologyPackage& pkg) {
  std::vector<Finding> findings;
  const std::string package_subject = pkg.name.empty() ? "package" : pkg.name;

  // Package and collateral versioning and hashes.
  if (pkg.version.empty()) {
    AddFinding(&findings, "missing_version", package_subject,
               "package version is empty");
  }
  {
    std::map<std::string, int> id_counts;
    for (const CollateralItem& item : pkg.collateral) {
      ++id_counts[item.id];
    }
    for (const auto& entry : id_counts) {
      if (entry.second > 1) {
        AddFinding(&findings, "duplicate_collateral_id", entry.first,
                   "collateral id appears " + std::to_string(entry.second) +
                       " times");
      }
    }
  }
  for (const CollateralItem& item : pkg.collateral) {
    if (!item.required) {
      continue;
    }
    if (item.version.empty()) {
      AddFinding(&findings, "missing_version", item.id,
                 "required collateral item has an empty version");
    }
    if (!IsValidSha256(item.sha256)) {
      AddFinding(&findings, "missing_hash", item.id,
                 "required collateral item sha256 is not exactly 64 "
                 "lowercase hex characters");
    }
  }

  // Units.
  if (pkg.time_unit.empty()) {
    AddFinding(&findings, "unit_mismatch", package_subject,
               "time_unit is empty");
  }
  if (pkg.capacitance_unit.empty()) {
    AddFinding(&findings, "unit_mismatch", package_subject,
               "capacitance_unit is empty");
  }

  // Cell presence and pin agreement across views.
  const std::map<std::string, const CellPins*> liberty_by_name =
      IndexCells(pkg.liberty_cells);
  const std::map<std::string, const CellPins*> lef_by_name =
      IndexCells(pkg.lef_cells);
  const std::map<std::string, const CellPins*> logical_by_name =
      IndexCells(pkg.logical_cells);

  for (const auto& entry : liberty_by_name) {
    if (lef_by_name.find(entry.first) == lef_by_name.end()) {
      AddFinding(&findings, "cell_missing_in_lef", entry.first,
                 "cell is present in Liberty but has no LEF cell of the "
                 "same name");
    }
  }
  for (const auto& entry : lef_by_name) {
    if (liberty_by_name.find(entry.first) == liberty_by_name.end()) {
      AddFinding(&findings, "cell_missing_in_liberty", entry.first,
                 "cell is present in LEF but has no Liberty cell of the "
                 "same name");
    }
  }

  for (const auto& entry : liberty_by_name) {
    const CellPins& liberty_cell = *entry.second;
    const std::set<std::string> liberty_pins = ToSet(liberty_cell.pins);
    const std::set<std::string> liberty_pg = ToSet(liberty_cell.pg_pins);

    const auto lef_it = lef_by_name.find(entry.first);
    if (lef_it != lef_by_name.end()) {
      const CellPins& lef_cell = *lef_it->second;
      const std::set<std::string> lef_pins = ToSet(lef_cell.pins);
      if (liberty_pins != lef_pins) {
        AddFinding(&findings, "pin_mismatch", entry.first,
                   "signal pins differ between Liberty and LEF (" +
                       DescribeDifference(liberty_pins, lef_pins, "Liberty",
                                          "LEF") +
                       ")");
      }
      const std::set<std::string> lef_pg = ToSet(lef_cell.pg_pins);
      if (liberty_pg != lef_pg) {
        AddFinding(&findings, "pg_pin_mismatch", entry.first,
                   "power and ground pins differ between Liberty and LEF (" +
                       DescribeDifference(liberty_pg, lef_pg, "Liberty",
                                          "LEF") +
                       ")");
      }
    }

    const auto logical_it = logical_by_name.find(entry.first);
    if (logical_it != logical_by_name.end()) {
      const CellPins& logical_cell = *logical_it->second;
      const std::set<std::string> logical_pins = ToSet(logical_cell.pins);
      if (liberty_pins != logical_pins) {
        AddFinding(&findings, "pin_mismatch", entry.first,
                   "signal pins differ between Liberty and logical view (" +
                       DescribeDifference(liberty_pins, logical_pins,
                                          "Liberty", "logical") +
                       ")");
      }
    }
  }

  // Supply aliases: a canonical target must be a real supply,
  // never another alias, and never the alias itself.
  for (const auto& entry : pkg.supply_aliases) {
    const std::string& alias = entry.first;
    const std::string& canonical = entry.second;
    if (alias == canonical) {
      AddFinding(&findings, "supply_alias_ambiguous", alias,
                 "supply alias maps to itself");
    } else if (pkg.supply_aliases.find(canonical) !=
               pkg.supply_aliases.end()) {
      AddFinding(&findings, "supply_alias_ambiguous", alias,
                 "supply alias canonical target " + canonical +
                     " is itself an alias");
    }
  }

  // Macros: required views and black box contracts.
  const ViewKind kRequiredMacroViews[] = {ViewKind::kCellLef, ViewKind::kGds,
                                          ViewKind::kLiberty, ViewKind::kCdl};
  for (const MacroDef& macro : pkg.macros) {
    const std::set<ViewKind> views(macro.views.begin(), macro.views.end());
    for (const ViewKind required : kRequiredMacroViews) {
      if (views.find(required) == views.end()) {
        AddFinding(&findings, "macro_missing_view", macro.name,
                   "macro is missing required view " +
                       ViewKindName(required));
      }
    }
    if (macro.black_box && macro.abstraction_contract.empty()) {
      AddFinding(&findings, "black_box_without_contract", macro.name,
                 "black box macro has an empty abstraction contract");
    }
  }

  // Special cells: the named cell must exist in some view.
  {
    std::set<std::string> known_cells;
    for (const auto& entry : liberty_by_name) {
      known_cells.insert(entry.first);
    }
    for (const auto& entry : lef_by_name) {
      known_cells.insert(entry.first);
    }
    for (const auto& entry : logical_by_name) {
      known_cells.insert(entry.first);
    }
    for (const SpecialCell& special : pkg.special_cells) {
      if (known_cells.find(special.cell) == known_cells.end()) {
        AddFinding(&findings, "special_cell_unknown", special.cell,
                   "special cell for role " + special.role +
                       " is present in none of the Liberty, LEF or logical "
                       "views");
      }
    }
  }

  // Scenarios: the PVT corner must have Liberty collateral.
  {
    const std::set<std::string> corners(pkg.liberty_corners.begin(),
                                        pkg.liberty_corners.end());
    for (const PackageScenario& scenario : pkg.scenarios) {
      if (corners.find(scenario.pvt_corner) == corners.end()) {
        AddFinding(&findings, "scenario_unknown_corner", scenario.name,
                   "scenario PVT corner " + scenario.pvt_corner +
                       " has no Liberty collateral");
      }
    }
  }

  std::sort(findings.begin(), findings.end(),
            [](const Finding& left, const Finding& right) {
              if (left.rule != right.rule) {
                return left.rule < right.rule;
              }
              if (left.subject != right.subject) {
                return left.subject < right.subject;
              }
              return left.detail < right.detail;
            });
  return findings;
}

}  // namespace eos::tech
