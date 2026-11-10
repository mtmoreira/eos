#!/usr/bin/env python3
"""Generate the EOS mock library/PDK (v1).

EOS-owned synthetic data for CI only. Every value below was
authored for this file; none was copied from any real PDK.
Structure and format are Liberty/LEF-compatible so the EOS
engines read the mock exactly like a real library, but no
mock result may ever be cited as PDK correlation: the mock
validates engine mechanics, never silicon behavior.

Usage: python3 gen_mock.py (writes eos_mock.lib, eos_mock.lef,
eos_mock.tlef next to this script). Output is deterministic;
re-running must produce byte-identical files.

Enrichment loop: when a new engine need appears (a cell, an
arc, or a tech parameter), add it here together with the
test that failed without it, and extend mock/README.md.
"""

import os

HERE = os.path.dirname(os.path.abspath(__file__))

# Table axes (Liberty units: ns for slew, pF for load).
SLEWS = [0.01, 0.05, 0.2, 0.8]
LOADS = [0.001, 0.005, 0.02, 0.1]
CON_SLEWS = [0.01, 0.2, 0.8]


def fmt(v):
    return "%.6f" % v


def delay_table(base, a_slew, b_load):
    """Rows over SLEWS, columns over LOADS: base + a*slew + b*load."""
    rows = []
    for s in SLEWS:
        rows.append(", ".join(fmt(base + a_slew * s + b_load * l)
                              for l in LOADS))
    return rows


def trans_table(base, a_slew, b_load):
    return delay_table(base, a_slew, b_load)


def con_table(base, a_slew):
    return ", ".join(fmt(base + a_slew * s) for s in CON_SLEWS)


# ---------------------------------------------------------------- cells
# Each cell: name, footprint (equiv group for upsizing), area (um2),
# LEF width (um, height is 3.2 for all core cells), pins, arcs.
# Pin: (name, dir, cap_pF, max_trans_ns or None, max_cap_pF or None,
#       function or None, clock flag).
# Arc: (from_pin, to_pin, base_ns, a_slew, b_load, tbase_ns, ta_slew,
#       tb_load, timing_sense).
def buf(name, base, b_load, cap, width, area, footprint="EM_BUF",
        max_trans=None):
    return {"name": name, "footprint": footprint, "area": area,
            "width": width,
            "pins": [("A", "input", cap, 1.5, None, None, False),
                     ("Z", "output", 0.0, max_trans, 0.13, "(A)", False)],
            "arcs": [("A", "Z", base, 0.28, b_load, 0.020, 0.25, 0.60,
                      "positive_unate")]}


def inv(name, base, b_load, cap, width, area, footprint="EM_INV"):
    # Nangate inverter output pin is ZN (confirmed by the
    # clk_inverters1 DEF), not Z.
    return {"name": name, "footprint": footprint, "area": area,
            "width": width,
            "pins": [("A", "input", cap, 1.5, None, None, False),
                     ("ZN", "output", 0.0, None, 0.13, "(!A)", False)],
            "arcs": [("A", "ZN", base, 0.28, b_load, 0.020, 0.25, 0.60,
                      "negative_unate")]}


def comb(name, inputs, func, base, area, width, sense="positive_unate"):
    # Nangate combinational output pin is ZN (confirmed by the
    # setup2 DEF for AND2/NAND2/XNOR2; applied to the whole family
    # until a DEF proves otherwise for a cell).
    pins = [(p, "input", 0.002, 1.5, None, None, False) for p in inputs]
    pins.append(("ZN", "output", 0.0, None, 0.13, func, False))
    arcs = [(p, "ZN", base, 0.30, 0.60, 0.020, 0.25, 0.60, sense)
            for p in inputs]
    return {"name": name, "footprint": name, "area": area, "width": width,
            "pins": pins, "arcs": arcs}


