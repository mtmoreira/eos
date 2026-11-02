"""Unit tests for tools/dep_graph.py.

Each test builds a tiny fixture repository in a temporary directory
and runs the script against it as a subprocess, so the tests
exercise the real command line, exit codes, and messages. The
fixture's dependency shape is hand-checked:

    base (L0, foundation)
    leaf (L1)            <- mid (L1, includes leaf.h)
                         <- linked (L1, CMake-links leaf)
    mid  (L1)            <- top (L2, includes mid.h)
    unrelated (L1)

So a change to leaf selects exactly the tests of leaf, mid,
linked, and top (plus the qualification test, which links top
and mid), and nothing else.
"""

import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / "tools" / "dep_graph.py"

LAYERS_YAML = """\
layers:
  - id: L0
    level: 0
    name: foundation
    roots: [src/foundation]
  - id: L1
    level: 1
    name: lib
    roots: [src/lib]
  - id: L2
    level: 2
    name: app
    roots: [src/app]
modules:
"""

MODULES_YAML = """\
  - name: base
    layer: L0
    path: src/foundation/base
    target: eda_foundation_base
    adopted: false
  - name: leaf
    layer: L1
    path: src/lib/leaf
    target: eda_lib_leaf
    adopted: false
  - name: mid
    layer: L1
    path: src/lib/mid
    target: eda_lib_mid
    adopted: false
  - name: linked
    layer: L1
    path: src/lib/linked
    target: eda_lib_linked
    adopted: false
  - name: top
    layer: L2
    path: src/app/top
    target: eda_app_top
    adopted: false
  - name: unrelated
    layer: L1
    path: src/lib/unrelated
    target: eda_lib_unrelated
    adopted: false
"""

UNIT_CMAKELISTS = """\
add_executable(eda_foundation_base_unit_test foundation/base/base_test.cc)
target_link_libraries(eda_foundation_base_unit_test
  PRIVATE eda_foundation_base GTest::gtest GTest::gtest_main)
add_test(NAME foundation.base COMMAND eda_foundation_base_unit_test)

add_executable(eda_lib_leaf_unit_test lib/leaf/leaf_test.cc)
target_link_libraries(eda_lib_leaf_unit_test
  PRIVATE eda_lib_leaf GTest::gtest GTest::gtest_main)
add_test(NAME lib.leaf COMMAND eda_lib_leaf_unit_test)

add_executable(eda_lib_mid_unit_test lib/mid/mid_test.cc)
target_link_libraries(eda_lib_mid_unit_test
  PRIVATE eda_lib_mid GTest::gtest GTest::gtest_main)
add_test(NAME lib.mid COMMAND eda_lib_mid_unit_test)

add_executable(eda_lib_linked_unit_test lib/linked/linked_test.cc)
target_link_libraries(eda_lib_linked_unit_test
  PRIVATE eda_lib_linked GTest::gtest GTest::gtest_main)
add_test(NAME lib.linked COMMAND eda_lib_linked_unit_test)

add_executable(eda_app_top_unit_test app/top/top_test.cc)
target_link_libraries(eda_app_top_unit_test
  PRIVATE eda_app_top GTest::gtest GTest::gtest_main)
add_test(NAME app.top COMMAND eda_app_top_unit_test)

add_executable(eda_lib_unrelated_unit_test lib/unrelated/unrelated_test.cc)
target_link_libraries(eda_lib_unrelated_unit_test
  PRIVATE eda_lib_unrelated GTest::gtest GTest::gtest_main)
add_test(NAME lib.unrelated COMMAND eda_lib_unrelated_unit_test)
"""

QUAL_CMAKELISTS = """\
add_executable(eda_qualification_combo_test combo_test.cc)
target_link_libraries(eda_qualification_combo_test
  PRIVATE eda_app_top eda_lib_mid GTest::gtest GTest::gtest_main)
add_test(NAME qualification.combo COMMAND eda_qualification_combo_test)
"""


