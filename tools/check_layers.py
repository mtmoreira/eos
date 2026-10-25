#!/usr/bin/env python3
"""EOS structural checker.

Enforces the layer and module rules from the EOS Code Structure document
against the registry in structure.yaml at the repository root:

- every registered module exists on disk with the standard anatomy
  (README.md, include/, src/, internal/, tests/, and PROVENANCE.md for
  adopted modules);
- every module directory on disk (identified by a CMakeLists.txt that
  declares an eda_* library target) is registered;
- a module path sits under one of its layer's roots, and its
  CMakeLists.txt declares the registered target name;
- the include graph respects the layers: no upward dependency, no
  include of another module's internal/ headers, and no public header
  (under include/) including any internal/ header.

Include resolution convention: a quoted include is resolved by finding
the module that owns a matching file under its include/, src/ or
internal/ directory. When the including module itself owns a matching
file, only its own files are candidates: a quoted include resolves in
the including file's own directories first, exactly as the compiler
resolves it, so a same-named header in another module is not the file
being included. Unresolvable quoted includes are ignored in this
version (system and third-party headers are not the checker's business).

Usage: check_layers.py [--root PATH]   (default root: the repository
root, derived from this file's location). Exit code 0 means clean.
"""
import argparse
import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:  # pragma: no cover
    print("check_layers: PyYAML is required (pip install pyyaml)", file=sys.stderr)
    raise SystemExit(2)

SOURCE_EXTS = {".h", ".hh", ".hpp", ".c", ".cc", ".cpp", ".cxx"}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')
TARGET_RE = re.compile(r"add_library\((eda_[A-Za-z0-9_]+)")


class Registry:
    def __init__(self, data):
        self.layers = {}
        for layer in data.get("layers", []):
            self.layers[layer["id"]] = layer
        self.modules = []
        for mod in data.get("modules", []):
            mod = dict(mod)
            mod["level"] = self.layers[mod["layer"]]["level"]
            self.modules.append(mod)
        self.by_path = {m["path"]: m for m in self.modules}


def load_registry(root):
    with open(root / "structure.yaml", "r", encoding="utf-8") as fh:
        return Registry(yaml.safe_load(fh))


def check_registry(reg, violations):
    levels = [layer["level"] for layer in reg.layers.values()]
    if sorted(levels) != list(range(len(levels))):
        violations.append("layer levels must be a contiguous range starting at 0")
    paths = [m["path"] for m in reg.modules]
    if len(paths) != len(set(paths)):
        violations.append("duplicate module path in structure.yaml")
    targets = [m["target"] for m in reg.modules]
    if len(targets) != len(set(targets)):
        violations.append("duplicate module target in structure.yaml")


def check_anatomy(root, reg, violations):
    for mod in reg.modules:
        base = root / mod["path"]
        if not base.is_dir():
            violations.append(f"{mod['path']}: module directory missing")
            continue
        for need in ("README.md",):
            if not (base / need).is_file():
                violations.append(f"{mod['path']}: missing {need}")
        for need in ("include", "src", "internal", "tests"):
            if not (base / need).is_dir():
                violations.append(f"{mod['path']}: missing {need}/ directory")
        if mod.get("adopted") and not (base / "PROVENANCE.md").is_file():
            violations.append(f"{mod['path']}: adopted module missing PROVENANCE.md")
        cmake = base / "CMakeLists.txt"
        if not cmake.is_file():
            violations.append(f"{mod['path']}: missing CMakeLists.txt")
        else:
            found = TARGET_RE.search(cmake.read_text(encoding="utf-8"))
            if not found:
                violations.append(f"{mod['path']}: CMakeLists.txt declares no eda_* target")
            elif found.group(1) != mod["target"]:
                violations.append(
                    f"{mod['path']}: CMakeLists.txt declares {found.group(1)}, "
                    f"structure.yaml says {mod['target']}")
        roots = reg.layers[mod["layer"]]["roots"]
        if not any(mod["path"] == r or mod["path"].startswith(r + "/") for r in roots):
            violations.append(
                f"{mod['path']}: path is not under any root of layer {mod['layer']} {roots}")