CELLS = [
    # Buffers: drive variants share a footprint so the resizer can
    # upsize/downsize within the family. Stronger means a smaller
    # load coefficient (lower drive resistance).
    buf("EM_BUF_X1", 0.060, 0.80, 0.002, 1.0, 4.0),
    buf("EM_BUF_X2", 0.048, 0.50, 0.0035, 1.8, 6.0),
    buf("EM_BUF_X4", 0.038, 0.30, 0.006, 3.2, 10.0),
    buf("EM_BUF_X8", 0.030, 0.18, 0.011, 6.0, 18.0),
    # Clock buffers: same shape as buffers, own footprint.
    buf("EM_CLKBUF_X1", 0.052, 0.65, 0.002, 1.0, 4.0,
        footprint="EM_CLKBUF"),
    buf("EM_CLKBUF_X2", 0.042, 0.40, 0.0035, 1.8, 6.0,
        footprint="EM_CLKBUF"),
    # Inverters.
    inv("EM_INV_X1", 0.050, 0.70, 0.002, 1.0, 4.0),
    inv("EM_INV_X2", 0.040, 0.45, 0.0035, 1.8, 6.0),
    inv("EM_INV_X4", 0.032, 0.27, 0.006, 3.2, 10.0),
    # Weak slow buffer: the slew4 BUF1 analog. Its input pin carries
    # a tight max_transition so a slow driver edge violates it.
    {"name": "EM_BUFWK", "footprint": "EM_BUFWK", "area": 4.0, "width": 1.0,
     "pins": [("A", "input", 0.002, 0.08, None, None, False),
              ("Z", "output", 0.0, None, 0.13, "(A)", False)],
     "arcs": [("A", "Z", 0.150, 0.60, 1.50, 0.060, 0.50, 1.20,
               "positive_unate")]},
    # Combinational.
    comb("EM_AND2_X1", ["A1", "A2"], "(A1 & A2)", 0.070, 5.0, 1.4),
    comb("EM_AND3_X1", ["A1", "A2", "A3"], "(A1 & A2 & A3)", 0.085, 7.0,
         1.8),
    comb("EM_AND4_X1", ["A1", "A2", "A3", "A4"], "(A1 & A2 & A3 & A4)",
         0.100, 9.0, 2.2),
    comb("EM_NAND2_X1", ["A1", "A2"], "(!(A1 & A2))", 0.065, 5.0, 1.4,
         "negative_unate"),
    comb("EM_NAND3_X1", ["A1", "A2", "A3"], "(!(A1 & A2 & A3))", 0.080,
         7.0, 1.8, "negative_unate"),
    comb("EM_NAND4_X1", ["A1", "A2", "A3", "A4"],
         "(!(A1 & A2 & A3 & A4))", 0.095, 9.0, 2.2, "negative_unate"),
    comb("EM_NOR2_X1", ["A1", "A2"], "(!(A1 | A2))", 0.070, 5.0, 1.4,
         "negative_unate"),
    comb("EM_NOR3_X1", ["A1", "A2", "A3"], "(!(A1 | A2 | A3))", 0.085,
         7.0, 1.8, "negative_unate"),
    comb("EM_NOR4_X1", ["A1", "A2", "A3", "A4"],
         "(!(A1 | A2 | A3 | A4))", 0.100, 9.0, 2.2, "negative_unate"),
    comb("EM_OR2_X1", ["A1", "A2"], "(A1 | A2)", 0.070, 5.0, 1.4),
    comb("EM_OR3_X1", ["A1", "A2", "A3"], "(A1 | A2 | A3)", 0.085, 7.0,
         1.8),
    comb("EM_XOR2_X1", ["A", "B"], "(A ^ B)", 0.110, 8.0, 2.0,
         "non_unate"),
    comb("EM_XNOR2_X1", ["A", "B"], "(!(A ^ B))", 0.110, 8.0, 2.0,
         "non_unate"),
    comb("EM_AOI21_X1", ["A1", "A2", "B1"], "!((A1 & A2) | B1)", 0.075,
         7.0, 1.8, "non_unate"),
    comb("EM_AOI211_X1", ["A1", "A2", "B1", "C1"],
         "!((A1 & A2) | B1 | C1)", 0.090, 9.0, 2.2, "non_unate"),
    comb("EM_AOI22_X1", ["A1", "A2", "B1", "B2"],
         "!((A1 & A2) | (B1 & B2))", 0.090, 9.0, 2.2, "non_unate"),
    comb("EM_AOI221_X1", ["A1", "A2", "B1", "B2", "C1"],
         "!((A1 & A2) | (B1 & B2) | C1)", 0.105, 11.0, 2.6, "non_unate"),
    comb("EM_OAI21_X1", ["A1", "A2", "B1"], "!((A1 | A2) & B1)", 0.075,
         7.0, 1.8, "non_unate"),
    comb("EM_OAI211_X1", ["A1", "A2", "B1", "C1"],
         "!((A1 | A2) & B1 & C1)", 0.090, 9.0, 2.2, "non_unate"),
    comb("EM_MUX2_X1", ["A", "B", "S"], "((S & B) | ((!S) & A))", 0.095,
         9.0, 2.4, "non_unate"),
    # Weak OR-AND-INVERT driver: the slew6 o21ai analog. Small output
    # max_capacitance so a hi-fanout load violates it.
    {"name": "EM_O21AI_X1", "footprint": "EM_O21AI_X1", "area": 6.0,
     "width": 1.6,
     "pins": [("A1", "input", 0.002, 1.5, None, None, False),
              ("A2", "input", 0.002, 1.5, None, None, False),
              ("B1", "input", 0.002, 1.5, None, None, False),
              ("Y", "output", 0.0, None, 0.05, "!((A1 | A2) & B1)",
               False)],
     "arcs": [("A1", "Y", 0.120, 0.40, 1.20, 0.040, 0.40, 1.00,
               "non_unate"),
              ("A2", "Y", 0.120, 0.40, 1.20, 0.040, 0.40, 1.00,
               "non_unate"),
              ("B1", "Y", 0.100, 0.40, 1.20, 0.040, 0.40, 1.00,
               "non_unate")]},
]

