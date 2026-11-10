// waivers: waiver records (L2 design-data).
#include "eos/db/waivers.h"

#include <ctime>
#include <utility>

namespace eos::db {
namespace {

// The class vocabularies, in the same words the reports use. The
// check names mirror the check_timing table in the registry; the
// coverage types mirror the report_analysis_coverage list.
const char* kTimingClasses[] = {
    "max_delay", "min_delay", "max_transition", "max_capacitance",
    "max_fanout",
};
const char* kCheckClasses[] = {
    "generated_clocks", "loops", "no_driving_cell", "no_input_delay",
    "no_output_delay", "partial_input_delay", "pulse_clock_cell_type",
    "unconstrained_endpoints", "case_analysis", "clock_crossing",
    "gated_clock", "multiple_clock", "multicycle_path",
};
const char* kCoverageClasses[] = {
    "setup", "hold", "out_setup", "out_hold", "recovery", "removal",
    "clock_gating_setup", "clock_gating_hold", "no-change",
    "min_pulse_width",
};

bool InList(const char* const* list, std::size_t count,
            const std::string& value) {
  for (std::size_t i = 0; i < count; ++i) {
    if (value == list[i]) return true;
  }
  return false;
}

bool IsLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

Status Invalid(const std::string& what) {
  return Status::Error(foundation::ErrorCode::kInvalidArgument, what);
}

}  // namespace

std::optional<WaiverKind> ParseWaiverKind(const std::string& word) {
  if (word == "timing") return WaiverKind::kTiming;
  if (word == "check") return WaiverKind::kCheck;
  if (word == "coverage") return WaiverKind::kCoverage;
  return std::nullopt;
}

std::string WaiverKindWord(WaiverKind kind) {
  switch (kind) {
    case WaiverKind::kTiming: return "timing";
    case WaiverKind::kCheck: return "check";
    case WaiverKind::kCoverage: return "coverage";
  }
  return "unknown";
}

bool IsKnownWaiverClass(WaiverKind kind, const std::string& klass) {
  switch (kind) {
    case WaiverKind::kTiming:
      return InList(kTimingClasses,
                    sizeof(kTimingClasses) / sizeof(kTimingClasses[0]),
                    klass);
    case WaiverKind::kCheck:
      return InList(kCheckClasses,
                    sizeof(kCheckClasses) / sizeof(kCheckClasses[0]),
                    klass);
    case WaiverKind::kCoverage:
      return InList(kCoverageClasses,
                    sizeof(kCoverageClasses) / sizeof(kCoverageClasses[0]),
                    klass);
  }
  return false;
}

bool ParseIsoDate(const std::string& text) {
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (i == 4 || i == 7) continue;
    if (text[i] < '0' || text[i] > '9') return false;
  }
  const int year = std::stoi(text.substr(0, 4));
  const int month = std::stoi(text.substr(5, 2));
  const int day = std::stoi(text.substr(8, 2));
  if (month < 1 || month > 12) return false;
  static const int kDays[] = {31, 28, 31, 30, 31, 30,
                              31, 31, 30, 31, 30, 31};
  int limit = kDays[month - 1];
  if (month == 2 && IsLeapYear(year)) limit = 29;
  return day >= 1 && day <= limit;
}

std::string TodayUtc() {
  const std::time_t now = std::time(nullptr);
  std::tm broken = {};
#if defined(_WIN32)
  gmtime_s(&broken, &now);
#else
  gmtime_r(&now, &broken);
#endif
  char buf[11];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                broken.tm_year + 1900, broken.tm_mon + 1, broken.tm_mday);
  return std::string(buf, 10);
}

bool Waiver::Matches(const WaiverViolation& violation,
                     const std::string& today) const {
  if (IsExpired(today)) return false;
  if (violation.kind != kind) return false;
  if (violation.klass != klass) return false;
  if (violation.target != target) return false;
  if (!scene.empty() && violation.scene != scene) return false;
  return true;
}

std::string WaiverStatusWord(WaiverStatus status) {
  switch (status) {
    case WaiverStatus::kActive: return "active";
    case WaiverStatus::kUnmatched: return "unmatched";
    case WaiverStatus::kExpired: return "expired";
  }
  return "unknown";
}

