# eda: the EOS tool binary (main and startup only).
add_executable(eda main.cc)
target_link_libraries(eda PRIVATE eda_ui_tcl eda_ui_registry)
