# eos: the EOS tool binary (main and startup only).
add_executable(eos main.cc)
target_link_libraries(eos PRIVATE eda_ui_tcl eda_ui_registry)