# Sequential cells. DFF output max_capacitance 0.10 pF: a 60-load
# DFF fanout (0.12 pF of pin caps) violates it, which is the cap1
# shape; a 30-load fanout (0.06 pF) does not, which is the slew1
# shape (driven by its SDC max_transition instead).
def dff(name, width, area, q_max_trans=None, reset=False):
    pins = [("D", "input", 0.002, 1.5, None, None, False),
            ("CK", "input", 0.0025, 1.5, None, None, True)]
    if reset:
        pins.append(("R", "input", 0.002, 1.5, None, None, False))
    pins.append(("Q", "output", 0.0, q_max_trans, 0.10, "IQ", False))
    cell = {"name": name, "footprint": name, "area": area, "width": width,
            "pins": pins,
            "arcs": [("CK", "Q", 0.120, 0.30, 0.60, 0.030, 0.30, 0.50,
                      "non_unate")],
            "ff": {"clocked_on": "CK", "next_state": "D",
                   "clear": "R" if reset else None},
            "setup": (0.080, 0.10), "hold": (0.020, 0.05)}
    return cell


CELLS += [
    dff("EM_DFF_X1", 4.0, 20.0),
    dff("EM_DFFR_X1", 4.4, 22.0, reset=True),
    # DFF variant with a tight output max_transition: the slew2
    # DFF_MAX_SLEW analog.
    dff("EM_DFF_SLEW", 4.0, 20.0, q_max_trans=0.10),
    # Transparent latch: the setup3 dlxtn analog. The DEF connects
    # D, GATE_N, Q, so those are the pins (GATE_N is the
    # active-low enable in the original cell's naming).
    {"name": "EM_LATCH", "footprint": "EM_LATCH", "area": 16.0, "width": 3.2,
     "pins": [("D", "input", 0.002, 1.5, None, None, False),
              ("GATE_N", "input", 0.0025, 1.5, None, None, True),
              ("Q", "output", 0.0, None, 0.10, "IQ", False)],
     "arcs": [("GATE_N", "Q", 0.100, 0.30, 0.60, 0.030, 0.30, 0.50,
               "non_unate")],
     "latch": {"enable": "GATE_N", "data_in": "D"},
     "setup": (0.080, 0.10), "hold": (0.020, 0.05)},
    # Tie cells: constant outputs, no timing arcs.
    {"name": "EM_TIEHI", "footprint": "EM_TIEHI", "area": 2.0, "width": 1.0,
     "pins": [("Z", "output", 0.0, None, None, "1", False)],
     "arcs": []},
    {"name": "EM_TIEDUAL", "footprint": "EM_TIEDUAL", "area": 3.0, "width": 1.4,
     "pins": [("Z0", "output", 0.0, None, None, "0", False),
              ("Z1", "output", 0.0, None, None, "1", False)],
     "arcs": []},
    # Fill/tap: no signal pins. Only LEF SIZE matters (area for
    # utilization); kept for the pending placed/routed cases.
    {"name": "EM_FILL", "footprint": "EM_FILL", "area": 2.0, "width": 1.0,
     "pins": [], "arcs": []},
    {"name": "EM_TAP", "footprint": "EM_TAP", "area": 2.0, "width": 1.0,
     "pins": [], "arcs": []},
]