Result<WaiverId> WaiverSet::Add(
    WaiverKind kind, const std::string& name, const std::string& klass,
    const std::string& target, const std::string& scene,
    const std::string& reason, const std::string& approved_by,
    const std::string& expires, std::uint64_t created_revision,
    const std::vector<std::string>& known_scenes) {
  if (kind != WaiverKind::kTiming && kind != WaiverKind::kCheck &&
      kind != WaiverKind::kCoverage) {
    return Invalid("unknown waiver kind");
  }
  if (!IsKnownWaiverClass(kind, klass)) {
    return Invalid("unknown waiver class for kind " + WaiverKindWord(kind) +
                   ": " + klass);
  }
  if (!scene.empty()) {
    bool known = false;
    for (const std::string& name : known_scenes) {
      if (name == scene) {
        known = true;
        break;
      }
    }
    if (!known) {
      return Invalid("unknown scenario: " + scene);
    }
  }
  if (reason.empty()) {
    return Invalid("a waiver needs a reason");
  }
  if (approved_by.empty()) {
    return Invalid("a waiver needs an approver");
  }
  if (!expires.empty() && !ParseIsoDate(expires)) {
    return Invalid("malformed expiry date, want YYYY-MM-DD: " + expires);
  }
  if (Find(name) != nullptr) {
    return Invalid("duplicate waiver name: " + name);
  }
  Waiver waiver;
  waiver.id = next_id_++;
  waiver.name = name;
  waiver.kind = kind;
  waiver.klass = klass;
  waiver.target = target;
  waiver.scene = scene;
  waiver.reason = reason;
  waiver.approved_by = approved_by;
  waiver.expires = expires;
  waiver.created_revision = created_revision;
  waivers_.push_back(std::move(waiver));
  return waivers_.back().id;
}

bool WaiverSet::Remove(const std::string& name) {
  for (auto it = waivers_.begin(); it != waivers_.end(); ++it) {
    if (it->name == name) {
      waivers_.erase(it);
      return true;
    }
  }
  return false;
}

const Waiver* WaiverSet::Find(const std::string& name) const {
  for (const Waiver& waiver : waivers_) {
    if (waiver.name == name) return &waiver;
  }
  return nullptr;
}

const Waiver* WaiverSet::Find(WaiverId id) const {
  for (const Waiver& waiver : waivers_) {
    if (waiver.id == id) return &waiver;
  }
  return nullptr;
}

const Waiver* WaiverSet::Match(const WaiverViolation& violation,
                               const std::string& today) const {
  for (const Waiver& waiver : waivers_) {
    if (waiver.Matches(violation, today)) return &waiver;
  }
  return nullptr;
}

std::size_t WaiverSet::MatchesFor(
    const Waiver& waiver, const std::vector<WaiverViolation>& violations,
    const std::string& today) const {
  std::size_t count = 0;
  for (const WaiverViolation& violation : violations) {
    if (waiver.Matches(violation, today)) ++count;
  }
  return count;
}

WaiverStatus WaiverSet::StatusOf(const Waiver& waiver, std::size_t matches,
                                 const std::string& today) {
  if (waiver.IsExpired(today)) return WaiverStatus::kExpired;
  return matches > 0 ? WaiverStatus::kActive : WaiverStatus::kUnmatched;
}

WaiverCoverage WaiverSet::Coverage(
    const std::vector<WaiverViolation>& violations,
    const std::string& today) const {
  WaiverCoverage report;
  report.total = waivers_.size();
  for (const Waiver& waiver : waivers_) {
    switch (StatusOf(waiver, MatchesFor(waiver, violations, today),
                     today)) {
      case WaiverStatus::kActive: ++report.active; break;
      case WaiverStatus::kUnmatched: ++report.unmatched; break;
      case WaiverStatus::kExpired: ++report.expired; break;
    }
  }
  return report;
}

WaiverState WaiverSet::ExportState() const {
  WaiverState state;
  state.waivers = waivers_;
  state.next_id = next_id_;
  return state;
}

Result<WaiverSet> WaiverSet::FromState(const WaiverState& state) {
  WaiverSet set;
  WaiverId max_id = 0;
  for (const Waiver& waiver : state.waivers) {
    if (waiver.id == 0 || set.Find(waiver.id) != nullptr ||
        set.Find(waiver.name) != nullptr ||
        (waiver.kind != WaiverKind::kTiming &&
         waiver.kind != WaiverKind::kCheck &&
         waiver.kind != WaiverKind::kCoverage) ||
        !IsKnownWaiverClass(waiver.kind, waiver.klass) ||
        waiver.reason.empty() || waiver.approved_by.empty() ||
        (!waiver.expires.empty() && !ParseIsoDate(waiver.expires))) {
      return Invalid("waiver state: duplicate or inconsistent waiver");
    }
    set.waivers_.push_back(waiver);
    if (waiver.id > max_id) {
      max_id = waiver.id;
    }
  }
  if (state.next_id <= max_id) {
    return Invalid("waiver state: next_id would reuse an id");
  }
  set.next_id_ = state.next_id;
  return set;
}

}  // namespace eos::db
