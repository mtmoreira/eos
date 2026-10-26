# Qualification tests: end-to-end exit suites. Unlike the unit
# tests, these do not mirror one source module; they compose the
# modules the way a real session does and judge the result against
# a phase exit condition from the Revised Plan, Section 17.

add_executable(eda_qualification_phase1_exit_test phase1_exit_test.cc)
target_link_libraries(eda_qualification_phase1_exit_test
  PRIVATE eda_ui_registry eda_session_reports eda_session_session
          GTest::gtest GTest::gtest_main)
add_test(NAME qualification.phase1_exit
  COMMAND eda_qualification_phase1_exit_test)

add_executable(eda_qualification_phase2_exit_test phase2_exit_test.cc)
target_link_libraries(eda_qualification_phase2_exit_test
  PRIVATE eda_session_flow eda_eco_timing_eco eda_session_reports
          GTest::gtest GTest::gtest_main)
add_test(NAME qualification.phase2_exit
  COMMAND eda_qualification_phase2_exit_test)
