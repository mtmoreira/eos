# rsz: the OpenROAD v2.0 timing-repair engine (see README.md).
add_library(eda_pd_rsz STATIC
  src/BufferedNet.cc
  src/EstimateWireParasitics.cc
  src/MakeResizer.cc
  src/Rebuffer.cc
  src/Resizer.cc
  src/SteinerTree.cc)

# Public headers live in include/rsz/; the module's own headers use the
# upstream same-directory include style (SteinerTree.hh, BufferedNet.hh),
# resolved by the private include/rsz entry (pdr precedent).
target_include_directories(eda_pd_rsz
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/rsz)

# Same-layer dependencies (L4 on L4) are not upward dependencies.
# eda_analysis_sta is PUBLIC for C++20 and STA types (dbsta precedent);
# eda_pd_dbsta for the dbSta/dbNetwork adapter, eda_pd_pdr for
# primDijkstra and FLUTE via SteinerTree.
target_link_libraries(eda_pd_rsz
  PUBLIC eda_pd_dbsta eda_pd_pdr eda_db_odb eda_analysis_sta)

# Resizer.cc includes <tcl.h> for the scratch-sta interp (P-30).
# Consumers do not need Tcl headers: Resizer.hh gets Tcl_Interp
# transitively via Sta.hh's forward declaration.
if(TCL_FOUND)
  target_include_directories(eda_pd_rsz PRIVATE ${TCL_INCLUDE_PATH})
  target_link_libraries(eda_pd_rsz PRIVATE ${TCL_LIBRARY})
endif()

set_target_properties(eda_pd_rsz PROPERTIES POSITION_INDEPENDENT_CODE ON)
