# yosys: Yosys, imported from the pinned v0.70 tree as EOS's own
# code (see PROVENANCE.md for the exact commit, the file inventory
# and the patch list). The upstream build files are not imported;
# this file is EOS's own build for the whole library closure as the
# single EOS target eda_synth_yosys, modeled on the upstream CMake
# component lists at the pin:
#
#   src/<dir>     upstream sources, in the upstream layout
#   src/yosys_facade.cc  the only EOS-authored translation unit in
#                 the target
#
# Configuration, chosen to match what the EOS CI environment
# provides (see .github/workflows/ci.yml) and to keep the target
# a library, not an executable:
#   - Yosys's ABC integration is OFF: EOS already has its own ABC
#     (eda_synth_abc), and Yosys's abc pass shells out to an abc
#     executable, which the EOS no-external-process rule forbids.
#     The techmap abc passes still compile; invoking one at
#     runtime fails, as upstream without an abc binary fails.
#   - The slang frontend is OFF: it needs the slang submodule,
#     which is not imported (the Phase 0 pin is the Verilog front
#     end; the slang decision is plan Section 6, open decision 1).
#   - Readline, editline and libffi are OFF: CI installs none of
#     their development packages, and upstream auto-disables them
#     when absent. The shell fallback (plain fgets) is never used
#     by the facade, which drives Yosys through its C++ API.
#   - Tcl is ON when the EOS Tcl dependency is found (CI installs
#     tcl-dev): tclapi.cc is compiled either way, guarded by
#     YOSYS_ENABLE_TCL inside, exactly as upstream.
#   - Zlib and threads are ON (CI provides both).
#   - C++20, as upstream requires at this pin (the EOS top level
#     is C++17; this target overrides it, as the sta target does
#     for its own units). The facade header exposes no Yosys types,
#     so no other EOS target needs C++20 to use this module.
#   - Consumers must link this target whole-archive
#     ("$<LINK_LIBRARY:WHOLE_ARCHIVE,eda_synth_yosys>"): Yosys
#     registers passes and frontends through global constructors
#     that a plain static-archive pull never includes. See
#     PROVENANCE.md and the facade header.
#
# Generated files, all produced at build time from imported inputs
# (never committed, never patched sources):
#   - kernel/version.cc from kernel/version.cc.in, with the pin's
#     version and commit baked in (the upstream generator reads
#     git metadata of the Yosys checkout, which does not exist
#     inside the EOS tree; the values here are pin facts).
#   - kernel/yosys_config.h from kernel/yosys_config.h.in.
#   - kernel/simlib_help.inc and kernel/simcells_help.inc from
#     techlibs/common/simlib.v and simcells.v via kernel/cellhelp.py.
#   - The Verilog lexer and parser from frontends/verilog/*.l/*.y
#     via flex and bison (CI installs both).
#   - Pattern-matcher headers (*_pm.h) from the .pmg sources via
#     passes/pmgen/pmgen.py, one custom command per upstream
#     pmgen_command call.
# The generated outputs are laid out under the binary dir at the
# same relative paths the sources include them by
# ("kernel/yosys_config.h", "passes/opt/peepopt_pm.h", ...), so no
# source is edited to find them.
#
# Two library exclusions, both documented in PROVENANCE.md:
#   - kernel/driver.cc: the yosys executable's main() plus its
#     console log sink, referenced by nothing else in the tree.
#   - passes/techmap/filterlib.cc: the standalone yosys-filterlib
#     tool, which is just FILTERLIB plus a textual #include of
#     libparse.cc, so its object defines main() and a
#     FILTERLIB-mode LibertyParser. The library instead compiles
#     libparse.cc itself (without FILTERLIB, its main() is not
#     compiled in), which provides the LibertyParser the
#     dfflibmap, clockgate and libcache passes use.
# The share directory is staged, not found: passes that need
# techlib .v data at runtime (techmap, and later the synth_*
# passes) look it up relative to the host executable, which is
# not an EOS concept. Unit S1 stages exactly the one share file
# it needs, techmap.v, into the build tree at configure time,
# and the facade points Yosys's share global at that directory
# after setup (see yosys_facade.cc). The staged copy is a build
# artifact, never committed; a fresh configure regenerates it.
# Other share files the synth_* passes would need stay unwired
# until a unit needs them.
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/src/techlibs/common/techmap.v
  DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/share)
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB QUIET)

