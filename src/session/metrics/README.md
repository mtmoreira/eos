# metrics (L5 session)

Collects and compares run metrics and quality-of-results history.

Status: implemented (Phase 1 increment 5.2). DiffMetrics lines two MetricMaps up per metric (before, after, delta) and rejects a metric whose unit changed. CompareCost is the CostPolicy verdict of Revised Plan Section 8: lexicographic, timing first (worst_slack_ns, higher is better), then power (total_power_mw, lower is better), then area (cell_area_um2, lower is better), verdict Improved, Regressed or Unchanged with the deciding metric named. Comparisons are exact; no epsilon is defined in the plan.
The stable public API of this module lives in include/ and is versioned per the EOS Code Structure document.
