# Shared support for physical-design engines: FLUTE Steiner-tree tables
# and the pdr Prim-Dijkstra Steiner-tree implementation (see README.md).
add_library(eda_pd_pdr STATIC)

# FLUTE lookup tables are generated at build time from the committed
# .dat data files and are never committed (POWV9 about 3.8 MB, POST9
# about 8.7 MB). The generator is the EOS-owned script
# tools/make_dat_var.py (see README.md licence decision); no upstream
# generator script is imported.
find_program(PDR_PYTHON3 NAMES python3)
if(NOT PDR_PYTHON3)
  message(FATAL_ERROR
    "eda_pd_pdr: python3 is required to generate the FLUTE lookup tables "
    "from src/pd/pdr/data/*.dat. Install python3 and re-configure.")
endif()
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/POWV9.cpp
  COMMAND ${PDR_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_dat_var.py
          powv9 ${CMAKE_CURRENT_BINARY_DIR}/POWV9.cpp
          ${CMAKE_CURRENT_SOURCE_DIR}/data/POWV9.dat
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_dat_var.py
          ${CMAKE_CURRENT_SOURCE_DIR}/data/POWV9.dat
  COMMENT "Generating FLUTE POWV9 table")
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/POST9.cpp
  COMMAND ${PDR_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_dat_var.py
          post9 ${CMAKE_CURRENT_BINARY_DIR}/POST9.cpp
          ${CMAKE_CURRENT_SOURCE_DIR}/data/POST9.dat
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_dat_var.py
          ${CMAKE_CURRENT_SOURCE_DIR}/data/POST9.dat
  COMMENT "Generating FLUTE POST9 table")

target_sources(eda_pd_pdr PRIVATE
  src/flute.cpp
  src/graph.cpp
  src/node.cpp
  src/edge.cpp
  src/pdrev.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/POWV9.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/POST9.cpp)

# Public headers live in include/pdr/; the module's own sources use the
# upstream same-directory include style (graph.h, flute.h), resolved by
# the private include/pdr entry.
target_include_directories(eda_pd_pdr
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/pdr)

# Link note: the staged CMakeLists linked only eda_db_odb, while the
# stage-1 plan said eda_db_odb plus eda_analysis_sta (PUBLIC, C++20
# and STA types). The sources actually need only eda_db_odb: they
# include utl/Logger.h and no STA header, and use no C++20 construct
# (the repo default is C++17). The build proves the staged form, so
# it stands; if a future source adds an STA dependency, the link
# grows then and this note is updated.
target_link_libraries(eda_pd_pdr PUBLIC eda_db_odb)

set_target_properties(eda_pd_pdr PROPERTIES POSITION_INDEPENDENT_CODE ON)
