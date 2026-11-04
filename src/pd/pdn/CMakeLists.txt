# pdn: OpenROAD power distribution (PdnGen) as the P7 support import
# for the finish runner's basic power grid (see src/pd/finish).
# Imported from OpenROAD at ead3236, see PROVENANCE.md and
# PATCHES.md. The Tcl factory (MakePdnGen.cc) and all .tcl/.i
# surfaces are not imported: the EOS runner constructs pdn::PdnGen
# directly and the EOS registry registers eos::finish. Boost
# headers are a declared system dependency (see PATCHES.md
# section 6); no Boost source is vendored.
find_package(Boost REQUIRED)
add_library(eda_pd_pdn STATIC
  src/PdnGen.cc
  src/connect.cpp
  src/domain.cpp
  src/grid.cpp
  src/grid_component.cpp
  src/power_cells.cpp
  src/renderer.cpp
  src/rings.cpp
  src/shape.cpp
  src/sroute.cpp
  src/straps.cpp
  src/techlayer.cpp
  src/via.cpp
  src/via_repair.cpp
)
target_include_directories(eda_pd_pdn
  PUBLIC include
  # Upstream pdn translation units include their sibling headers
  # bare (connect.h, domain.h, grid.h, ...), resolved upstream by
  # src/pdn/src. The port keeps those headers in src/, so that
  # directory is the private include entry here (the stt/grt
  # pattern). No imported file is edited to add a prefix.
  PRIVATE src
)
target_link_libraries(eda_pd_pdn
  PUBLIC eda_db_odb
  PRIVATE eda_pd_dbsta
)
if(TARGET Boost::headers)
  target_link_libraries(eda_pd_pdn PRIVATE Boost::headers)
elseif(Boost_INCLUDE_DIRS)
  target_include_directories(eda_pd_pdn PRIVATE ${Boost_INCLUDE_DIRS})
endif()
set_target_properties(eda_pd_pdn PROPERTIES POSITION_INDEPENDENT_CODE ON)
