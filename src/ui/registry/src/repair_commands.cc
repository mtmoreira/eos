// repair_commands: eos::repair_timing and eos::set_wire_rc (R1 step 2, Q4/Q6).
//
// repair_timing is a self-contained per-call repair session: it builds
// its own dbSta session from explicit file options, mirroring the PR3
// C++ driver sequence (lefin tlef+lef, defin, Sta_Init, evalTclInit,
// readDbAfter, read_liberty, read_sdc), because the timing session has
// no LEF/DEF load path (import_odb is binary .odb only) and the
// adapted suite fixtures are DEF/LEF/LIB. It does not depend on
// ctx.timing and leaves no session state behind: every call creates
// and destroys its own database, STA session, and Tcl interp.
//
// Wire RC policy (Q6, decided and documented in src/pd/rsz/README.md):
// the repair scalar is selected per call, in this order:
//   1. explicit -wire_r/-wire_c override (both required together);
//   2. explicit -wire_layer (or an eos::set_wire_rc override for that
//      layer, which wins over the tech-derived value for the layer);
//   3. tech default layer: the lowest-numbered routing layer with
//      complete tech data, excluding li1-class local-interconnect
//      layers when a metal layer exists.
// The per-layer table is derived and reported, but the repair itself
// consumes one scalar per scene via setWireSignalRC: the engine's
// layerRC/setLayerRC storage is never read by any repair pass.
// Clock RC defaults to the signal scalar (upstream no-flag
// set_wire_rc sets both); -wire_layer_clk overrides the clock side
// only (setup2: signal metal3, clock metal5). The fail-fast backstop
// fires at repair entry when no signal RC source resolves: never 0.0,
// never NaN, never a silent substitution.
//
// Units in options: lengths in meters, times in ns, RC in ohm/m and
// F/m, percents as given (converted /100 internally, matching the
// upstream Tcl conversion). Upstream scripts use microns for wire
// length and ns for slack margin; adapted scripts convert and document
// the conversion in their headers.
//
// Pass order in one call is fixed: design, setup, hold, tie,
// clk_nets, clk_inverters; only requested passes run. With no pass
// flag the default is setup+hold, exactly upstream repair_timing.
// Limit errors (RSZ-0050 max-utilization, RSZ-0060 max-buffer-count)
// are thrown by the engine as std::runtime_error; the command records
// them as utilization_limited/buffer_limited flags, stops further
// passes, and still returns Ok with post-repair metrics, mirroring
// the upstream catch-and-continue scripts.
#include "eos/ui/registry.h"

#include <tcl.h>

#include "db_sta/dbNetwork.hh"
#include "db_sta/dbSta.hh"
#include "odb/db.h"
#include "odb/defin.h"
#include "odb/lefin.h"
#include "rsz/Resizer.hh"
#include "sta/Liberty.hh"
#include "sta/MinMax.hh"
#include "sta/PatternMatch.hh"
#include "sta/StaMain.hh"
#include "utl/Logger.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

extern "C" {
extern int Sta_Init(Tcl_Interp *interp);
}

namespace sta {
extern const char *tcl_inits[];
}

