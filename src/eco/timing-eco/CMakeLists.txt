# timing-eco: the timing ECO adapters (gate sizing, buffer
# insertion, and the resynthesis adapter with its mandatory
# equivalence proof) over the ODB facade, for use inside an ECO
# transaction. The resynthesis adapter links the ABC module (same
# layer, L4 engines) for the remap and the proof.
add_library(eda_eco_timing_eco STATIC src/timing_eco.cc src/resynthesis.cc)
target_include_directories(eda_eco_timing_eco PUBLIC include)
target_link_libraries(eda_eco_timing_eco
  PUBLIC eda_db_odb eda_foundation_errors
  PRIVATE eda_synth_abc)
