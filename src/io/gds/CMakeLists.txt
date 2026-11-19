# gds: the GDSII stream reader (the read side of the layout
# file surface whose write side lives in pd/finish).
add_library(eda_io_gds STATIC src/gds.cc)
target_include_directories(eda_io_gds PUBLIC include)
target_link_libraries(eda_io_gds PUBLIC eda_foundation_errors)
