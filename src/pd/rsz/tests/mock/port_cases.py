#!/usr/bin/env python3
"""Port the PR4 pending-licence resizer cases onto the mock library.

Reads the original repair_*.tcl scripts and their fixtures from
src/pd/rsz/tests, and writes mock ports next to this script under
mock/: a ported .def, a ported .sdc (verbatim except hold2/hold3
faithful forms and the hold5 pin rename), and a ported .tcl that
loads mock/eos_mock.{tlef,lef,lib} instead of any real PDK file.

DEF transform (mechanical, everything else byte-preserved):
  * master renames per MASTER_MAP (original cell -> EM_* mock cell);
  * ROW statements dropped (the coverage spec explicitly allows
    this; it removes the need for a SITE workaround);
  * PIN layer names metal*/met* -> em*;
  * units normalized to 2000 dbu/micron with exact integer
    coordinate scaling, so micron distances (hence wire lengths)
    are unchanged;
  * instance-pin renames for the sky130 naming deltas (CLK -> CK
    on DFFs, X/Y -> Z on buffers/inverters);
  * validation: every COMPONENTS master must map, and every NETS
    instance pin must exist on the mock macro (PAD instances are
    exempt: their pins live in the committed fixtures/pad.lef).

SDC transform: verbatim copy, except:
  * hold2: faithful propagated-clock SDC (the committed script
    uses an ideal clock, which the coverage spec calls vacuous);
  * hold3: new propagated-clock SDC, and the script swaps
    -clock/-period for -sdc;
  * hold5: get_pins r1/CLK -> r1/CK (mock DFF clock pin name).

Usage: python3 port_cases.py  (run from src/pd/rsz/tests)
Deterministic: re-running overwrites identical bytes.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.dirname(HERE)
FIX = os.path.join(TESTS, "fixtures")

PORTED = ["setup1", "setup2", "setup3", "hold1", "hold2", "hold3",
          "hold4", "hold5", "slew1", "slew2", "slew3", "slew4",
          "slew5", "slew6", "cap1", "cap2", "fanout2", "fanout3",
          "wire3", "tie1", "tie2", "tie3", "tie4", "tie5", "tie6",
          "tie7", "clk_inverters1"]

# Original DEF per case (shared DEFs: hold1, hold4, wire1).
CASE_DEF = {"hold2": "repair_hold1.def", "hold3": "repair_hold3.def",
            "hold4": "repair_hold4.def", "hold5": "repair_hold4.def",
            "wire3": "repair_wire1.def"}
# Original SDC per case (None = the script carries no -sdc).
CASE_SDC = {"hold2": "repair_hold2.sdc", "hold3": "repair_hold3.sdc"}

MASTER_MAP = {}
for _old, _new in [("BUF_X1", "EM_BUF_X1"), ("BUF_X2", "EM_BUF_X2"),
                   ("BUF_X4", "EM_BUF_X4"), ("BUF_X8", "EM_BUF_X8"),
                   ("BUF1", "EM_BUFWK"),
                   ("INV_X1", "EM_INV_X1"), ("INV_X2", "EM_INV_X2"),
                   ("INV_X4", "EM_INV_X4"),
                   ("CLKBUF_X1", "EM_CLKBUF_X1"),
                   ("CLKBUF_X2", "EM_CLKBUF_X2"),
                   ("LOGIC1_X1", "EM_TIEHI"), ("TIE_X1", "EM_TIEDUAL"),
                   ("DFF_X1", "EM_DFF_X1"), ("DFFR_X1", "EM_DFFR_X1"),
                   ("DFF_MAX_SLEW", "EM_DFF_SLEW"),
                   ("AND2_X1", "EM_AND2_X1"), ("AND3_X1", "EM_AND3_X1"),
                   ("AND4_X1", "EM_AND4_X1"),
                   ("NAND2_X1", "EM_NAND2_X1"),
                   ("NAND3_X1", "EM_NAND3_X1"),
                   ("NAND4_X1", "EM_NAND4_X1"),
                   ("NOR2_X1", "EM_NOR2_X1"), ("NOR3_X1", "EM_NOR3_X1"),
                   ("NOR4_X1", "EM_NOR4_X1"),
                   ("OR2_X1", "EM_OR2_X1"), ("OR3_X1", "EM_OR3_X1"),
                   ("XOR2_X1", "EM_XOR2_X1"),
                   ("XNOR2_X1", "EM_XNOR2_X1"),
                   ("AOI21_X1", "EM_AOI21_X1"),
                   ("AOI211_X1", "EM_AOI211_X1"),
                   ("AOI22_X1", "EM_AOI22_X1"),
                   ("AOI221_X1", "EM_AOI221_X1"),
                   ("OAI21_X1", "EM_OAI21_X1"),
                   ("OAI211_X1", "EM_OAI211_X1"),
                   ("MUX2_X1", "EM_MUX2_X1"),
                   ("LEFONLY", "EM_LEFONLY"), ("PAD", "PAD"),
                   ("FILLCELL_X1", "EM_FILL")]:
    MASTER_MAP[_old] = _new
for _pre in ("sky130_fd_sc_hd__", "sky130_fd_sc_hs__"):
    for _old, _new in [("buf_1", "EM_BUF_X1"), ("buf_2", "EM_BUF_X2"),
                       ("buf_4", "EM_BUF_X4"), ("buf_8", "EM_BUF_X8"),
                       ("inv_1", "EM_INV_X1"), ("inv_2", "EM_INV_X2"),
                       ("inv_4", "EM_INV_X4"),
                       ("clkbuf_1", "EM_CLKBUF_X1"),
                       ("clkbuf_2", "EM_CLKBUF_X2"),
                       ("dfxtp_1", "EM_DFF_X1"),
                       ("dfxbp_1", "EM_DFF_X1"),
                       ("dfrtp_1", "EM_DFFR_X1"),
                       ("dlxtn_1", "EM_LATCH"),
                       ("o21ai_0", "EM_O21AI_X1")]:
        MASTER_MAP[_pre + _old] = _new

LAYER_MAP = {"metal1": "em1", "metal2": "em2", "metal3": "em3",
             "metal4": "em4", "metal5": "em5", "metal6": "em6",
             "metal7": "em7", "metal8": "em8", "metal9": "em9",
             "metal10": "em10", "metal99": "em99",
             "met1": "em1", "met2": "em2", "met3": "em3",
             "met5": "em5", "met99": "em99"}

# Instance-pin renames by mock master (sky130 naming deltas).
PIN_RENAME = {}
for _m in ("EM_DFF_X1", "EM_DFFR_X1", "EM_DFF_SLEW"):
    PIN_RENAME[_m] = {"CLK": "CK"}
for _m in ("EM_BUF_X1", "EM_BUF_X2", "EM_BUF_X4", "EM_BUF_X8",
           "EM_CLKBUF_X1", "EM_CLKBUF_X2"):
    PIN_RENAME[_m] = {"X": "Z"}
for _m in ("EM_INV_X1", "EM_INV_X2", "EM_INV_X4"):
    PIN_RENAME[_m] = {"X": "ZN", "Y": "ZN"}

FAITHFUL_SDC = {
    "repair_hold2.sdc": (
        "create_clock -period 2 clk\n"
        "set_input_delay -clock clk 0 [get_ports {in1 in2}]\n"
        "set_output_delay -clock clk -0.3 [get_ports out]\n"
        "set_propagated_clock clk\n"),
    "repair_hold3.sdc": (
        "create_clock -period 2 clk\n"
        "set_propagated_clock clk\n"),
}


def mock_macro_pins():
    """MACRO name -> set of PIN names, from the generated mock LEF."""
    pins = {}
    cur = None
    with open(os.path.join(HERE, "eos_mock.lef")) as f:
        for line in f:
            m = re.match(r"MACRO (\S+)", line)
            if m:
                cur = m.group(1)
                pins[cur] = set()
                continue
            m = re.match(r"  PIN (\S+)", line)
            if m and cur:
                pins[cur].add(m.group(1))
    return pins


def port_def(src_name, macros):
    """Port one DEF file; return the ported text."""
    with open(os.path.join(FIX, src_name)) as f:
        lines = f.read().splitlines(keepends=True)

    # Units: normalize to 2000 dbu/micron with exact scaling.
    units = None
    for line in lines:
        m = re.search(r"UNITS DISTANCE MICRONS (\d+)", line)
        if m:
            units = int(m.group(1))
            break
    if units is None:
        raise SystemExit("no UNITS line in %s" % src_name)
    if 2000 % units != 0:
        raise SystemExit("units %d do not divide 2000 in %s"
                         % (units, src_name))
    scale = 2000 // units

    out = []
    section = None
    dropping_row = False
    inst_master = {}
    for line in lines:
        if dropping_row:
            if ";" in line:
                dropping_row = False
            continue
        if re.match(r"\s*ROW\s", line):
            if ";" not in line:
                dropping_row = True
            continue
        m = re.match(r"(COMPONENTS|PINS|NETS|SPECIALNETS|DIEAREA)"
                     r"\b", line)
        if m:
            section = m.group(1)
        if line.startswith("END "):
            section = None
        if re.search(r"UNITS DISTANCE MICRONS", line):
            line = re.sub(r"MICRONS \d+", "MICRONS 2000", line)
        if section == "COMPONENTS":
            m = re.match(r"\s*- (\S+) (\S+)", line)
            if m:
                inst, master = m.group(1), m.group(2)
                if master not in MASTER_MAP:
                    raise SystemExit(
                        "%s: unmapped master %s (instance %s)"
                        % (src_name, master, inst))
                inst_master[inst] = MASTER_MAP[master]
                line = line.replace(" " + master + " ",
                                    " " + MASTER_MAP[master] + " ", 1)
        if section in ("DIEAREA", "COMPONENTS", "PINS") and scale != 1:
            def _scale(m):
                return "( %d %d )" % (int(m.group(1)) * scale,
                                      int(m.group(2)) * scale)
            line = re.sub(r"\(\s*(-?\d+)\s+(-?\d+)\s*\)", _scale, line)
        out.append(line)
    text = "".join(out)

    # NETS section: instance-pin renames + validation.
    def _nets_sub(m):
        head, body, tail = m.group(1), m.group(2), m.group(3)

        def _pair(p):
            inst, pin = p.group(1), p.group(2)
            if inst == "PIN":
                return p.group(0)
            if inst not in inst_master:
                raise SystemExit(
                    "%s: NETS references unknown instance %s"
                    % (src_name, inst))
            master = inst_master[inst]
            pin = PIN_RENAME.get(master, {}).get(pin, pin)
            if master != "PAD" and pin not in macros.get(master, set()):
                raise SystemExit(
                    "%s: pin %s not on mock macro %s (instance %s)"
                    % (src_name, pin, master, inst))
            return "( %s %s )" % (inst, pin)

        return head + re.sub(r"\(\s*(\S+)\s+(\S+)\s*\)", _pair, body) \
            + tail

    text = re.sub(r"(^NETS\b.*?^)(.*?)(^END NETS)",
                  _nets_sub, text, flags=re.M | re.S)

    # Layer names, word-boundary.
    for old, new in LAYER_MAP.items():
        text = re.sub(r"\b%s\b" % old, new, text)
    return text


def parse_tcl(path):
    """Return (header_comments, option_tokens) for a repair_*.tcl."""
    with open(path) as f:
        text = f.read()
    m = re.search(r"puts \[eos::repair_timing (.*?)\]", text, re.S)
    if m:
        return text, m.group(1).split()
    # Negative case (wire3): the command sits inside a catch block.
    # Anchor at line start so the header comment mentioning the
    # command name does not match first.
    m = re.search(r"^\s*eos::repair_timing (.*?)\n", text, re.M)
    if m:
        return text, m.group(1).split()
    return text, None


def port_script(case, tokens, use_pad):
    """Rewrite option tokens onto the mock files."""
    out = []
    i = 0
    files_done = False
    while i < len(tokens):
        tok = tokens[i]
        if tok in ("-lef", "-tlef", "-liberty"):
            i += 2
            if not files_done:
                out += ["-tlef", "mock/eos_mock.tlef",
                        "-lef", "mock/eos_mock.lef"]
                if use_pad:
                    out += ["-lef", "fixtures/pad.lef"]
                out += ["-liberty", "mock/eos_mock.lib"]
                files_done = True
            continue
        if tok == "-def":
            out += ["-def", "mock/" + os.path.basename(tokens[i + 1])]
            i += 2
            continue
        if tok == "-sdc":
            out += ["-sdc", "mock/" + os.path.basename(tokens[i + 1])]
            i += 2
            continue
        if tok in ("-wire_layer", "-wire_layer_clk"):
            out += [tok, LAYER_MAP[tokens[i + 1]]]
            i += 2
            continue
        if tok == "-buffer_cell":
            out += [tok, MASTER_MAP[tokens[i + 1]]]
            i += 2
            continue
        if tok == "-tie_port":
            cell, pin = tokens[i + 1].split("/", 1)
            out += [tok, MASTER_MAP[cell] + "/" + pin]
            i += 2
            continue
        out.append(tok)
        i += 1
    if not files_done:
        raise SystemExit("%s: no file option found" % case)
    if case in ("hold2", "hold3"):
        # Faithful port: propagated-clock SDC replaces ideal clock.
        out = [t for t in out]
        j = out.index("-clock")
        del out[j:j + 4]  # -clock clk -period 2
        k = out.index("-def") + 2
        out[k:k] = ["-sdc", "mock/" + CASE_SDC[case]]
    return out


def main():
    macros = mock_macro_pins()
    ported_defs = {}
    for case in PORTED:
        src_tcl = os.path.join(TESTS, "repair_%s.tcl" % case)
        _, tokens = parse_tcl(src_tcl)
        if tokens is None:
            raise SystemExit("%s: no puts line (special form?)" % case)
        src_def = CASE_DEF.get(case, "repair_%s.def" % case)
        if src_def not in ported_defs:
            ported_defs[src_def] = port_def(src_def, macros)
        with open(os.path.join(HERE, src_def), "w") as f:
            f.write(ported_defs[src_def])
        # SDC: verbatim, faithful hold2/hold3, hold5 pin rename.
        sdc_name = None
        if "-sdc" in tokens:
            sdc_name = os.path.basename(
                tokens[tokens.index("-sdc") + 1])
        elif case in CASE_SDC:
            sdc_name = CASE_SDC[case]
        if sdc_name:
            if sdc_name in FAITHFUL_SDC:
                sdc_text = FAITHFUL_SDC[sdc_name]
            else:
                with open(os.path.join(FIX, sdc_name)) as f:
                    sdc_text = f.read()
                if case == "hold5":
                    sdc_text = sdc_text.replace("r1/CLK", "r1/CK")
            with open(os.path.join(HERE, sdc_name), "w") as f:
                f.write(sdc_text)
        # Script.
        use_pad = "fixtures/pad.lef" in tokens
        new_tokens = port_script(case, tokens, use_pad)
        header = ("# repair_%s, mock port: loads the EOS-owned synthetic\n"
                  "# mock library (mock/eos_mock.*) instead of any real\n"
                  "# PDK file. Values are synthetic; the .ok pins mock\n"
                  "# behavior, not upstream parity. See mock/README.md.\n"
                  % case)
        if case == "wire3":
            body = ("if {[catch {\n"
                    "  eos::repair_timing %s\n"
                    "} msg]} {\n"
                    "  puts \"error=$msg\"\n"
                    "} else {\n"
                    "  puts \"status=unexpected-ok\"\n"
                    "}\n" % " ".join(new_tokens))
        else:
            body = "puts [eos::repair_timing %s]\n" % " ".join(new_tokens)
        with open(os.path.join(HERE, "repair_%s.tcl" % case), "w") as f:
            f.write(header + body)
        print("ported %s (def %s%s)" % (
            case, src_def,
            (", sdc %s" % sdc_name) if sdc_name else ""))


if __name__ == "__main__":
    main()
