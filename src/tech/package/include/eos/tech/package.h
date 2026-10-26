// package: technology package model and consistency checker
// (L2 design-data).
//
// A technology package is the versioned collection of collateral
// a process provides: timing libraries, physical views, netlist
// views, verification runsets and RC rules, together with the
// cell, macro, special-cell and scenario definitions that tie
// those views together. Tools and flows consume the package by
// name and version, so an inconsistency between views is not a
// minor defect: a pin that exists in Liberty but not in LEF, or
// a macro without a GDS view, only surfaces much later and much
// more expensively if the package itself is not checked.
//
// This module models the package as plain data and provides a
// single consistency checker, CheckPackage, that applies the
// bring-up rules to that data. The checker reports every problem
// it finds as a Finding naming the rule, the subject and the
// detail, rather than stopping at the first problem, so a package
// author can fix a whole class of issues in one pass. An empty
// result means the package is consistent under every rule.
//
// The model holds no file handles and performs no IO. Loading
// collateral from disk and parsing individual views belong to
// other modules; this module only judges whether the assembled
// description agrees with itself.
//
// This header is a stable surface.
#ifndef EOS_TECH_PACKAGE_H_
#define EOS_TECH_PACKAGE_H_

#include <map>
#include <string>
#include <vector>

namespace eos::tech {

// The kind of a collateral item or of a view a macro carries.
// Each kind names one distinct role in the flow, so a missing
// kind cannot be covered by a different file that happens to
// describe the same cells.
enum class ViewKind {
  kLiberty,
  kTechLef,
  kCellLef,
  kGds,
  kCdl,
  kSpice,
  kVerilogModel,
  kDrcRunset,
  kLvsRunset,
  kAntennaRunset,
  kDensityRunset,
  kRcRules,
};

// One piece of collateral in the package: a file the flow
// consumes, identified by a stable id within the package.
//
// version is the immutable version identifier of the item as
// supplied by its source, and sha256 is the lowercase hex
// SHA-256 of its contents. Both are required on required items
// so a consumer can tell exactly which revision it ran against
// and can detect a silent change of contents.
struct CollateralItem {
  std::string id;
  ViewKind kind = ViewKind::kLiberty;
  std::string path;
  std::string version;
  std::string sha256;
  bool required = false;
};

// One cell as seen in one view: its signal pins and its power
// and ground pins, kept separate because the rules treat them
// separately. The same cell name appearing in the Liberty, LEF
// and logical views denotes the same cell, and the checker
// requires the pin sets of those appearances to agree.
//
// pins holds signal pin names only. pg_pins holds power and
// ground pin names. Order does not carry meaning; the checker
// compares both as sets.
struct CellPins {
  std::string name;
  std::vector<std::string> pins;
  std::vector<std::string> pg_pins;
};

// A macro definition: a large or externally supplied block the
// package carries, together with the views provided for it.
//
// A macro must carry its required views (cell LEF, GDS, Liberty
// and CDL) whether or not it is a black box. A black box is only
// acceptable under a documented abstraction contract, recorded
// in abstraction_contract, that states what a consumer may
// assume in place of the missing internal detail.
struct MacroDef {
  std::string name;
  std::vector<ViewKind> views;
  bool black_box = false;
  std::string abstraction_contract;
};

// A special cell binding: the role the flow needs filled and
// the cell that fills it. Roles used by the package are
// tie_high, tie_low, tap, endcap, filler, decap, antenna,
// cts_buffer and cts_inverter. The named cell must exist in at
// least one of the package cell views, otherwise a flow stage
// that reaches for the role would find nothing to place.
struct SpecialCell {
  std::string role;
  std::string cell;
};

// A package scenario: a named pairing of a PVT corner with an
// RC corner. The PVT corner must be one the package actually
// supplies Liberty collateral for, recorded in
// TechnologyPackage::liberty_corners, otherwise the scenario
// names an analysis the package cannot support.
struct PackageScenario {
  std::string name;
  std::string pvt_corner;
  std::string rc_corner;
};

// The complete technology package description the checker
// judges.
//
// name and version identify the package itself. time_unit and
// capacitance_unit state the units the package collateral is
// expressed in, and both must be present so consumers never
// have to guess a unit from context.
//
// liberty_cells, lef_cells and logical_cells are the same cell
// population as seen in the Liberty view, the LEF view and the
// logical (Verilog model) view respectively. liberty_corners
// lists the PVT corners that have a Liberty collateral item.
// supply_aliases maps an alias supply name to its canonical
// supply name; a canonical name must be a real supply, never
// another alias, so resolution always terminates in one step.
struct TechnologyPackage {
  std::string name;
  std::string version;
  std::string time_unit;
  std::string capacitance_unit;
  std::vector<CollateralItem> collateral;
  std::vector<CellPins> liberty_cells;
  std::vector<CellPins> lef_cells;
  std::vector<CellPins> logical_cells;
  std::vector<MacroDef> macros;
  std::vector<SpecialCell> special_cells;
  std::vector<PackageScenario> scenarios;
  std::vector<std::string> liberty_corners;
  std::map<std::string, std::string> supply_aliases;
};

// One consistency problem found by CheckPackage.
//
// rule is the stable machine-readable name of the rule that
// fired, for example "pin_mismatch". subject names the package
// element the finding is about, such as a cell, a macro or a
// collateral item. detail is a human-readable explanation that
// names the concrete differing values where a rule compares
// sets, so the author can fix the package without rerunning
// the comparison by hand.
struct Finding {
  std::string rule;
  std::string subject;
  std::string detail;
};

// Checks a technology package for internal consistency and
// returns every finding, sorted by rule and then by subject
// so the output is deterministic for a given package. An
// empty result means the package passed every rule.
//
// The rules, by their Finding::rule names, are:
//   missing_version: the package version is empty, or a
//     required collateral item has an empty version.
//   missing_hash: a required collateral item whose sha256 is
//     not exactly 64 lowercase hex characters.
//   duplicate_collateral_id: the same collateral id appears
//     more than once.
//   cell_missing_in_lef: a Liberty cell with no LEF cell of
//     the same name.
//   cell_missing_in_liberty: a LEF cell with no Liberty cell
//     of the same name.
//   pin_mismatch: a cell whose signal pin set differs between
//     Liberty and LEF, or between Liberty and the logical view
//     when a logical entry for the cell exists.
//   pg_pin_mismatch: a cell whose power and ground pin set
//     differs between Liberty and LEF.
//   supply_alias_ambiguous: a supply alias whose canonical
//     target is itself an alias, or an alias equal to its
//     canonical target.
//   unit_mismatch: the package time unit or capacitance unit
//     is empty.
//   macro_missing_view: a macro missing one of its required
//     views (cell LEF, GDS, Liberty or CDL).
//   black_box_without_contract: a black box macro with an
//     empty abstraction contract.
//   special_cell_unknown: a special cell naming a cell that
//     appears in none of the Liberty, LEF or logical views.
//   scenario_unknown_corner: a scenario whose PVT corner is
//     not among the corners with Liberty collateral.
std::vector<Finding> CheckPackage(const TechnologyPackage& pkg);

}  // namespace eos::tech

#endif  // EOS_TECH_PACKAGE_H_