namespace eos::ui {
namespace {

// ---- small helpers (registry-local; core_commands.cc has its own copies) ----

CommandResult FailConfig(const std::string &message) {
  return CommandResult::Fail(Diag::kInvalidConfiguration, message);
}

CommandResult FailIo(const std::string &message) {
  return CommandResult::Fail(Diag::kIoError, message);
}

bool Readable(const std::string &path) {
  std::ifstream in(path);
  return in.good();
}

bool ParseDouble(const std::string &text, double &out) {
  try {
    std::size_t used = 0;
    out = std::stod(text, &used);
    return used == text.size() && std::isfinite(out);
  } catch (...) {
    return false;
  }
}

std::string Fmt(double value, int precision = 6) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
}

std::string FmtRaw(double value) {
  std::ostringstream out;
  out << std::setprecision(9) << std::defaultfloat << value;
  return out.str();
}

// ---- wire-RC derivation from tech data (command layer, not in rsz) ----

struct LayerRc {
  std::string name;
  int routing_level = 0;
  double r_ohm_per_m = 0.0;
  double c_f_per_m = 0.0;
  bool complete = false;
};

// Derivation, verified against sky130 met2: R = RPERSQ / width,
// C = (CPERSQDIST * width + 2 * EDGECAPACITANCE), with odb units
// (ohm/sq, pF/um2, pF/um) converted to ohm/m and F/m. A layer is
// complete only when width, resistance, and capacitance data are all
// present and positive; an incomplete layer can never be selected.
std::vector<LayerRc> DeriveLayerRc(odb::dbTech *tech) {
  std::vector<LayerRc> layers;
  if (tech == nullptr) return layers;
  const double dbu_per_micron = tech->getDbUnitsPerMicron();
  if (dbu_per_micron <= 0) return layers;
  for (odb::dbTechLayer *layer : tech->getLayers()) {
    if (layer->getType() != odb::dbTechLayerType::ROUTING) continue;
    LayerRc rc;
    rc.name = layer->getName();
    rc.routing_level = layer->getRoutingLevel();
    const double width_um = layer->getWidth() / dbu_per_micron;
    const double rpersq = layer->getResistance();
    const double cpersq = layer->getCapacitance();
    const double edgecap = layer->getEdgeCapacitance();
    if (width_um > 0 && rpersq > 0 && cpersq >= 0 && edgecap >= 0) {
      rc.r_ohm_per_m = rpersq / (width_um * 1e-6);
      rc.c_f_per_m = (cpersq * width_um + 2.0 * edgecap) * 1e-6;
      rc.complete = rc.r_ohm_per_m > 0 && rc.c_f_per_m > 0;
    }
    layers.push_back(rc);
  }
  std::sort(layers.begin(), layers.end(),
            [](const LayerRc &a, const LayerRc &b) {
              return a.routing_level < b.routing_level;
            });
  return layers;
}

bool IsLocalInterconnect(const std::string &name) {
  std::string lower;
  for (char c : name) lower += static_cast<char>(std::tolower(c));
  return lower.rfind("li", 0) == 0;
}

// Default layer rule: lowest-numbered complete routing layer,
// excluding li1-class local-interconnect layers when any metal
// (non-li) complete layer exists. Returns nullopt when no complete
// layer exists.
std::optional<LayerRc> DefaultLayer(const std::vector<LayerRc> &layers) {
  const LayerRc *fallback = nullptr;
  for (const LayerRc &rc : layers) {
    if (!rc.complete) continue;
    if (fallback == nullptr) fallback = &rc;
    if (!IsLocalInterconnect(rc.name)) return rc;
  }
  if (fallback != nullptr) return *fallback;
  return std::nullopt;
}

const LayerRc *FindLayer(const std::vector<LayerRc> &layers,
                         const std::string &name) {
  for (const LayerRc &rc : layers) {
    if (rc.name == name) return &rc;
  }
  return nullptr;
}

// ---- SITE recreation (general loader step, not sky130-specific) ----
// lefin drops tech-file SITE definitions when no library is being
// created, so a DEF with ROW statements naming such a site fails with
// ODB-0155. Parse SITE blocks from the tech file and create any site
// missing from the tech before DEF read. SIZE is in microns.
struct SiteDef {
  std::string name;
  std::string symmetry;
  std::string klass;
  double width_um = 0.0;
  double height_um = 0.0;
};

std::vector<SiteDef> ParseSites(const std::string &tech_path) {
  std::ifstream in(tech_path);
  std::vector<SiteDef> sites;
  if (!in.good()) return sites;
  std::string line;
  bool in_site = false;
  SiteDef cur;
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    std::string tok;
    ss >> tok;
    if (!in_site) {
      if (tok == "SITE") {
        cur = SiteDef();
        ss >> cur.name;
        in_site = true;
      }
      continue;
    }
    if (tok == "END") {
      if (!cur.name.empty() && cur.width_um > 0 && cur.height_um > 0)
        sites.push_back(cur);
      in_site = false;
      continue;
    }
    if (tok == "SYMMETRY") {
      ss >> cur.symmetry;
    } else if (tok == "CLASS") {
      ss >> cur.klass;
    } else if (tok == "SIZE") {
      std::string by;
      ss >> cur.width_um >> by >> cur.height_um;
    }
  }
  return sites;
}

}  // namespace
}  // namespace eos::ui

