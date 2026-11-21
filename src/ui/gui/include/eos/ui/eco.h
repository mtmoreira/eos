// eco: the GUI's ECO model (L6 interface, Qt-free).
//
// The model the Timing panel's ECO section reads: the
// recorded close_timing run from an eco_summary payload, the
// run's outcome field for field. Parsing is all-or-nothing
// like the other models: a malformed payload leaves the
// previous model untouched and reports false. Counts are
// parsed integers and slacks are parsed doubles, so the
// panel renders exactly what the query measured. A scene
// metric the analysis did not produce is carried as an
// absent optional (the payload's "none"), never as a
// fabricated zero. The step, stop_reason, and note lines
// carry their values verbatim, spaces and all: an accepted
// edit's label and the run's stop reason are sentences, not
// tokens.
#ifndef EOS_UI_ECO_H_
#define EOS_UI_ECO_H_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace eos::ui {

// One scene's closure metrics, before and after the run, in
// nanoseconds. Each metric is optional: the payload prints
// an absent metric as "none" and the model keeps it absent.
struct EcoSceneLine {
  std::string name;
  std::optional<double> initial_worst_slack_ns;
  std::optional<double> initial_worst_hold_slack_ns;
  std::optional<double> worst_slack_ns;
  std::optional<double> total_negative_slack_ns;
  std::optional<double> worst_hold_slack_ns;
  std::optional<double> total_negative_hold_slack_ns;
};

class EcoModel {
 public:
  // Parses an eco_summary payload: an "eco" header line with
  // the run's verdict and counters, then one "scene" line
  // per scene the run analyzed, then one "step" line per
  // accepted iteration carrying its edit label verbatim,
  // then the "stop_reason" and "note" lines carrying their
  // texts verbatim, in that order. Scene names must be
  // unique, no scene line may follow a step line, and the
  // stop_reason and note lines close the payload, exactly
  // once each. Returns false (model unchanged) on any
  // malformed line, unknown key, duplicate scene name, or
  // bad number. A successful load replaces the previous
  // model.
  bool LoadPayload(const std::string& payload);
  void Clear();

  bool Loaded() const { return loaded_; }
  bool Empty() const { return !loaded_; }

  bool Closed() const { return closed_; }
  std::size_t Iterations() const { return iterations_; }
  std::size_t CandidatesTried() const { return candidates_tried_; }
  std::size_t CandidatesRejected() const {
    return candidates_rejected_;
  }
  std::size_t CandidatesSkippedStale() const {
    return candidates_skipped_stale_;
  }
  std::size_t CandidatesSkippedMemory() const {
    return candidates_skipped_memory_;
  }
  std::size_t MaxIterations() const { return max_iterations_; }
  const std::vector<EcoSceneLine>& Scenes() const { return scenes_; }
  const std::vector<std::string>& Steps() const { return steps_; }
  const std::string& StopReason() const { return stop_reason_; }
  const std::string& Note() const { return note_; }

 private:
  bool loaded_ = false;
  bool closed_ = false;
  std::size_t iterations_ = 0;
  std::size_t candidates_tried_ = 0;
  std::size_t candidates_rejected_ = 0;
  std::size_t candidates_skipped_stale_ = 0;
  std::size_t candidates_skipped_memory_ = 0;
  std::size_t max_iterations_ = 0;
  std::vector<EcoSceneLine> scenes_;
  std::vector<std::string> steps_;
  std::string stop_reason_;
  std::string note_;
};

}  // namespace eos::ui

#endif  // EOS_UI_ECO_H_
