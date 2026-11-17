// extract: the GUI's extract model (L6 interface, Qt-free).
//
// The model the Timing panel's Extraction section reads: the
// recorded extract run from an extract_summary payload, with
// the estimated-versus-extracted comparison the query paired
// from the latest run_timing. Parsing is all-or-nothing like
// the other models: a malformed payload leaves the previous
// model untouched and reports false. Counts are parsed
// integers and slacks are parsed doubles, so the panel
// renders exactly what the query measured. A payload value
// is a single token: no value in this payload contains a
// space, except the sta_note line, whose value is the
// extract payload's unavailable statement carried verbatim,
// spaces and all.
#ifndef EOS_UI_EXTRACT_H_
#define EOS_UI_EXTRACT_H_

#include <cstddef>
#include <string>
#include <vector>

namespace eos::ui {

// One scene's post-route estimated worst slack, as the
// extract run's STA leg measured it, in nanoseconds.
struct ExtractSlack {
  std::string scene;
  double worst_slack_ns = 0.0;
};

// One estimated-versus-extracted comparison pair: the two
// scenes of one (mode, corner) the latest run_timing held at
// both parasitics levels, with their worst setup and hold
// slacks in nanoseconds.
struct ExtractCompare {
  std::string mode;
  std::string corner;
  std::string estimate_scene;
  std::string extracted_scene;
  double estimated_worst_slack_ns = 0.0;
  double extracted_worst_slack_ns = 0.0;
  double estimated_worst_hold_slack_ns = 0.0;
  double extracted_worst_hold_slack_ns = 0.0;
};

class ExtractModel {
 public:
  // Parses an extract_summary payload: an "extract" header
  // line with the run's counts and the SPEF and model paths,
  // then one "slack" line per scene the run's STA leg
  // measured, or one "sta_note" line carrying the run's
  // unavailable statement verbatim when its STA leg could
  // not analyze, then one "compare" line per (mode, corner)
  // pair the latest run_timing held at both parasitics
  // levels. Slack scene names must be unique, compare
  // (mode, corner) pairs must be unique, a record carries
  // slack lines or one sta_note, never both, and no slack
  // or sta_note line may follow a compare line. Returns
  // false (model unchanged) on any malformed line, unknown
  // key, duplicate name or pair, or bad number. A
  // successful load replaces the previous model.
  bool LoadPayload(const std::string& payload);
  void Clear();

  bool Loaded() const { return loaded_; }
  bool Empty() const { return !loaded_; }

  std::size_t Nets() const { return nets_; }
  std::size_t Rsegs() const { return rsegs_; }
  std::size_t CapNodes() const { return cap_nodes_; }
  std::size_t CcSegs() const { return cc_segs_; }
  const std::string& SpefPath() const { return spef_path_; }
  const std::string& ModelPath() const { return model_path_; }
  const std::vector<ExtractSlack>& Slacks() const { return slacks_; }
  const std::string& StaNote() const { return sta_note_; }
  const std::vector<ExtractCompare>& Compares() const {
    return compares_;
  }

 private:
  bool loaded_ = false;
  std::size_t nets_ = 0;
  std::size_t rsegs_ = 0;
  std::size_t cap_nodes_ = 0;
  std::size_t cc_segs_ = 0;
  std::string spef_path_;
  std::string model_path_;
  std::vector<ExtractSlack> slacks_;
  std::string sta_note_;
  std::vector<ExtractCompare> compares_;
};

}  // namespace eos::ui

#endif  // EOS_UI_EXTRACT_H_
