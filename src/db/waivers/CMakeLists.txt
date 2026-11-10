# waivers: waiver records (L2 design-data).
add_library(eda_db_waivers STATIC src/waivers.cc)
target_include_directories(eda_db_waivers PUBLIC include)
target_link_libraries(eda_db_waivers
  PUBLIC eda_foundation_errors)
