# transactions: the ECO transaction engine. Checkpoints the ODB
# database, applies an edit, re-analyzes every required scene
# through the timing service (L3) and commits or rolls back on the
# all-scene verdict. The ledger wrap is the caller's (L5 owns the
# ledger; see include/eos/eco/transactions.h).
add_library(eda_eco_transactions STATIC src/transactions.cc)
target_include_directories(eda_eco_transactions PUBLIC include)
target_link_libraries(eda_eco_transactions
  PUBLIC eda_analysis_timing eda_db_odb eda_foundation_errors)
