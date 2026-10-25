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
#   src/cdl     CDL writer                 src/zlib   ZException only
#   src/tm      object factory (Z*)        src/utl    logger (from OpenROAD src/utl)
#
# plus the EOS facade in src/odb_facade.cc, the only EOS-authored
# translation unit in the target. Parsers are generated with bison
# from the upstream grammars, exactly as upstream builds them.
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
  src/def/def include include/opendb ${CMAKE_CURRENT_BINARY_DIR})
add_library(eos_odb_lef_parser OBJECT ${BISON_eos_lef_parser_OUTPUT_SOURCE})
target_include_directories(eos_odb_lef_parser PRIVATE
  src/lef/lef include include/opendb ${CMAKE_CURRENT_BINARY_DIR})

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
  src/tm/*.cpp
  src/utl/*.cpp
  src/zlib/*.cpp
  src/zutil/*.cpp
)

add_library(eda_db_odb STATIC
  src/odb_facade.cc
  ${EOS_ODB_UPSTREAM_SOURCES}
  $<TARGET_OBJECTS:eos_odb_def_parser>
  $<TARGET_OBJECTS:eos_odb_lef_parser>
)
target_include_directories(eda_db_odb
  PUBLIC include
  PRIVATE
    include/opendb
    src/cdl
    src/db
    src/defin
    src/defout
    src/lefin
    src/lefout
    src/tm
    src/zlib
    src/zutil
    src/def/def
    src/def/defzlib
    src/lef/lef
    src/lef/lefzlib
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
# The 2021 upstream headers use uint32_t/uint64_t without including
# <cstdint>, which older libstdc++ versions pulled in transitively.
# Force-include it at build level instead of patching upstream sources.
# src/fmt_compat.h (EOS-authored) is force-included for the same
# reason: it restores the pre-fmt9 enum formatting that the upstream
# logger calls rely on, without touching any upstream file.
target_compile_options(eda_db_odb PRIVATE
  "SHELL:-include cstdint"
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/src/fmt_compat.h")
# The generated parsers call no logger and need only the cstdint
# force-include (the fmt shim would pull spdlog headers into them).
foreach(eos_odb_target eos_odb_def_parser eos_odb_lef_parser)
  target_compile_options(${eos_odb_target} PRIVATE
    "SHELL:-include cstdint")
endforeach()
