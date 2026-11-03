# drt: OpenROAD detailed routing (TritonRoute/FlexDR) plus the
# EOS-owned route runner's detailed stage (see src/pd/route).
# Imported from OpenROAD at ead3236, see PROVENANCE.md and
# PATCHES.md. MakeTritonRoute.cpp (Tcl/SWIG factory), all .tcl/.i
# surfaces, and the upstream test directory are not imported:
# the EOS runner constructs TritonRoute directly and the EOS
# registry registers eos::route. Distributed routing is not
# imported: the dst backend is replaced by EOS-owned shim
# headers under internal/dst (see PATCHES.md), and the EOS
# runner never enables distributed mode.
#
# Boost serialization is a declared external dependency (see
# third_party_manifest.yaml): it is never vendored. The build
# finds it through its CMake config (libboost-serialization-dev).
# OpenMP comes with the compiler (libgomp); no package install.
find_package(Boost REQUIRED COMPONENTS serialization)
find_package(OpenMP REQUIRED)
find_package(ZLIB REQUIRED)

add_library(eda_pd_drt STATIC
  src/gr/FlexGRCMap.cpp
  src/gr/FlexGR.cpp
  src/gr/FlexGR_end.cpp
  src/gr/FlexGRGridGraph.cpp
  src/gr/FlexGRGridGraph_maze.cpp
  src/gr/FlexGR_init.cpp
  src/gr/FlexGR_maze.cpp
  src/gr/FlexGR_rq.cpp
  src/gr/FlexGR_topo.cpp
  src/dr/FlexDR_conn.cpp
  src/dr/FlexDR_init.cpp
  src/dr/FlexDR.cpp
  src/db/drObj/drNet.cpp
  src/dr/FlexDR_maze.cpp
  src/dr/FlexGridGraph_maze.cpp
  src/dr/FlexGridGraph.cpp
  src/dr/FlexDR_rq.cpp
  src/dr/FlexDR_end.cpp
  src/dr/FlexDR_graphics.cpp
  src/ta/FlexTA_end.cpp
  src/ta/FlexTA_init.cpp
  src/ta/FlexTA_rq.cpp
  src/ta/FlexTA_assign.cpp
  src/ta/FlexTA.cpp
  src/ta/FlexTA_graphics.cpp
  src/global.cpp
  src/gc/FlexGC_end.cpp
  src/gc/FlexGC_rq.cpp
  src/gc/FlexGC.cpp
  src/gc/FlexGC_init.cpp
  src/gc/FlexGC_main.cpp
  src/gc/FlexGC_eol.cpp
  src/gc/FlexGC_inf.cpp
  src/gc/FlexGC_cut.cpp
  src/db/drObj/drAccessPattern.cpp
  src/db/drObj/drPin.cpp
  src/db/drObj/drShape.cpp
  src/db/drObj/drVia.cpp
  src/db/infra/frTime_helper.cpp
  src/db/infra/frTime.cpp
  src/db/taObj/taShape.cpp
  src/db/obj/frShape.cpp
  src/db/obj/frInst.cpp
  src/db/obj/frVia.cpp
  src/db/obj/frAccess.cpp
  src/db/obj/frRPin.cpp
  src/db/obj/frNode.cpp
  src/db/obj/frInstTerm.cpp
  src/db/tech/frConstraint.cc
  src/db/obj/frMarker.cpp
  src/db/tech/frLayer.cc
  src/frRegionQuery.cpp
  src/io/io_pin.cpp
  src/io/io.cpp
  src/io/io_guide.cpp
  src/io/io_parser_helper.cpp
  src/pa/FlexPA_init.cpp
  src/pa/FlexPA.cpp
  src/pa/FlexPA_prep.cpp
  src/pa/FlexPA_unique.cpp
  src/pa/FlexPA_graphics.cpp
  src/rp/FlexRP_init.cpp
  src/rp/FlexRP.cpp
  src/rp/FlexRP_prep.cpp
  src/distributed/frArchive.cpp
  src/distributed/drUpdate.cpp
  src/distributed/paUpdate.cpp
  src/TritonRoute.cpp
  src/frBaseTypes.cpp
  src/DesignCallBack.cpp
)
target_include_directories(eda_pd_drt
  PUBLIC include
  PRIVATE src internal
)
target_link_libraries(eda_pd_drt
  PUBLIC eda_pd_stt eda_pd_dbsta eda_db_odb eda_analysis_sta
  PRIVATE Boost::serialization OpenMP::OpenMP_CXX ZLIB::ZLIB
)
# TritonRoute.h includes <tcl.h> and is a public header, so
# consumers need the Tcl include path as well.
if(TCL_FOUND)
  target_include_directories(eda_pd_drt PUBLIC ${TCL_INCLUDE_PATH})
  target_link_libraries(eda_pd_drt PUBLIC ${TCL_LIBRARY})
endif()
set_target_properties(eda_pd_drt PROPERTIES POSITION_INDEPENDENT_CODE ON)
