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
