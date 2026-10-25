# scenarios: scenario identity (L2 design-data).
add_library(eda_db_scenarios STATIC src/scenarios.cc)
target_include_directories(eda_db_scenarios PUBLIC include)
target_link_libraries(eda_db_scenarios PUBLIC eda_foundation_errors)
