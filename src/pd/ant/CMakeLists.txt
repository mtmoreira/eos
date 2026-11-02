# ant: OpenROAD AntennaChecker (1 TU), a link/runtime support import
# for grt only. There is no eos::antenna command in this phase; PV
# stays orchestration (see PROVENANCE.md). The Tcl factory
# (MakeAntennaChecker) is not imported.
add_library(eda_pd_ant STATIC src/AntennaChecker.cc)
target_include_directories(eda_pd_ant PUBLIC include)
target_link_libraries(eda_pd_ant
  PUBLIC eda_db_odb eda_analysis_sta)
# AntennaChecker.cc includes <tcl.h> (upstream include, kept).
if(TCL_FOUND)
  target_include_directories(eda_pd_ant PRIVATE ${TCL_INCLUDE_PATH})
  target_link_libraries(eda_pd_ant PRIVATE ${TCL_LIBRARY})
endif()
set_target_properties(eda_pd_ant PROPERTIES POSITION_INDEPENDENT_CODE ON)
