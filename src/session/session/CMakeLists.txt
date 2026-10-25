# session: the Session and Project objects (L5 session).
add_library(eda_session_session STATIC src/session.cc src/project.cc)
target_include_directories(eda_session_session PUBLIC include)
target_link_libraries(eda_session_session
  PUBLIC eda_session_ledger eda_db_checkpoint eda_db_design
         eda_db_scenarios eda_db_constraints
         eda_foundation_ids eda_foundation_errors)
