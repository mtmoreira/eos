// cts: the GUI's CTS model (L6 interface, Qt-free).
//
// The model the Clocks panel's CTS section reads: the recorded
// clock trees from a cts_summary payload, one entry per clock
// net the session ran eos::cts on. Parsing is all-or-nothing
// like the other models: a malformed payload leaves the
// previous model untouched and reports false. Counts are
// parsed integers and slacks are parsed doubles, so the panel
// renders exactly what the query measured. A payload value is
// a single token: no value in this payload contains a space,
// except the sta_note line, whose value is the cts payload's
// unavailable statement carried verbatim, spaces and all.
#ifndef EOS_UI_CTS_H_
#define EOS_UI_CTS_H_

#include <cstddef>
#include <string>
#include <vector>

namespace eos::ui {

// One scene's post-CTS worst slack for one clock net, as the
// cts run measured it, in nanoseconds.
struct CtsSlack {
  std::string scene;
  double worst_slack_ns = 0.0;
};

// One recorded CTS run: the clock net it built the tree for,
// the engine's counts (roots, buffers inserted, sinks, clock
// subnets), the Liberty corner characterization used, and the
// run's post-CTS STA outcome. sinks counts every electrical
// load pin on the net; clock_sinks counts the flop clock
// pins among them (the clock view's sink count for the net),
// and the two differ when a net also feeds a non-clock load.
// The outcome is either the per-scene post-CTS slacks the run
// measured, or sta_note, the cts payload's unavailable
// statement verbatim, when the run's STA leg could not
// analyze; the two never appear together.
struct CtsNet {
  std::string name;
  std::size_t roots = 0;
  std::size_t buffers = 0;
  std::size_t sinks = 0;
  std::size_t clock_sinks = 0;
  std::size_t subnets = 0;
  std::string liberty_corner;
  std::vector<CtsSlack> slacks;
  std::string sta_note;
};

class CtsModel {
 public:
  // Parses a cts_summary payload: a "cts" header line with
  // the net count, then per recorded net one "net" line with
  // the run's counts and Liberty corner, each followed by one
  // "slack" line per scene that run measured, or by one
  // "sta_note" line carrying the run's unavailable statement
  // verbatim when its STA leg could not analyze. The header's
  // net count must match the net lines, net names must be
  // unique, slack scene names must be unique per net, and a
  // slack or sta_note line must follow a net line (a net
  // carries slack lines or one sta_note, never both).
  // Returns false (model unchanged) on any malformed line,
  // unknown key, duplicate name, bad number, or count
  // mismatch. A successful load replaces the previous model.
  bool LoadPayload(const std::string& payload);
  void Clear();

  bool Loaded() const { return loaded_; }
  bool Empty() const { return !loaded_; }
  const std::vector<CtsNet>& Nets() const { return nets_; }

 private:
  bool loaded_ = false;
  std::vector<CtsNet> nets_;
};

}  // namespace eos::ui

#endif  // EOS_UI_CTS_H_
