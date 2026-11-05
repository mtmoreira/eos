# Assembles eos-gui.app around an already-built eos-gui binary.
# Inputs (as -D arguments): EOS_GUI_BINARY, EOS_GUI_ICON_DIR,
# EOS_GUI_BUNDLE_DIR. Plain file operations only, so it runs on
# any platform; only macOS shows the bundle icon in the Dock.
# Script mode sets no policies on its own; pin CMP0054 so bare
# words in if() comparisons stay literals.
cmake_policy(SET CMP0054 NEW)
# The bundle keeps the binary runnable from a terminal: run the
# binary inside the bundle and the terminal REPL still works,
# while the Dock shows the EOS icon.
if(NOT EXISTS "${EOS_GUI_BINARY}")
  message(FATAL_ERROR "eos-gui binary not found: ${EOS_GUI_BINARY}")
endif()
if(NOT EXISTS "${EOS_GUI_ICON_DIR}/eos-icon.icns")
  message(FATAL_ERROR "icon not found: ${EOS_GUI_ICON_DIR}/eos-icon.icns")
endif()
if(NOT EXISTS "${EOS_GUI_ICON_DIR}/Info.plist")
  message(FATAL_ERROR "Info.plist not found: ${EOS_GUI_ICON_DIR}/Info.plist")
endif()
file(REMOVE_RECURSE "${EOS_GUI_BUNDLE_DIR}")
file(MAKE_DIRECTORY "${EOS_GUI_BUNDLE_DIR}/Contents/MacOS")
file(MAKE_DIRECTORY "${EOS_GUI_BUNDLE_DIR}/Contents/Resources")
file(COPY "${EOS_GUI_BINARY}"
  DESTINATION "${EOS_GUI_BUNDLE_DIR}/Contents/MacOS"
  FILE_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)
file(COPY "${EOS_GUI_ICON_DIR}/eos-icon.icns"
  DESTINATION "${EOS_GUI_BUNDLE_DIR}/Contents/Resources")
file(COPY "${EOS_GUI_ICON_DIR}/Info.plist"
  DESTINATION "${EOS_GUI_BUNDLE_DIR}/Contents")
message(STATUS "eos-gui.app packaged at ${EOS_GUI_BUNDLE_DIR}")
