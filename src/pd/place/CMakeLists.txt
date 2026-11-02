# place: RePlAce global placement (gpl) + OpenROAD detailed placement
# (dpl) + the EOS-owned place runner. OR-Tools and LEMON are declared
# external dependencies (see third_party_manifest.yaml): they are
# never vendored. The build finds OR-Tools through its CMake config
# (CMAKE_PREFIX_PATH pointing at the extracted upstream tarball, or
# a system install); LEMON through its CMake config (liblemon-dev).
# A missing OR-Tools or LEMON fails configure loudly, because the
# mbff and Steiner-tree code cannot compile without them.
find_package(ortools QUIET)
if(NOT ortools_FOUND)
  message(FATAL_ERROR
    "place: OR-Tools 9.5.2237 not found. Download the upstream "
    "tarball (see third_party_manifest.yaml) and configure with "
    "-DCMAKE_PREFIX_PATH=<extracted or-tools dir>, or install it to "
    "a standard prefix. OR-Tools is a declared external dependency "
    "and is never vendored into the repo.")
endif()
# LEMON ships lemonConfig.cmake (lowercase), which sets the
# LEMON_INCLUDE_DIRS / LEMON_LIBRARIES variables and no imported
# target; find_package(lemon) is the spelling that finds it.
find_package(lemon QUIET)
if(NOT lemon_FOUND)
  message(FATAL_ERROR
    "place: LEMON not found. Install liblemon-dev (system package, "
    "see third_party_manifest.yaml and .github/workflows/ci.yml).")
endif()

# gpl sources: RePlAce (upstream, ported copy of timingBase.cpp only;
# see PATCHES.md). gpuSolver.cu is excluded: CPU-only build.
add_library(eda_pd_place STATIC
  src/place.cc
  src/place_resizer.cc
  src/gpl/fft.cpp
  src/gpl/fftsg.cpp
  src/gpl/fftsg2d.cpp
  src/gpl/graphics.cpp
  src/gpl/initialPlace.cpp
  src/gpl/mbff.cpp
  src/gpl/nesterovBase.cpp
  src/gpl/nesterovPlace.cpp
  src/gpl/placerBase.cpp
  src/gpl/point.cpp
  src/gpl/replace.cpp
  src/gpl/routeBase.cpp
  src/gpl/solver.cpp
  src/gpl/timingBase.cpp
  src/dpl/CheckPlacement.cpp
  src/dpl/FillerPlacement.cpp
  src/dpl/Graphics.cpp
  src/dpl/Grid.cpp
  src/dpl/Opendp.cpp
  src/dpl/OptMirror.cpp
  src/dpl/Place.cpp
  src/dpl/dbToOpendp.cpp
)
target_include_directories(eda_pd_place
  PUBLIC include
  PRIVATE src/gpl src/dpl
)
target_link_libraries(eda_pd_place
  PUBLIC eda_pd_grt eda_pd_rsz eda_pd_dbsta eda_db_odb
         eda_analysis_sta eda_foundation_errors
  PRIVATE ortools::ortools ${LEMON_LIBRARIES}
)
# place.cc creates a private Tcl interp for its STA session (see the
# TclInterpGuard comment there); consumers do not need Tcl headers.
if(TCL_FOUND)
  target_include_directories(eda_pd_place PRIVATE ${TCL_INCLUDE_PATH})
  target_link_libraries(eda_pd_place PRIVATE ${TCL_LIBRARY})
endif()
target_include_directories(eda_pd_place PRIVATE ${LEMON_INCLUDE_DIRS})
# gpl uses Eigen and OpenMP (upstream CMakeLists). Eigen3 comes from
# the top-level dependency check; OpenMP from the toolchain.
find_package(Eigen3 QUIET)
if(Eigen3_FOUND OR EIGEN3_FOUND)
  target_link_libraries(eda_pd_place PRIVATE Eigen3::Eigen)
endif()
find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
  target_link_libraries(eda_pd_place PRIVATE OpenMP::OpenMP_CXX)
endif()
set_target_properties(eda_pd_place PROPERTIES POSITION_INDEPENDENT_CODE ON)
