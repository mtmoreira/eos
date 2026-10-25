# reports: the report registry and the export freshness gate.
add_library(eda_session_reports STATIC src/reports.cc)
target_include_directories(eda_session_reports PUBLIC include)
target_link_libraries(eda_session_reports
  PUBLIC eda_session_session eda_session_ledger eda_db_scenarios
         eda_db_constraints eda_foundation_errors)
