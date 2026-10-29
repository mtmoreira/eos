# tcl: the EOS Tcl dialect over the command registry (L6 interface).
# When no Tcl development package is found, the module builds its
# stub implementation instead: the shell API reports
# kUnsupportedFeature rather than failing the build.
# The vendored linenoise line editor is C, and the top-level project
# is CXX-only, so this module enables C the same way the sta module
# does for its vendored CUDD closure.
enable_language(C)
add_library(eda_ui_tcl STATIC src/tcl.cc src/linenoise.c)
target_include_directories(eda_ui_tcl PUBLIC include)
target_link_libraries(eda_ui_tcl PUBLIC eda_ui_registry)
if(TCL_FOUND)
  target_compile_definitions(eda_ui_tcl PRIVATE EOS_HAS_TCL=1)
  target_include_directories(eda_ui_tcl PRIVATE ${TCL_INCLUDE_PATH})
  target_link_libraries(eda_ui_tcl PUBLIC ${TCL_LIBRARY})
endif()