# LEF-only macro: the fanout3 LEFONLY analog. Present in the LEF,
# deliberately absent from the Liberty, so a load without timing
# is exercised. Its instances connect via CK and D (see the
# fanout3 DEF), so those are the pins the macro carries.
LEF_ONLY = {"name": "EM_LEFONLY", "width": 2.0,
            "pins": [("CK", "input"), ("D", "input")]}

HEIGHT = 3.2
# Row site width (um). Every core macro width is a multiple of
# this, so rows tile without gaps. Added for the floorplan
# engine (unit F0, 2026-11-07): InitFloorplan finds its row
# sites from the placed instances' masters, so a LEF with no
# SITE fails floorplan with "No sites found".
SITE_WIDTH = 0.2
SITE_NAME = "em_site"

# Tech layers: synthetic values, own names. Derived scalars via the
# command formula R = RPERSQ / width, C = CPERSQDIST * width +
# 2 * EDGECAPACITANCE (width in um, R in ohm/m, C in F/m):
#   em1: R 1,000,000  C 5.0e-08
#   em2: R   750,000  C 3.8e-08
#   em3: R   333,333  C 3.3e-08
#   em5: R   100,000  C 2.6e-08
# DRC rules (invented fixture values, added for the F13 DRC engine,
# 2026-11-10; documented as invented, never process data):
#   SPACING = pitch - width (adjacent-track wires sit exactly at
#   the rule, so router output on adjacent tracks passes);
#   MINWIDTH = width - 0.02 (just below the default wire width);
#   AREA = width * 0.1 um2 (a 0.1 um long segment of default
#   width); MINSTEP = 0.1 um on every layer.
LAYERS = [
    # name, direction, width_um, pitch_um, rpersq, cpersqdist, edgecap
    ("em1", "HORIZONTAL", 0.2, 0.4, 0.20, 0.05, 0.020),
    ("em2", "VERTICAL", 0.2, 0.4, 0.15, 0.04, 0.015),
    ("em3", "HORIZONTAL", 0.3, 0.6, 0.10, 0.03, 0.012),
    ("em4", "VERTICAL", 0.4, 0.8, 0.07, 0.025, 0.010),
    ("em5", "VERTICAL", 0.5, 1.0, 0.05, 0.02, 0.008),
    ("em6", "HORIZONTAL", 0.6, 1.2, 0.04, 0.018, 0.007),
    ("em7", "VERTICAL", 0.8, 1.6, 0.03, 0.015, 0.006),
    ("em8", "HORIZONTAL", 1.0, 2.0, 0.025, 0.012, 0.005),
    ("em9", "VERTICAL", 1.2, 2.4, 0.02, 0.010, 0.004),
    ("em10", "HORIZONTAL", 1.5, 3.0, 0.015, 0.008, 0.003),
]

