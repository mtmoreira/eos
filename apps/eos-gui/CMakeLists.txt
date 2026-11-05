# eos-gui: the EOS GUI binary (main and startup only).
add_executable(eos-gui main.cc)
target_link_libraries(eos-gui PRIVATE eda_ui_gui eda_ui_registry eda_ui_tcl)

# Minimal install rule (U7 packaging): the binary only. Qt stays an
# external runtime dependency and is never bundled by this rule.
install(TARGETS eos-gui RUNTIME DESTINATION bin)

# macOS .app bundle packaging: assembles eos-gui.app around the
# already-built eos-gui binary, carrying the EOS icon in
# Resources with an Info.plist naming it. The bare binary keeps
# working exactly as before; the bundle exists because a bare
# binary launched from a terminal has no bundle, and macOS then
# shows the generic executable icon in the Dock. Running the
# binary inside the bundle keeps the terminal REPL and shows
# the EOS icon in the Dock.
add_custom_target(eos-gui-bundle
  DEPENDS eos-gui
  COMMAND ${CMAKE_COMMAND}
    -DEOS_GUI_BINARY=$<TARGET_FILE:eos-gui>
    -DEOS_GUI_ICON_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../../src/ui/gui/icons
    -DEOS_GUI_BUNDLE_DIR=${CMAKE_CURRENT_BINARY_DIR}/eos-gui.app
    -P ${CMAKE_CURRENT_SOURCE_DIR}/make_bundle.cmake
  COMMENT "Packaging eos-gui.app with the EOS icon"
  VERBATIM)
