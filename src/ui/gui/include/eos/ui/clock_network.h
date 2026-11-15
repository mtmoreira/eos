// clock_network: the GUI's clock-network model (L6 interface,
// Qt-free).
//
// The model the Clocks panel reads: the structured clock
// network from a clock_network_summary payload. Parsing is
// all-or-nothing like the other models: a malformed payload
// leaves the previous model untouched and reports false.
// Measured values stay as the payload's own tokens, so the
// panel renders exactly what the command measured; counts are
// parsed integers. A payload value may be a brace group, which
// may contain spaces; the parser is brace-aware.
#ifndef EOS_UI_CLOCK_NETWORK_H_
#define EOS_UI_CLOCK_NETWORK_H_

#include <cstddef>
#include <string>
#include <vector>

namespace eos::ui {

// One node of one clock's tree: the source, a leaf group with
// its sink count (elaborated stage), a buffer, or a single
// clock-pin sink (mapped stage). `count` keeps the payload
// token ("-" when the node carries no count); `pin` is "-"
// outside the mapped stage; `generates` names the generated
// clock a divider or sink instance sources, or "-".
struct ClockNetworkNode {
  int depth = 0;
  std::string kind;
  std::string name;
  std::string pin;
  std::string count;
  std::string generates;
};

// One clock of the network: the report_clock entry values
// (period, waveform, skew as the payload's tokens), the tree
// stage's verdict (tree: none, leaves, or pins; sinks: the
// count or "-"), the generated clock's master and modification
// ("-" when not generated), and the tree's nodes in walk
// order.
struct ClockNetworkClock {
  std::string name;
  bool generated = false;
  std::string period;
  std::string rise;
  std::string fall;
  std::string skew;
  std::string sources;  // brace group, as written ("{clk}")
  std::string sinks;
  std::string master;
  std::string modification;  // brace group or "-"
  std::string tree;
  std::vector<ClockNetworkNode> nodes;
};

// Two clocks sharing sink instances: the count of instances
// that are sinks of both.
struct ClockNetworkShared {
  std::string clock;
  std::string other;
  std::size_t count = 0;
};

// The parsed clock_network_summary payload: the stage
// (constraints, elaborated, mapped), the clocks with their
// trees, the shared-sink lines, and the notes.
class ClockNetworkModel {
 public:
  // Replaces the model with the payload's contents. Returns
  // false, leaving the model untouched, when the payload is
  // malformed in any way.
  bool LoadPayload(const std::string& payload);

  bool Loaded() const { return loaded_; }
  const std::string& stage() const { return stage_; }
  const std::vector<ClockNetworkClock>& clocks() const { return clocks_; }
  const std::vector<ClockNetworkShared>& shared() const { return shared_; }
  const std::vector<std::string>& notes() const { return notes_; }

 private:
  bool loaded_ = false;
  std::string stage_;
  std::vector<ClockNetworkClock> clocks_;
  std::vector<ClockNetworkShared> shared_;
  std::vector<std::string> notes_;
};

}  // namespace eos::ui

#endif  // EOS_UI_CLOCK_NETWORK_H_