def check_unregistered(root, reg, violations):
    src_root = root / "src"
    if not src_root.is_dir():
        return
    for cmake in sorted(src_root.rglob("CMakeLists.txt")):
        mod_dir = cmake.parent.relative_to(root).as_posix()
        if TARGET_RE.search(cmake.read_text(encoding="utf-8")) and mod_dir not in reg.by_path:
            violations.append(f"{mod_dir}: module on disk is not registered in structure.yaml")


def build_file_index(root, reg):
    """Map every source file, relative to its owning module, for include resolution."""
    index = {}
    for mod in reg.modules:
        base = root / mod["path"]
        for sub in ("include", "src", "internal"):
            folder = base / sub
            if not folder.is_dir():
                continue
            for path in folder.rglob("*"):
                if path.is_file() and path.suffix in SOURCE_EXTS:
                    rel = path.relative_to(base).as_posix()
                    index.setdefault(rel, []).append((mod, sub, path))
                    index.setdefault(path.name, []).append((mod, sub, path))
    return index


def resolve_include(index, inc):
    """Find candidate owner files for a quoted include string."""
    candidates = []
    for key in (inc, Path(inc).name):
        for hit in index.get(key, []):
            if hit not in candidates:
                candidates.append(hit)
    # Prefer an exact relative-path match when one exists.
    exact = [hit for hit in candidates if hit[2].as_posix().endswith("/" + inc)]
    return exact if exact else candidates


def check_includes(root, reg, violations):
    index = build_file_index(root, reg)
    for mod in reg.modules:
        base = root / mod["path"]
        for sub in ("include", "src", "internal"):
            folder = base / sub
            if not folder.is_dir():
                continue
            for path in sorted(folder.rglob("*")):
                if not (path.is_file() and path.suffix in SOURCE_EXTS):
                    continue
                for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
                    match = INCLUDE_RE.match(line)
                    if not match:
                        continue
                    inc = match.group(1)
                    candidates = resolve_include(index, inc)
                    # A quoted include resolves inside the including
                    # module first (the compiler searches the including
                    # file's own directories before other include
                    # paths), so when this module owns a header with
                    # this name, same-named headers in other modules
                    # are not candidates and cannot cause spurious
                    # cross-module findings.
                    own = [hit for hit in candidates if hit[0] is mod]
                    if own:
                        candidates = own
                    for owner, owner_sub, _owner_path in candidates:
                        where = path.relative_to(root).as_posix()
                        if owner is mod:
                            if sub == "include" and owner_sub == "internal":
                                violations.append(
                                    f"{where}: public header includes an internal header ({inc})")
                            continue
                        if owner["level"] > mod["level"]:
                            violations.append(
                                f"{where}: upward dependency, {mod['layer']} includes "
                                f"{owner['layer']} header ({inc})")
                        elif owner_sub == "internal":
                            violations.append(
                                f"{where}: includes another module's internal header ({inc})")


def main(argv=None):
    parser = argparse.ArgumentParser(description="EOS layer and structure checker")
    parser.add_argument("--root", default=None,
                        help="repository root (default: parent of this tools directory)")
    args = parser.parse_args(argv)
    root = Path(args.root).resolve() if args.root else Path(__file__).resolve().parent.parent
    if not (root / "structure.yaml").is_file():
        print(f"check_layers: no structure.yaml at {root}", file=sys.stderr)
        return 2
    reg = load_registry(root)
    violations = []
    check_registry(reg, violations)
    check_anatomy(root, reg, violations)
    check_unregistered(root, reg, violations)
    check_includes(root, reg, violations)
    if violations:
        for violation in violations:
            print(f"VIOLATION: {violation}")
        print(f"check_layers: {len(violations)} violation(s)")
        return 1
    print(f"check_layers: OK ({len(reg.modules)} modules, {len(reg.layers)} layers)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
