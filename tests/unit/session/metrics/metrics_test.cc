// Unit tests: session/metrics, metric diff and the CostPolicy
// verdict.
#include "eos/session/metrics.h"

#include <gtest/gtest.h>

namespace eos::session {
namespace {

MetricMap Map(std::initializer_list<std::pair<const char*, MetricValue>>
                  entries) {
  MetricMap out;
  for (const auto& entry : entries) {
    out[entry.first] = entry.second;
  }
  return out;
}

MetricValue V(double value, const char* unit) {
  MetricValue metric;
  metric.value = value;
  metric.unit = unit;
  return metric;
}

const MetricDiffEntry* Find(const std::vector<MetricDiffEntry>& diff,
                            const std::string& name) {
  for (const MetricDiffEntry& entry : diff) {
    if (entry.name == name) return &entry;
  }
  return nullptr;
}

TEST(MetricDiff, BeforeAfterAndDeltaPerMetric) {
  const MetricMap before = Map({
      {"worst_slack_ns", V(-0.50, "ns")},
      {"total_power_mw", V(10.0, "mW")},
  });
  const MetricMap after = Map({
      {"worst_slack_ns", V(0.25, "ns")},
      {"total_power_mw", V(12.0, "mW")},
      {"cell_area_um2", V(100.0, "um2")},
  });
  const Result<std::vector<MetricDiffEntry>> diff =
      DiffMetrics(before, after);
  ASSERT_TRUE(diff.ok());
  ASSERT_EQ(diff.value().size(), 3u);

  const MetricDiffEntry* slack = Find(diff.value(), "worst_slack_ns");
  ASSERT_NE(slack, nullptr);
  EXPECT_TRUE(slack->in_before);
  EXPECT_TRUE(slack->in_after);
  EXPECT_DOUBLE_EQ(slack->before, -0.50);
  EXPECT_DOUBLE_EQ(slack->after, 0.25);
  ASSERT_TRUE(slack->has_delta);
  EXPECT_DOUBLE_EQ(slack->delta, 0.75);
  EXPECT_EQ(slack->unit, "ns");

  const MetricDiffEntry* power = Find(diff.value(), "total_power_mw");
  ASSERT_NE(power, nullptr);
  ASSERT_TRUE(power->has_delta);
  EXPECT_DOUBLE_EQ(power->delta, 2.0);

  // A metric present on one side only has no delta to claim.
  const MetricDiffEntry* area = Find(diff.value(), "cell_area_um2");
  ASSERT_NE(area, nullptr);
  EXPECT_FALSE(area->in_before);
  EXPECT_TRUE(area->in_after);
  EXPECT_FALSE(area->has_delta);
}

TEST(MetricDiff, UnitChangeIsAnErrorNeverASilentDelta) {
  const MetricMap before = Map({{"total_power_mw", V(10.0, "mW")}});
  const MetricMap after = Map({{"total_power_mw", V(0.010, "W")}});
  const Result<std::vector<MetricDiffEntry>> diff =
      DiffMetrics(before, after);
  ASSERT_FALSE(diff.ok());
  EXPECT_EQ(diff.status().code(), foundation::ErrorCode::kInvalidArgument);
  EXPECT_NE(diff.status().message().find("total_power_mw"),
            std::string::npos);
}

TEST(CostPolicy, TimingDecidesFirstEvenWhenPowerRegresses) {
  const MetricMap before = Map({
      {"worst_slack_ns", V(-0.50, "ns")},
      {"total_power_mw", V(10.0, "mW")},
      {"cell_area_um2", V(100.0, "um2")},
  });
  const MetricMap after = Map({
      {"worst_slack_ns", V(0.10, "ns")},
      {"total_power_mw", V(14.0, "mW")},
      {"cell_area_um2", V(120.0, "um2")},
  });
  const Result<CostComparison> result = CompareCost(before, after);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().verdict, CostVerdict::kImproved);
  EXPECT_EQ(result.value().deciding_metric, "worst_slack_ns");
  EXPECT_EQ(std::string(CostVerdictName(result.value().verdict)),
            "improved");
  EXPECT_EQ(result.value().diff.size(), 3u);
}

TEST(CostPolicy, TimingRegressionIsARegressionWhateverElseImproves) {
  const MetricMap before = Map({
      {"worst_slack_ns", V(0.20, "ns")},
      {"total_power_mw", V(10.0, "mW")},
      {"cell_area_um2", V(100.0, "um2")},
  });
  const MetricMap after = Map({
      {"worst_slack_ns", V(0.05, "ns")},
      {"total_power_mw", V(5.0, "mW")},
      {"cell_area_um2", V(50.0, "um2")},
  });
  const Result<CostComparison> result = CompareCost(before, after);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().verdict, CostVerdict::kRegressed);
  EXPECT_EQ(result.value().deciding_metric, "worst_slack_ns");
}

