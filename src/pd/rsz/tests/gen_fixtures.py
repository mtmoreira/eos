#!/usr/bin/env python3
"""Generate the hi-fanout / tie-fanout DEF fixtures for the adapted
rsz Tcl regression suite (PR4).

EOS-owned translation of the upstream test-helper DEF shapes
(write_hi_fanout_def1/def2, write_tie_hi_fanout_def): same
components, nets, placements, and die areas, expressed from the
parameters each adapted case needs. The upstream Tcl helpers are
NOT copied: this file restates the shape (a driver instance plus
N load instances in a grid, one clock net, one driven net) in
Python, so the suite can ship static fixtures instead of
depending on upstream helper scripts at test time.

Usage: gen_fixtures.py <out-dir>
Writes one .def per case into <out-dir>.
"""
import math
import os
import sys

HEADER = """VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;

DESIGN hi_fanout ;
"""

SPECIAL_NETS = """SPECIALNETS 2 ;
- VSS  ( * VSS )
  + USE GROUND ;
- VDD  ( * VDD )
  + USE POWER ;
END SPECIALNETS
"""


def row_count(fanout):
    return int(math.sqrt(fanout))


def fanout_port_lines(port_name, port_layer):
    return [
        f"- {port_name} + NET {port_name} + DIRECTION INPUT + USE SIGNAL",
        f"+ LAYER {port_layer} ( 0 0 ) ( 100 100 ) + FIXED ( 1000 1000 ) N ;",
    ]


def fanout_load_lines(fanout, load_inst, load_cell, load_in, load_space):
    rows = row_count(fanout)
    out = []
    for i in range(fanout):
        x = (i % rows) * load_space
        y = (i // rows) * load_space
        out.append(f"- {load_inst}{i} {load_cell} + PLACED ( {x} {y} ) N ;")
    return out


def fanout_clk_terms(fanout, load_inst, load_clk):
    if not load_clk:
        return ""
    parts = []
    for i in range(fanout):
        parts.append(f" ( {load_inst}{i} {load_clk} )")
        if i % 10 == 0:
            parts.append("\n")
    return "".join(parts)


def fanout_load_terms(fanout, load_inst, load_in):
    parts = []
    for i in range(fanout):
        parts.append(f" ( {load_inst}{i} {load_in} )")
        if i % 10 == 0:
            parts.append("\n")
    return "".join(parts)


def diearea(fanout, load_space):
    rows = row_count(fanout)
    dx = (rows + 1) * load_space
    dy = (fanout // rows + 1) * load_space
    return f"DIEAREA ( 0 0 ) ( {dx} {dy} ) ;"


def write_def1(path, fanout, drvr_inst, drvr_cell, drvr_clk, drvr_out,
               load_inst, load_cell, load_clk, load_in, load_space,
               port_layer, dbu):
    lines = [HEADER.rstrip("\n"),
             f"UNITS DISTANCE MICRONS {dbu} ;",
             diearea(fanout, load_space),
             "",
             f"COMPONENTS {fanout + 1} ;",
             f"- {drvr_inst} {drvr_cell} + PLACED ( 1000 1000 ) N ;"]
    lines += fanout_load_lines(fanout, load_inst, load_cell, load_in,
                               load_space)
    lines += ["END COMPONENTS", "",
              "PINS 1 ;"]
    lines += fanout_port_lines("clk1", port_layer)
    lines += ["END PINS", "", SPECIAL_NETS.rstrip("\n"), "",
              "NETS 2 ;"]
    clk = f"- clk1 ( PIN clk1 )"
    if drvr_clk:
        clk += f" ( {drvr_inst} {drvr_clk} )"
    clk += fanout_clk_terms(fanout, load_inst, load_clk)
    clk += " ;"
    lines.append(clk)
    lines.append("")
    net = f"- net0 ( {drvr_inst} {drvr_out} )"
    net += fanout_load_terms(fanout, load_inst, load_in)
    net += " ;"
    lines.append(net)
    lines += ["", "END NETS", "", "END DESIGN", ""]
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))


