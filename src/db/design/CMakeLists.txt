# design: the Design object graph (L2 design-data).
add_library(eda_db_design STATIC src/design.cc)
target_include_directories(eda_db_design PUBLIC include)
target_link_libraries(eda_db_design
  PUBLIC eda_foundation_ids eda_foundation_errors)
