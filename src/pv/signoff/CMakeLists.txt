# signoff: the signoff verdict record and the parsers for the
# external PV evidence reports (F13 panel unit). Pure text
# processing: no engine, no layout access, no dependencies
# beyond the foundation.
add_library(eda_pv_signoff STATIC src/signoff.cc)
target_include_directories(eda_pv_signoff PUBLIC include)