set(_eos_yosys_gen_dir ${CMAKE_CURRENT_BINARY_DIR})

# The generated outputs below live in subdirectories of the binary
# dir that no add_subdirectory creates in this single-target build,
# so create them at configure time; without this, flex, bison and
# pmgen fail because their output directory does not exist yet.
file(MAKE_DIRECTORY
  ${_eos_yosys_gen_dir}/kernel
  ${_eos_yosys_gen_dir}/frontends/verilog
  ${_eos_yosys_gen_dir}/passes/opt
  ${_eos_yosys_gen_dir}/passes/pmgen
  ${_eos_yosys_gen_dir}/techlibs/ice40
  ${_eos_yosys_gen_dir}/techlibs/lattice
  ${_eos_yosys_gen_dir}/techlibs/microchip
  ${_eos_yosys_gen_dir}/techlibs/quicklogic
  ${_eos_yosys_gen_dir}/techlibs/xilinx)

# Feature defines for yosys_config.h.in (#cmakedefine).
set(YOSYS_ENABLE_GLOB 1)
set(YOSYS_ENABLE_SPAWN 1)
set(YOSYS_ENABLE_THREADS 1)
set(YOSYS_ENABLE_PLUGINS 1)
if(ZLIB_FOUND)
  set(YOSYS_ENABLE_ZLIB 1)
endif()
if(TCL_FOUND)
  set(YOSYS_ENABLE_TCL 1)
endif()
set(YOSYS_PROGRAM_PREFIX "")
set(YOSYS_INSTALL_FULL_DATADIR "")
configure_file(src/kernel/yosys_config.h.in
  ${_eos_yosys_gen_dir}/kernel/yosys_config.h @ONLY)

# Version stamp: pin facts, not git metadata of the EOS tree.
set(YOSYS_BUILD_INFO "Yosys 0.70 (git sha1 28ba3cb92)")
set(YOSYS_CHECKOUT_INFO "28ba3cb92")
configure_file(src/kernel/version.cc.in
  ${_eos_yosys_gen_dir}/kernel/version.cc @ONLY)

# Cell help tables for the kernel's cell-type registry.
foreach(_lib simlib simcells)
  add_custom_command(
    OUTPUT ${_eos_yosys_gen_dir}/kernel/${_lib}_help.inc
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/cellhelp.py
            ${CMAKE_CURRENT_SOURCE_DIR}/src/techlibs/common/${_lib}.v
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/cellhelp.py
            ${CMAKE_CURRENT_SOURCE_DIR}/src/techlibs/common/${_lib}.v
            > ${_eos_yosys_gen_dir}/kernel/${_lib}_help.inc
    VERBATIM)
endforeach()

# Verilog lexer and parser.
FLEX_TARGET(verilog_lexer
  ${CMAKE_CURRENT_SOURCE_DIR}/src/frontends/verilog/verilog_lexer.l
  ${_eos_yosys_gen_dir}/frontends/verilog/verilog_lexer.cc)
BISON_TARGET(verilog_parser
  ${CMAKE_CURRENT_SOURCE_DIR}/src/frontends/verilog/verilog_parser.y
  ${_eos_yosys_gen_dir}/frontends/verilog/verilog_parser.tab.cc)
set_source_files_properties(
  ${BISON_verilog_parser_OUTPUT_SOURCE}
  PROPERTIES COMPILE_DEFINITIONS -DYYMAXDEPTH=10000000)