namespace eos::ui {
namespace {

struct RepairOptions {
  std::optional<std::string> tlef;
  std::vector<std::string> lefs;
  std::optional<std::string> def;
  std::vector<std::string> liberties;
  std::optional<std::string> sdc;
  std::optional<std::string> clock;
  std::optional<double> period_ns;
  bool setup = false;
  bool hold = false;
  bool design = false;
  bool tie = false;
  bool clk_nets = false;
  bool clk_inverters = false;
  std::optional<double> slack_margin_ns;
  std::optional<double> max_buffer_percent;
  bool allow_setup_violations = false;
  std::optional<double> max_utilization;
  std::optional<double> max_wire_length_m;
  std::optional<double> max_slew_margin;
  std::optional<double> max_cap_margin;
  std::vector<std::string> tie_ports;
  std::optional<double> separation_m;
  std::optional<std::string> end_pin;
  std::optional<std::string> buffer_cell;
  std::optional<std::string> wire_layer;
  std::optional<std::string> wire_layer_clk;
  std::optional<double> wire_r;
  std::optional<double> wire_c;
  std::vector<std::string> clock_nets;
  std::vector<std::string> dont_use;
};

// Parses -key value pairs and bare flags. Unknown keys, missing
// values, and non-numeric numerics fail fast here, never later.
std::optional<CommandResult> ParseRepairArgs(
    const std::vector<std::string> &args, RepairOptions &o) {
  static const std::string kUsage =
      "usage: eos::repair_timing -def <def> -liberty <lib> "
      "[-tlef <tlef>] [-lef <lef>]... [-sdc <sdc>] "
      "[-clock <name> -period <ns>] "
      "[-setup] [-hold] [-design] [-tie] [-clk_nets] [-clk_inverters] "
      "[-slack_margin <ns>] [-max_buffer_percent <pct>] "
      "[-allow_setup_violations] [-max_utilization <pct>] "
      "[-max_wire_length <m>] [-max_slew_margin <v>] "
      "[-max_cap_margin <v>] [-tie_port <cell/pin>]... "
      "[-separation <m>] [-end_pin <inst/pin>] [-buffer_cell <cell>] "
      "[-wire_layer <layer>] [-wire_layer_clk <layer>] "
      "[-wire_r <ohm/m> -wire_c <F/m>] [-clock_net <net>]... "
      "[-dont_use <glob>]...";
  auto need_value = [&](std::size_t i, const std::string &key)
      -> std::optional<CommandResult> {
    if (i + 1 >= args.size())
      return FailConfig("missing value for " + key + " (" + kUsage + ")");
    return std::nullopt;
  };
  auto need_double = [&](const std::string &text, const std::string &key,
                         double &out) -> std::optional<CommandResult> {
    if (!ParseDouble(text, out))
      return FailConfig("bad numeric value for " + key + ": " + text);
    return std::nullopt;
  };
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string &a = args[i];
    if (a == "-tlef" || a == "-def" || a == "-sdc" || a == "-clock" ||
        a == "-period" || a == "-slack_margin" || a == "-max_buffer_percent" ||
        a == "-max_utilization" || a == "-max_wire_length" ||
        a == "-max_slew_margin" || a == "-max_cap_margin" ||
        a == "-separation" || a == "-end_pin" || a == "-buffer_cell" ||
        a == "-wire_layer" || a == "-wire_layer_clk" || a == "-wire_r" ||
        a == "-wire_c") {
      if (auto bad = need_value(i, a)) return bad;
      const std::string &v = args[++i];
      if (a == "-tlef") o.tlef = v;
      else if (a == "-def") o.def = v;
      else if (a == "-sdc") o.sdc = v;
      else if (a == "-clock") o.clock = v;
      else if (a == "-end_pin") o.end_pin = v;
      else if (a == "-buffer_cell") o.buffer_cell = v;
      else if (a == "-wire_layer") o.wire_layer = v;
      else if (a == "-wire_layer_clk") o.wire_layer_clk = v;
      else {
        double d = 0.0;
        if (auto bad = need_double(v, a, d)) return bad;
        if (a == "-period") o.period_ns = d;
        else if (a == "-slack_margin") o.slack_margin_ns = d;
        else if (a == "-max_buffer_percent") o.max_buffer_percent = d;
        else if (a == "-max_utilization") o.max_utilization = d;
        else if (a == "-max_wire_length") o.max_wire_length_m = d;
        else if (a == "-max_slew_margin") o.max_slew_margin = d;
        else if (a == "-max_cap_margin") o.max_cap_margin = d;
        else if (a == "-separation") o.separation_m = d;
        else if (a == "-wire_r") o.wire_r = d;
        else if (a == "-wire_c") o.wire_c = d;
      }
    } else if (a == "-lef" || a == "-liberty" || a == "-tie_port" ||
               a == "-clock_net" || a == "-dont_use") {
      if (auto bad = need_value(i, a)) return bad;
      const std::string &v = args[++i];
      if (a == "-lef") o.lefs.push_back(v);
      else if (a == "-liberty") o.liberties.push_back(v);
      else if (a == "-tie_port") o.tie_ports.push_back(v);
      else if (a == "-clock_net") o.clock_nets.push_back(v);
      else o.dont_use.push_back(v);
    } else if (a == "-setup") o.setup = true;
    else if (a == "-hold") o.hold = true;
    else if (a == "-design") o.design = true;
    else if (a == "-tie") o.tie = true;
    else if (a == "-clk_nets") o.clk_nets = true;
    else if (a == "-clk_inverters") o.clk_inverters = true;
    else if (a == "-allow_setup_violations") o.allow_setup_violations = true;
    else return FailConfig("unknown option: " + a + " (" + kUsage + ")");
  }
  return std::nullopt;
}

// Cross-option validation: required inputs, pass-specific options
// without their pass, and ambiguous combinations. Runs before any
// file is opened, so a bad call fails without side effects.
std::optional<CommandResult> ValidateRepairOptions(const RepairOptions &o) {
  if (!o.def.has_value())
    return FailConfig("missing required option -def");
  if (o.liberties.empty())
    return FailConfig("missing required option -liberty");
  if (o.lefs.empty())
    return FailConfig("missing required option -lef");
  if (!Readable(*o.def)) return FailIo("could not read file: " + *o.def);
  if (o.tlef.has_value() && !Readable(*o.tlef))
    return FailIo("could not read file: " + *o.tlef);
  for (const std::string &p : o.lefs)
    if (!Readable(p)) return FailIo("could not read file: " + p);
  for (const std::string &p : o.liberties)
    if (!Readable(p)) return FailIo("could not read file: " + p);
  if (o.sdc.has_value() && !Readable(*o.sdc))
    return FailIo("could not read file: " + *o.sdc);
  if (o.sdc.has_value() && (o.clock.has_value() || o.period_ns.has_value()))
    return FailConfig("-sdc and -clock/-period together are ambiguous: "
                      "give the clock in one place only");
  if (o.clock.has_value() != o.period_ns.has_value())
    return FailConfig("-clock and -period must be given together");
  if (o.period_ns.has_value() && *o.period_ns <= 0)
    return FailConfig("-period must be positive");
  if (o.slack_margin_ns.has_value() && *o.slack_margin_ns < 0)
    return FailConfig("-slack_margin must be non-negative");
  if (o.max_buffer_percent.has_value() &&
      (*o.max_buffer_percent < 0 || *o.max_buffer_percent > 100))
    return FailConfig("-max_buffer_percent must be between 0 and 100");
  if (o.max_utilization.has_value() &&
      (*o.max_utilization < 0 || *o.max_utilization > 100))
    return FailConfig("-max_utilization must be between 0 and 100");
  if (o.max_wire_length_m.has_value() && *o.max_wire_length_m < 0)
    return FailConfig("-max_wire_length must be non-negative");
  if (o.max_slew_margin.has_value() && *o.max_slew_margin < 0)
    return FailConfig("-max_slew_margin must be non-negative");
  if (o.max_cap_margin.has_value() && *o.max_cap_margin < 0)
    return FailConfig("-max_cap_margin must be non-negative");
  if (o.separation_m.has_value() && *o.separation_m < 0)
    return FailConfig("-separation must be non-negative");
  const bool any_pass = o.design || o.setup || o.hold || o.tie ||
                        o.clk_nets || o.clk_inverters;
  if (o.max_wire_length_m.has_value() && !o.design && !o.clk_nets)
    return FailConfig("-max_wire_length requires -design or -clk_nets");
  if ((o.max_slew_margin.has_value() || o.max_cap_margin.has_value()) &&
      !o.design)
    return FailConfig("-max_slew_margin/-max_cap_margin require -design");
  if ((!o.tie_ports.empty() || o.separation_m.has_value()) && !o.tie)
    return FailConfig("-tie_port/-separation require -tie");
  if (o.tie && o.tie_ports.empty())
    return FailConfig("-tie requires at least one -tie_port");
  if ((o.end_pin.has_value() || o.buffer_cell.has_value()) && !o.hold)
    return FailConfig("-end_pin/-buffer_cell require -hold");
  if (o.end_pin.has_value() != o.buffer_cell.has_value())
    return FailConfig("-end_pin and -buffer_cell must be given together "
                      "(pin-level hold repair)");
  if (o.slack_margin_ns.has_value() && !o.setup && !o.hold)
    return FailConfig("-slack_margin requires -setup or -hold");
  if (o.allow_setup_violations && !o.hold)
    return FailConfig("-allow_setup_violations requires -hold");
  if (o.max_buffer_percent.has_value() && !o.hold)
    return FailConfig("-max_buffer_percent requires -hold");
  if ((o.wire_r.has_value() || o.wire_c.has_value()) &&
      !(o.wire_r.has_value() && o.wire_c.has_value()))
    return FailConfig("-wire_r and -wire_c must be given together");
  if (o.wire_r.has_value() &&
      (*o.wire_r <= 0 || *o.wire_c <= 0))
    return FailConfig("-wire_r/-wire_c must be positive (never 0.0)");
  (void)any_pass;
  return std::nullopt;
}

}  // namespace
}  // namespace eos::ui

