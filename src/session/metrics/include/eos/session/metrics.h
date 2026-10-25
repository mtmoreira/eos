// metrics: metric comparison and the CostPolicy verdict
// (L5 session).
//
// Two jobs live here, both pure judgments over MetricMaps (the
// ledger's metric type: a name mapped to a value with an explicit
// unit).
//
// DiffMetrics lines two maps up metric by metric: for every name in
// either map it reports the before value, the after value and the
// delta, and it checks units: a metric whose unit changed between
// the two maps is an error, never a silent delta, because a bare
// number compared across units is how wrong results get certified.
// A metric present on only one side is reported with its presence
// flags and no delta.
//
// CompareCost is the CostPolicy verdict of Revised Plan Section 8:
// lexicographic, never a weighted sum. Timing decides first
// (worst_slack_ns, higher is better), then power (total_power_mw,
// lower is better), then area (cell_area_um2, lower is better). The
// first policy metric that differs decides the verdict, and the
// verdict names that metric; a policy metric that worsens is a
// regression even when a lower-priority metric improves. When every
// comparable policy metric is equal the verdict is unchanged. The
// comparison is exact (no epsilon): no tolerance values are defined
// anywhere in the plan, and inventing one would hide regressions.
// A policy metric present on one side only, or with mismatched
// units, is an error, checked for every policy metric before any
// verdict is reached, and so is a comparison in which no policy
// metric is present on both sides: there is nothing to judge.
//
// This header is a stable surface.
#ifndef EOS_SESSION_METRICS_H_
#define EOS_SESSION_METRICS_H_

#include <cstdint>
#include <string>
#include <vector>

#include "eos/foundation/errors.h"
#include "eos/session/ledger.h"

namespace eos::session {

using foundation::Result;
using foundation::Status;

// One metric lined up across two maps. delta is after minus before
// and is present only when the metric exists on both sides.
struct MetricDiffEntry {
  std::string name;
  std::string unit;
  bool in_before = false;
  bool in_after = false;
  double before = 0.0;
  double after = 0.0;
  bool has_delta = false;
  double delta = 0.0;
};

// The union of both maps, sorted by metric name (MetricMap order).
// Fails with kInvalidArgument when a metric present on both sides
// carries different units.
Result<std::vector<MetricDiffEntry>> DiffMetrics(const MetricMap& before,
                                                   const MetricMap& after);

enum class CostVerdict : std::int32_t {
  kImproved = 0,
  kRegressed = 1,
  kUnchanged = 2,
};

const char* CostVerdictName(CostVerdict verdict);

struct CostComparison {
  CostVerdict verdict = CostVerdict::kUnchanged;
  // The policy metric that decided the verdict. Empty when the
  // verdict is unchanged.
  std::string deciding_metric;
  // The full per-metric diff the verdict was judged from.
  std::vector<MetricDiffEntry> diff;
};

// The CostPolicy verdict over two metric maps, per the rules in
// this header's banner. Fails with kInvalidArgument on a unit
// mismatch, on a policy metric present on only one side, or when
// none of the three policy metrics is present on both sides.
Result<CostComparison> CompareCost(const MetricMap& before,
                                   const MetricMap& after);

}  // namespace eos::session

#endif  // EOS_SESSION_METRICS_H_
