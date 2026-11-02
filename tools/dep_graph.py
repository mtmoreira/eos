#!/usr/bin/env python3
"""EOS source dependency graph and affected-test selection.

The graph is built from the same two sources the plan names, so it
cannot drift from the build:

- the actual ``#include`` edges, resolved with the exact resolver
  from ``tools/check_layers.py`` (same file index, same
  own-first and reachability rules), so an edge here is an edge
  the layer checker sees;
- the module registry in ``structure.yaml`` (the module layer
  graph): module paths, layers, and targets.

One conservative addition: a module's ``CMakeLists.txt`` link
edges (``target_link_libraries`` naming another registered
``eda_*`` target) are also edges. A link edge can exist without
a direct include (for example, a module that only forwards a
linked library's types through its own public headers in a way
the include scan cannot see), and for test selection a missed
edge is a bug while an extra edge only costs a few more tests.
Edges are therefore a superset of the include edges, never a
subset.

The graph is checked in at ``tools/dependency-graph.json`` and
regenerated deterministically (sorted keys, sorted edge lists).
``check`` fails on any drift, on a layer-order violation (a
module depending on a higher layer, which ``check_layers.py``
also forbids), and on a test that links no registered module
(such a test would otherwise never be selected). Script-driven
entries are the exception that proves the rule: an add_test
block mentioning rsz/tests maps to [rsz, registry] (see
test_map), so the rsz Tcl suite entries are selected by a
change to either module instead of being flagged. CI runs
``check`` in the structure job; the build job uses ``select``
on pull requests for the fast path, and always runs the full
suite on push events (including merge commits).

Selection rules (``select``), in order:

- A changed file under a module path selects that module.
- A changed file under ``tests/`` selects the tests built from
  that directory (a test source change affects only its own
  tests); an unrecognized file under ``tests/`` selects the
  whole suite.
- A changed file that is build-affecting (any
  ``CMakeLists.txt``, ``structure.yaml``, the workflow files,
  ``third_party_manifest.yaml``, anything under ``cmake/`` or
  the top-level ``include/``) selects the whole suite.
- Anything else (docs, ``tools/``, ``scripts/``, ``apps/``,
  ``platforms/``) selects no CTest tests: docs and apps cannot
  change library behaviour, and tool changes are already gated
  by the structure job's pytest run plus this script's
  ``check`` step.
- The affected modules are the changed modules plus every
  module that transitively depends on them. A test is selected
  when any module it links is affected, plus any test selected
  directly by a changed test file.
- A change to a foundation (L0) module selects the whole suite.
  L0 is what every layer builds on, including through paths
  the include scan cannot see (build glue, adopted-engine
  configuration); the graph must not pretend a foundation
  change is small. This is a conservative rule on top of the
  closure, not a claim about the closure's size.

Usage:

    python3 tools/dep_graph.py generate [--root PATH] [--out PATH]
    python3 tools/dep_graph.py check [--root PATH] [--graph PATH]
    python3 tools/dep_graph.py select [--root PATH] [--graph PATH]
        [--files-from FILE] [--ctest-regex] [FILE ...]

``select`` with no files selects nothing (exit 0). ``--ctest-regex``
prints one ``^(a|b)$`` regex for ``ctest -R``; an empty selection
prints ``a^``, which matches no test, so ``ctest`` with
``--no-tests=ignore`` exits 0. Exit code 0 means the command
succeeded; it says nothing about whether tests pass.
"""

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_layers  # noqa: E402

LINK_BLOCK_RE = re.compile(
    r"target_link_libraries\s*\((.*?)\)", re.DOTALL)
TARGET_TOKEN_RE = re.compile(r"eda_[A-Za-z0-9_]+")
ADD_EXE_RE = re.compile(
    r"add_executable\s*\(\s*(\S+)\s+([^)]*)\)", re.DOTALL)
ADD_TEST_RE = re.compile(
    r"add_test\s*\(\s*NAME\s+(\S+)\s+COMMAND\s+([^\s)]+)", re.DOTALL)
GRAPH_NAME = "dependency-graph.json"

# Files whose change cannot affect any CTest test. Tools changes are
# gated by the structure job (pytest plus `dep_graph.py check`);
# docs, app, and script changes cannot change library behaviour.
NO_TEST_PREFIXES = ("docs/", "tools/", "scripts/", "apps/",
                     "platforms/")
