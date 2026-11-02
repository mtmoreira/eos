# stt: OpenROAD Steiner-tree builder (SteinerTreeBuilder, FLUTE,
# and the ead3236 Prim-Dijkstra pd.cpp), a support import for grt.
# The Tcl factory (MakeSteinerTreeBuilder) and the LinesRenderer GUI
# helper are not imported. FLUTE lookup tables are generated at
# build time from the pdr module's committed .dat files (referenced
# by CMake path, not duplicated) by the EOS-owned generator in
# tools/make_dat_var.py, emitting namespace stt::flt variables as
# flute.cpp declares them. The upstream MakeDatVar.tcl is GPL-3.0
# and is not imported.
find_program(STT_PYTHON3 NAMES python3)
if(NOT STT_PYTHON3)
  message(FATAL_ERROR
    "eda_pd_stt: python3 is required to generate the FLUTE lookup "
    "tables from src/pd/pdr/data/*.dat. Install python3 and "
    "re-configure.")
endif()
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/POWV9.cpp
  COMMAND ${STT_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_dat_var.py
          powv9 ${CMAKE_CURRENT_BINARY_DIR}/POWV9.cpp
          ${CMAKE_CURRENT_SOURCE_DIR}/../pdr/data/POWV9.dat
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_dat_var.py
          ${CMAKE_CURRENT_SOURCE_DIR}/../pdr/data/POWV9.dat
  COMMENT "Generating stt FLUTE POWV9 table")
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/POST9.cpp
  COMMAND ${STT_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_dat_var.py
          post9 ${CMAKE_CURRENT_BINARY_DIR}/POST9.cpp
          ${CMAKE_CURRENT_SOURCE_DIR}/../pdr/data/POST9.dat
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_dat_var.py
          ${CMAKE_CURRENT_SOURCE_DIR}/../pdr/data/POST9.dat
  COMMENT "Generating stt FLUTE POST9 table")

add_library(eda_pd_stt STATIC
  src/SteinerTreeBuilder.cpp
  src/flute.cpp
  src/pd.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/POWV9.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/POST9.cpp
)

# Public headers live in include/stt/; flute.h includes
# "SteinerTreeBuilder.h" bare, resolved by the private include/stt
# entry (rsz/pdr precedent). pd.cpp includes "stt/pd.h" and
# <lemon/list_graph.h>: LEMON is a declared external system
# dependency (liblemon-dev), found through its CMake config.
target_include_directories(eda_pd_stt
  PUBLIC include
  PRIVATE include/stt
)
# LEMON ships lemonConfig.cmake (lowercase), which sets the
# LEMON_INCLUDE_DIRS / LEMON_LIBRARIES variables and no imported
# target; find_package(lemon) is the spelling that finds it.
find_package(lemon QUIET)
if(NOT lemon_FOUND)
  message(FATAL_ERROR
    "eda_pd_stt: LEMON not found. Install liblemon-dev (system "
    "package, see third_party_manifest.yaml and "
    ".github/workflows/ci.yml).")
endif()
target_link_libraries(eda_pd_stt
  PUBLIC eda_db_odb eda_analysis_sta
  PRIVATE ${LEMON_LIBRARIES}
)
target_include_directories(eda_pd_stt PRIVATE ${LEMON_INCLUDE_DIRS})
set_target_properties(eda_pd_stt PROPERTIES POSITION_INDEPENDENT_CODE ON)
# LEMON 1.3.1 predates the C++20 removal of
# std::allocator::construct/destroy, so its array_map.h does not
# compile under the gnu++20 that eda_analysis_sta propagates
# (target_compile_features PUBLIC cxx_std_20). This target's
# upstream sources are C++17-era OpenROAD code and include only
# odb, utl/Logger, and standard headers (no sta headers), and the
# in-tree odb target already ships gnu++17 objects into gnu++20
# consumers, so compiling this target at gnu++17 is the same
# pattern. Glue only: no imported file is patched, no system
# header is patched, and the sta link above is unchanged. The
# PRIVATE -std flag is placed after the propagated gnu++20 flag
# on the compile line, so GCC uses the last -std it sees.
target_compile_options(eda_pd_stt
  PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>")
