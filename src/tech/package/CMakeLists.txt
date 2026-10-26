# package: technology package model and consistency checker (L2 design-data).
add_library(eda_tech_package STATIC src/package.cc)
target_include_directories(eda_tech_package PUBLIC include)