# Files whose change can affect the build of everything.
WHOLE_SUITE_NAMES = {"structure.yaml", "third_party_manifest.yaml",
                     GRAPH_NAME}
WHOLE_SUITE_PREFIXES = (".github/", "cmake/", "include/")
WHOLE_SUITE_SUFFIXES = ("CMakeLists.txt", ".cmake")


def load_graph_file(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def dump_graph(graph):
    return json.dumps(graph, indent=2, sort_keys=True) + "\n"


def module_edges(root, reg):
    """Return {module name: {path, layer, depends_on, via}}."""
    index = check_layers.build_file_index(root, reg)
    by_target = {m["target"]: m for m in reg.modules}
    edges = {}
    for mod in reg.modules:
        deps = {}
        base = root / mod["path"]
        for sub in ("include", "src", "internal"):
            folder = base / sub
            if not folder.is_dir():
                continue
            for path in sorted(folder.rglob("*")):
                if not (path.is_file()
                        and path.suffix in check_layers.SOURCE_EXTS):
                    continue
                try:
                    text = path.read_text(encoding="utf-8",
                                          errors="replace")
                except OSError:
                    continue
                for line in text.splitlines():
                    match = check_layers.INCLUDE_RE.match(line)
                    if not match:
                        continue
                    candidates = check_layers.resolve_include(
                        index, match.group(1), mod)
                    own = [hit for hit in candidates
                           if hit[0] is mod]
                    if own:
                        candidates = own
                    for owner, _sub, _path in candidates:
                        if owner is mod:
                            continue
                        deps.setdefault(owner["name"], set()).add(
                            "include")
        cmake = base / "CMakeLists.txt"
        if cmake.is_file():
            text = cmake.read_text(encoding="utf-8", errors="replace")
            for block in LINK_BLOCK_RE.findall(text):
                for token in TARGET_TOKEN_RE.findall(block):
                    owner = by_target.get(token)
                    if owner is not None and owner is not mod:
                        deps.setdefault(owner["name"], set()).add(
                            "cmake-link")
        edges[mod["name"]] = {
            "path": mod["path"],
            "layer": mod["layer"],
            "depends_on": sorted(deps),
            "via": {name: sorted(methods)
                    for name, methods in sorted(deps.items())},
        }
    return edges


def add_test_blocks(text):
    """Yield (name, block text) for each add_test(...) call.

    Paren matching is string-aware (double quotes, backslash
    escapes) and skips CMake line comments (# to end of line)
    and bracket comments (#[[ ... ]]) outside strings, so a
    paren inside either never extends the block. Used for
    script-driven entries, which the exe-link scan cannot
    see: their COMMAND is an interpreter, not a test exe.
    """
    blocks = []
    for m in re.finditer(r"add_test\s*\(\s*NAME\s+(\S+)", text):
        name = m.group(1)
        depth = 0
        in_string = False
        j = m.start()
        # Find the opening paren of the call.
        while j < len(text) and text[j] != "(":
            j += 1
        start = j
        while j < len(text):
            c = text[j]
            if in_string:
                if c == "\\":
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
            if c == "#":
                if text.startswith("#[[", j):
                    stop = text.find("]]", j + 3)
                    j = len(text) if stop < 0 else stop + 2
                else:
                    stop = text.find("\n", j)
                    j = len(text) if stop < 0 else stop + 1
                continue
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    j += 1
                    break
            j += 1
        blocks.append((name, text[start:j]))
    return blocks


def test_map(root, reg):
    """Return {ctest name: {modules, sources}} from test CMakeLists.

    A test's modules are the registered modules its executable
    links. ``sources`` are the test's source files relative to
    ``tests/`` (for example ``unit/foundation/ids/ids_test.cc``),
    used to map a changed test file to its own tests.

    Script-driven entries (COMMAND is an interpreter, not a
    test exe) link no module, so ``check`` would flag them as
    unselected. The rsz Tcl suite entries are that shape:
    an add_test block mentioning rsz/tests maps to modules
    [rsz, registry] (the engine under test plus the command
    layer driving it), unioned with any exe-link modules
    the entry already has.
    """
    by_target = {m["target"]: m for m in reg.modules}
    tests = {}
    for rel in ("unit/CMakeLists.txt", "qualification/CMakeLists.txt"):
        cmake = root / "tests" / rel
        if not cmake.is_file():
            continue
        text = cmake.read_text(encoding="utf-8", errors="replace")
        exe_sources = {}
        for exe, sources in ADD_EXE_RE.findall(text):
            exe_sources[exe] = [
                (Path("tests") / rel).parent.joinpath(
                    s.strip()).as_posix()
                for s in sources.split() if s.strip()]
        exe_links = {}
        for exe, block in re.findall(
                r"target_link_libraries\s*\(\s*(\S+)\s+([^)]*)\)",
                text, re.DOTALL):
            mods = sorted({by_target[t]["name"]
                           for t in TARGET_TOKEN_RE.findall(block)
                           if t in by_target})
            exe_links[exe] = mods
        for name, exe in ADD_TEST_RE.findall(text):
            tests[name] = {
                "modules": exe_links.get(exe, []),
                "sources": exe_sources.get(exe, []),
            }
        for name, block in add_test_blocks(text):
            if "rsz/tests" not in block:
                continue
            entry = tests.setdefault(name, {"modules": [], "sources": []})
            entry["modules"] = sorted(
                set(entry["modules"]) | {"registry", "rsz"})
    return tests


def generate(root):
    reg = check_layers.load_registry(root)
    return {
        "generator": "tools/dep_graph.py",
        "modules": module_edges(root, reg),
        "tests": test_map(root, reg),
    }


def layer_violations(graph):
    """Edges that point at a higher layer than the dependent."""
    reg_levels = {}
    # Layer levels come from structure.yaml order: L0 < L1 < ...
    # The numeric suffix of the layer id is its level in this repo.
    violations = []
    for name, mod in graph["modules"].items():
        own = int(mod["layer"][1:])
        for dep in mod["depends_on"]:
            dep_mod = graph["modules"].get(dep)
            if dep_mod is not None and int(dep_mod["layer"][1:]) > own:
                violations.append(f"{name} depends on higher-layer "
                                  f"{dep}")
    return violations


def check(root, graph_path):
    checked_in = load_graph_file(graph_path)
    fresh = generate(root)
    problems = []
    if checked_in != fresh:
        for name in sorted(set(checked_in.get("modules", {}))
                           | set(fresh["modules"])):
            if checked_in.get("modules", {}).get(name) != \
                    fresh["modules"].get(name):
                problems.append(f"module {name} differs from "
                                f"regeneration")
        for name in sorted(set(checked_in.get("tests", {}))
                           | set(fresh["tests"])):
            if checked_in.get("tests", {}).get(name) != \
                    fresh["tests"].get(name):
                problems.append(f"test {name} differs from "
                                f"regeneration")
        if not problems:
            problems.append("graph differs from regeneration "
                            "(top-level fields)")
    problems.extend(layer_violations(fresh))
    for name, test in fresh["tests"].items():
        if not test["modules"]:
            problems.append(f"test {name} links no registered "
                            f"module and would never be selected")
    return problems


def dependents_map(graph):
    dependents = {name: set() for name in graph["modules"]}
    for name, mod in graph["modules"].items():
        for dep in mod["depends_on"]:
            if dep in dependents:
                dependents[dep].add(name)
    return dependents


def classify_files(files, graph):
    """Return (changed modules, direct tests, whole_suite flag)."""
    by_path = {m["path"]: name for name, m in graph["modules"].items()}
    changed = set()
    direct_tests = set()
    whole = False
    test_source_dirs = {}
    for name, test in graph["tests"].items():
        for src in test["sources"]:
            test_source_dirs.setdefault(
                str(Path(src).parent), set()).add(name)
    for raw in files:
        rel = raw.strip()
        if not rel:
            continue
        rel = rel.lstrip("./")
        # Build-affecting files first: a CMakeLists.txt under a
        # module path is still a build change, not a module change.
        name = Path(rel).name
        if name in WHOLE_SUITE_NAMES or rel.startswith(
                WHOLE_SUITE_PREFIXES) or rel.endswith(
                WHOLE_SUITE_SUFFIXES):
            whole = True
            continue
        if rel.startswith("src/"):
            # Longest matching module path wins (nested modules
            # such as src/analysis/timing/sta under timing).
            best = None
            for path, name in by_path.items():
                if rel == path or rel.startswith(path + "/"):
                    if best is None or len(path) > len(best[0]):
                        best = (path, name)
            if best is not None:
                changed.add(best[1])
            else:
                whole = True
            continue
        if rel.startswith("tests/"):
            parent = str(Path(rel).parent)
            if parent in test_source_dirs:
                direct_tests.update(test_source_dirs[parent])
            else:
                whole = True
            continue
        if rel.startswith(NO_TEST_PREFIXES) or rel.endswith(".md"):
            continue
        whole = True
    return changed, direct_tests, whole


def select_tests(graph, files):
    """Return the sorted selected CTest names for changed files."""
    all_tests = sorted(graph["tests"])
    changed, direct, whole = classify_files(files, graph)
    if whole:
        return all_tests
    dependents = dependents_map(graph)
    affected = set(changed)
    queue = list(changed)
    while queue:
        current = queue.pop()
        for dependent in dependents.get(current, ()):
            if dependent not in affected:
                affected.add(dependent)
                queue.append(dependent)
    foundation = {name for name, mod in graph["modules"].items()
                  if mod["layer"] == "L0"}
    if changed & foundation:
        return all_tests
    selected = set(direct)
    for name, test in graph["tests"].items():
        if set(test["modules"]) & affected:
            selected.add(name)
    return sorted(selected)


def ctest_regex(names):
    if not names:
        return "a^"
    return "^(" + "|".join(re.escape(n) for n in names) + ")$"


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="EOS source dependency graph and test selection")
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--root", default=None,
                        help="repository root (default: parent of "
                             "this tools directory)")
    common.add_argument("--graph", default=None,
                        help="checked-in graph path (default: "
                             "tools/dependency-graph.json under root)")
    sub = parser.add_subparsers(dest="command", required=True)
    gen = sub.add_parser("generate", parents=[common],
                         help="regenerate the graph")
    gen.add_argument("--out", default=None,
                     help="output path (default: stdout)")
    sub.add_parser("check", parents=[common],
                   help="verify the checked-in graph matches a "
                        "regeneration")
    sel = sub.add_parser("select", parents=[common],
                         help="select affected tests for changed files")
    sel.add_argument("--files-from", default=None,
                     help="read changed files from this file (\"-\" "
                          "for stdin), one per line")
    sel.add_argument("--ctest-regex", action="store_true",
                     help="print one ctest -R regex instead of test "
                          "names")
    sel.add_argument("files", nargs="*", help="changed files")
    args = parser.parse_args(argv)
    root = Path(args.root).resolve() if args.root else \
        Path(__file__).resolve().parent.parent
    graph_path = Path(args.graph).resolve() if args.graph else \
        root / "tools" / GRAPH_NAME
    if not (root / "structure.yaml").is_file():
        print(f"dep_graph: no structure.yaml at {root}",
              file=sys.stderr)
        return 2
    if args.command == "generate":
        text = dump_graph(generate(root))
        if args.out:
            Path(args.out).write_text(text, encoding="utf-8")
        else:
            sys.stdout.write(text)
        return 0
    if args.command == "check":
        if not graph_path.is_file():
            print(f"dep_graph: no checked-in graph at {graph_path}",
                  file=sys.stderr)
            return 2
        problems = check(root, graph_path)
        if problems:
            for problem in problems:
                print(f"dep_graph: {problem}")
            print(f"dep_graph: {len(problems)} problem(s); "
                  f"regenerate with: python3 tools/dep_graph.py "
                  f"generate --out tools/{GRAPH_NAME}")
            return 1
        graph = load_graph_file(graph_path)
        print(f"dep_graph: OK ({len(graph['modules'])} modules, "
              f"{len(graph['tests'])} tests)")
        return 0
    # select
    files = list(args.files)
    if args.files_from:
        if args.files_from == "-":
            files.extend(line.strip() for line in sys.stdin)
        else:
            files.extend(Path(args.files_from).read_text(
                encoding="utf-8").splitlines())
    if not graph_path.is_file():
        print(f"dep_graph: no checked-in graph at {graph_path}",
              file=sys.stderr)
        return 2
    graph = load_graph_file(graph_path)
    names = select_tests(graph, files)
    if args.ctest_regex:
        print(ctest_regex(names))
    else:
        for name in names:
            print(name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
