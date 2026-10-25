# timing: the timing service over scenes. Fronts the STA engine
# (eda_analysis_sta) and the ODB facade (eda_db_odb) for every
# timing consumer; report registration stays in L5/L6 by design
# (see include/eos/analysis/timing.h).
add_library(eda_analysis_timing STATIC src/timing.cc src/timing_service.cc)
target_include_directories(eda_analysis_timing PUBLIC include)
target_link_libraries(eda_analysis_timing
  PUBLIC eda_analysis_sta eda_db_odb eda_db_scenarios
         eda_foundation_errors)
