# finish: the EOS-owned chip-finish driver over the imported fin
# (Finale metal fill) and pdn (PdnGen power grid) engines, plus
# the EOS-owned GDSII stream writer in src/gds_stream.cc. Filler
# placement reuses the in-tree detailed placer's fillerPlacement
# (place module). No OpenDB GDSII writer exists at the ead3236 pin
# (see include/eos/pd/finish.h), so stream-out is EOS-owned glue
# in this module, not an import.
add_library(eda_pd_finish STATIC
  src/finish.cc
  src/gds_stream.cc
)
target_include_directories(eda_pd_finish
  PUBLIC include
  PRIVATE src
)
target_link_libraries(eda_pd_finish
  PUBLIC eda_pd_fin eda_pd_pdn eda_pd_place eda_db_odb
         eda_foundation_errors
)
set_target_properties(eda_pd_finish PROPERTIES POSITION_INDEPENDENT_CODE ON)
