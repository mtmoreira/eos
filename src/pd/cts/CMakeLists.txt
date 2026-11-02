# cts: TritonCTS clock tree synthesis (imported from OpenROAD at
# ead3236, see PROVENANCE.md and PATCHES.md) plus the EOS-owned
# cts runner. LEMON is a declared external dependency (see
# third_party_manifest.yaml): it is never vendored. The build finds
# LEMON through its CMake config (liblemon-dev). A missing LEMON
# fails configure loudly, because the sink-clustering code cannot
# compile without it.
find_package(lemon QUIET)
if(NOT lemon_FOUND)
  message(FATAL_ERROR
    "cts: LEMON not found. Install liblemon-dev (system package, "
    "see third_party_manifest.yaml and .github/workflows/ci.yml).")
endif()

# cts sources: TritonCTS (upstream, ported copies of TritonCTS.cpp,
# TechChar.cpp/.h, Clock.cpp, and Util.h only; see PATCHES.md).
# MakeTritoncts.cpp
# (Tcl factory), CtsGraphics.cpp/.h (GUI renderer), all .tcl/.i
# surfaces, and the upstream test directory are not imported: the
# EOS runner constructs TritonCTS directly and the EOS registry
# registers eos::cts.
add_library(eda_pd_cts STATIC
  src/cts.cc
  src/cts_resizer.cc
  src/Clock.cpp
  src/Clustering.cpp
  src/HTreeBuilder.cpp
  src/LevelBalancer.cpp
  src/SinkClustering.cpp
  src/TechChar.cpp
  src/TreeBuilder.cpp
  src/TritonCTS.cpp
)
target_include_directories(eda_pd_cts
  PUBLIC include
  PRIVATE src
)
target_link_libraries(eda_pd_cts
  PUBLIC eda_pd_stt eda_pd_rsz eda_pd_dbsta eda_db_odb
         eda_analysis_sta eda_foundation_errors
  PRIVATE ${LEMON_LIBRARIES}
)
# cts.cc creates a private Tcl interp for its STA session and
# TechChar.cpp creates one for its scratch STA session (see the
# TclInterpGuard comment in cts.cc); consumers do not need Tcl
# headers.
if(TCL_FOUND)
  target_include_directories(eda_pd_cts PRIVATE ${TCL_INCLUDE_PATH})
  target_link_libraries(eda_pd_cts PRIVATE ${TCL_LIBRARY})
endif()
target_include_directories(eda_pd_cts PRIVATE ${LEMON_INCLUDE_DIRS})
# Clustering.cpp is the only CTS translation unit that includes
# LEMON headers, and it includes no STA headers; LEMON 1.3.1
# predates the C++20 removal of std::allocator::construct/destroy,
# so its array_map.h does not compile under the gnu++20 that
# eda_analysis_sta propagates. Compiling just that unit at
# gnu++17 follows the eda_pd_stt precedent (same LEMON, same
# reason); the PRIVATE -std flag is placed after the propagated
# gnu++20 flag on the compile line, so GCC uses the last -std it
# sees. No imported file is patched.
set_source_files_properties(src/Clustering.cpp PROPERTIES
  COMPILE_OPTIONS "-std=gnu++17")
set_target_properties(eda_pd_cts PROPERTIES POSITION_INDEPENDENT_CODE ON)
