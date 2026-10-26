"""Unit tests for tools/check_layers.py.

Each test builds a tiny fixture repository in a temporary directory and
runs the checker against it as a subprocess, so the tests exercise the
real command line, exit codes and messages.
"""
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CHECKER = REPO / "tools" / "check_layers.py"

LAYERS_YAML = """\
layers:
  - id: L0
    level: 0
    name: foundation
    roots: [src/foundation]
  - id: L1
    level: 1
    name: interface
    roots: [src/ui]
modules:
"""


def write_module(root, name, layer, path, target, adopted=False, provenance=True):
    base = root / path
    (base / "include").mkdir(parents=True)
    (base / "src").mkdir(parents=True)
    (base / "internal").mkdir(parents=True)
    (base / "tests").mkdir(parents=True)
    (base / "README.md").write_text(f"# {name}\n", encoding="utf-8")
    (base / "CMakeLists.txt").write_text(
        f"add_library({target} INTERFACE)\n", encoding="utf-8")
    if adopted and provenance:
        (base / "PROVENANCE.md").write_text("# Provenance\n", encoding="utf-8")


def make_repo(tmp_path, modules_yaml, build):
    (tmp_path / "structure.yaml").write_text(LAYERS_YAML + modules_yaml, encoding="utf-8")
    build(tmp_path)
    return tmp_path


def run_checker(root):
    return subprocess.run(
        [sys.executable, str(CHECKER), "--root", str(root)],
        capture_output=True, text=True)


ONE_MODULE = """\
  - name: types
    layer: L0
    path: src/foundation/types
    target: eda_foundation_types
    adopted: false
"""


def build_one(root):
    write_module(root, "types", "L0", "src/foundation/types", "eda_foundation_types")


def test_valid_tree_passes(tmp_path):
    repo = make_repo(tmp_path, ONE_MODULE, build_one)
    result = run_checker(repo)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "OK" in result.stdout


def test_missing_tests_dir_fails(tmp_path):
    def build(root):
        build_one(root)
        tests_dir = root / "src/foundation/types/tests"
        tests_dir.rmdir()
    repo = make_repo(tmp_path, ONE_MODULE, build)
    result = run_checker(repo)
    assert result.returncode == 1
    assert "tests" in result.stdout


def test_unregistered_module_fails(tmp_path):
    def build(root):
        build_one(root)
        write_module(root, "rogue", "L0", "src/foundation/rogue", "eda_foundation_rogue")
    repo = make_repo(tmp_path, ONE_MODULE, build)
    result = run_checker(repo)
    assert result.returncode == 1
    assert "not registered" in result.stdout


def test_adopted_without_provenance_fails(tmp_path):
    modules = """\
  - name: sta
    layer: L1
    path: src/ui/sta
    target: eda_ui_sta
    adopted: true
"""
    def build(root):
        write_module(root, "sta", "L1", "src/ui/sta", "eda_ui_sta", adopted=True, provenance=False)
    repo = make_repo(tmp_path, modules, build)
    result = run_checker(repo)
    assert result.returncode == 1
    assert "PROVENANCE" in result.stdout


TWO_MODULES = """\
  - name: types
    layer: L0
    path: src/foundation/types
    target: eda_foundation_types
    adopted: false
  - name: tcl
    layer: L1
    path: src/ui/tcl
    target: eda_ui_tcl
    adopted: false
"""


def build_two(root):
    write_module(root, "types", "L0", "src/foundation/types", "eda_foundation_types")
    write_module(root, "tcl", "L1", "src/ui/tcl", "eda_ui_tcl")
    (root / "src/foundation/types/include/types.h").write_text("", encoding="utf-8")
    (root / "src/ui/tcl/include/tcl.h").write_text("", encoding="utf-8")
    (root / "src/ui/tcl/internal/secret.h").write_text("", encoding="utf-8")


def test_upward_include_fails(tmp_path):
    def build(root):
        build_two(root)
        (root / "src/foundation/types/src/types.cc").write_text(
            '#include "tcl.h"\n', encoding="utf-8")
    repo = make_repo(tmp_path, TWO_MODULES, build)
    result = run_checker(repo)
    assert result.returncode == 1
    assert "upward" in result.stdout