TEST(CostPolicy, PowerDecidesWhenTimingIsEqual) {
  const MetricMap before = Map({
      {"worst_slack_ns", V(0.20, "ns")},
      {"total_power_mw", V(10.0, "mW")},
      {"cell_area_um2", V(100.0, "um2")},
  });
  const MetricMap after = Map({
      {"worst_slack_ns", V(0.20, "ns")},
      {"total_power_mw", V(8.0, "mW")},
      {"cell_area_um2", V(120.0, "um2")},
  });
  const Result<CostComparison> result = CompareCost(before, after);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().verdict, CostVerdict::kImproved);
  EXPECT_EQ(result.value().deciding_metric, "total_power_mw");

  const MetricMap worse = Map({
      {"worst_slack_ns", V(0.20, "ns")},
      {"total_power_mw", V(11.0, "mW")},
      {"cell_area_um2", V(90.0, "um2")},
  });
  const Result<CostComparison> regressed = CompareCost(before, worse);
  ASSERT_TRUE(regressed.ok());
  EXPECT_EQ(regressed.value().verdict, CostVerdict::kRegressed);
  EXPECT_EQ(regressed.value().deciding_metric, "total_power_mw");
}

TEST(CostPolicy, AreaDecidesWhenTimingAndPowerAreEqual) {
  const MetricMap before = Map({
      {"worst_slack_ns", V(0.20, "ns")},
      {"total_power_mw", V(10.0, "mW")},
      {"cell_area_um2", V(100.0, "um2")},
  });
  const MetricMap after = Map({
      {"worst_slack_ns", V(0.20, "ns")},
      {"total_power_mw", V(10.0, "mW")},
      {"cell_area_um2", V(90.0, "um2")},
  });
  const Result<CostComparison> result = CompareCost(before, after);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().verdict, CostVerdict::kImproved);
  EXPECT_EQ(result.value().deciding_metric, "cell_area_um2");
}

TEST(CostPolicy, AllEqualIsUnchangedWithNoDecidingMetric) {
  const MetricMap metrics = Map({
      {"worst_slack_ns", V(0.20, "ns")},
      {"total_power_mw", V(10.0, "mW")},
      {"cell_area_um2", V(100.0, "um2")},
  });
  const Result<CostComparison> result = CompareCost(metrics, metrics);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().verdict, CostVerdict::kUnchanged);
  EXPECT_TRUE(result.value().deciding_metric.empty());
}

TEST(CostPolicy, PartialPolicyMetricsStillJudgeLexicographically) {
  // Slack absent from both sides is not an error: power decides.
  const MetricMap before = Map({{"total_power_mw", V(10.0, "mW")}});
  const MetricMap after = Map({{"total_power_mw", V(9.0, "mW")}});
  const Result<CostComparison> result = CompareCost(before, after);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().verdict, CostVerdict::kImproved);
  EXPECT_EQ(result.value().deciding_metric, "total_power_mw");
}

TEST(CostPolicy, UnjudgeableComparisonsAreErrors) {
  const MetricMap before = Map({
      {"worst_slack_ns", V(0.20, "ns")},
      {"total_power_mw", V(10.0, "mW")},
  });
  // A policy metric present on one side only.
  const MetricMap dropped = Map({{"worst_slack_ns", V(0.30, "ns")}});
  EXPECT_FALSE(CompareCost(before, dropped).ok());
  // No policy metric on either side.
  const MetricMap other = Map({{"wirelength_um", V(10.0, "um")}});
  EXPECT_FALSE(CompareCost(other, other).ok());
  // A unit change inside a policy metric.
  const MetricMap wrong_unit = Map({
      {"worst_slack_ns", V(200.0, "ps")},
      {"total_power_mw", V(10.0, "mW")},
  });
  EXPECT_FALSE(CompareCost(before, wrong_unit).ok());
}

}  // namespace
}  // namespace eos::session
