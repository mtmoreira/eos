# Unit tests, mirroring src/ layer for layer and module for module.

add_executable(eda_foundation_ids_unit_test foundation/ids/ids_test.cc)
target_link_libraries(eda_foundation_ids_unit_test
  PRIVATE eda_foundation_ids GTest::gtest GTest::gtest_main)
add_test(NAME foundation.ids COMMAND eda_foundation_ids_unit_test)

add_executable(eda_foundation_errors_unit_test foundation/errors/errors_test.cc)
target_link_libraries(eda_foundation_errors_unit_test
  PRIVATE eda_foundation_errors GTest::gtest GTest::gtest_main)
add_test(NAME foundation.errors COMMAND eda_foundation_errors_unit_test)

add_executable(eda_session_ledger_unit_test session/ledger/ledger_test.cc)
target_link_libraries(eda_session_ledger_unit_test
  PRIVATE eda_session_ledger GTest::gtest GTest::gtest_main)
add_test(NAME session.ledger COMMAND eda_session_ledger_unit_test)

add_executable(eda_db_design_unit_test db/design/design_test.cc)
target_link_libraries(eda_db_design_unit_test
  PRIVATE eda_db_design GTest::gtest GTest::gtest_main)
add_test(NAME db.design COMMAND eda_db_design_unit_test)

add_executable(eda_db_scenarios_unit_test db/scenarios/scenarios_test.cc)
target_link_libraries(eda_db_scenarios_unit_test
  PRIVATE eda_db_scenarios GTest::gtest GTest::gtest_main)
add_test(NAME db.scenarios COMMAND eda_db_scenarios_unit_test)

add_executable(eda_db_constraints_unit_test db/constraints/constraints_test.cc)
target_link_libraries(eda_db_constraints_unit_test
  PRIVATE eda_db_constraints GTest::gtest GTest::gtest_main)
add_test(NAME db.constraints COMMAND eda_db_constraints_unit_test)
