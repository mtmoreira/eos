// metrics: metric comparison and the CostPolicy verdict
// (L5 session).
#include "eos/session/metrics.h"

#include <set>

namespace eos::session {

using foundation::ErrorCode;

Result<std::vector<MetricDiffEntry>> DiffMetrics(const MetricMap& before,
                                                   const MetricMap& after) {
  std::set<std::string> names;
  for (const auto& entry : before) names.insert(entry.first);
  for (const auto& entry : after) names.insert(entry.first);
  std::vector<MetricDiffEntry> out;
  for (const std::string& name : names) {
    const auto b = before.find(name);
    const auto a = after.find(name);
    MetricDiffEntry entry;
    entry.name = name;
    entry.in_before = b != before.end();
    entry.in_after = a != after.end();
    if (entry.in_before && entry.in_after) {
      if (b->second.unit != a->second.unit) {
        return Status::Error(
            ErrorCode::kInvalidArgument,
            "metric " + name + " changed unit: " + b->second.unit +
                " vs " + a->second.unit);
      }
      entry.unit = b->second.unit;
      entry.before = b->second.value;
      entry.after = a->second.value;
      entry.has_delta = true;
      entry.delta = a->second.value - b->second.value;
    } else if (entry.in_before) {
      entry.unit = b->second.unit;
      entry.before = b->second.value;
    } else {
      entry.unit = a->second.unit;
      entry.after = a->second.value;
    }
    out.push_back(entry);
  }
  return out;
}

const char* CostVerdictName(CostVerdict verdict) {
  switch (verdict) {
    case CostVerdict::kImproved: return "improved";
    case CostVerdict::kRegressed: return "regressed";
    case CostVerdict::kUnchanged: return "unchanged";
  }
  return "unchanged";
}

Result<CostComparison> CompareCost(const MetricMap& before,
                                   const MetricMap& after) {
  const Result<std::vector<MetricDiffEntry>> diff =
      DiffMetrics(before, after);
  if (!diff.ok()) {
    return diff.status();
  }
  // The policy, in deciding order: name and whether higher is
  // better. Lexicographic: the first entry that differs decides.
  struct PolicyMetric {
    const char* name;
    bool higher_is_better;
  };
  const PolicyMetric policy[] = {
      {"worst_slack_ns", true},
      {"total_power_mw", false},
      {"cell_area_um2", false},
  };
  // Presence is validated for every policy metric before any
  // verdict: a dropped metric is an error even when a higher
  // priority metric would have decided on its own.
  bool any_comparable = false;
  for (const PolicyMetric& metric : policy) {
    const auto b = before.find(metric.name);
    const auto a = after.find(metric.name);
    if ((b == before.end()) != (a == after.end())) {
      return Status::Error(
          ErrorCode::kInvalidArgument,
          std::string("cost policy metric ") + metric.name +
              " is present on only one side");
    }
    if (b != before.end()) {
      any_comparable = true;
    }
  }
  for (const PolicyMetric& metric : policy) {
    const auto b = before.find(metric.name);
    const auto a = after.find(metric.name);
    if (b == before.end() ||
        a->second.value == b->second.value) {
      continue;
    }
    const bool better = metric.higher_is_better
                            ? a->second.value > b->second.value
                            : a->second.value < b->second.value;
    CostComparison comparison;
    comparison.verdict =
        better ? CostVerdict::kImproved : CostVerdict::kRegressed;
    comparison.deciding_metric = metric.name;
    comparison.diff = diff.value();
    return comparison;
  }
  if (!any_comparable) {
    return Status::Error(
        ErrorCode::kInvalidArgument,
        "no cost policy metric is present on both sides");
  }
  CostComparison comparison;
  comparison.verdict = CostVerdict::kUnchanged;
  comparison.diff = diff.value();
  return comparison;
}

}  // namespace eos::session
