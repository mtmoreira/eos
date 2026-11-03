# route: the EOS-owned global route runner over the imported
# OpenROAD global router (grt, P2 support import). No engine is
# imported by this unit: grt, ant, stt, and the detailed placer
# (whose Opendp the router takes as its diode-legalization source)
# are already in-tree. The runner passes a null resizer and pins
# the critical-nets percentage to 0, because the wire-parasitics
# builder that path needs is still deferred (see grt PATCHES.md).
add_library(eda_pd_route STATIC
  src/route.cc
)
target_include_directories(eda_pd_route
  PUBLIC include
  PRIVATE src
)
target_link_libraries(eda_pd_route
  PUBLIC eda_pd_grt eda_pd_drt eda_pd_place eda_pd_dbsta eda_db_odb
         eda_analysis_sta eda_foundation_errors
)
# route.cc creates a private Tcl interp for its STA session (see
# the TclInterpGuard comment in route.cc); consumers do not need
# Tcl headers.
if(TCL_FOUND)
  target_include_directories(eda_pd_route PRIVATE ${TCL_INCLUDE_PATH})
  target_link_libraries(eda_pd_route PRIVATE ${TCL_LIBRARY})
endif()
set_target_properties(eda_pd_route PROPERTIES POSITION_INDEPENDENT_CODE ON)