# Pattern-matcher headers. Each entry is <output> <source dir>
# <prefix or -> <inputs...>; prefix "-" means pmgen.py's default
# (the single input's basename).
set(_eos_yosys_pmgen "")
function(_eos_yosys_pmgen name dir prefix)
  set(_inputs "")
  foreach(_in IN LISTS ARGN)
    list(APPEND _inputs ${CMAKE_CURRENT_SOURCE_DIR}/src/${dir}/${_in})
  endforeach()
  set(_out ${_eos_yosys_gen_dir}/${dir}/${name}_pm.h)
  if(prefix STREQUAL "-")
    add_custom_command(
      OUTPUT ${_out}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/passes/pmgen/pmgen.py
              ${_inputs}
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/src/passes/pmgen/pmgen.py
              -o ${_out} ${_inputs}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/${dir}
      VERBATIM)
  else()
    add_custom_command(
      OUTPUT ${_out}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/passes/pmgen/pmgen.py
              ${_inputs}
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/src/passes/pmgen/pmgen.py
              -p ${prefix} -o ${_out} ${_inputs}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/${dir}
      VERBATIM)
  endif()
  set(_eos_yosys_pmgen ${_eos_yosys_pmgen} ${_out} PARENT_SCOPE)
endfunction()
_eos_yosys_pmgen(peepopt passes/opt peepopt
  peepopt_shiftmul_right.pmg peepopt_shiftmul_left.pmg
  peepopt_shiftadd.pmg peepopt_shiftpow2.pmg peepopt_muldiv.pmg
  peepopt_muldiv_c.pmg peepopt_formal_clockgateff.pmg)
_eos_yosys_pmgen(test_pmgen passes/pmgen - test_pmgen.pmg)
_eos_yosys_pmgen(ice40_dsp techlibs/ice40 - ice40_dsp.pmg)
_eos_yosys_pmgen(ice40_wrapcarry techlibs/ice40 - ice40_wrapcarry.pmg)
_eos_yosys_pmgen(lattice_dsp_nexus techlibs/lattice - lattice_dsp_nexus.pmg)
_eos_yosys_pmgen(microchip_dsp techlibs/microchip - microchip_dsp.pmg)
_eos_yosys_pmgen(microchip_dsp_cascade techlibs/microchip - microchip_dsp_cascade.pmg)
_eos_yosys_pmgen(microchip_dsp_CREG techlibs/microchip - microchip_dsp_CREG.pmg)
_eos_yosys_pmgen(ql_dsp_macc techlibs/quicklogic - ql_dsp_macc.pmg)
_eos_yosys_pmgen(xilinx_dsp techlibs/xilinx - xilinx_dsp.pmg)
_eos_yosys_pmgen(xilinx_dsp48a techlibs/xilinx - xilinx_dsp48a.pmg)
_eos_yosys_pmgen(xilinx_dsp_CREG techlibs/xilinx - xilinx_dsp_CREG.pmg)
_eos_yosys_pmgen(xilinx_dsp_cascade techlibs/xilinx - xilinx_dsp_cascade.pmg)
_eos_yosys_pmgen(xilinx_srl techlibs/xilinx - xilinx_srl.pmg)

