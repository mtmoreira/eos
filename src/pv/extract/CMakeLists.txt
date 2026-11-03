# extract: OpenROAD parasitic extraction (OpenRCX) plus the EOS-owned
# runner in src/extract.cc. Imported from OpenROAD at ead3236, see
# PROVENANCE.md and PATCHES.md. The Tcl factory (MakeOpenRCX.cpp)
# and all .tcl/.i surfaces are not imported: the EOS runner
# constructs rcx::Ext directly and the EOS registry registers
# eos::extract_parasitics.
add_library(eda_pv_extract STATIC
  src/ext.cpp
  src/extBench.cpp
  src/extBenchDB.cpp
  src/extCC.cpp
  src/extCoords.cpp
  src/extDebugPrint.cpp
  src/extFlow.cpp
  src/extRCmodel.cpp
  src/extSpef.cpp
  src/extSpefIn.cpp
  src/extmain.cpp
  src/extmeasure.cpp
  src/extmeasure_res.cpp
  src/extprocess.cpp
  src/extstats.cpp
  src/name.cpp
  src/netRC.cpp
  src/extract.cc
)
target_include_directories(eda_pv_extract
  PUBLIC include
  PRIVATE src
)
target_link_libraries(eda_pv_extract
  PUBLIC eda_pd_dbsta eda_db_odb eda_analysis_sta eda_foundation_errors
)
set_target_properties(eda_pv_extract PROPERTIES POSITION_INDEPENDENT_CODE ON)
