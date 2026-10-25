# registry: the one command registry (L6 interface).
add_library(eda_ui_registry STATIC src/registry.cc src/core_commands.cc)
target_include_directories(eda_ui_registry PUBLIC include)
target_link_libraries(eda_ui_registry
  PUBLIC eda_session_session eda_session_ledger eda_session_reports
         eda_db_design eda_db_scenarios eda_db_constraints
         eda_db_checkpoint eda_foundation_ids eda_foundation_errors)