set(EOS_YOSYS_SOURCE
  src/backends/aiger/aiger.cc
  src/backends/aiger/xaiger.cc
  src/backends/aiger2/aiger.cc
  src/backends/blif/blif.cc
  src/backends/btor/btor.cc
  src/backends/cxxrtl/cxxrtl_backend.cc
  src/backends/edif/edif.cc
  src/backends/firrtl/firrtl.cc
  src/backends/functional/cxx.cc
  src/backends/functional/smtlib.cc
  src/backends/functional/smtlib_rosette.cc
  src/backends/functional/test_generic.cc
  src/backends/intersynth/intersynth.cc
  src/backends/jny/jny.cc
  src/backends/json/json.cc
  src/backends/rtlil/rtlil_backend.cc
  src/backends/simplec/simplec.cc
  src/backends/smt2/smt2.cc
  src/backends/smv/smv.cc
  src/backends/spice/spice.cc
  src/backends/table/table.cc
  src/backends/verilog/verilog_backend.cc
  src/frontends/aiger/aigerparse.cc
  src/frontends/ast/ast.cc
  src/frontends/ast/dpicall.cc
  src/frontends/ast/genrtlil.cc
  src/frontends/ast/simplify.cc
  src/frontends/blif/blifparse.cc
  src/frontends/json/jsonparse.cc
  src/frontends/liberty/liberty.cc
  src/frontends/rpc/rpc_frontend.cc
  src/frontends/rtlil/rtlil_frontend.cc
  src/frontends/verific/verific.cc
  src/frontends/verilog/const2ast.cc
  src/frontends/verilog/preproc.cc
  src/frontends/verilog/verilog_error.cc
  src/frontends/verilog/verilog_frontend.cc
  src/kernel/calc.cc
  src/kernel/cellaigs.cc
  src/kernel/celledges.cc
  src/kernel/compressor_tree.cc
  src/kernel/cost.cc
  src/kernel/drivertools.cc
  src/kernel/ff.cc
  src/kernel/ffmerge.cc
  src/kernel/fmt.cc
  src/kernel/fstdata.cc
  src/kernel/functional.cc
  src/kernel/gzip.cc
  src/kernel/io.cc
  src/kernel/json.cc
  src/kernel/log.cc
  src/kernel/log_help.cc
  src/kernel/mem.cc
  src/kernel/qcsat.cc
  src/kernel/register.cc
  src/kernel/rtlil.cc
  src/kernel/rtlil_bufnorm.cc
  src/kernel/satgen.cc
  src/kernel/scopeinfo.cc
  src/kernel/sexpr.cc
  src/kernel/tclapi.cc
  src/kernel/threading.cc
  src/kernel/yosys.cc
  src/kernel/yw.cc
  src/libs/bigint/BigInteger.cc
  src/libs/bigint/BigIntegerAlgorithms.cc
  src/libs/bigint/BigIntegerUtils.cc
  src/libs/bigint/BigUnsigned.cc
  src/libs/bigint/BigUnsignedInABase.cc
  src/libs/ezsat/ezcmdline.cc
  src/libs/ezsat/ezminisat.cc
  src/libs/ezsat/ezsat.cc
  src/libs/fst/fastlz.cc
  src/libs/fst/fstapi.cc
  src/libs/fst/lz4.cc
  src/libs/json11/json11.cpp
  src/libs/minisat/Options.cc
  src/libs/minisat/SimpSolver.cc
  src/libs/minisat/Solver.cc
  src/libs/minisat/System.cc
  src/libs/sha1/sha1.cpp
  src/libs/subcircuit/subcircuit.cc
  src/passes/cmds/abstract.cc
  src/passes/cmds/add.cc
  src/passes/cmds/autoname.cc
  src/passes/cmds/blackbox.cc
  src/passes/cmds/box_derive.cc
  src/passes/cmds/bugpoint.cc
  src/passes/cmds/check.cc
  src/passes/cmds/chformal.cc
  src/passes/cmds/chtype.cc
  src/passes/cmds/clean_zerowidth.cc
  src/passes/cmds/connect.cc
  src/passes/cmds/connwrappers.cc
  src/passes/cmds/copy.cc
  src/passes/cmds/delete.cc
  src/passes/cmds/design.cc
  src/passes/cmds/design_equal.cc
  src/passes/cmds/dft_tag.cc
  src/passes/cmds/edgetypes.cc
  src/passes/cmds/example_dt.cc
  src/passes/cmds/exec.cc
  src/passes/cmds/future.cc
  src/passes/cmds/glift.cc
  src/passes/cmds/icell_liberty.cc
  src/passes/cmds/internal_stats.cc
  src/passes/cmds/linecoverage.cc
  src/passes/cmds/linux_perf.cc
  src/passes/cmds/logcmd.cc
  src/passes/cmds/logger.cc
  src/passes/cmds/ltp.cc
  src/passes/cmds/optbarriers.cc
  src/passes/cmds/plugin.cc
  src/passes/cmds/portarcs.cc
  src/passes/cmds/portlist.cc
  src/passes/cmds/printattrs.cc
  src/passes/cmds/rename.cc
  src/passes/cmds/scatter.cc
  src/passes/cmds/scc.cc
  src/passes/cmds/scratchpad.cc
  src/passes/cmds/sdc/sdc.cc
  src/passes/cmds/select.cc
  src/passes/cmds/setattr.cc
  src/passes/cmds/setenv.cc
  src/passes/cmds/setundef.cc
  src/passes/cmds/show.cc
  src/passes/cmds/sort.cc
  src/passes/cmds/splice.cc
  src/passes/cmds/splitcells.cc
  src/passes/cmds/splitnets.cc
  src/passes/cmds/sta.cc
  src/passes/cmds/stat.cc
  src/passes/cmds/symfpu.cc
  src/passes/cmds/tee.cc
  src/passes/cmds/test_select.cc
  src/passes/cmds/timeest.cc
  src/passes/cmds/torder.cc
  src/passes/cmds/trace.cc
  src/passes/cmds/viz.cc
  src/passes/cmds/wrapcell.cc
  src/passes/cmds/write_file.cc
  src/passes/cmds/xprop.cc
  src/passes/equiv/equiv_add.cc
  src/passes/equiv/equiv_induct.cc
  src/passes/equiv/equiv_make.cc
  src/passes/equiv/equiv_mark.cc
  src/passes/equiv/equiv_miter.cc
  src/passes/equiv/equiv_opt.cc
  src/passes/equiv/equiv_purge.cc
  src/passes/equiv/equiv_remove.cc
  src/passes/equiv/equiv_simple.cc
  src/passes/equiv/equiv_status.cc
  src/passes/equiv/equiv_struct.cc
  src/passes/fsm/fsm.cc
  src/passes/fsm/fsm_detect.cc
  src/passes/fsm/fsm_expand.cc
  src/passes/fsm/fsm_export.cc
  src/passes/fsm/fsm_extract.cc
  src/passes/fsm/fsm_info.cc
  src/passes/fsm/fsm_map.cc
  src/passes/fsm/fsm_opt.cc
  src/passes/fsm/fsm_recode.cc
  src/passes/hierarchy/flatten.cc
  src/passes/hierarchy/hierarchy.cc
  src/passes/hierarchy/keep_hierarchy.cc
  src/passes/hierarchy/submod.cc
  src/passes/hierarchy/uniquify.cc
  src/passes/memory/memlib.cc
  src/passes/memory/memory.cc
  src/passes/memory/memory_bmux2rom.cc
  src/passes/memory/memory_bram.cc
  src/passes/memory/memory_collect.cc
  src/passes/memory/memory_dff.cc
  src/passes/memory/memory_libmap.cc
  src/passes/memory/memory_map.cc
  src/passes/memory/memory_memx.cc
  src/passes/memory/memory_narrow.cc
  src/passes/memory/memory_nordff.cc
  src/passes/memory/memory_share.cc
  src/passes/memory/memory_unpack.cc
  src/passes/opt/clean/cells_all.cc
  src/passes/opt/clean/cells_temp.cc
  src/passes/opt/clean/inits.cc
  src/passes/opt/clean/opt_clean.cc
  src/passes/opt/clean/wires.cc
  src/passes/opt/dff/constbits.cc
  src/passes/opt/dff/eqbits.cc
  src/passes/opt/dff/opt_dff.cc
  src/passes/opt/dff/simple.cc
  src/passes/opt/muxpack.cc
  src/passes/opt/opt.cc
  src/passes/opt/opt_balance_tree.cc
  src/passes/opt/opt_demorgan.cc
  src/passes/opt/opt_expr.cc
  src/passes/opt/opt_ffinv.cc
  src/passes/opt/opt_hier.cc
  src/passes/opt/opt_lut.cc
  src/passes/opt/opt_lut_ins.cc
  src/passes/opt/opt_mem.cc
  src/passes/opt/opt_mem_feedback.cc
  src/passes/opt/opt_mem_priority.cc
  src/passes/opt/opt_mem_widen.cc
  src/passes/opt/opt_merge.cc
  src/passes/opt/opt_muxtree.cc
  src/passes/opt/opt_reduce.cc
  src/passes/opt/opt_share.cc
  src/passes/opt/peepopt.cc
  src/passes/opt/pmux2shiftx.cc
  src/passes/opt/rmports.cc
  src/passes/opt/share.cc
  src/passes/opt/wreduce.cc
  src/passes/pmgen/test_pmgen.cc
  src/passes/proc/proc.cc
  src/passes/proc/proc_arst.cc
  src/passes/proc/proc_clean.cc
  src/passes/proc/proc_dff.cc
  src/passes/proc/proc_dlatch.cc
  src/passes/proc/proc_init.cc
  src/passes/proc/proc_memwr.cc
  src/passes/proc/proc_mux.cc
  src/passes/proc/proc_prune.cc
  src/passes/proc/proc_rmdead.cc
  src/passes/proc/proc_rom.cc
  src/passes/sat/assertpmux.cc
  src/passes/sat/async2sync.cc
  src/passes/sat/clk2fflogic.cc
  src/passes/sat/cutpoint.cc
  src/passes/sat/eval.cc
  src/passes/sat/expose.cc
  src/passes/sat/fmcombine.cc
  src/passes/sat/fminit.cc
  src/passes/sat/formalff.cc
  src/passes/sat/freduce.cc
  src/passes/sat/miter.cc
  src/passes/sat/mutate.cc
  src/passes/sat/qbfsat.cc
  src/passes/sat/recover_names.cc
  src/passes/sat/sat.cc
  src/passes/sat/sim.cc
  src/passes/sat/supercover.cc
  src/passes/sat/synthprop.cc
  src/passes/techmap/abc.cc
  src/passes/techmap/abc9.cc
  src/passes/techmap/abc9_exe.cc
  src/passes/techmap/abc9_ops.cc
  src/passes/techmap/abc_new.cc
  src/passes/techmap/abc_ops_reintegrate.cc
  src/passes/techmap/aigmap.cc
  src/passes/techmap/alumacc.cc
  src/passes/techmap/arith_tree.cc
  src/passes/techmap/attrmap.cc
  src/passes/techmap/attrmvcp.cc
  src/passes/techmap/bmuxmap.cc
  src/passes/techmap/booth.cc
  src/passes/techmap/bufnorm.cc
  src/passes/techmap/bwmuxmap.cc
  src/passes/techmap/cellmatch.cc
  src/passes/techmap/clkbufmap.cc
  src/passes/techmap/clockgate.cc
  src/passes/techmap/constmap.cc
  src/passes/techmap/deminout.cc
  src/passes/techmap/demuxmap.cc
  src/passes/techmap/dffinit.cc
  src/passes/techmap/dfflegalize.cc
  src/passes/techmap/dfflibmap.cc
  src/passes/techmap/dffunmap.cc
  src/passes/techmap/extract.cc
  src/passes/techmap/extract_counter.cc
  src/passes/techmap/extract_fa.cc
  src/passes/techmap/extract_reduce.cc
  src/passes/techmap/extractinv.cc
  src/passes/techmap/hilomap.cc
  src/passes/techmap/insbuf.cc
  src/passes/techmap/iopadmap.cc
  src/passes/techmap/libcache.cc
  src/passes/techmap/libparse.cc
  src/passes/techmap/lut2bmux.cc
  src/passes/techmap/lut2mux.cc
  src/passes/techmap/maccmap.cc
  src/passes/techmap/mcm.cc
  src/passes/techmap/muxcover.cc
  src/passes/techmap/pmuxtree.cc
  src/passes/techmap/shregmap.cc
  src/passes/techmap/simplemap.cc
  src/passes/techmap/techmap.cc
  src/passes/techmap/tribuf.cc
  src/passes/techmap/zinit.cc
  src/passes/tests/raise_error.cc
  src/passes/tests/test_abcloop.cc
  src/passes/tests/test_autotb.cc
  src/passes/tests/test_cell.cc
  src/passes/tests/test_kogge_stone.cc
  src/techlibs/achronix/synth_achronix.cc
  src/techlibs/analogdevices/synth_analogdevices.cc
  src/techlibs/anlogic/anlogic_eqn.cc
  src/techlibs/anlogic/anlogic_fixcarry.cc
  src/techlibs/anlogic/synth_anlogic.cc
  src/techlibs/common/opensta.cc
  src/techlibs/common/prep.cc
  src/techlibs/common/sdc_expand.cc
  src/techlibs/common/synth.cc
  src/techlibs/easic/synth_easic.cc
  src/techlibs/efinix/efinix_fixcarry.cc
  src/techlibs/efinix/synth_efinix.cc
  src/techlibs/fabulous/synth_fabulous.cc
  src/techlibs/gatemate/gatemate_foldinv.cc
  src/techlibs/gatemate/synth_gatemate.cc
  src/techlibs/gowin/synth_gowin.cc
  src/techlibs/ice40/ice40_braminit.cc
  src/techlibs/ice40/ice40_dsp.cc
  src/techlibs/ice40/ice40_opt.cc
  src/techlibs/ice40/ice40_wrapcarry.cc
  src/techlibs/ice40/synth_ice40.cc
  src/techlibs/intel/synth_intel.cc
  src/techlibs/intel_alm/synth_intel_alm.cc
  src/techlibs/lattice/lattice_dsp_nexus.cc
  src/techlibs/lattice/lattice_gsr.cc
  src/techlibs/lattice/synth_lattice.cc
  src/techlibs/microchip/microchip_dffopt.cc
  src/techlibs/microchip/microchip_dsp.cc
  src/techlibs/microchip/synth_microchip.cc
  src/techlibs/nanoxplore/nx_carry.cc
  src/techlibs/nanoxplore/synth_nanoxplore.cc
  src/techlibs/quicklogic/ql_bram_merge.cc
  src/techlibs/quicklogic/ql_bram_types.cc
  src/techlibs/quicklogic/ql_dsp_io_regs.cc
  src/techlibs/quicklogic/ql_dsp_macc.cc
  src/techlibs/quicklogic/ql_dsp_simd.cc
  src/techlibs/quicklogic/ql_ioff.cc
  src/techlibs/quicklogic/synth_quicklogic.cc
  src/techlibs/sf2/synth_sf2.cc
  src/techlibs/xilinx/synth_xilinx.cc
  src/techlibs/xilinx/xilinx_dffopt.cc
  src/techlibs/xilinx/xilinx_dsp.cc
  src/techlibs/xilinx/xilinx_srl.cc
)

