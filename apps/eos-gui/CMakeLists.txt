# eos-gui: the EOS GUI binary (main and startup only).
add_executable(eos-gui main.cc)
target_link_libraries(eos-gui PRIVATE eda_ui_gui eda_ui_registry eda_ui_tcl)

# Minimal install rule (U7 packaging): the binary only. Qt stays an
# external runtime dependency and is never bundled by this rule.
install(TARGETS eos-gui RUNTIME DESTINATION bin)
