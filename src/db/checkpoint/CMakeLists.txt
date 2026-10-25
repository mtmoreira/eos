# checkpoint: saves and restores design-data checkpoints (L2).
add_library(eda_db_checkpoint STATIC src/checkpoint.cc)
target_include_directories(eda_db_checkpoint PUBLIC include)
target_link_libraries(eda_db_checkpoint
  PUBLIC eda_db_design eda_db_scenarios eda_db_constraints
         eda_foundation_ids eda_foundation_errors)
