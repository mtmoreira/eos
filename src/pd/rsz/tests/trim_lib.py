#!/usr/bin/env python3
"""Trim a Liberty library to the cells a DEF set needs (PR4 suite).

EOS-owned. Keeps every non-cell top-level group verbatim
(templates, operating conditions, header comments, including
any licence header) and drops only non-selected cell groups
by brace matching. Trimming does NOT change a file's licence
status and never adds or removes licence text: a trimmed
Nangate45 library is still proprietary, and a trimmed file
whose source carried no licence text still carries none.
Read the source file's header before any PR includes a
trimmed file.

Keep set = DEF-referenced masters, plus drive-strength
variants of their families, plus buffer / inverter /
clock-buffer / tie families (Nangate45: BUF_X*, INV_X*,
CLKBUF_X*, LOGIC1_X*; sky130: __buf_*, __inv_*,
__clkbuf_*, __conb_*). Extra cells can be forced with
--keep-extra.

Usage:
  trim_lib.py --lib IN.lib --defs A.def [B.def ...] --out OUT.lib
      [--keep-extra CELL ...] [--cells-out KEPT.txt] [--report]

The scanner is string-aware (double-quoted names, backslash
escapes) and comment-aware (/* */ skipped outside strings),
so braces inside strings or comments never confuse the
brace matcher. Input is never modified.
"""
import argparse
import re
import sys

CELL_RE = re.compile(r'cell\s*\(\s*"?([^"\s\)]+)"?\s*\)\s*\{')
BUF_SUFFIXES = ("buf", "inv", "clkbuf", "clkinv", "conb")
TIE_FAMILIES = ("logic0", "logic1")


def family_key(cell):
    """Strip the drive-strength suffix: BUF_X1 -> BUF, __buf_1 -> __buf."""
    key = re.sub(r'_X\d+$', '', cell)
    key = re.sub(r'_\d+$', '', key)
    return key


def is_buffer_or_tie_family(key):
    low = key.lower()
    return low.endswith(BUF_SUFFIXES) or low in TIE_FAMILIES


def scan_segments(text):
    """Split a Liberty file into top-level segments.

    Returns (prologue, segments, epilogue) where prologue is
    everything through the library group's opening brace,
    segments is a list of (kind, name, text) with kind in
    {"cell", "other"}, and epilogue is the closing brace
    plus trailing text. The scan tracks string state and
    skips /* */ comments outside strings, so braces inside
    either never affect depth.
    """
    n = len(text)
    i = 0
    depth = 0
    in_string = False
    # Find the library group's opening brace.
    while i < n:
        c = text[i]
        if in_string:
            if c == '\\':
                i += 2
                continue
            if c == '"':
                in_string = False
            i += 1
            continue
        if c == '"':
            in_string = True
            i += 1
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            end = text.find('*/', i + 2)
            i = n if end < 0 else end + 2
            continue
        if c == '{':
            depth = 1
            i += 1
            break
        i += 1
    prologue = text[:i]
    segments = []
    # Scan depth-1 items until the library closing brace.
    while i < n:
        # Skip whitespace and comments between segments.
        while i < n:
            c = text[i]
            if c in ' \t\r\n':
                i += 1
                continue
            if c == '/' and i + 1 < n and text[i + 1] == '*':
                end = text.find('*/', i + 2)
                i = n if end < 0 else end + 2
                continue
            break
        if i >= n:
            break
        if text[i] == '}':
            break
        start = i
        # Find the segment end: a group (brace-matched) or a
        # simple statement (ending at ';'), string-aware.
        j = i
        in_string = False
        seg_depth = 0
        end = None
        while j < n:
            c = text[j]
            if in_string:
                if c == '\\':
                    j += 2
                    continue
                if c == '"':
                    in_string = False
                j += 1
                continue
            if c == '"':
                in_string = True
                j += 1
                continue
            if c == '/' and j + 1 < n and text[j + 1] == '*':
                stop = text.find('*/', j + 2)
                j = n if stop < 0 else stop + 2
                continue
            if c == '{':
                seg_depth += 1
                j += 1
                continue
            if c == '}':
                seg_depth -= 1
                j += 1
                if seg_depth == 0:
                    end = j
                    break
                continue
            if c == ';' and seg_depth == 0:
                end = j + 1
                break
            j += 1
        if end is None:
            end = n
        seg_text = text[start:end]
        m = CELL_RE.match(seg_text)
        if m:
            segments.append(("cell", m.group(1), seg_text))
        else:
            segments.append(("other", None, seg_text))
        i = end
    epilogue = text[i:]
    return prologue, segments, epilogue


def def_masters(def_text):
    """Cell masters referenced by a DEF COMPONENTS section."""
    masters = set()
    in_components = False
    for line in def_text.splitlines():
        s = line.strip()
        if s.startswith("COMPONENTS"):
            in_components = True
            continue
        if s.startswith("END COMPONENTS"):
            in_components = False
            continue
        if in_components:
            m = re.match(r'-\s+(\S+)\s+(\S+)', s)
            if m:
                masters.add(m.group(2))
    return masters


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--lib", required=True)
    ap.add_argument("--defs", nargs="+", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--keep-extra", nargs="*", default=[])
    ap.add_argument("--cells-out", default=None)
    ap.add_argument("--report", action="store_true")
    args = ap.parse_args()

    with open(args.lib, encoding="utf-8", errors="replace") as f:
        text = f.read()
    prologue, segments, epilogue = scan_segments(text)
    all_cells = [name for kind, name, _ in segments if kind == "cell"]
    if not all_cells:
        print("trim_lib: no cell groups found in %s" % args.lib,
              file=sys.stderr)
        return 1

    referenced = set()
    for def_path in args.defs:
        with open(def_path, encoding="utf-8",
                  errors="replace") as f:
            referenced |= def_masters(f.read())
    missing = sorted(c for c in referenced if c not in set(all_cells))
    if missing:
        print("trim_lib: DEF masters missing from library: %s"
              % ", ".join(missing), file=sys.stderr)
        return 1

    families = {family_key(c) for c in referenced}
    keep = set()
    for cell in all_cells:
        key = family_key(cell)
        if cell in referenced or key in families:
            keep.add(cell)
        elif is_buffer_or_tie_family(key):
            keep.add(cell)
    keep |= set(args.keep_extra)
    keep &= set(all_cells)

    out_parts = [prologue]
    for kind, name, seg_text in segments:
        if kind == "cell" and name not in keep:
            continue
        out_parts.append(seg_text)
    out_parts.append(epilogue)
    out_text = "".join(out_parts)
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(out_text)
    if args.cells_out:
        with open(args.cells_out, "w", encoding="utf-8") as f:
            for cell in sorted(keep):
                f.write(cell + "\n")
    if args.report:
        print("input cells: %d" % len(all_cells))
        print("referenced masters: %d" % len(referenced))
        print("kept cells: %d" % len(keep))
        print("dropped cells: %d" % (len(all_cells) - len(keep)))
        print("input bytes: %d" % len(text.encode("utf-8")))
        print("output bytes: %d" % len(out_text.encode("utf-8")))
        print("kept: %s" % ", ".join(sorted(keep)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
