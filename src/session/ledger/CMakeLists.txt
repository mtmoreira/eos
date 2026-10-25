# ledger: the state ledger (L5 session).
add_library(eda_session_ledger STATIC src/ledger.cc)
target_include_directories(eda_session_ledger PUBLIC include)
target_link_libraries(eda_session_ledger
  PUBLIC eda_foundation_ids eda_foundation_errors)