# ---------------------------------------------------------------- Liberty
def emit_lib(path):
    out = []
    out.append("/* EOS mock library (v1).")
    out.append(" * Synthetic values authored for CI; see gen_mock.py and")
    out.append(" * mock/README.md. Not PDK data, never cite for signoff.")
    out.append(" */")
    out.append('library ("eos_mock") {')
    out.append('  technology("cmos");')
    out.append("  delay_model : table_lookup;")
    out.append('  time_unit : "1ns";')
    out.append('  voltage_unit : "1V";')
    out.append('  capacitive_load_unit(1, "pf");')
    out.append('  current_unit : "1mA";')
    out.append('  leakage_power_unit : "1mW";')
    out.append('  pulling_resistance_unit : "1kohm";')
    out.append("  nom_voltage : 1.0000000000;")
    out.append("  nom_temperature : 25.000000000;")
    out.append("  nom_process : 1.0000000000;")
    out.append("  default_max_transition : 1.5000000000;")
    out.append("  input_threshold_pct_rise : 50;")
    out.append("  input_threshold_pct_fall : 50;")
    out.append("  output_threshold_pct_rise : 50;")
    out.append("  output_threshold_pct_fall : 50;")
    out.append("  slew_lower_threshold_pct_rise : 20;")
    out.append("  slew_lower_threshold_pct_fall : 20;")
    out.append("  slew_upper_threshold_pct_rise : 80;")
    out.append("  slew_upper_threshold_pct_fall : 80;")
    out.append("  lu_table_template (del_4_4) {")
    out.append("    variable_1 : input_net_transition;")
    out.append("    variable_2 : total_output_net_capacitance;")
    out.append("    index_1 (\"%s\");" %
               ", ".join(fmt(s) for s in SLEWS))
    out.append("    index_2 (\"%s\");" %
               ", ".join(fmt(l) for l in LOADS))
    out.append("  }")
    out.append("  lu_table_template (con_3) {")
    out.append("    variable_1 : related_pin_transition;")
    out.append("    index_1 (\"%s\");" %
               ", ".join(fmt(s) for s in CON_SLEWS))
    out.append("  }")
    for cell in CELLS:
        out.append("  cell (\"%s\") {" % cell["name"])
        out.append("    area : %s;" % fmt(cell["area"]))
        out.append("    cell_footprint : \"%s\";" % cell["footprint"])
        if "ff" in cell:
            ff = cell["ff"]
            clear = ('; clear : "%s"' % ff["clear"]) if ff["clear"] \
                else ""
            out.append("    ff (\"IQ\", \"IQ_N\") {")
            out.append("      clocked_on : \"%s\";" % ff["clocked_on"])
            out.append("      next_state : \"%s\";%s" %
                       (ff["next_state"], clear))
            out.append("    }")
        if "latch" in cell:
            out.append("    latch (\"IQ\", \"IQ_N\") {")
            out.append("      enable : \"%s\";" % cell["latch"]["enable"])
            out.append("      data_in : \"%s\";" % cell["latch"]["data_in"])
            out.append("    }")
        # One pin() block per pin: attributes plus its timing groups
        # (arcs targeting it, setup/hold constraints on D).
        arcs_by_to = {}
        for arc in cell["arcs"]:
            arcs_by_to.setdefault(arc[1], []).append(arc)
        for (pname, direction, cap, max_trans, max_cap, func,
             is_clock) in cell["pins"]:
            out.append("    pin (\"%s\") {" % pname)
            if cap:
                out.append("      capacitance : %s;" % fmt(cap))
            if is_clock:
                out.append("      clock : \"true\";")
            out.append("      direction : \"%s\";" % direction)
            if func:
                out.append("      function : \"%s\";" % func)
            if max_cap is not None:
                out.append("      max_capacitance : %s;" % fmt(max_cap))
            if max_trans is not None:
                out.append("      max_transition : %s;" % fmt(max_trans))
            for (frm, to, base, a_s, b_l, tb, ta_s, tb_l, sense) in \
                    arcs_by_to.get(pname, []):
                out.append("      timing () {")
                out.append("        cell_fall (\"del_4_4\") {")
                out.append("          index_1 (\"%s\");" %
                           ", ".join(fmt(s) for s in SLEWS))
                out.append("          index_2 (\"%s\");" %
                           ", ".join(fmt(l) for l in LOADS))
                out.append("          values (\\")
                rows = delay_table(base, a_s, b_l)
                for i, row in enumerate(rows):
                    cont = ", \\" if i < len(rows) - 1 else ""
                    out.append("            \"%s\"%s" % (row, cont))
                out.append("          );")
                out.append("        }")
                out.append("        cell_rise (\"del_4_4\") {")
                out.append("          index_1 (\"%s\");" %
                           ", ".join(fmt(s) for s in SLEWS))
                out.append("          index_2 (\"%s\");" %
                           ", ".join(fmt(l) for l in LOADS))
                out.append("          values (\\")
                rows = delay_table(base, a_s, b_l)
                for i, row in enumerate(rows):
                    cont = ", \\" if i < len(rows) - 1 else ""
                    out.append("            \"%s\"%s" % (row, cont))
                out.append("          );")
                out.append("        }")
                out.append("        fall_transition (\"del_4_4\") {")
                out.append("          index_1 (\"%s\");" %
                           ", ".join(fmt(s) for s in SLEWS))
                out.append("          index_2 (\"%s\");" %
                           ", ".join(fmt(l) for l in LOADS))
                out.append("          values (\\")
                rows = trans_table(tb, ta_s, tb_l)
                for i, row in enumerate(rows):
                    cont = ", \\" if i < len(rows) - 1 else ""
                    out.append("            \"%s\"%s" % (row, cont))
                out.append("          );")
                out.append("        }")
                out.append("        related_pin : \"%s\";" % frm)
                out.append("        rise_transition (\"del_4_4\") {")
                out.append("          index_1 (\"%s\");" %
                           ", ".join(fmt(s) for s in SLEWS))
                out.append("          index_2 (\"%s\");" %
                           ", ".join(fmt(l) for l in LOADS))
                out.append("          values (\\")
                rows = trans_table(tb, ta_s, tb_l)
                for i, row in enumerate(rows):
                    cont = ", \\" if i < len(rows) - 1 else ""
                    out.append("            \"%s\"%s" % (row, cont))
                out.append("          );")
                out.append("        }")
                out.append("        timing_sense : \"%s\";" % sense)
                if "ff" in cell:
                    out.append("        timing_type : \"rising_edge\";")
                out.append("      }")
            if "setup" in cell and pname == "D":
                rel = "CK" if "ff" in cell else "GATE_N"
                for kind, (base, slope) in (("setup_rising",
                                             cell["setup"]),
                                            ("hold_rising",
                                             cell["hold"])):
                    out.append("      timing () {")
                    out.append("        fall_constraint (\"con_3\") {")
                    out.append("          index_1 (\"%s\");" %
                               ", ".join(fmt(s) for s in CON_SLEWS))
                    out.append("          values (\"%s\");" %
                               con_table(base, slope))
                    out.append("        }")
                    out.append("        related_pin : \"%s\";" % rel)
                    out.append("        rise_constraint (\"con_3\") {")
                    out.append("          index_1 (\"%s\");" %
                               ", ".join(fmt(s) for s in CON_SLEWS))
                    out.append("          values (\"%s\");" %
                               con_table(base, slope))
                    out.append("        }")
                    out.append("        timing_type : \"%s\";" % kind)
                    out.append("      }")
            out.append("    }")
        out.append("  }")
    out.append("}")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")


