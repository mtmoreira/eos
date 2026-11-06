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
  src/issues.cc
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
    src/splash_screen.cc
    src/gui_app.cc)
  # The splash logo rides inside the binary via the Qt resource
  # system, so a bare binary launched from any directory still
  # finds its artwork. resources/splash.qrc embeds the PNG
  # converted from the committed dark logo webp (see README.md).
  set_property(TARGET eda_ui_gui PROPERTY AUTORCC ON)
  target_sources(eda_ui_gui PRIVATE resources/splash.qrc)
  target_include_directories(eda_ui_gui PRIVATE internal)
  target_link_libraries(eda_ui_gui PUBLIC Qt6::Widgets)
  # Window icon: embed the 512 pixel iconset PNG as a C array
  # so gui_app.cc can set the window icon without a runtime
  # file lookup. Generated at configure time with plain CMake
  # file commands, so no extra build tool is needed.
  set(EOS_GUI_ICON_PNG
    ${CMAKE_CURRENT_SOURCE_DIR}/icons/eos-icon.iconset/icon_512x512.png)
  file(READ "${EOS_GUI_ICON_PNG}" EOS_GUI_ICON_HEX HEX)
  string(LENGTH "${EOS_GUI_ICON_HEX}" EOS_GUI_ICON_HEX_LEN)
  math(EXPR EOS_GUI_ICON_BYTES "${EOS_GUI_ICON_HEX_LEN} / 2")
  set(EOS_GUI_ICON_ARRAY "")
  set(EOS_GUI_ICON_POS 0)
  while(EOS_GUI_ICON_POS LESS EOS_GUI_ICON_HEX_LEN)
    string(SUBSTRING "${EOS_GUI_ICON_HEX}" ${EOS_GUI_ICON_POS} 200
      EOS_GUI_ICON_CHUNK)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1,"
      EOS_GUI_ICON_CHUNK_FMT "${EOS_GUI_ICON_CHUNK}")
    string(APPEND EOS_GUI_ICON_ARRAY "${EOS_GUI_ICON_CHUNK_FMT}\n")
    math(EXPR EOS_GUI_ICON_POS "${EOS_GUI_ICON_POS} + 200")
  endwhile()
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/eos_icon_png.h
    "// Generated at configure time from icons/eos-icon.iconset/icon_512x512.png. Do not edit.\n"
    "#pragma once\n"
    "static const unsigned char kEosIconPng[] = {\n${EOS_GUI_ICON_ARRAY}};\n"
    "static const unsigned int kEosIconPngSize = ${EOS_GUI_ICON_BYTES};\n")
  target_include_directories(eda_ui_gui PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  # The terminal REPL evaluates through the Tcl shell, so the
  # Qt part of the module links the tcl module (same L6 layer
  # as the registry link above).
  target_link_libraries(eda_ui_gui PRIVATE eda_ui_tcl)
  set(EOS_HAVE_QT6 TRUE PARENT_SCOPE)
endif()
