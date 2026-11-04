# fin: OpenROAD chip finish (Finale + DensityFill) plus the EOS-owned
# finish runner's metal-fill use (see src/pd/finish). Imported from
# OpenROAD at ead3236, see PROVENANCE.md and PATCHES.md. The Tcl
# factory (MakeFinale.cpp) and all .tcl/.i surfaces are not
# imported: the EOS runner constructs fin::Finale directly and the
# EOS registry registers eos::finish. Boost headers are a declared
# system dependency (see PATCHES.md section 5); no Boost source is
# vendored.
find_package(Boost REQUIRED)
add_library(eda_pd_fin STATIC
  src/Finale.cpp
  src/DensityFill.cpp
  src/graphics.cpp
)
target_include_directories(eda_pd_fin
  PUBLIC include
  # Upstream fin translation units include their sibling headers
  # bare (DensityFill.h, graphics.h, polygon.h), resolved upstream
  # by src/fin/src. The port keeps those headers in src/, so that
  # directory is the private include entry here (the stt/grt
  # pattern). No imported file is edited to add a prefix.
  PRIVATE src
)
target_link_libraries(eda_pd_fin
  PUBLIC eda_db_odb
  PRIVATE eda_pd_dbsta
)
if(TARGET Boost::headers)
  target_link_libraries(eda_pd_fin PRIVATE Boost::headers)
elseif(Boost_INCLUDE_DIRS)
  target_include_directories(eda_pd_fin PRIVATE ${Boost_INCLUDE_DIRS})
endif()
set_target_properties(eda_pd_fin PROPERTIES POSITION_INDEPENDENT_CODE ON)
