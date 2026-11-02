# grt: OpenROAD global routing (GlobalRouter + FastRoute) plus the
# EOS-owned MakeGlobalRouter factory. MakeWireParasitics is NOT
# imported in P2 (see PATCHES.md): its Corner API has no in-tree
# equivalent, and the placement path never calls it.
#
# Link note: RepairAntennas calls dpl::Opendp methods (detailed
# placement and pad queries for diode legalization), so grt links
# the place module PRIVATE. GlobalRouter.h only forward-declares
# Opendp, so consumers of grt do not need the dpl header. The
# place module links grt PUBLIC, so the two static archives form a
# cycle; CMake repeats static archives on the link line, which
# resolves it. Both modules are L4, so this is a same-layer edge,
# not an upward dependency.
add_library(eda_pd_grt STATIC
  src/GlobalRouter.cpp
  src/Grid.cpp
  src/MakeGlobalRouter.cc
  src/Net.cpp
  src/Pin.cpp
  src/RepairAntennas.cpp
  src/GrouteRenderer.cpp
  src/heatMap.cpp
  src/fastroute/FastRoute.cpp
  src/fastroute/FastRouteRenderer.cpp
  src/fastroute/RSMT.cpp
  src/fastroute/RipUp.cpp
  src/fastroute/maze.cpp
  src/fastroute/maze3D.cpp
  src/fastroute/route.cpp
  src/fastroute/utility.cpp
)
target_include_directories(eda_pd_grt
  PUBLIC include
  # Upstream fastroute TUs include their headers bare
  # (AbstractFastRouteRenderer.h, DataType.h, FastRoute.h,
  # FastRouteRenderer.h), resolved upstream by
  # src/grt/src/fastroute/include. The port moved those headers to
  # include/fastroute, so that directory is the private include
  # entry here (stt include/stt precedent). No imported file is
  # edited to add a fastroute/ prefix.
  PRIVATE src src/fastroute include/fastroute internal
)
target_link_libraries(eda_pd_grt
  PUBLIC eda_pd_stt eda_pd_ant eda_pd_dbsta eda_db_odb eda_analysis_sta
  PRIVATE eda_pd_place
)
# FastRouteRenderer includes gui/gui.h, covered by the dbsta gui stub.
set_target_properties(eda_pd_grt PROPERTIES POSITION_INDEPENDENT_CODE ON)
