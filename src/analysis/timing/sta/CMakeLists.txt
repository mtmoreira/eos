# sta: OpenSTA, imported from the pinned upstream commit as EOS's own
# code (see PROVENANCE.md for the exact commit, the file inventory and
# the patch list), plus the CUDD 3.0.0 dependency closure vendored
# under src/cudd (see PROVENANCE.md). The upstream build files are not
# imported; this file is EOS's own build for the whole closure as the
# single EOS target eda_analysis_sta, modeled on the upstream
# CMakeLists.txt at the pin:
#
#   include/sta   upstream public headers
#   src/<dir>     upstream sources (app, dcalc, graph, liberty,
#                 network, parasitics, power, sdc, sdf, search, spice,
#                 tcl, util, verilog), generated parsers for the six
#                 flex/bison grammars, the SWIG Tcl wrapper generated
#                 from src/app/StaApp.i, and the Tcl init sources
#                 encoded by src/etc/TclEncode.tcl
#   src/cudd      the vendored CUDD closure, compiled as C objects
#                 folded into the target
#
# plus the EOS facade in src/sta_facade.cc, the only EOS-authored
# translation unit in the target. OpenSTA at this pin requires C++20;
# the standard is raised on this module's targets only (EOS is C++17
# elsewhere). No upstream warning set is applied: EOS does not build
# with -Werror and the imported code is not warning-clean under the
# upstream set with this compiler, so the module builds with the
# default flags plus the SWIG wrapper suppressions upstream uses.
enable_language(C)

find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)
find_package(SWIG 3.0 REQUIRED)
include(UseSWIG)
find_package(Eigen3 REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB)

# StaConfig.hh, generated from the upstream template (kept at
# src/util/StaConfig.hh.cmake) into the binary dir, as upstream does.
set(STA_VERSION "3.1.0")
set(STA_GIT_SHA1 "0f2529d18218b87aa0e82de444395a830d5058a1")
set(TCL_READLINE 0)
# std::format check, exactly as upstream: with no std::format the
# build needs fmt headers. Prefer a system fmt package: some
# spdlog builds (notably Homebrew's) are built against external
# fmt and ship no bundled headers, so linking spdlog's bundled
# dir would dangle and fail late at compile time with
# "fmt/core.h not found". Fall back to the bundled copy only
# when it actually exists, and fail at configure time otherwise.
include(CheckCXXSourceCompiles)
set(_eos_sta_saved_flags "${CMAKE_REQUIRED_FLAGS}")
if(MSVC)
  string(APPEND CMAKE_REQUIRED_FLAGS " /std:c++20")
else()
  string(APPEND CMAKE_REQUIRED_FLAGS " -std=c++20")