def write_module(root, name, path, target, includes=(), links=()):
    base = root / path
    for sub in ("include", "src", "internal", "tests"):
        (base / sub).mkdir(parents=True)
    (base / "include" / f"{name}.h").write_text(
        f"// {name} header\n", encoding="utf-8")
    src_lines = [f'#include "{name}.h"']
    src_lines += [f'#include "{inc}"' for inc in includes]
    (base / "src" / f"{name}.cc").write_text(
        "\n".join(src_lines) + "\n", encoding="utf-8")
    (base / "README.md").write_text(f"# {name}\n", encoding="utf-8")
    cmake = f"add_library({target} STATIC src/{name}.cc)\n"
    if links:
        cmake += (f"target_link_libraries({target} PUBLIC "
                  f"{' '.join(links)})\n")
    (base / "CMakeLists.txt").write_text(cmake, encoding="utf-8")


def make_repo(tmp_path):
    (tmp_path / "structure.yaml").write_text(
        LAYERS_YAML + MODULES_YAML, encoding="utf-8")
    write_module(tmp_path, "base", "src/foundation/base",
                 "eda_foundation_base")
    write_module(tmp_path, "leaf", "src/lib/leaf", "eda_lib_leaf")
    write_module(tmp_path, "mid", "src/lib/mid", "eda_lib_mid",
                 includes=("leaf.h",))
    write_module(tmp_path, "linked", "src/lib/linked",
                 "eda_lib_linked", links=("eda_lib_leaf",))
    write_module(tmp_path, "top", "src/app/top", "eda_app_top",
                 includes=("mid.h",))
    write_module(tmp_path, "unrelated", "src/lib/unrelated",
                 "eda_lib_unrelated")
    (tmp_path / "tests" / "unit").mkdir(parents=True)
    (tmp_path / "tests" / "unit" / "CMakeLists.txt").write_text(
        UNIT_CMAKELISTS, encoding="utf-8")
    (tmp_path / "tests" / "qualification").mkdir(parents=True)
    (tmp_path / "tests" / "qualification" / "CMakeLists.txt").write_text(
        QUAL_CMAKELISTS, encoding="utf-8")
    return tmp_path


def run_script(root, command, *args):
    return subprocess.run(
        [sys.executable, str(SCRIPT), command, "--root", str(root),
         *args],
        capture_output=True, text=True)


def generate(tmp_path):
    graph_path = tmp_path / "tools" / "dependency-graph.json"
    graph_path.parent.mkdir(exist_ok=True)
    result = run_script(tmp_path, "generate", "--out", str(graph_path))
    assert result.returncode == 0, result.stderr
    return graph_path, json.loads(graph_path.read_text(encoding="utf-8"))


def select(tmp_path, graph_path, *files):
    result = run_script(tmp_path, "select", "--graph", str(graph_path),
                        *files)
    assert result.returncode == 0, result.stderr
    return result.stdout.split()


def test_generate_edges(tmp_path):
    root = make_repo(tmp_path)
    _path, graph = generate(root)
    mods = graph["modules"]
    assert mods["leaf"]["depends_on"] == []
    assert mods["mid"]["depends_on"] == ["leaf"]
    assert mods["mid"]["via"] == {"leaf": ["include"]}
    assert mods["linked"]["depends_on"] == ["leaf"]
    assert mods["linked"]["via"] == {"leaf": ["cmake-link"]}
    assert mods["top"]["depends_on"] == ["mid"]
    assert mods["unrelated"]["depends_on"] == []
    assert mods["base"]["depends_on"] == []
    assert graph["tests"]["lib.leaf"]["modules"] == ["leaf"]
    assert graph["tests"]["qualification.combo"]["modules"] == [
        "mid", "top"]


def test_select_leaf_is_exact(tmp_path):
    root = make_repo(tmp_path)
    graph_path, _graph = generate(root)
    selected = select(root, graph_path, "src/lib/leaf/src/leaf.cc")
    assert selected == ["app.top", "lib.leaf", "lib.linked", "lib.mid",
                        "qualification.combo"]


