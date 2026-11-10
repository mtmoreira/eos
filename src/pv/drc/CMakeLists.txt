# drc: the LEF-driven in-tree layout DRC engine (F13 DRC unit).
# EOS-owned, no import: rules from the tech LEF through OpenDB
# (dbTechLayer tables), geometry from the live ODB through
# RawDatabase(). Links eda_db_odb for the OpenDB API.
add_library(eda_pv_drc STATIC src/drc.cc)
target_include_directories(eda_pv_drc PUBLIC include)
target_link_libraries(eda_pv_drc PUBLIC eda_db_odb)