namespace eos::ui {
namespace {

// ---- session run ----

using Snap = std::map<std::string, std::string>;

Snap SnapshotBlock(odb::dbBlock *block) {
  Snap s;
  if (block == nullptr) return s;
  for (odb::dbInst *inst : block->getInsts()) {
    odb::dbMaster *master = inst->getMaster();
    s[inst->getName()] = master ? master->getName() : std::string("?");
  }
  return s;
}

struct SnapDiff {
  std::size_t added = 0;
  std::size_t removed = 0;
  std::size_t resized = 0;
};

SnapDiff DiffSnaps(const Snap &before, const Snap &after) {
  SnapDiff d;
  for (const auto &kv : after) {
    auto it = before.find(kv.first);
    if (it == before.end()) d.added += 1;
    else if (it->second != kv.second) d.resized += 1;
  }
  for (const auto &kv : before) {
    if (after.find(kv.first) == after.end()) d.removed += 1;
  }
  return d;
}

struct TimingMetrics {
  double wns_ns = 0.0;
  double tns_ns = 0.0;
  double hold_ns = 0.0;
  double tns_hold_ns = 0.0;
};

TimingMetrics Measure(sta::dbSta *sta) {
  TimingMetrics m;
  m.wns_ns = sta->worstSlack(sta::MinMax::max()) * 1e9;
  m.tns_ns = sta->totalNegativeSlack(sta::MinMax::max()) * 1e9;
  m.hold_ns = sta->worstSlack(sta::MinMax::min()) * 1e9;
  m.tns_hold_ns = sta->totalNegativeSlack(sta::MinMax::min()) * 1e9;
  return m;
}

std::optional<std::string> TclEval(Tcl_Interp *interp,
                                   const std::string &cmd) {
  if (Tcl_Eval(interp, cmd.c_str()) != TCL_OK) {
    const char *result = Tcl_GetStringResult(interp);
    return std::string(result ? result : "tcl eval failed");
  }
  return std::nullopt;
}

std::string TclQuote(const std::string &path) {
  return "{" + path + "}";
}

// Splits "inst/pin" at the last '/'. Returns false when there is no '/'.
bool SplitInstPin(const std::string &text, std::string &inst,
                  std::string &pin) {
  const std::size_t slash = text.rfind('/');
  if (slash == std::string::npos || slash == 0 ||
      slash + 1 >= text.size())
    return false;
  inst = text.substr(0, slash);
  pin = text.substr(slash + 1);
  return true;
}

// Splits "cell/pin" the same way (liberty cell names contain no '/').
bool SplitCellPin(const std::string &text, std::string &cell,
                  std::string &pin) {
  return SplitInstPin(text, cell, pin);
}

CommandResult RunRepair(CommandContext &ctx, const RepairOptions &o) {
  utl::Logger logger;
  odb::dbDatabase *db = odb::dbDatabase::create();
  db->setLogger(&logger);
  Tcl_Interp *interp = nullptr;
  sta::dbSta *sta = nullptr;
  // RAII-ish cleanup on every exit path below (early returns included).
  struct Guard {
    odb::dbDatabase *db;
    Tcl_Interp *interp;
    sta::dbSta *sta;
    ~Guard() {
      delete sta;
      if (interp != nullptr) Tcl_DeleteInterp(interp);
      if (db != nullptr) odb::dbDatabase::destroy(db);
    }
  };
  // The guard is armed after each resource is created; pointers are
  // updated as construction proceeds.
  Guard guard{db, nullptr, nullptr};

  odb::dbTech *tech = nullptr;
  std::vector<odb::dbLib *> libs;
  {
    odb::lefin lef_reader(db, &logger, false);
    if (o.tlef.has_value()) {
      tech = lef_reader.createTech("eos_tech", o.tlef->c_str());
      if (tech == nullptr)
        return FailIo("createTech failed for: " + *o.tlef);
      int n = 0;
      for (const std::string &lef : o.lefs) {
        odb::dbLib *lib = lef_reader.createLib(
            tech, ("eos_lib_" + std::to_string(n++)).c_str(), lef.c_str());
        if (lib == nullptr) return FailIo("createLib failed for: " + lef);
        libs.push_back(lib);
      }
    } else {
      // First LEF carries tech+lib (Nangate45's combined file); the
      // rest are cell libraries on the same tech.
      odb::dbLib *first = lef_reader.createTechAndLib(
          "eos_tech", "eos_lib_0", o.lefs[0].c_str());
      if (first == nullptr)
        return FailIo("createTechAndLib failed for: " + o.lefs[0]);
      tech = db->getTech();
      if (tech == nullptr) return FailIo("no tech after createTechAndLib");
      libs.push_back(first);
      int n = 1;
      for (std::size_t i = 1; i < o.lefs.size(); ++i) {
        odb::dbLib *lib = lef_reader.createLib(
            tech, ("eos_lib_" + std::to_string(n++)).c_str(),
            o.lefs[i].c_str());
        if (lib == nullptr) return FailIo("createLib failed for: " + o.lefs[i]);
        libs.push_back(lib);
      }
    }
    // Recreate tech-file SITE definitions lefin drops, before DEF read.
    const std::string tech_path =
        o.tlef.has_value() ? *o.tlef : o.lefs[0];
    const double dbu_per_micron = tech->getDbUnitsPerMicron();
    if (dbu_per_micron > 0 && !libs.empty()) {
      for (const SiteDef &site : ParseSites(tech_path)) {
        if (libs[0]->findSite(site.name.c_str()) != nullptr) continue;
        odb::dbSite *s = odb::dbSite::create(libs[0], site.name.c_str());
        s->setWidth(static_cast<uint>(site.width_um * dbu_per_micron));
        s->setHeight(static_cast<uint>(site.height_um * dbu_per_micron));
        if (site.symmetry == "Y") s->setSymmetryY();
        else if (site.symmetry == "X") s->setSymmetryX();
        else if (site.symmetry == "R90") s->setSymmetryR90();
        if (site.klass == "CORE") s->setClass(odb::dbSiteClass::CORE);
      }
    }
    odb::defin def_reader(db, &logger);
    odb::dbChip *chip =
        def_reader.createChip(libs, o.def->c_str(), tech);
    if (chip == nullptr)
      return FailIo("createChip failed for: " + *o.def);
  }
  odb::dbBlock *block =
      (db->getChip() == nullptr) ? nullptr : db->getChip()->getBlock();
  if (block == nullptr) return FailIo("DEF produced no block");

  interp = Tcl_CreateInterp();
  guard.interp = interp;
  if (Tcl_Init(interp) != TCL_OK)
    return FailIo("Tcl_Init failed");
  sta = new sta::dbSta;
  guard.sta = sta;
  sta->init(interp, db, &logger);
  if (Sta_Init(interp) != TCL_OK)
    return FailIo("Sta_Init failed");
  sta::evalTclInit(interp, sta::tcl_inits);
  if (auto err = TclEval(interp, "init_sta_cmds"))
    return FailIo("init_sta_cmds failed: " + *err);
  sta->getDbNetwork()->readDbAfter(db);
  for (const std::string &lib : o.liberties) {
    if (auto err = TclEval(interp, "read_liberty " + TclQuote(lib)))
      return FailIo("read_liberty failed for " + lib + ": " + *err);
  }
  if (o.sdc.has_value()) {
    if (auto err = TclEval(interp, "read_sdc " + TclQuote(*o.sdc)))
      return FailIo("read_sdc failed for " + *o.sdc + ": " + *err);
  } else if (o.clock.has_value()) {
    std::ostringstream cmd;
    cmd << "create_clock -name " << *o.clock << " -period "
        << FmtRaw(*o.period_ns) << " " << *o.clock;
    if (auto err = TclEval(interp, cmd.str()))
      return FailIo("create_clock failed: " + *err);
  }

  // ---- wire-RC resolution (fail-fast backstop lives here) ----
  std::vector<LayerRc> layers = DeriveLayerRc(tech);
  auto lookup_layer = [&](const std::optional<std::string> &name)
      -> std::optional<std::pair<double, double>> {
    if (!name.has_value()) return std::nullopt;
    auto it = ctx.wire_rc_overrides.find(*name);
    if (it != ctx.wire_rc_overrides.end()) return it->second;
    const LayerRc *rc = FindLayer(layers, *name);
    if (rc == nullptr || !rc->complete) return std::nullopt;
    return std::make_pair(rc->r_ohm_per_m, rc->c_f_per_m);
  };
  std::string wire_layer_name;
  std::string wire_rc_source;
  double wire_r = 0.0, wire_c = 0.0;
  if (o.wire_r.has_value()) {
    wire_r = *o.wire_r;
    wire_c = *o.wire_c;
    wire_rc_source = "override";
    wire_layer_name = o.wire_layer.value_or("");
  } else if (o.wire_layer.has_value()) {
    auto rc = lookup_layer(o.wire_layer);
    if (!rc.has_value())
      return FailConfig(
          "no wire RC data for layer " + *o.wire_layer +
          " (load tech data for it, pass -wire_layer naming a layer "
          "with data, or pass -wire_r/-wire_c)");
    wire_r = rc->first;
    wire_c = rc->second;
    wire_layer_name = *o.wire_layer;
    wire_rc_source =
        ctx.wire_rc_overrides.count(*o.wire_layer) ? "wire_layer"
                                                   : "tech_default";
  } else {
    auto dflt = DefaultLayer(layers);
    if (dflt.has_value()) {
      auto it = ctx.wire_rc_overrides.find(dflt->name);
      if (it != ctx.wire_rc_overrides.end()) {
        wire_r = it->second.first;
        wire_c = it->second.second;
        wire_rc_source = "wire_layer";
      } else {
        wire_r = dflt->r_ohm_per_m;
        wire_c = dflt->c_f_per_m;
        wire_rc_source = "tech_default";
      }
      wire_layer_name = dflt->name;
    } else {
      return FailConfig(
          "no wire RC source: no tech layer carries complete resistance "
          "and capacitance data (load tech data, or pass -wire_layer, "
          "or pass -wire_r/-wire_c)");
    }
  }
  // Clock RC rule (upstream no-flag behavior): a bare set_wire_rc
  // sets BOTH signal and clock RC. So the clock scalar defaults to
  // the signal scalar, and -wire_layer_clk only overrides the clock
  // side (setup2: signal metal3, clock metal5). Without this, clock
  // path parasitics stay zero and repair choices diverge from
  // upstream (setup1: upstream resizes, EOS-only-signal rebuffers).
  std::string wire_layer_clk_name;
  std::string wire_clk_source;
  double wire_clk_r = wire_r, wire_clk_c = wire_c;
  bool have_clk_rc = true;
  if (o.wire_layer_clk.has_value()) {
    auto rc = lookup_layer(o.wire_layer_clk);
    if (!rc.has_value())
      return FailConfig(
          "no wire RC data for clock layer " + *o.wire_layer_clk +
          " (load tech data for it, pass -wire_layer_clk naming a "
          "layer with data, or set it with eos::set_wire_rc)");
    wire_clk_r = rc->first;
    wire_clk_c = rc->second;
    wire_layer_clk_name = *o.wire_layer_clk;
    wire_clk_source =
        ctx.wire_rc_overrides.count(*o.wire_layer_clk) ? "wire_layer"
                                                       : "tech_default";
  } else if (o.wire_layer.has_value()) {
    wire_layer_clk_name = *o.wire_layer;
    wire_clk_source = wire_rc_source;
  } else if (o.wire_r.has_value()) {
    wire_clk_source = "override";
  } else {
    wire_layer_clk_name = wire_layer_name;
    wire_clk_source = wire_rc_source;
  }

  // ---- engine setup ----
  rsz::Resizer resizer;
  resizer.init(nullptr, &logger, db, sta, 0.3f);
  const sta::Scene *scene = sta->cmdScene();
  resizer.setWireSignalRC(scene, wire_r, wire_c);
  if (have_clk_rc) resizer.setWireClkRC(scene, wire_clk_r, wire_clk_c);
  if (o.max_utilization.has_value())
    resizer.setMaxUtilization(*o.max_utilization / 100.0);
  if (!o.dont_use.empty()) {
    // Upstream glob semantics via STA's own PatternMatch (unix glob),
    // the same matcher upstream set_dont_use expands through. A
    // pattern matching nothing contributes nothing; that is not an
    // error, matching upstream behavior.
    sta::LibertyCellSeq *seq = new sta::LibertyCellSeq;
    sta::dbNetwork *network = sta->getDbNetwork();
    for (const std::string &pattern : o.dont_use) {
      sta::PatternMatch matcher(pattern);
      sta::LibertyLibraryIterator *lib_iter = network->libertyLibraryIterator();
      while (lib_iter->hasNext()) {
        sta::LibertyLibrary *lib = lib_iter->next();
        sta::LibertyCellSeq cells = lib->findLibertyCellsMatching(&matcher);
        for (sta::LibertyCell *cell : cells) seq->push_back(cell);
      }
      delete lib_iter;
    }
    resizer.setDontUse(seq);
  }
  for (const std::string &net_name : o.clock_nets) {
    odb::dbNet *net = block->findNet(net_name.c_str());
    if (net == nullptr)
      return FailConfig("clock net not found: " + net_name);
    net->setSigType(odb::dbSigType::CLOCK);
  }

  sta->ensureGraph();
  // Upstream driver order: estimate_parasitics -placement BEFORE
  // resizer_preamble and before measuring start. Without the
  // estimate, violations visible only with wire parasitics
  // (setup1: +0.047 ns pre-parasitic, -0.239 ns with parasitics)
  // are invisible to the repair search, which then repairs nothing.
  // Wire RC is set above, so estimation runs (it is skipped when
  // wire RC is unset).
  resizer.estimateWireParasitics();
  TimingMetrics start = Measure(sta);
  Snap start_snap = SnapshotBlock(block);

  resizer.resizePreamble();
  const double slack_margin_s =
      o.slack_margin_ns.has_value() ? *o.slack_margin_ns * 1e-9 : 0.0;
  const double max_buffer_frac =
      (o.max_buffer_percent.has_value() ? *o.max_buffer_percent : 20.0) /
      100.0;
  const double max_wire_m = o.max_wire_length_m.value_or(0.0);
  const double max_slew = o.max_slew_margin.value_or(0.0);
  const double max_cap = o.max_cap_margin.value_or(0.0);

  const bool run_design = o.design;
  const bool run_setup = o.setup || (!o.design && !o.hold && !o.tie &&
                                     !o.clk_nets && !o.clk_inverters);
  const bool run_hold = o.hold || (!o.design && !o.setup && !o.tie &&
                                   !o.clk_nets && !o.clk_inverters);

  bool utilization_limited = false;
  bool buffer_limited = false;
  Snap prev = start_snap;
  std::map<std::string, SnapDiff> pass_diffs;
  // A limit throw records its flag and stops further passes; any other
  // throw aborts the whole call with the engine message. Diffs
  // accumulate per pass name, so a repeated pass (two -tie_port
  // values) reports its total.
  std::optional<std::string> fatal;
  auto run_pass_strict = [&](const std::string &name, auto &&fn) {
    if (utilization_limited || buffer_limited || fatal.has_value()) return;
    SnapDiff diff;
    try {
      fn();
    } catch (const std::exception &e) {
      const std::string msg = e.what();
      if (msg.find("RSZ-0050") != std::string::npos) {
        utilization_limited = true;
      } else if (msg.find("RSZ-0060") != std::string::npos) {
        buffer_limited = true;
      } else {
        fatal = msg;
      }
      diff = DiffSnaps(prev, SnapshotBlock(block));
      prev = SnapshotBlock(block);
      SnapDiff &acc = pass_diffs[name];
      acc.added += diff.added;
      acc.removed += diff.removed;
      acc.resized += diff.resized;
      return;
    }
    diff = DiffSnaps(prev, SnapshotBlock(block));
    prev = SnapshotBlock(block);
    SnapDiff &acc = pass_diffs[name];
    acc.added += diff.added;
    acc.removed += diff.removed;
    acc.resized += diff.resized;
  };

  if (run_design) {
    run_pass_strict("design", [&] {
      resizer.repairDesign(max_wire_m, max_slew, max_cap);
    });
  }
  if (run_setup) {
    run_pass_strict("setup", [&] {
      resizer.repairSetup(static_cast<float>(slack_margin_s));
    });
  }
  if (run_hold) {
    if (o.end_pin.has_value()) {
      std::string inst_name, pin_name;
      if (!SplitInstPin(*o.end_pin, inst_name, pin_name)) {
        return FailConfig("bad -end_pin form (want inst/pin): " + *o.end_pin);
      }
      sta::dbNetwork *network = sta->getDbNetwork();
      sta::Instance *inst = network->findInstance(inst_name);
      if (inst == nullptr)
        return FailConfig("instance not found: " + inst_name);
      sta::Pin *pin = network->findPin(inst, pin_name);
      if (pin == nullptr)
        return FailConfig("pin not found: " + *o.end_pin);
      sta::LibertyCell *buffer_cell =
          network->findLibertyCell(*o.buffer_cell);
      if (buffer_cell == nullptr)
        return FailConfig("buffer cell not found: " + *o.buffer_cell);
      run_pass_strict("hold", [&] {
        resizer.repairHold(pin, buffer_cell,
                           static_cast<float>(slack_margin_s),
                           o.allow_setup_violations,
                           static_cast<float>(max_buffer_frac));
      });
    } else {
      run_pass_strict("hold", [&] {
        resizer.repairHold(static_cast<float>(slack_margin_s),
                           o.allow_setup_violations,
                           static_cast<float>(max_buffer_frac));
      });
    }
  }
  if (o.tie) {
    for (const std::string &tie_port : o.tie_ports) {
      std::string cell_name, port_name;
      if (!SplitCellPin(tie_port, cell_name, port_name))
        return FailConfig("bad -tie_port form (want cell/pin): " + tie_port);
      sta::LibertyCell *cell =
          sta->getDbNetwork()->findLibertyCell(cell_name);
      if (cell == nullptr)
        return FailConfig("tie cell not found: " + cell_name);
      sta::LibertyPort *port = cell->findLibertyPort(port_name);
      if (port == nullptr)
        return FailConfig("tie port not found: " + tie_port);
      const double separation = o.separation_m.value_or(0.0);
      run_pass_strict("tie", [&] {
        resizer.repairTieFanout(port, separation, false);
      });
      if (utilization_limited || buffer_limited || fatal.has_value()) break;
    }
  }
  if (o.clk_nets) {
    run_pass_strict("clk_nets", [&] {
      resizer.repairClkNets(max_wire_m);
    });
  }
  if (o.clk_inverters) {
    run_pass_strict("clk_inverters", [&] {
      resizer.repairClkInverters();
    });
  }
  if (fatal.has_value()) return FailIo("repair failed: " + *fatal);

  TimingMetrics end = Measure(sta);
  Snap end_snap = SnapshotBlock(block);
  SnapDiff total = DiffSnaps(start_snap, end_snap);

  // Tie cross-check (upstream check_ties): every net driven by a tie
  // instance must have exactly 2 connections; locations of tie
  // instances, sorted, in microns with 1 decimal (upstream
  // report_ties form).
  std::size_t tie_nets = 0, tie_bad_nets = 0;
  std::vector<std::pair<double, double>> tie_locs;
  if (o.tie) {
    const double dbu_per_micron = tech->getDbUnitsPerMicron();
    for (odb::dbInst *inst : block->getInsts()) {
      odb::dbMaster *master = inst->getMaster();
      if (master == nullptr) continue;
      bool is_tie = false;
      for (const std::string &tie_port : o.tie_ports) {
        std::string cell_name, port_name;
        if (SplitCellPin(tie_port, cell_name, port_name) &&
            master->getName() == cell_name)
          is_tie = true;
      }
      if (!is_tie) continue;
      int x = 0, y = 0;
      inst->getLocation(x, y);
      tie_locs.emplace_back(x / dbu_per_micron, y / dbu_per_micron);
    }
    std::sort(tie_locs.begin(), tie_locs.end());
    for (odb::dbNet *net : block->getNets()) {
      odb::dbInst *driver = nullptr;
      for (odb::dbITerm *iterm : net->getITerms()) {
        // An output iterm drives the net; input iterms load it.
        odb::dbMTerm *mterm = iterm->getMTerm();
        if (mterm != nullptr &&
            mterm->getIoType() == odb::dbIoType::OUTPUT) {
          driver = iterm->getInst();
          break;
        }
      }
      if (driver == nullptr) continue;
      odb::dbMaster *master = driver->getMaster();
      if (master == nullptr) continue;
      bool driver_is_tie = false;
      for (const std::string &tie_port : o.tie_ports) {
        std::string cell_name, port_name;
        if (SplitCellPin(tie_port, cell_name, port_name) &&
            master->getName() == cell_name)
          driver_is_tie = true;
      }
      if (!driver_is_tie) continue;
      tie_nets += 1;
      // Connections count instance terminals AND top-level pins: a
      // tie net driving an output port (tie6) has 1 iterm plus 1
      // bterm, which is 2 connections and passes upstream check_ties
      // (tie6 .ok shows no check error). Counting iterms only
      // flagged that net wrongly.
      if (net->getITerms().size() + net->getBTerms().size() != 2)
        tie_bad_nets += 1;
    }
  }

  std::ostringstream payload;
  payload << "status=ok";
  payload << " start_wns_ns=" << Fmt(start.wns_ns, 6);
  payload << " start_tns_ns=" << Fmt(start.tns_ns, 6);
  payload << " start_hold_ns=" << Fmt(start.hold_ns, 6);
  payload << " start_instances=" << start_snap.size();
  payload << " end_wns_ns=" << Fmt(end.wns_ns, 6);
  payload << " end_tns_ns=" << Fmt(end.tns_ns, 6);
  payload << " end_hold_ns=" << Fmt(end.hold_ns, 6);
  payload << " end_tns_hold_ns=" << Fmt(end.tns_hold_ns, 6);
  payload << " end_instances=" << end_snap.size();
  payload << " upsized=" << total.resized;
  payload << " inserted=" << total.added;
  payload << " removed=" << total.removed;
  for (const char *pass : {"design", "setup", "hold", "tie", "clk_nets",
                           "clk_inverters"}) {
    auto it = pass_diffs.find(pass);
    if (it != pass_diffs.end()) {
      payload << " " << pass << "_inserted=" << it->second.added;
      payload << " " << pass << "_upsized=" << it->second.resized;
      payload << " " << pass << "_removed=" << it->second.removed;
    }
  }
  payload << " wire_layer=" << wire_layer_name;
  payload << " wire_rc_source=" << wire_rc_source;
  payload << " wire_r_ohm_per_m=" << FmtRaw(wire_r);
  payload << " wire_c_f_per_m=" << FmtRaw(wire_c);
  if (!wire_layer_clk_name.empty())
    payload << " wire_layer_clk=" << wire_layer_clk_name;
  payload << " wire_clk_rc_source=" << wire_clk_source;
  payload << " wire_clk_r_ohm_per_m=" << FmtRaw(wire_clk_r);
  payload << " wire_clk_c_f_per_m=" << FmtRaw(wire_clk_c);
  payload << " design_area_um2=" << FmtRaw(resizer.designArea() * 1e12);
  payload << " utilization_percent=" << FmtRaw(resizer.utilization() * 100.0);
  payload << " utilization_limited=" << (utilization_limited ? 1 : 0);
  payload << " buffer_limited=" << (buffer_limited ? 1 : 0);
  if (o.tie) {
    payload << " tie_nets=" << tie_nets;
    payload << " tie_bad_nets=" << tie_bad_nets;
    payload << " tie_locations=";
    bool first = true;
    for (const auto &loc : tie_locs) {
      if (!first) payload << ";";
      first = false;
      payload << Fmt(loc.first, 1) << "," << Fmt(loc.second, 1);
    }
  }
  return CommandResult::Ok(payload.str());
}

}  // namespace
}  // namespace eos::ui

