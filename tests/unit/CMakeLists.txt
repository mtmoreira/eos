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

add_executable(eda_analysis_sta_unit_test analysis/sta/sta_test.cc)
target_link_libraries(eda_analysis_sta_unit_test
  PRIVATE eda_analysis_sta GTest::gtest GTest::gtest_main)
add_test(NAME analysis.sta COMMAND eda_analysis_sta_unit_test)

add_executable(eda_analysis_timing_unit_test
  analysis/timing/timing_service_test.cc)
target_link_libraries(eda_analysis_timing_unit_test
  PRIVATE eda_analysis_timing GTest::gtest GTest::gtest_main)
add_test(NAME analysis.timing COMMAND eda_analysis_timing_unit_test)

add_executable(eda_eco_transactions_unit_test
  eco/transactions/transactions_test.cc)
target_link_libraries(eda_eco_transactions_unit_test
  PRIVATE eda_eco_transactions eda_eco_timing_eco eda_session_ledger
          GTest::gtest GTest::gtest_main)
add_test(NAME eco.transactions COMMAND eda_eco_transactions_unit_test)

add_executable(eda_eco_timing_eco_unit_test eco/timing-eco/timing_eco_test.cc)
target_link_libraries(eda_eco_timing_eco_unit_test
  PRIVATE eda_eco_timing_eco eda_analysis_timing
          GTest::gtest GTest::gtest_main)
add_test(NAME eco.timing_eco COMMAND eda_eco_timing_eco_unit_test)

add_executable(eda_db_odb_unit_test db/odb/odb_test.cc)
target_link_libraries(eda_db_odb_unit_test
  PRIVATE eda_db_odb GTest::gtest GTest::gtest_main)
add_test(NAME db.odb COMMAND eda_db_odb_unit_test)

add_executable(eda_db_checkpoint_unit_test db/checkpoint/checkpoint_test.cc)
target_link_libraries(eda_db_checkpoint_unit_test
  PRIVATE eda_db_checkpoint GTest::gtest GTest::gtest_main)
add_test(NAME db.checkpoint COMMAND eda_db_checkpoint_unit_test)

add_executable(eda_session_ledger_snapshot_unit_test
  session/ledger/ledger_snapshot_test.cc)
target_link_libraries(eda_session_ledger_snapshot_unit_test
  PRIVATE eda_session_ledger GTest::gtest GTest::gtest_main)
add_test(NAME session.ledger.snapshot
  COMMAND eda_session_ledger_snapshot_unit_test)

add_executable(eda_session_ledger_text_unit_test
  session/ledger/ledger_text_test.cc)
target_link_libraries(eda_session_ledger_text_unit_test
  PRIVATE eda_session_ledger GTest::gtest GTest::gtest_main)
add_test(NAME session.ledger.text
  COMMAND eda_session_ledger_text_unit_test)

add_executable(eda_session_project_unit_test
  session/session/project_test.cc)
target_link_libraries(eda_session_project_unit_test
  PRIVATE eda_session_session GTest::gtest GTest::gtest_main)
add_test(NAME session.project COMMAND eda_session_project_unit_test)

add_executable(eda_session_reports_unit_test
  session/reports/reports_test.cc)
target_link_libraries(eda_session_reports_unit_test
  PRIVATE eda_session_reports GTest::gtest GTest::gtest_main)
add_test(NAME session.reports COMMAND eda_session_reports_unit_test)

add_executable(eda_session_flow_unit_test session/flow/flow_test.cc)
target_link_libraries(eda_session_flow_unit_test
  PRIVATE eda_session_flow GTest::gtest GTest::gtest_main)
add_test(NAME session.flow COMMAND eda_session_flow_unit_test)

add_executable(eda_session_metrics_unit_test
  session/metrics/metrics_test.cc)
target_link_libraries(eda_session_metrics_unit_test
  PRIVATE eda_session_metrics GTest::gtest GTest::gtest_main)
add_test(NAME session.metrics COMMAND eda_session_metrics_unit_test)

add_executable(eda_ui_registry_unit_test ui/registry/registry_test.cc)
target_link_libraries(eda_ui_registry_unit_test
  PRIVATE eda_ui_registry eda_db_checkpoint GTest::gtest GTest::gtest_main)
add_test(NAME ui.registry COMMAND eda_ui_registry_unit_test)

if(TCL_FOUND)
  add_executable(eda_ui_tcl_unit_test ui/tcl/tcl_shell_test.cc)
  target_link_libraries(eda_ui_tcl_unit_test
    PRIVATE eda_ui_tcl GTest::gtest GTest::gtest_main)
  add_test(NAME ui.tcl COMMAND eda_ui_tcl_unit_test)
endif()
