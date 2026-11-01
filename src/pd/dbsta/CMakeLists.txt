# dbsta: the OpenROAD dbSta network bridge (L4 engines), merged
# from the OpenDB pin commit ead3236 (P1 side) and OpenROAD v2.0
# at a5a56584 (R1 side) (see PROVENANCE.md and PATCHES.md). The port copies are EOS-owned code: every adaptation
# to the in-tree OpenSTA API lives in these copies, and no file
# under src/analysis/timing/sta is modified. The C++20 standard
# comes from eda_analysis_sta, which sets it PUBLIC.
add_library(eda_pd_dbsta STATIC
  src/dbNetwork.cc
  src/dbSta.cc
  src/dbReadVerilog.cc
  src/dbSdcNetwork.cc
  src/ord_shim.cc
  src/stub.cpp
  src/stub_heatMap.cpp)
target_include_directories(eda_pd_dbsta PUBLIC include)
target_link_libraries(eda_pd_dbsta
  PUBLIC eda_db_odb eda_analysis_sta)
if(TCL_FOUND)
  target_include_directories(eda_pd_dbsta PRIVATE ${TCL_INCLUDE_PATH})
  target_link_libraries(eda_pd_dbsta PRIVATE ${TCL_LIBRARY})
endif()
set_target_properties(eda_pd_dbsta PROPERTIES POSITION_INDEPENDENT_CODE ON)