namespace eos::ui {
namespace {

CommandResult RepairTimingHandler(CommandContext &ctx,
                                  const std::vector<std::string> &args) {
  RepairOptions o;
  if (auto bad = ParseRepairArgs(args, o)) return *bad;
  if (auto bad = ValidateRepairOptions(o)) return *bad;
  return RunRepair(ctx, o);
}

CommandResult SetWireRcHandler(CommandContext &ctx,
                               const std::vector<std::string> &args) {
  if (args.size() != 3) {
    return FailConfig(
        "usage: eos::set_wire_rc <layer> <r_ohm_per_m> <c_f_per_m>");
  }
  double r = 0.0, c = 0.0;
  if (!ParseDouble(args[1], r) || !ParseDouble(args[2], c)) {
    return FailConfig("set_wire_rc values must be numbers: " + args[1] +
                      " " + args[2]);
  }
  if (r <= 0 || c <= 0) {
    return FailConfig(
        "set_wire_rc values must be positive (never 0.0): " + args[1] +
        " " + args[2]);
  }
  ctx.wire_rc_overrides[args[0]] = std::make_pair(r, c);
  return CommandResult::Ok("layer=" + args[0] +
                           " r_ohm_per_m=" + FmtRaw(r) +
                           " c_f_per_m=" + FmtRaw(c));
}

}  // namespace

// Registers the repair commands. Called from RegisterCoreCommands;
// registration cannot fail unless this file names a command twice,
// which the registry unit test catches by counting the set.
void RegisterRepairCommands(CommandRegistry &registry) {
  (void)registry.Register(
      "repair_timing",
      "repair_timing -def <def> -liberty <lib> [-tlef <tlef>] "
      "[-lef <lef>]... [-sdc <sdc>] [-clock <name> -period <ns>] "
      "[-setup] [-hold] [-design] [-tie] [-clk_nets] [-clk_inverters] "
      "[-slack_margin <ns>] [-max_buffer_percent <pct>] "
      "[-allow_setup_violations] [-max_utilization <pct>] "
      "[-max_wire_length <m>] [-max_slew_margin <v>] "
      "[-max_cap_margin <v>] [-tie_port <cell/pin>]... "
      "[-separation <m>] [-end_pin <inst/pin>] [-buffer_cell <cell>] "
      "[-wire_layer <layer>] [-wire_layer_clk <layer>] "
      "[-wire_r <ohm/m> -wire_c <F/m>] [-clock_net <net>]... "
      "[-dont_use <glob>]...",
      RepairTimingHandler);
  (void)registry.Register(
      "set_wire_rc", "set_wire_rc <layer> <r_ohm_per_m> <c_f_per_m>",
      SetWireRcHandler);
}

}  // namespace eos::ui
