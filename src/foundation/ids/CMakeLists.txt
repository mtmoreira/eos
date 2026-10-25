# ids: persistent object ID types (L0 foundation).
add_library(eda_foundation_ids STATIC src/ids.cc)
target_include_directories(eda_foundation_ids PUBLIC include)
target_link_libraries(eda_foundation_ids PUBLIC eda_foundation_errors)