# ---------------------------------------------------------------- LEF
def emit_lef(path):
    out = []
    out.append("# EOS mock LEF (v1). Synthetic sizes authored for CI;")
    out.append("# see gen_mock.py and mock/README.md.")
    out.append("VERSION 5.8 ;")
    out.append('BUSBITCHARS "[]" ;')
    out.append('DIVIDERCHAR "/" ;')
    out.append("SITE %s" % SITE_NAME)
    out.append("  CLASS CORE ;")
    out.append("  SIZE %s BY %s ;" % (fmt(SITE_WIDTH), fmt(HEIGHT)))
    out.append("END %s" % SITE_NAME)
    macros = [(c["name"], c["width"],
               [(p[0], p[1], p[6]) for p in c["pins"]])
              for c in CELLS]
    macros.append((LEF_ONLY["name"], LEF_ONLY["width"],
                   [(p[0], p[1], False) for p in LEF_ONLY["pins"]]))
    for name, width, pins in macros:
        out.append("MACRO %s" % name)
        out.append("  CLASS CORE ;")
        out.append("  ORIGIN 0 0 ;")
        out.append("  SIZE %s BY %s ;" % (fmt(width), fmt(HEIGHT)))
        out.append("  SITE %s ;" % SITE_NAME)
        out.append("  SYMMETRY X Y ;")
        for i, (pname, direction, is_clock) in enumerate(pins):
            x = 0.1 if direction == "input" else width - 0.3
            y = 0.4 + 0.6 * i
            if y > HEIGHT - 0.3:
                y = HEIGHT - 0.3
            out.append("  PIN %s" % pname)
            out.append("    DIRECTION %s ;" % direction.upper())
            out.append("    USE %s ;" % ("CLOCK" if is_clock else "SIGNAL"))
            out.append("    PORT")
            out.append("      LAYER em1 ;")
            out.append("        RECT %s %s %s %s ;" %
                       (fmt(x), fmt(y), fmt(x + 0.2), fmt(y + 0.2)))
            out.append("    END")
            out.append("  END %s" % pname)
        out.append("END %s" % name)
    out.append("END LIBRARY")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")


