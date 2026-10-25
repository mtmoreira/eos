# constraints: constraint objects bound to design objects (L2).
add_library(eda_db_constraints STATIC src/constraints.cc)
target_include_directories(eda_db_constraints PUBLIC include)
target_link_libraries(eda_db_constraints
  PUBLIC eda_db_design eda_foundation_ids eda_foundation_errors)
