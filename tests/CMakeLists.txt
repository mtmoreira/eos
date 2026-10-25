# EOS tests. One framework per language (Code Structure, Section 6):
# GoogleTest for C++. Unit tests mirror the source tree under unit/.
#
# GoogleTest comes from the system package when present; otherwise a
# pinned FetchContent build, so CI needs no extra system packages.
find_package(GTest QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2)
  FetchContent_MakeAvailable(googletest)
endif()

add_subdirectory(unit)
add_subdirectory(qualification)