# ---------------------------------------------------------------- tech LEF
def emit_tlef(path):
    out = []
    out.append("# EOS mock tech LEF (v1). Synthetic layer data authored")
    out.append("# for CI; see gen_mock.py and mock/README.md.")
    out.append("VERSION 5.8 ;")
    out.append('BUSBITCHARS "[]" ;')
    out.append('DIVIDERCHAR "/" ;')
    out.append("UNITS")
    out.append("  DATABASE MICRONS 2000 ;")
    out.append("END UNITS")
    for (name, direction, width, pitch, rpersq, cpersq, edgecap) \
            in LAYERS:
        out.append("LAYER %s" % name)
        out.append("  TYPE ROUTING ;")
        out.append("  DIRECTION %s ;" % direction)
        out.append("  PITCH %s ;" % fmt(pitch))
        out.append("  WIDTH %s ;" % fmt(width))
        out.append("  SPACING %s ;" % fmt(pitch - width))
        out.append("  MINWIDTH %s ;" % fmt(width - 0.02))
        out.append("  AREA %s ;" % fmt(width * 0.1))
        out.append("  MINSTEP %s ;" % fmt(0.1))
        out.append("  RESISTANCE RPERSQ %s ;" % fmt(rpersq))
        out.append("  CAPACITANCE CPERSQDIST %s ;" % fmt(cpersq))
        out.append("  EDGECAPACITANCE %s ;" % fmt(edgecap))
        out.append("END %s" % name)
    out.append("END LIBRARY")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")


def main():
    emit_lib(os.path.join(HERE, "eos_mock.lib"))
    emit_lef(os.path.join(HERE, "eos_mock.lef"))
    emit_tlef(os.path.join(HERE, "eos_mock.tlef"))


if __name__ == "__main__":
    main()
