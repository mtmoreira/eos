# flow: flow orchestration (L5 session). Its first feature is the
# timing closure loop: it sequences the L4 ECO transaction engine
# and adapters under the project's ledger and report registry.
add_library(eda_session_flow STATIC src/flow.cc)
target_include_directories(eda_session_flow PUBLIC include)
target_link_libraries(eda_session_flow
  PUBLIC eda_session_session eda_session_reports eda_session_ledger
         eda_eco_transactions eda_eco_timing_eco eda_analysis_timing
         eda_db_odb eda_foundation_errors)
