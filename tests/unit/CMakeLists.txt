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

add_executable(eda_synth_abc_unit_test synth/abc/abc_test.cc)
target_link_libraries(eda_synth_abc_unit_test
  PRIVATE eda_synth_abc GTest::gtest GTest::gtest_main)
add_test(NAME synth.abc COMMAND eda_synth_abc_unit_test)

# Yosys must be linked whole-archive: it registers its passes and
# frontends through global constructors, and a plain static-archive
# pull never includes an object nothing references, so read_verilog
# (and every other pass) would be unregistered at runtime. This is
# the mechanism upstream's static users use (its shared default
# hides it); every consumer of eda_synth_yosys must link it this
# way, not plain. LINK_LIBRARY needs CMake 3.24 or later.
add_executable(eda_synth_yosys_unit_test synth/yosys/yosys_test.cc)
target_link_libraries(eda_synth_yosys_unit_test
  PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,eda_synth_yosys>"
          # YOSYS_LINK_ABC makes the yosys target reference
          # abc::Abc_RealMain, so this binary needs the ABC
          # library at link time too.
          eda_synth_abc GTest::gtest GTest::gtest_main)
add_test(NAME synth.yosys COMMAND eda_synth_yosys_unit_test)

# The `abc -dff` in-process integration test: sequential
# synth_generic through the upstream pass. Links the ABC
# library alongside Yosys so the pass can call Abc_RealMain.
add_executable(eda_synth_yosys_abc_dff_unit_test
  synth/yosys/abc_dff_test.cc)
target_link_libraries(eda_synth_yosys_abc_dff_unit_test
  PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,eda_synth_yosys>"
          eda_synth_abc GTest::gtest GTest::gtest_main)
add_test(NAME synth.yosys.abc_dff
  COMMAND eda_synth_yosys_abc_dff_unit_test)

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

add_executable(eda_tech_package_unit_test tech/package/package_test.cc)
target_link_libraries(eda_tech_package_unit_test
  PRIVATE eda_tech_package GTest::gtest GTest::gtest_main)
add_test(NAME tech.package COMMAND eda_tech_package_unit_test)

add_executable(eda_pd_floorplan_unit_test pd/floorplan/floorplan_test.cc)
target_link_libraries(eda_pd_floorplan_unit_test
  PRIVATE eda_pd_floorplan eda_db_odb GTest::gtest GTest::gtest_main)
add_test(NAME pd.floorplan COMMAND eda_pd_floorplan_unit_test)

add_executable(eda_pd_place_unit_test pd/place/place_test.cc)
target_link_libraries(eda_pd_place_unit_test
  PRIVATE eda_pd_place eda_pd_floorplan eda_db_odb GTest::gtest GTest::gtest_main)
add_test(NAME pd.place COMMAND eda_pd_place_unit_test)

add_executable(eda_pd_cts_unit_test pd/cts/cts_test.cc)
target_link_libraries(eda_pd_cts_unit_test
  PRIVATE eda_pd_cts eda_pd_place eda_pd_floorplan eda_db_odb GTest::gtest GTest::gtest_main)
add_test(NAME pd.cts COMMAND eda_pd_cts_unit_test)

add_executable(eda_pd_route_unit_test pd/route/route_test.cc)
target_link_libraries(eda_pd_route_unit_test
  PRIVATE eda_pd_route eda_pd_place eda_pd_floorplan eda_db_odb GTest::gtest GTest::gtest_main)
add_test(NAME pd.route COMMAND eda_pd_route_unit_test)

add_executable(eda_pv_extract_unit_test pv/extract/extract_test.cc)
target_link_libraries(eda_pv_extract_unit_test
  PRIVATE eda_pv_extract eda_pd_route eda_pd_place eda_pd_floorplan
          eda_analysis_timing eda_db_odb GTest::gtest GTest::gtest_main)
add_test(NAME pv.extract COMMAND eda_pv_extract_unit_test)

add_executable(eda_pv_signoff_unit_test pv/signoff/signoff_test.cc)
target_link_libraries(eda_pv_signoff_unit_test
  PRIVATE eda_pv_signoff GTest::gtest GTest::gtest_main)