def test_select_unrelated_is_exact(tmp_path):
    root = make_repo(tmp_path)
    graph_path, _graph = generate(root)
    selected = select(root, graph_path,
                      "src/lib/unrelated/src/unrelated.cc")
    assert selected == ["lib.unrelated"]


def test_select_foundation_is_whole_suite(tmp_path):
    root = make_repo(tmp_path)
    graph_path, graph = generate(root)
    selected = select(root, graph_path,
                      "src/foundation/base/src/base.cc")
    assert selected == sorted(graph["tests"])


def test_select_test_file_selects_only_its_test(tmp_path):
    root = make_repo(tmp_path)
    graph_path, _graph = generate(root)
    selected = select(root, graph_path,
                      "tests/unit/lib/leaf/leaf_test.cc")
    assert selected == ["lib.leaf"]


def test_select_cmake_change_is_whole_suite(tmp_path):
    root = make_repo(tmp_path)
    graph_path, graph = generate(root)
    selected = select(root, graph_path, "src/lib/leaf/CMakeLists.txt")
    assert selected == sorted(graph["tests"])


def test_select_docs_selects_nothing_and_regex_matches_nothing(tmp_path):
    root = make_repo(tmp_path)
    graph_path, _graph = generate(root)
    assert select(root, graph_path, "docs/notes.md") == []
    result = run_script(root, "select", "--graph", str(graph_path),
                        "docs/notes.md", "--ctest-regex")
    assert result.returncode == 0
    regex = result.stdout.strip()
    assert regex == "a^"
    assert re.match(regex, "lib.leaf") is None


def test_ctest_regex_matches_exactly_the_selection(tmp_path):
    root = make_repo(tmp_path)
    graph_path, _graph = generate(root)
    result = run_script(root, "select", "--graph", str(graph_path),
                        "src/lib/leaf/src/leaf.cc", "--ctest-regex")
    assert result.returncode == 0
    regex = result.stdout.strip()
    for name in ("app.top", "lib.leaf", "lib.linked", "lib.mid",
                 "qualification.combo"):
        assert re.match(regex, name) is not None, name
    for name in ("foundation.base", "lib.unrelated"):
        assert re.match(regex, name) is None, name


def test_check_passes_on_generated_graph_and_fails_on_drift(tmp_path):
    root = make_repo(tmp_path)
    graph_path, graph = generate(root)
    result = run_script(root, "check", "--graph", str(graph_path))
    assert result.returncode == 0, result.stdout + result.stderr
    assert "OK" in result.stdout
    graph["modules"]["mid"]["depends_on"] = []
    graph_path.write_text(json.dumps(graph, indent=2, sort_keys=True)
                          + "\n", encoding="utf-8")
    result = run_script(root, "check", "--graph", str(graph_path))
    assert result.returncode == 1
    assert "mid" in result.stdout


def test_real_repo_check_passes_and_rules_hold():
    graph_path = REPO / "tools" / "dependency-graph.json"
    assert graph_path.is_file()
    result = run_script(REPO, "check", "--graph", str(graph_path))
    assert result.returncode == 0, result.stdout + result.stderr
    graph = json.loads(graph_path.read_text(encoding="utf-8"))
    all_tests = sorted(graph["tests"])
    # R1 step 2 adds pd.dbsta, pd.pdr, and pd.rsz, and U1 adds
    # ui.gui, the 27th, 28th, 29th, and 30th ctest entries.
    # PR4 adds 0 ctest entries: the adapted rsz suite (42 cases
    # plus gcd) is pending-licence, so no case is runnable in CI
    # yet; the rsz/tests dep-graph rule and its synthetic test
    # below already cover the entries when they land.
    assert len(all_tests) == 30
    # Foundation rule on the real repo: a change to an L0 module
    # selects the whole suite.
    result = run_script(REPO, "select", "--graph", str(graph_path),
                        "src/foundation/types/src/types.cc")
    assert result.returncode == 0
    assert result.stdout.split() == all_tests
    # Docs change selects nothing.
    result = run_script(REPO, "select", "--graph", str(graph_path),
                        "docs/notes.md")
    assert result.returncode == 0
    assert result.stdout.split() == []


