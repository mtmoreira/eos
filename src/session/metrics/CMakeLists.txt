# metrics: metric comparison and the CostPolicy verdict.
add_library(eda_session_metrics STATIC src/metrics.cc)
target_include_directories(eda_session_metrics PUBLIC include)
target_link_libraries(eda_session_metrics
  PUBLIC eda_session_ledger eda_foundation_errors)
