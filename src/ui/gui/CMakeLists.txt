# gui: the EOS GUI adapter over the command registry (L6 interface).
#
# Qt6 is optional at configure time: without it, the module builds
# its Qt-free logic (SessionView) and its tests, and the eos-gui
# app target is skipped. CI installs Qt6 so CI always builds the
# full GUI. Qt is a declared external dependency, never vendored;
# its LGPL terms are flagged for licence review at distribution
# time (see PROVENANCE.md and README.md).
add_library(eda_ui_gui STATIC
  src/gui_session.cc
  src/console_entry.cc
  src/diagram.cc
  src/hierarchy.cc
  src/panels.cc
  src/schematic.cc)
target_include_directories(eda_ui_gui PUBLIC include)
target_link_libraries(eda_ui_gui PUBLIC eda_ui_registry)

find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
  target_sources(eda_ui_gui PRIVATE
    src/main_window.cc
    src/hierarchy_browser.cc
    src/block_diagram.cc
    src/schematic_diagram.cc
    src/panels_view.cc
    src/gui_app.cc)
  target_include_directories(eda_ui_gui PRIVATE internal)
  target_link_libraries(eda_ui_gui PUBLIC Qt6::Widgets)
  # The terminal REPL evaluates through the Tcl shell, so the
  # Qt part of the module links the tcl module (same L6 layer
  # as the registry link above).
  target_link_libraries(eda_ui_gui PRIVATE eda_ui_tcl)
  set(EOS_HAVE_QT6 TRUE PARENT_SCOPE)
endif()
