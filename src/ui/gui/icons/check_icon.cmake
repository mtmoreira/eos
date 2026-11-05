# Verifies the committed eos-gui icon assets. Run as a CTest
# test with -DEOS_GUI_ICON_DIR pointing at src/ui/gui/icons.
# Pure CMake file checks: no image tool is needed.
# Script mode sets no policies on its own, so pin CMP0054: bare
# words in if() comparisons must stay literals, not variable
# dereferences (otherwise "icns" would read as ${icns}).
cmake_policy(SET CMP0054 NEW)
set(EOS_GUI_ICONSET_NAMES
  icon_16x16.png icon_16x16@2x.png
  icon_32x32.png icon_32x32@2x.png
  icon_128x128.png icon_128x128@2x.png
  icon_256x256.png icon_256x256@2x.png
  icon_512x512.png icon_512x512@2x.png)
foreach(name IN LISTS EOS_GUI_ICONSET_NAMES)
  set(path "${EOS_GUI_ICON_DIR}/eos-icon.iconset/${name}")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "missing iconset file: ${path}")
  endif()
  file(SIZE "${path}" size)
  if(size LESS 200)
    message(FATAL_ERROR "iconset file too small: ${path} (${size} bytes)")
  endif()
  file(READ "${path}" magic LIMIT 4 HEX)
  if(NOT magic STREQUAL "89504e47")
    message(FATAL_ERROR "iconset file is not a PNG: ${path}")
  endif()
endforeach()

# .icns: 'icns' magic and a declared total matching the file size.
# The magic is compared in HEX: a plain text read of a binary
# file mangles NUL bytes, so text comparison is unreliable here.
set(icns_path "${EOS_GUI_ICON_DIR}/eos-icon.icns")
if(NOT EXISTS "${icns_path}")
  message(FATAL_ERROR "missing icon: ${icns_path}")
endif()
file(SIZE "${icns_path}" icns_size)
file(READ "${icns_path}" icns_magic LIMIT 4 HEX)
if(NOT icns_magic STREQUAL "69636e73")
  message(FATAL_ERROR "icon is not an icns file: ${icns_path}")
endif()
file(READ "${icns_path}" icns_len_hex LIMIT 4 OFFSET 4 HEX)
math(EXPR icns_declared "0x${icns_len_hex}")
if(NOT icns_declared EQUAL icns_size)
  message(FATAL_ERROR
    "icns length mismatch: header says ${icns_declared}, file is ${icns_size}")
endif()

# Info.plist names the icon file (without extension, as macOS wants).
file(READ "${EOS_GUI_ICON_DIR}/Info.plist" plist)
string(FIND "${plist}"
  "<key>CFBundleIconFile</key>\n  <string>eos-icon</string>" found)
if(found EQUAL -1)
  message(FATAL_ERROR "Info.plist does not name eos-icon")
endif()
message(STATUS "icon assets OK: ${EOS_GUI_ICON_DIR}")
