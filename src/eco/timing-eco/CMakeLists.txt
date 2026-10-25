# timing-eco: the v1 timing ECO adapters (gate sizing and buffer
# insertion) over the ODB facade, for use inside an ECO
# transaction.
add_library(eda_eco_timing_eco STATIC src/timing_eco.cc)
target_include_directories(eda_eco_timing_eco PUBLIC include)
target_link_libraries(eda_eco_timing_eco
  PUBLIC eda_db_odb eda_foundation_errors)
