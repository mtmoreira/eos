# registry: the one command registry (L6 interface).
add_library(eda_ui_registry STATIC src/registry.cc src/core_commands.cc)
target_include_directories(eda_ui_registry PUBLIC include)
# The synthesis commands drive the Yosys and ABC facades, so the
# registry links both: Yosys whole-archive, exactly as every other
# Yosys consumer must, because its passes register through global
# constructors a plain archive pull never includes.
target_link_libraries(eda_ui_registry
  PUBLIC eda_session_session eda_session_ledger eda_session_reports
         eda_session_flow
         eda_db_design eda_db_scenarios eda_db_constraints
         eda_db_checkpoint eda_db_odb eda_analysis_timing
         eda_foundation_ids eda_foundation_errors
         eda_pd_floorplan
         eda_synth_abc
         "$<LINK_LIBRARY:WHOLE_ARCHIVE,eda_synth_yosys>")
