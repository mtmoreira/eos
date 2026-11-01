# eos-gui: the EOS GUI binary (main and startup only).
add_executable(eos-gui main.cc)
target_link_libraries(eos-gui PRIVATE eda_ui_gui eda_ui_registry eda_ui_tcl)