def write_def2(path, fanout, in_port, load_inst, load_cell, load_clk,
               load_in, load_space, port_layer, dbu):
    lines = [HEADER.rstrip("\n"),
             f"UNITS DISTANCE MICRONS {dbu} ;",
             diearea(fanout, load_space),
             "",
             f"COMPONENTS {fanout} ;"]
    lines += fanout_load_lines(fanout, load_inst, load_cell, load_in,
                               load_space)
    lines += ["END COMPONENTS", "",
              "PINS 2 ;"]
    lines += fanout_port_lines(in_port, port_layer)
    lines += fanout_port_lines("clk1", port_layer)
    lines += ["END PINS", "", SPECIAL_NETS.rstrip("\n"), "",
              "NETS 2 ;"]
    net = f"- {in_port} ( PIN {in_port} )"
    net += fanout_load_terms(fanout, load_inst, load_in)
    net += " ;"
    lines.append(net)
    lines.append("")
    clk = "- clk1 ( PIN clk1 )"
    clk += fanout_clk_terms(fanout, load_inst, load_clk)
    clk += " ;"
    lines.append(clk)
    lines += ["", "END NETS", "", "END DESIGN", ""]
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))


def write_tie_def(path, tie_cell, tie_port_name, load_port_name, fanout):
    # Mirrors the upstream tie shape: one tie instance plus fanout
    # BUF_X1 loads on a 10 um grid, tie net n1. The upstream
    # helper hardcodes BUF_X1 loads; this generator keeps that.
    rows = int(math.sqrt(fanout))
    spacing = 10000
    x_origin = 10000
    y_origin = 10000
    size = x_origin + spacing * (rows + 1)
    lines = ["VERSION 5.8 ;",
             'DIVIDERCHAR "/" ;',
             'BUSBITCHARS "[]" ;',
             "DESIGN top ;",
             "UNITS DISTANCE MICRONS 1000 ;",
             "",
             f"DIEAREA ( 0 0 ) ( {size} {size} ) ;",
             "",
             f"COMPONENTS {fanout + 1} ;",
             f"    - t1 {tie_cell} ;"]
    for i in range(fanout):
        x = (i % rows) * spacing + x_origin
        y = (i // rows) * spacing + y_origin
        lines.append(f" - u{i} BUF_X1 + PLACED ( {x} {y} ) N ;")
    lines += ["END COMPONENTS",
              "NETS 1 ;"]
    net = f"- n1 ( t1 {tie_port_name} ) "
    for i in range(fanout):
        net += f"  ( u{i} {load_port_name} )"
        if i % 5 == 0:
            net += "\n"
    net += " + USE SIGNAL ;"
    lines.append(net)
    lines += ["END NETS",
              "END DESIGN", ""]
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))


CASES = [
    # (filename, kind, args...)
    ("repair_cap1.def", "def1", 60, "drvr", "DFF_X1", "CK", "Q",
     "load", "DFF_X1", "CK", "D", 5000, "metal1", 1000),
    ("repair_cap2.def", "def1", 60, "drvr", "DFF_X1", "CK", "Q",
     "load", "DFF_X1", "CK", "D", 5000, "metal1", 1000),
    ("repair_fanout1.def", "def1", 35, "drvr", "DFF_X1", "CK", "Q",
     "load", "DFF_X1", "CK", "D", 5000, "metal1", 1000),
    ("repair_slew1.def", "def1", 30, "drvr", "DFF_X1", "CK", "Q",
     "load", "DFF_X1", "CK", "D", 5000, "metal1", 1000),
    ("repair_slew2.def", "def1", 20, "rdrv", "DFF_MAX_SLEW", "CK", "Q",
     "r", "DFF_X1", "CK", "D", 5000, "metal1", 1000),
    ("repair_slew6.def", "def1", 50, "rdrv", "sky130_fd_sc_hd__o21ai_0", "", "Y",
     "r", "sky130_fd_sc_hd__dfxtp_1", "", "D", 5000, "met1", 1000),
    ("repair_slew8.def", "def2", 250, "reset",
     "r", "sky130_fd_sc_hd__dfrtp_1", "CLK", "RESET_B", 100000, "met2", 1000),
    # Upstream cap3/slew9 reuse the name repair_slew1.def for this
    # shape (sequential runs overwrite it); EOS ships it under a
    # distinct static name to avoid colliding with slew1's fixture.
    ("repair_max_slew_cap.def", "def1", 104, "rdrv", "sky130_fd_sc_hd__o21ai_0",
     "", "Y", "r", "sky130_fd_sc_hd__dfxtp_1", "", "D", 5000, "met1", 1000),
    ("repair_tie4.def", "tie", "LOGIC1_X1", "Z", "A", 10),
]


def main():
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    for case in CASES:
        name, kind = case[0], case[1]
        path = os.path.join(out_dir, name)
        if kind == "def1":
            write_def1(path, *case[2:])
        elif kind == "def2":
            write_def2(path, *case[2:])
        elif kind == "tie":
            write_tie_def(path, *case[2:])
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