THREE_MODULES = TWO_MODULES + """\
  - name: python
    layer: L1
    path: src/ui/python
    target: eda_ui_python
    adopted: false
"""


def test_same_named_own_header_shadows_other_module(tmp_path):
    # Regression: an L0 module that owns util.h and includes "util.h"
    # must not be flagged for the same-named header owned by an L1
    # module; the quoted include resolves to its own header first.
    def build(root):
        build_two(root)
        (root / "src/foundation/types/include/util.h").write_text(
            "", encoding="utf-8")
        (root / "src/ui/tcl/include/util.h").write_text("", encoding="utf-8")
        (root / "src/foundation/types/src/types.cc").write_text(
            '#include "util.h"\n', encoding="utf-8")
    repo = make_repo(tmp_path, TWO_MODULES, build)
    result = run_checker(repo)
    assert result.returncode == 0, result.stdout + result.stderr


def test_path_include_does_not_match_unrelated_basename(tmp_path):
    # Regression (Phase 2 unit 2.6): an L0 module that includes the
    # system header "boost/random.hpp" must not be flagged for a
    # same-named file at an unrelated path inside an L1 module
    # (ABC bundles "sat/cadical/random.hpp"); a path-ful include
    # only matches a file at that path.
    def build(root):
        build_two(root)
        deep = root / "src/ui/tcl/src/sat/cadical"
        deep.mkdir(parents=True)
        (deep / "random.hpp").write_text("", encoding="utf-8")
        (root / "src/foundation/types/src/types.cc").write_text(
            '#include "boost/random.hpp"\n', encoding="utf-8")
    repo = make_repo(tmp_path, TWO_MODULES, build)
    result = run_checker(repo)
    assert result.returncode == 0, result.stdout + result.stderr


def test_bundled_src_header_does_not_shadow_system_include(tmp_path):
    # Regression (Phase 2 unit 2.6): an L0 module that includes the
    # system header "zlib.h" must not be flagged for the copy of
    # zlib.h bundled deep inside an L1 module's src/ tree (ABC's
    # src/misc/zlib/zlib.h); a bare name reaches another module's
    # include/ or internal/ surface, never its src/ tree.
    def build(root):
        build_two(root)
        deep = root / "src/ui/tcl/src/misc/zlib"
        deep.mkdir(parents=True)
        (deep / "zlib.h").write_text("", encoding="utf-8")
        (root / "src/foundation/types/src/types.cc").write_text(
            '#include "zlib.h"\n', encoding="utf-8")
    repo = make_repo(tmp_path, TWO_MODULES, build)
    result = run_checker(repo)
    assert result.returncode == 0, result.stdout + result.stderr


def test_sideways_internal_include_fails(tmp_path):
    def build(root):
        build_two(root)
        write_module(root, "python", "L1", "src/ui/python", "eda_ui_python")
        (root / "src/ui/python/src/python.cc").write_text(
            '#include "secret.h"\n', encoding="utf-8")
    repo = make_repo(tmp_path, THREE_MODULES, build)
    result = run_checker(repo)
    assert result.returncode == 1
    assert "another module's internal header" in result.stdout


def test_own_internal_include_from_src_passes(tmp_path):
    def build(root):
        build_two(root)
        (root / "src/ui/tcl/internal/secret.h").write_text("", encoding="utf-8")
        (root / "src/ui/tcl/src/tcl.cc").write_text(
            '#include "secret.h"\n', encoding="utf-8")
    repo = make_repo(tmp_path, TWO_MODULES, build)
    result = run_checker(repo)
    assert result.returncode == 0, result.stdout + result.stderr


def test_public_header_including_internal_fails(tmp_path):
    def build(root):
        build_two(root)
        (root / "src/ui/tcl/include/tcl.h").write_text(
            '#include "secret.h"\n', encoding="utf-8")
    repo = make_repo(tmp_path, TWO_MODULES, build)
    result = run_checker(repo)
    assert result.returncode == 1
    assert "public header includes an internal header" in result.stdout