def make_rsz_repo(tmp_path):
    """Fixture repo with rsz + registry and a script-driven entry.

    The suite entry runs an interpreter, not a test exe, so the
    exe-link scan maps it to nothing; the rsz/tests rule must
    map it to [rsz, registry] instead.
    """
    (tmp_path / "structure.yaml").write_text(
        "layers:\n"
        "  - id: L4\n"
        "    level: 4\n"
        "    name: pd\n"
        "    roots: [src/pd]\n"
        "  - id: L6\n"
        "    level: 6\n"
        "    name: ui\n"
        "    roots: [src/ui]\n"
        "modules:\n"
        "  - name: rsz\n"
        "    layer: L4\n"
        "    path: src/pd/rsz\n"
        "    target: eda_pd_rsz\n"
        "    adopted: false\n"
        "  - name: registry\n"
        "    layer: L6\n"
        "    path: src/ui/registry\n"
        "    target: eda_ui_registry\n"
        "    adopted: false\n",
        encoding="utf-8")
    write_module(tmp_path, "rsz", "src/pd/rsz", "eda_pd_rsz")
    write_module(tmp_path, "registry", "src/ui/registry",
                 "eda_ui_registry")
    (tmp_path / "tests" / "unit").mkdir(parents=True)
    (tmp_path / "tests" / "unit" / "CMakeLists.txt").write_text(
        "add_executable(eda_ui_registry_unit_test\n"
        "  ui/registry/registry_test.cc)\n"
        "target_link_libraries(eda_ui_registry_unit_test\n"
        "  PRIVATE eda_ui_registry GTest::gtest GTest::gtest_main)\n"
        "add_test(NAME ui.registry COMMAND eda_ui_registry_unit_test)\n"
        "\n"
        "add_test(NAME rsz.repair_cap1\n"
        "  COMMAND python3 run_case.py cap1.tcl\n"
        "  WORKING_DIRECTORY\n"
        "  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/pd/rsz/tests)\n",
        encoding="utf-8")
    (tmp_path / "tests" / "qualification").mkdir(parents=True)
    (tmp_path / "tests" / "qualification" / "CMakeLists.txt").write_text(
        "", encoding="utf-8")
    return tmp_path


def test_script_driven_rsz_entry_maps_to_rsz_and_registry(tmp_path):
    root = make_rsz_repo(tmp_path)
    graph_path, graph = generate(root)
    assert graph["tests"]["rsz.repair_cap1"]["modules"] == [
        "registry", "rsz"]
    # The exe-linked entry is untouched by the rule.
    assert graph["tests"]["ui.registry"]["modules"] == ["registry"]
    # Both entries are mapped, so check passes.
    result = run_script(root, "check", "--graph", str(graph_path))
    assert result.returncode == 0, result.stdout + result.stderr
    # An rsz change selects the suite entry but not the
    # registry unit test; a registry change selects both.
    assert select(root, graph_path,
                  "src/pd/rsz/src/Resizer.cc") == ["rsz.repair_cap1"]
    assert select(root, graph_path,
                  "src/ui/registry/src/repair_commands.cc") == [
                      "rsz.repair_cap1", "ui.registry"]


def test_script_driven_entry_without_rsz_tests_maps_to_nothing(tmp_path):
    root = make_rsz_repo(tmp_path)
    unit = root / "tests" / "unit" / "CMakeLists.txt"
    unit.write_text(unit.read_text(encoding="utf-8")
                    + "\nadd_test(NAME rsz.other\n"
                    "  COMMAND python3 run_case.py other.tcl)\n",
                    encoding="utf-8")
    _path, graph = generate(root)
    assert graph["tests"]["rsz.other"]["modules"] == []