add_test(NAME pv.signoff COMMAND eda_pv_signoff_unit_test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(eda_pd_finish_unit_test pd/finish/finish_test.cc)
target_link_libraries(eda_pd_finish_unit_test
  PRIVATE eda_pd_finish eda_pd_route eda_pd_place eda_pd_floorplan
          eda_db_odb GTest::gtest GTest::gtest_main)
add_test(NAME pd.finish COMMAND eda_pd_finish_unit_test)

add_executable(eda_pd_dbsta_unit_test pd/dbsta/dbsta_test.cc)
target_link_libraries(eda_pd_dbsta_unit_test
  PRIVATE eda_pd_dbsta eda_db_odb GTest::gtest GTest::gtest_main)
add_test(NAME pd.dbsta COMMAND eda_pd_dbsta_unit_test)

add_executable(eda_pd_pdr_unit_test pd/pdr/pdr_test.cc)
target_link_libraries(eda_pd_pdr_unit_test
  PRIVATE eda_pd_pdr eda_db_odb GTest::gtest GTest::gtest_main)
add_test(NAME pd.pdr COMMAND eda_pd_pdr_unit_test)

add_executable(eda_pd_rsz_unit_test pd/rsz/rsz_test.cc)
target_link_libraries(eda_pd_rsz_unit_test
  PRIVATE eda_pd_rsz GTest::gtest GTest::gtest_main)
add_test(NAME pd.rsz COMMAND eda_pd_rsz_unit_test)

# Mock-library resizer cases: one literal add_test per ported case
# (literal lines, not a foreach loop, so tools/dep_graph.py sees
# each entry). The scripts run against the EOS-owned synthetic
# mock library, so they are runnable in CI with no real PDK file.
# WORKING_DIRECTORY is the rsz tests dir (the harness cwd).
add_test(NAME pd.rsz.mock.setup1
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_setup1.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.setup3
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_setup3.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.hold4
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_hold4.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.hold5
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_hold5.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.fanout2
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_fanout2.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.wire3
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_wire3.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.tie1
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_tie1.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.tie2
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_tie2.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.tie3
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_tie3.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.tie4
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_tie4.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.tie5
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_tie5.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.tie6
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_tie6.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.tie7
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_tie7.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.clk_inverters1
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_clk_inverters1.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.cap1
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_cap1.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.cap2
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_cap2.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.fanout3
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_fanout3.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.hold1
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_hold1.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.hold2
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_hold2.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.hold3
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_hold3.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.slew1
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_slew1.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.slew2
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_slew2.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.slew3
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_slew3.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.slew4
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_slew4.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.slew5
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_slew5.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.slew6
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_slew6.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.setup2
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_setup2.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.setup2_override0
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_setup2_override0.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)
add_test(NAME pd.rsz.mock.fallback_zero_drive
  COMMAND python3 ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests/run_case.py mock/repair_fallback_zero_drive.tcl
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/pd/rsz/tests)

add_executable(eda_ui_registry_unit_test ui/registry/registry_test.cc)
target_link_libraries(eda_ui_registry_unit_test
  PRIVATE eda_ui_registry eda_db_checkpoint GTest::gtest GTest::gtest_main)
# F0: the mock-PDK bridge test reads the demo design and the mock
# PDK from the source tree, like the GUI icon check reads its
# assets from a path. The defines carry quoted absolute paths.
target_compile_definitions(eda_ui_registry_unit_test
  PRIVATE
    EOS_MOCK_PDK_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../src/pd/rsz/tests/mock"
    EOS_DEMO_SYNTH_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../demo/synth")
add_test(NAME ui.registry COMMAND eda_ui_registry_unit_test)

if(TCL_FOUND)
  add_executable(eda_ui_tcl_unit_test ui/tcl/tcl_shell_test.cc)
  target_link_libraries(eda_ui_tcl_unit_test
    PRIVATE eda_ui_tcl GTest::gtest GTest::gtest_main)
  add_test(NAME ui.tcl COMMAND eda_ui_tcl_unit_test)
endif()

add_executable(eda_ui_gui_unit_test ui/gui/gui_test.cc)
target_link_libraries(eda_ui_gui_unit_test
  PRIVATE eda_ui_gui GTest::gtest GTest::gtest_main)
# The splash sources compile only when Qt6 was found for the
# gui module, and the Qt6 targets live in that module's scope,
# so re-find the package here (cached, cheap) to ask the same
# question in this scope. Without Qt6 the splash sources are
# not compiled and these tests are compiled out (see the
# EOS_UI_GUI_HAVE_QT6 guard in the test).
find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
  # SplashScreen lives in the gui module's internal/ dir; the
  # test needs its header for the minimum-visible timing seam
  # only.
  target_include_directories(eda_ui_gui_unit_test
    PRIVATE ${CMAKE_SOURCE_DIR}/src/ui/gui/internal)
  target_compile_definitions(eda_ui_gui_unit_test
    PRIVATE EOS_UI_GUI_HAVE_QT6)
endif()
add_test(NAME ui.gui COMMAND eda_ui_gui_unit_test)

if(EOS_HAVE_QT6)
  # Widget rendering tests in gui_test.cc need the internal
  # widget headers and a Qt build; without Qt the file stays
  # Qt-free and the guarded tests compile out.
  target_include_directories(eda_ui_gui_unit_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ui/gui/internal)
  target_compile_definitions(eda_ui_gui_unit_test PRIVATE EOS_GUI_HAVE_QT6=1)
endif()

# Icon assets: the committed iconset PNGs, the .icns, and the
# Info.plist naming the icon. Pure file checks, no image tool.
add_test(NAME ui.gui.icon
  COMMAND ${CMAKE_COMMAND}
    -DEOS_GUI_ICON_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../../src/ui/gui/icons
    -P ${CMAKE_CURRENT_SOURCE_DIR}/../../src/ui/gui/icons/check_icon.cmake)
