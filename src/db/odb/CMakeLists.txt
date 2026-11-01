# odb: OpenDB, imported from the pinned upstream commit as EOS's own
# code (see PROVENANCE.md for the exact commit, the file inventory and
# the patch list). The upstream per-directory CMake files are kept in
# the tree for provenance but are not used; this file builds the whole
# dependency closure as the single EOS target eda_db_odb:
#
#   src/db      the database core          src/lef    LEF parser library
#   src/defin   DEF reader                 src/lefin  LEF reader
#   src/defout  DEF writer                 src/lefout LEF writer
#   src/def     DEF parser library         src/zutil  compression utils
#   src/cdl     CDL writer                 src/utl    logger (from OpenROAD src/utl)
#
# plus the EOS facade in src/odb_facade.cc, the only EOS-authored
# translation unit in the target. Parsers are generated with bison
# from the upstream grammars, exactly as upstream builds them.
#
# Layout change at the ead3236 pin against the v2.0 import: the
# public headers move from include/opendb to include/odb, the tm
# object factory and the zlib exception helper leave the tree (the
# factory is inside db now), and utl is the separate OpenROAD
# src/utl module, imported here as src/utl plus include/utl.
find_package(ZLIB REQUIRED)
find_package(BISON REQUIRED)
# utl's Logger is built on spdlog (with its fmt dependency): a system
# dependency of the imported code, like zlib, not imported itself.
find_package(spdlog REQUIRED)

bison_target(eos_def_parser
  src/def/def/def.y
  ${CMAKE_CURRENT_BINARY_DIR}/def_parser.cpp
  COMPILE_FLAGS "--name-prefix=defyy")
bison_target(eos_lef_parser
  src/lef/lef/lef.y
  ${CMAKE_CURRENT_BINARY_DIR}/lef_parser.cpp
  COMPILE_FLAGS "--name-prefix=lefyy")

# The DEF and LEF parser trees each ship their own lex.h, and the two
# bison-generated parsers (which live together in the binary dir)
# include it by the same quoted name. Upstream keeps the parsers in
# separate libraries with separate include paths; EOS does the same
# with two object libraries whose own parser directory comes first,
# folded into the single eda_db_odb target below.
add_library(eos_odb_def_parser OBJECT ${BISON_eos_def_parser_OUTPUT_SOURCE})
target_include_directories(eos_odb_def_parser PRIVATE
  src/def/def include include/odb ${CMAKE_CURRENT_BINARY_DIR})
add_library(eos_odb_lef_parser OBJECT ${BISON_eos_lef_parser_OUTPUT_SOURCE})
target_include_directories(eos_odb_lef_parser PRIVATE
  src/lef/lef include include/odb ${CMAKE_CURRENT_BINARY_DIR})

file(GLOB EOS_ODB_UPSTREAM_SOURCES
  src/cdl/*.cpp
  src/db/*.cpp
  src/def/def/*.cpp
  src/def/defzlib/*.cpp
  src/defin/*.cpp
  src/defout/*.cpp
  src/lef/lef/*.cpp
  src/lef/lefzlib/*.cpp
  src/lefin/*.cpp
  src/lefout/*.cpp
  src/utl/*.cpp
  src/zutil/*.cpp
)

add_library(eda_db_odb STATIC
  src/odb_facade.cc
  ${EOS_ODB_UPSTREAM_SOURCES}
  $<TARGET_OBJECTS:eos_odb_def_parser>
  $<TARGET_OBJECTS:eos_odb_lef_parser>
)
# P1 (place-and-route unit P1) promotes include/odb to PUBLIC:
# engine consumers include bare odb headers (db.h, dbUtil.h,
# lefin.h, defin.h) that live only there. This is EOS-owned
# CMake, not an upstream patch.
target_include_directories(eda_db_odb
  PUBLIC include include/odb
  PRIVATE
    src/cdl
    src/db
    src/def/def
    src/def/defzlib
    src/defin
    src/defout
    src/lef/lef
    src/lef/lefzlib
    src/lefin
    src/lefout
    src/utl
    src/zutil
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(eda_db_odb
  PUBLIC eda_foundation_errors
  PRIVATE ZLIB::ZLIB spdlog::spdlog
)
if(TCL_FOUND)
  target_include_directories(eda_db_odb PRIVATE ${TCL_INCLUDE_PATH})
  target_link_libraries(eda_db_odb PRIVATE ${TCL_LIBRARY})
endif()
set_target_properties(eda_db_odb PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(eos_odb_def_parser eos_odb_lef_parser
  PROPERTIES POSITION_INDEPENDENT_CODE ON)
# The ead3236 headers include <cstdint> themselves, so the force
# include kept from the v2.0 import is belt and braces only; it is
# retained at build level rather than patching any upstream source.
# src/fmt_compat.h is not force-included at this pin: the ead3236
# sources target the modern fmt API, and the REPIN build is clean
# against the system fmt without the shim. The shim file itself is
# deleted in this unit (it served the v2.0 import only).
target_compile_options(eda_db_odb PRIVATE
  "SHELL:-include cstdint")
# The generated parsers call no logger and need only the cstdint
# force-include.
foreach(eos_odb_target eos_odb_def_parser eos_odb_lef_parser)
  target_compile_options(${eos_odb_target} PRIVATE
    "SHELL:-include cstdint")
endforeach()
