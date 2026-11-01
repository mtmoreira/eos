# floorplan: OpenROAD InitFloorplan plus the upf support module
# (L4 engines), ported from the OpenDB pin commit ead3236 (see
# PROVENANCE.md and PATCHES.md), driven by the EOS-owned runner in
# src/floorplan.cc. The C++20 standard comes from
# eda_analysis_sta, which sets it PUBLIC.
add_library(eda_pd_floorplan STATIC
  src/InitFloorplan.cc
  src/upf.cpp
  src/floorplan.cc)
target_include_directories(eda_pd_floorplan PUBLIC include)
target_link_libraries(eda_pd_floorplan
  PUBLIC eda_pd_dbsta eda_db_odb eda_analysis_sta eda_foundation_errors)
set_target_properties(eda_pd_floorplan PROPERTIES POSITION_INDEPENDENT_CODE ON)