add_library(eda_synth_yosys STATIC
  src/yosys_facade.cc
  ${_eos_yosys_gen_dir}/kernel/version.cc
  ${_eos_yosys_gen_dir}/kernel/simlib_help.inc
  ${_eos_yosys_gen_dir}/kernel/simcells_help.inc
  ${FLEX_verilog_lexer_OUTPUTS}
  ${BISON_verilog_parser_OUTPUTS}
  ${BISON_verilog_parser_OUTPUT_HEADER}
  ${_eos_yosys_pmgen}
  ${EOS_YOSYS_SOURCE})
target_include_directories(eda_synth_yosys
  PUBLIC include
  PRIVATE src ${_eos_yosys_gen_dir} src/libs/flex src/libs)
# macOS: defining _XOPEN_SOURCE without _DARWIN_C_SOURCE makes Apple's
# headers hide Darwin extensions, including vasprintf, which upstream
# io.h uses. _DARWIN_C_SOURCE restores them on Apple only; it expands to
# nothing on every other platform, so Linux CI is unaffected.
target_compile_definitions(eda_synth_yosys PRIVATE
  _YOSYS_ _DEFAULT_SOURCE _XOPEN_SOURCE=600
  $<$<PLATFORM_ID:Darwin>:_DARWIN_C_SOURCE>
  EOS_YOSYS_SHARE_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/share/\")
if(ZLIB_FOUND)
  target_link_libraries(eda_synth_yosys PRIVATE ZLIB::ZLIB)
endif()
if(TCL_FOUND)
  # YOSYS_ENABLE_TCL itself comes from the generated yosys_config.h,
  # as upstream; here only the headers and library are added.
  target_include_directories(eda_synth_yosys PRIVATE ${TCL_INCLUDE_PATH})
  target_link_libraries(eda_synth_yosys PRIVATE ${TCL_LIBRARY})
endif()
target_link_libraries(eda_synth_yosys
  PUBLIC eda_foundation_errors
  PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
set_target_properties(eda_synth_yosys PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON)
