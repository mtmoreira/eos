# sdc: scoped SDC clock parser (create_clock,
# create_generated_clock, set_clock_uncertainty).
add_library(eda_io_sdc STATIC src/sdc.cc)
target_include_directories(eda_io_sdc PUBLIC include)
target_link_libraries(eda_io_sdc PUBLIC eda_foundation_errors)