endif()
check_cxx_source_compiles("
#include <format>
#include <string>
int main() { (void)std::format(\"{}\", 42); return 0; }
" HAVE_CXX_STD_FORMAT)
set(CMAKE_REQUIRED_FLAGS "${_eos_sta_saved_flags}")
if(NOT HAVE_CXX_STD_FORMAT)
  find_package(spdlog REQUIRED)
  unset(_eos_sta_fmt_dir CACHE)
  find_path(_eos_sta_fmt_dir NAMES fmt/core.h)
  if(NOT _eos_sta_fmt_dir)
    get_target_property(_eos_sta_spdlog_inc spdlog::spdlog
      INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT EXISTS "${_eos_sta_spdlog_inc}/spdlog/fmt/bundled/core.h")
      message(FATAL_ERROR
        "OpenSTA needs fmt: no system fmt package found and "
        "spdlog ships no bundled fmt headers; install fmt "
        "(brew install fmt, or apt-get install libfmt-dev)")
    endif()
    set(_eos_sta_fmt_dir "${CMAKE_CURRENT_BINARY_DIR}/_spdlog_fmt")
    file(MAKE_DIRECTORY "${_eos_sta_fmt_dir}")
    file(CREATE_LINK "${_eos_sta_spdlog_inc}/spdlog/fmt/bundled"
      "${_eos_sta_fmt_dir}/fmt" SYMBOLIC)
  endif()
endif()
if(NOT HAVE_CXX_STD_FORMAT AND NOT MSVC)
  # With fmt 11+, <fmt/core.h> alone no longer declares
  # fmt::format/fmt::vformat, yet the imported sta/Format.hh
  # includes only core.h. Force-include <fmt/format.h> for every
  # C++ translation unit in this module instead of patching the
  # imported header. The generator expression keeps the flag off
  # the C sources (CUDD), which must not see a C++ header.
  # Directory scope cannot reach consumers outside this module
  # (dbsta failed exactly this way on Homebrew fmt 11); the
  # PUBLIC companion on eda_analysis_sta below carries the same
  # flag to every target linking the STA library.
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-include;fmt/format.h>")
endif()
configure_file(src/util/StaConfig.hh.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/include/sta/StaConfig.hh)

# Flex/bison scanner/parsers: the six upstream grammar pairs, outputs
# in the binary dir, as upstream builds them.
flex_target(eos_sta_verilog_lex src/verilog/VerilogLex.ll
  ${CMAKE_CURRENT_BINARY_DIR}/VerilogLex.cc)
bison_target(eos_sta_verilog_parse src/verilog/VerilogParse.yy
  ${CMAKE_CURRENT_BINARY_DIR}/VerilogParse.cc)
add_flex_bison_dependency(eos_sta_verilog_lex eos_sta_verilog_parse)
flex_target(eos_sta_liberty_lex src/liberty/LibertyLex.ll
  ${CMAKE_CURRENT_BINARY_DIR}/LibertyLex.cc)
bison_target(eos_sta_liberty_parse src/liberty/LibertyParse.yy
  ${CMAKE_CURRENT_BINARY_DIR}/LibertyParse.cc)
add_flex_bison_dependency(eos_sta_liberty_lex eos_sta_liberty_parse)
flex_target(eos_sta_libexpr_lex src/liberty/LibExprLex.ll
  ${CMAKE_CURRENT_BINARY_DIR}/LibExprLex.cc)
bison_target(eos_sta_libexpr_parse src/liberty/LibExprParse.yy
  ${CMAKE_CURRENT_BINARY_DIR}/LibExprParse.cc)
add_flex_bison_dependency(eos_sta_libexpr_lex eos_sta_libexpr_parse)
flex_target(eos_sta_sdf_lex src/sdf/SdfLex.ll
  ${CMAKE_CURRENT_BINARY_DIR}/SdfLex.cc)
bison_target(eos_sta_sdf_parse src/sdf/SdfParse.yy
  ${CMAKE_CURRENT_BINARY_DIR}/SdfParse.cc)
add_flex_bison_dependency(eos_sta_sdf_lex eos_sta_sdf_parse)
flex_target(eos_sta_spef_lex src/parasitics/SpefLex.ll
  ${CMAKE_CURRENT_BINARY_DIR}/SpefLex.cc)
bison_target(eos_sta_spef_parse src/parasitics/SpefParse.yy
  ${CMAKE_CURRENT_BINARY_DIR}/SpefParse.cc)
add_flex_bison_dependency(eos_sta_spef_lex eos_sta_spef_parse)
flex_target(eos_sta_saif_lex src/power/SaifLex.ll
  ${CMAKE_CURRENT_BINARY_DIR}/SaifLex.cc)
bison_target(eos_sta_saif_parse src/power/SaifParse.yy
  ${CMAKE_CURRENT_BINARY_DIR}/SaifParse.cc)
add_flex_bison_dependency(eos_sta_saif_lex eos_sta_saif_parse)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set_source_files_properties(
    ${FLEX_eos_sta_verilog_lex_OUTPUTS}
    ${FLEX_eos_sta_liberty_lex_OUTPUTS}
    ${FLEX_eos_sta_libexpr_lex_OUTPUTS}
    ${FLEX_eos_sta_sdf_lex_OUTPUTS}
    ${FLEX_eos_sta_spef_lex_OUTPUTS}
    ${FLEX_eos_sta_saif_lex_OUTPUTS}
    PROPERTIES COMPILE_FLAGS "-Wno-sign-compare")
endif()

# Tcl init sources are encoded into StaTclInitVar.cc by the upstream
# build tool src/etc/TclEncode.tcl (a sh/tclsh polyglot, run directly
# as upstream runs it), so no Tcl files need installing.
set(EOS_STA_TCL_FILES
  src/tcl/Init.tcl
  src/tcl/Util.tcl
  src/tcl/CmdArgs.tcl
  src/tcl/CmdUtil.tcl
  src/tcl/Property.tcl
  src/tcl/Sta.tcl
  src/tcl/Splash.tcl
  src/dcalc/DelayCalc.tcl
  src/graph/Graph.tcl
  src/liberty/Liberty.tcl
  src/network/Link.tcl
  src/network/Network.tcl
  src/network/NetworkEdit.tcl
  src/parasitics/Parasitics.tcl
  src/power/Power.tcl
  src/sdc/Sdc.tcl
  src/sdc/Variables.tcl
  src/sdf/Sdf.tcl
  src/search/Search.tcl
  src/spice/WriteSpice.tcl
  src/verilog/Verilog.tcl
  src/tcl/AnalysisCorner.tcl
)
set(EOS_STA_TCL_FILES_ABS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tcl/Init.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tcl/Util.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tcl/CmdArgs.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tcl/CmdUtil.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tcl/Property.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tcl/Sta.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tcl/Splash.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dcalc/DelayCalc.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/Graph.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/liberty/Liberty.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/network/Link.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/network/Network.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/network/NetworkEdit.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/parasitics/Parasitics.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/power/Power.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sdc/Sdc.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sdc/Variables.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sdf/Sdf.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/search/Search.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/spice/WriteSpice.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/verilog/Verilog.tcl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tcl/AnalysisCorner.tcl
)
set(EOS_STA_TCL_INIT ${CMAKE_CURRENT_BINARY_DIR}/StaTclInitVar.cc)
add_custom_command(OUTPUT ${EOS_STA_TCL_INIT}
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/src/etc/TclEncode.tcl
    ${EOS_STA_TCL_INIT} tcl_inits ${EOS_STA_TCL_FILES}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${EOS_STA_TCL_FILES_ABS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/etc/TclEncode.tcl)

# Upstream source list, verbatim from the pinned CMakeLists.txt (the
# duplicate search/Path.cc entry upstream carries is listed once),
# plus the platform machine file.
set(EOS_STA_SOURCE
  src/app/StaMain.cc
  src/dcalc/ArcDcalcWaveforms.cc
  src/dcalc/ArcDelayCalc.cc
  src/dcalc/ArnoldiDelayCalc.cc
  src/dcalc/ArnoldiReduce.cc
  src/dcalc/CcsCeffDelayCalc.cc
  src/dcalc/Delay.cc
  src/dcalc/DelayCalc.cc
  src/dcalc/DelayCalcBase.cc
  src/dcalc/DelayNormal.cc
  src/dcalc/DelayScalar.cc
  src/dcalc/DelaySkewNormal.cc
  src/dcalc/DmpCeff.cc
  src/dcalc/DmpDelayCalc.cc
  src/dcalc/FindRoot.cc
  src/dcalc/GraphDelayCalc.cc
  src/dcalc/LumpedCapDelayCalc.cc
  src/dcalc/NetCaps.cc
  src/dcalc/ParallelDelayCalc.cc
  src/dcalc/PrimaDelayCalc.cc
  src/dcalc/UnitDelayCalc.cc
  src/graph/Graph.cc
  src/graph/GraphCmp.cc
  src/liberty/EquivCells.cc
  src/liberty/FuncExpr.cc
  src/liberty/InternalPower.cc
  src/liberty/LeakagePower.cc
  src/liberty/Liberty.cc
  src/liberty/LibertyBuilder.cc
  src/liberty/LibertyBinaryWriter.cc
  src/liberty/LibertyBinaryReader.cc
  src/liberty/LibExprReader.cc
  src/liberty/LibertyParser.cc
  src/liberty/LibertyReader.cc
  src/liberty/LibertyWriter.cc
  src/liberty/LinearModel.cc
  src/liberty/Sequential.cc
  src/liberty/TableModel.cc
  src/liberty/TimingArc.cc
  src/liberty/TimingModel.cc
  src/liberty/TimingRole.cc
  src/liberty/Units.cc
  src/liberty/Wireload.cc
  src/network/ConcreteLibrary.cc
  src/network/ConcreteNetwork.cc
  src/network/HpinDrvrLoad.cc
  src/network/Network.cc
  src/network/NetworkCmp.cc
  src/network/ParseBus.cc
  src/network/PortDirection.cc
  src/network/SdcNetwork.cc
  src/network/VerilogNamespace.cc
  src/parasitics/ConcreteParasitics.cc
  src/parasitics/EstimateParasitics.cc
  src/parasitics/Parasitics.cc
  src/parasitics/ReduceParasitics.cc
  src/parasitics/ReportParasiticAnnotation.cc
  src/parasitics/SpefNamespace.cc
  src/parasitics/SpefReader.cc
  src/parasitics/SpefReaderPvt.hh
  src/power/Power.cc
  src/power/ReportPower.cc
  src/power/VcdReader.cc
  src/power/SaifReader.cc
  src/power/VcdParse.cc
  src/sdc/Clock.cc
  src/sdc/ClockGatingCheck.cc
  src/sdc/ClockGroups.cc
  src/sdc/ClockInsertion.cc
  src/sdc/ClockLatency.cc
  src/sdc/CycleAccting.cc
  src/sdc/DataCheck.cc
  src/sdc/DeratingFactors.cc
  src/sdc/DisabledPorts.cc
  src/sdc/ExceptionPath.cc
  src/sdc/FilterObjects.cc
  src/sdc/InputDrive.cc
  src/sdc/PinPair.cc
  src/sdc/PortDelay.cc
  src/sdc/PortExtCap.cc
  src/sdc/Sdc.cc
  src/sdc/SdcCmdComment.cc
  src/sdc/Variables.cc
  src/sdc/WriteSdc.cc
  src/sdf/ReportAnnotation.cc
  src/sdf/SdfReader.cc
  src/sdf/SdfWriter.cc
  src/search/Bdd.cc
  src/search/Bfs.cc
  src/search/CheckMaxSkews.cc
  src/search/CheckMinPeriods.cc
  src/search/CheckMinPulseWidths.cc
  src/search/CheckCapacitances.cc
  src/search/CheckFanouts.cc
  src/search/CheckSlews.cc
  src/search/CheckTiming.cc
  src/search/ClkInfo.cc
  src/search/ClkLatency.cc
  src/search/ClkNetwork.cc
  src/search/ClkSkew.cc
  src/search/Crpr.cc
  src/search/FindRegister.cc
  src/search/GatedClk.cc
  src/search/Genclks.cc
  src/search/Latches.cc
  src/search/Levelize.cc
  src/search/MakeTimingModel.cc
  src/search/Mode.cc
  src/search/Path.cc
  src/search/PathEnd.cc
  src/search/PathEnum.cc
  src/search/PathExpanded.cc
  src/search/PathGroup.cc
  src/search/PocvMode.cc
  src/search/Property.cc
  src/search/ReportPath.cc
  src/search/Search.cc
  src/search/SearchPred.cc
  src/search/AnalysisCorner.cc
  src/search/Scene.cc
  src/search/Sim.cc
  src/search/Sta.cc
  src/search/StaState.cc
  src/search/Tag.cc
  src/search/TagGroup.cc
  src/search/VertexVisitor.cc
  src/search/VisitPathEnds.cc
  src/search/WorstSlack.cc
  src/spice/WritePathSpice.cc
  src/spice/WriteSpice.cc
  src/spice/Xyce.cc
  src/tcl/TclTypeHelpers.cc
  src/util/Debug.cc
  src/util/DispatchQueue.cc
  src/util/Error.cc
  src/util/Fuzzy.cc
  src/util/Hash.cc
  src/util/MinMax.cc
  src/util/PatternMatch.cc
  src/util/Report.cc
  src/util/ReportStd.cc
  src/util/ReportTcl.cc
  src/util/RiseFallMinMax.cc
  src/util/RiseFallMinMaxDelay.cc
  src/util/RiseFallValues.cc
  src/util/Stats.cc
  src/util/StringUtil.cc
  src/util/Transition.cc
  src/verilog/VerilogReader.cc
  src/verilog/VerilogWriter.cc
)
if(APPLE)
  list(APPEND EOS_STA_SOURCE src/util/MachineApple.cc)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND EOS_STA_SOURCE src/util/MachineLinux.cc)
elseif(WIN32)
  list(APPEND EOS_STA_SOURCE src/util/MachineWin32.cc)
else()
  list(APPEND EOS_STA_SOURCE src/util/MachineUnknown.cc)
endif()

# The vendored CUDD closure, compiled as C objects folded into
# eda_analysis_sta below. src/cudd/config.h is the configuration
# header CUDD's configure generated for this platform, kept as a
# build configuration file (see PROVENANCE.md).
add_library(eos_sta_cudd OBJECT
  src/cudd/cudd/cuddAPI.c
  src/cudd/cudd/cuddAddAbs.c
  src/cudd/cudd/cuddAddApply.c
  src/cudd/cudd/cuddAddFind.c
  src/cudd/cudd/cuddAddInv.c
  src/cudd/cudd/cuddAddIte.c
  src/cudd/cudd/cuddAddNeg.c
  src/cudd/cudd/cuddAddWalsh.c
  src/cudd/cudd/cuddAndAbs.c
  src/cudd/cudd/cuddAnneal.c
  src/cudd/cudd/cuddApa.c
  src/cudd/cudd/cuddApprox.c
  src/cudd/cudd/cuddBddAbs.c
  src/cudd/cudd/cuddBddCorr.c
  src/cudd/cudd/cuddBddIte.c
  src/cudd/cudd/cuddBridge.c
  src/cudd/cudd/cuddCache.c
  src/cudd/cudd/cuddCheck.c
  src/cudd/cudd/cuddClip.c
  src/cudd/cudd/cuddCof.c
  src/cudd/cudd/cuddCompose.c
  src/cudd/cudd/cuddDecomp.c
  src/cudd/cudd/cuddEssent.c
  src/cudd/cudd/cuddExact.c
  src/cudd/cudd/cuddExport.c
  src/cudd/cudd/cuddGenCof.c
  src/cudd/cudd/cuddGenetic.c
  src/cudd/cudd/cuddGroup.c
  src/cudd/cudd/cuddHarwell.c
  src/cudd/cudd/cuddInit.c
  src/cudd/cudd/cuddInteract.c
  src/cudd/cudd/cuddLCache.c
  src/cudd/cudd/cuddLevelQ.c
  src/cudd/cudd/cuddLinear.c
  src/cudd/cudd/cuddLiteral.c
  src/cudd/cudd/cuddMatMult.c
  src/cudd/cudd/cuddPriority.c
  src/cudd/cudd/cuddRead.c
  src/cudd/cudd/cuddRef.c
  src/cudd/cudd/cuddReorder.c
  src/cudd/cudd/cuddSat.c
  src/cudd/cudd/cuddSign.c
  src/cudd/cudd/cuddSolve.c
  src/cudd/cudd/cuddSplit.c
  src/cudd/cudd/cuddSubsetHB.c
  src/cudd/cudd/cuddSubsetSP.c
  src/cudd/cudd/cuddSymmetry.c
  src/cudd/cudd/cuddTable.c
  src/cudd/cudd/cuddUtil.c
  src/cudd/cudd/cuddWindow.c
  src/cudd/cudd/cuddZddCount.c
  src/cudd/cudd/cuddZddFuncs.c
  src/cudd/cudd/cuddZddGroup.c
  src/cudd/cudd/cuddZddIsop.c
  src/cudd/cudd/cuddZddLin.c
  src/cudd/cudd/cuddZddMisc.c
  src/cudd/cudd/cuddZddPort.c
  src/cudd/cudd/cuddZddReord.c
  src/cudd/cudd/cuddZddSetop.c
  src/cudd/cudd/cuddZddSymm.c
  src/cudd/cudd/cuddZddUtil.c
  src/cudd/util/cpu_stats.c
  src/cudd/util/cpu_time.c
  src/cudd/util/cstringstream.c
  src/cudd/util/datalimit.c
  src/cudd/util/pathsearch.c
  src/cudd/util/pipefork.c
  src/cudd/util/prtime.c
  src/cudd/util/safe_mem.c
  src/cudd/util/strsav.c
  src/cudd/util/texpand.c
  src/cudd/util/ucbqsort.c
  src/cudd/st/st.c
  src/cudd/epd/epd.c
  src/cudd/mtr/mtrBasic.c
  src/cudd/mtr/mtrGroup.c
)
target_include_directories(eos_sta_cudd PRIVATE
  src/cudd src/cudd/cudd src/cudd/util src/cudd/st src/cudd/epd
  src/cudd/mtr)
set_target_properties(eos_sta_cudd PROPERTIES
  POSITION_INDEPENDENT_CODE ON)

add_library(eda_analysis_sta STATIC
  src/sta_facade.cc
  ${EOS_STA_SOURCE}
  ${EOS_STA_TCL_INIT}
  ${FLEX_eos_sta_libexpr_lex_OUTPUTS}
  ${BISON_eos_sta_libexpr_parse_OUTPUTS}
  ${FLEX_eos_sta_liberty_lex_OUTPUTS}
  ${BISON_eos_sta_liberty_parse_OUTPUTS}
  ${FLEX_eos_sta_spef_lex_OUTPUTS}
  ${BISON_eos_sta_spef_parse_OUTPUTS}
  ${FLEX_eos_sta_sdf_lex_OUTPUTS}
  ${BISON_eos_sta_sdf_parse_OUTPUTS}
  ${FLEX_eos_sta_verilog_lex_OUTPUTS}
  ${BISON_eos_sta_verilog_parse_OUTPUTS}
  ${FLEX_eos_sta_saif_lex_OUTPUTS}
  ${BISON_eos_sta_saif_parse_OUTPUTS}
  $<TARGET_OBJECTS:eos_sta_cudd>
)
# P1 (place-and-route unit P1) promotes the generated
# include/sta dir to PUBLIC: public sta headers (Delay.hh)
# include StaConfig.hh, which lives only there. This is
# EOS-owned CMake, not an upstream patch.
target_include_directories(eda_analysis_sta
  PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/include/sta
  PRIVATE
    include/sta
    src
    src/cudd src/cudd/cudd src/cudd/util src/cudd/st src/cudd/epd
    src/cudd/mtr
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${FLEX_INCLUDE_DIRS}
)
if(TCL_FOUND)
  target_include_directories(eda_analysis_sta PRIVATE ${TCL_INCLUDE_PATH})
  target_link_libraries(eda_analysis_sta PRIVATE ${TCL_LIBRARY})
endif()
target_link_libraries(eda_analysis_sta
  PUBLIC eda_foundation_errors
  PRIVATE Eigen3::Eigen Threads::Threads
)
if(ZLIB_FOUND)
  target_link_libraries(eda_analysis_sta PRIVATE ZLIB::ZLIB)
endif()
if(NOT HAVE_CXX_STD_FORMAT)
  target_include_directories(eda_analysis_sta PUBLIC "${_eos_sta_fmt_dir}")
  target_link_libraries(eda_analysis_sta PRIVATE spdlog::spdlog)
endif()
if(NOT HAVE_CXX_STD_FORMAT AND NOT MSVC)
  # Carry the fmt 11 force-include on the STA library's link
  # interface so every STA-header consumer outside this directory
  # (dbsta, floorplan, the timing service, tests, and anything
  # linking them) inherits it with the fmt include dir above.
  # PUBLIC so the flag travels the same link chains; the
  # COMPILE_LANGUAGE genex keeps it off C sources, and the guard
  # keeps it off MSVC and off std::format builds, where fmt may
  # not be installed at all and the forced include would fail.
  target_compile_options(eda_analysis_sta
    PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:-include;fmt/format.h>")
endif()
set_target_properties(eda_analysis_sta PROPERTIES
  POSITION_INDEPENDENT_CODE ON CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_features(eda_analysis_sta PUBLIC cxx_std_20)

# SWIG Tcl wrapper for src/app/StaApp.i, generated with the same
# options upstream uses (-module sta -namespace -prefix sta) and
# folded into eda_analysis_sta as objects: the wrapper defines the
# Sta_Init entry point the facade registers with its interpreter.
set_property(SOURCE src/app/StaApp.i PROPERTY CPLUSPLUS ON)
set_property(SOURCE src/app/StaApp.i PROPERTY COMPILE_OPTIONS
  -module sta -namespace -prefix sta)
set_property(SOURCE src/app/StaApp.i PROPERTY DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dcalc/DelayCalc.i
  ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/Graph.i
  ${CMAKE_CURRENT_SOURCE_DIR}/src/liberty/Liberty.i
  ${CMAKE_CURRENT_SOURCE_DIR}/src/network/Network.i
  ${CMAKE_CURRENT_SOURCE_DIR}/src/network/NetworkEdit.i
  ${CMAKE_CURRENT_SOURCE_DIR}/src/parasitics/Parasitics.i
  ${CMAKE_CURRENT_SOURCE_DIR}/src/power/Power.i
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sdc/Sdc.i
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sdf/Sdf.i
  ${CMAKE_CURRENT_SOURCE_DIR}/src/search/Property.i
  ${CMAKE_CURRENT_SOURCE_DIR}/src/search/Search.i
  ${CMAKE_CURRENT_SOURCE_DIR}/src/spice/WriteSpice.i
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tcl/Exception.i
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tcl/StaTclTypes.i
  ${CMAKE_CURRENT_SOURCE_DIR}/src/util/Util.i
  ${CMAKE_CURRENT_SOURCE_DIR}/src/verilog/Verilog.i
  ${CMAKE_CURRENT_SOURCE_DIR}/src/search/AnalysisCorner.i
)
swig_add_library(eos_sta_swig LANGUAGE tcl TYPE OBJECT
  SOURCES src/app/StaApp.i)
get_target_property(_eos_sta_swig_cxx eos_sta_swig SOURCES)
set_source_files_properties(${_eos_sta_swig_cxx}
  PROPERTIES COMPILE_OPTIONS
  "-Wunused-variable;-Wno-cast-qual;-Wno-missing-braces;-Wno-deprecated-declarations")
target_include_directories(eos_sta_swig
  PUBLIC include
  PRIVATE
    include/sta
    src
    src/cudd src/cudd/cudd src/cudd/util src/cudd/st src/cudd/epd
    src/cudd/mtr
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include/sta
)
target_link_libraries(eos_sta_swig PRIVATE Eigen3::Eigen)
if(TCL_FOUND)
  target_include_directories(eos_sta_swig PRIVATE ${TCL_INCLUDE_PATH})
endif()
if(NOT HAVE_CXX_STD_FORMAT)
  target_include_directories(eos_sta_swig PRIVATE "${_eos_sta_fmt_dir}")
endif()
set_target_properties(eos_sta_swig PROPERTIES
  POSITION_INDEPENDENT_CODE ON CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
  SWIG_USE_TARGET_INCLUDE_DIRECTORIES TRUE)
target_sources(eda_analysis_sta PRIVATE $<TARGET_OBJECTS:eos_sta_swig>)
