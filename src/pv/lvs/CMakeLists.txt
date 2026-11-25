# lvs: the connectivity LVS engine (F13 unit).
# EOS-owned, no import: the logical topology and the routed
# geometry both come from the live ODB through RawDatabase().
# Links eda_db_odb for the OpenDB API.
add_library(eda_pv_lvs STATIC src/lvs.cc)
target_include_directories(eda_pv_lvs PUBLIC include)
target_link_libraries(eda_pv_lvs PUBLIC eda_db_odb)
