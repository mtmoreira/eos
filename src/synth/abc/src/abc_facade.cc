// abc facade implementation: the only EOS-authored translation
// unit in eda_synth_abc. Everything upstream of this file is
// imported code (see PROVENANCE.md); this file translates between
// the EOS surface in include/eos/synth/abc.h and the upstream ABC
// command interpreter, and converts upstream outcomes (a nonzero
// command status, the prover's printed conclusion) into EOS Status
// and verdict values, so no upstream type crosses the boundary.
//
// Upstream ABC is driven the way its own library embedding
// prescribes: Abc_Start creates the global frame, commands run
// through Cmd_CommandExecute in process, Abc_Stop releases it. The
// frame is a singleton upstream, so the facade guards it the way
// the STA facade guards its singleton.
//
// The cec verdict: upstream's cec command returns success whether
// or not the networks proved equivalent; the conclusion exists
// only in what the prover prints ("Networks are equivalent ..."
// / "Networks are NOT EQUIVALENT."). The facade therefore captures
// the process output for the duration of the cec call (a file
// descriptor redirect of stdout and stderr to a scratch file,
// restored before returning) and classifies the captured text.
// Anything that is not one of the prover's two conclusions is
// kUnknown, never a pass.
#include "eos/synth/abc.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

extern "C" {
struct Abc_Frame_t_;
typedef struct Abc_Frame_t_ Abc_Frame_t;
void Abc_Start();
void Abc_Stop();
Abc_Frame_t* Abc_FrameGetGlobalFrame();
int Cmd_CommandExecute(Abc_Frame_t* pAbc, const char* sCommand);
}

namespace eos::synth {
namespace {

using foundation::ErrorCode;

// Upstream ABC keeps a single global frame. Guard it so a second
// live engine fails cleanly instead of corrupting the first.
std::atomic<bool> g_engine_alive{false};

std::string Quote(const std::string& path) { return "\"" + path + "\""; }

Status IoError(const std::string& what, const std::string& path) {
  return Status::Error(ErrorCode::kIoError, what + ": " + path);
}

// ---------------------------------------------------------------------------
// The Liberty subset the genlib needs: per cell, its area, whether
// it is sequential (an ff or latch block), and per pin the
// direction, capacitance, combinational function, and scalar delay
// (the maximum of the pin's cell_rise and cell_fall scalar values,
// with table arcs flagged so the caller can refuse them). This is
// the same subset and the same qualification the ECO cone
// resynthesis converter uses internally; it is re-implemented
// here rather than shared because that converter is internal to
// the timing-eco module, which already depends on this facade.

std::string TrimText(const std::string& text) {
  const std::size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const std::size_t last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

std::string ReadTextFile(const std::string& path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

// Returns the index just past the closing brace that matches the
// opening brace at open, or std::string::npos.
std::size_t MatchBrace(const std::string& text, std::size_t open) {
  int depth = 0;
  for (std::size_t i = open; i < text.size(); ++i) {
    if (text[i] == '{') {
      ++depth;
    } else if (text[i] == '}') {
      if (--depth == 0) return i + 1;
    }
  }
  return std::string::npos;
}

// Finds the blocks opened by headers of the form keyword ( name ) {
// inside text, calling fn(name, block_text_without_outer_braces).
// A keyword occurrence that is not followed by '(' (for example
// "cell" at the start of "cell_rise") is skipped, not treated as
// a block.
template <typename Fn>
void ForEachBlock(const std::string& text, const std::string& keyword,
                  Fn&& fn) {
  std::size_t pos = 0;
  while (true) {
    const std::size_t at = text.find(keyword, pos);
    if (at == std::string::npos) return;
    std::size_t i = at + keyword.size();
    while (i < text.size() &&
           std::isspace(static_cast<unsigned char>(text[i]))) {
      ++i;
    }
    if (i >= text.size() || text[i] != '(') {
      pos = at + keyword.size();
      continue;
    }
    const std::size_t close_paren = text.find(')', i);
    if (close_paren == std::string::npos) return;
    std::string name = TrimText(text.substr(i + 1, close_paren - i - 1));
    if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
      name = name.substr(1, name.size() - 2);
    }
    const std::size_t open = text.find('{', close_paren);
    if (open == std::string::npos) return;
    const std::size_t end = MatchBrace(text, open);
    if (end == std::string::npos) return;
    fn(name, text.substr(open + 1, end - open - 2));
    pos = end;
  }
}

bool FindScalar(const std::string& text, const std::string& key,
                double* out) {
  const std::size_t at = text.find(key);
  if (at == std::string::npos) return false;
  const std::size_t colon = text.find(':', at);
  if (colon == std::string::npos) return false;
  const char* begin = text.c_str() + colon + 1;
  char* end = nullptr;
  const double value = std::strtod(begin, &end);
  if (end == begin) return false;
  *out = value;
  return true;
}

std::string FindQuoted(const std::string& text, const std::string& key) {
  const std::size_t at = text.find(key);
  if (at == std::string::npos) return "";
  const std::size_t open = text.find('"', at);
  if (open == std::string::npos) return "";
  const std::size_t close = text.find('"', open + 1);
  if (close == std::string::npos) return "";
  return text.substr(open + 1, close - open - 1);
}

struct LibPin {
  bool is_input = false;
  bool is_output = false;
  double capacitance = 0.0;
  std::string function;
  double delay = -1.0;
  bool delay_is_table = false;
};

struct LibCell {
  std::string name;
  double area = 0.0;
  bool sequential = false;
  std::map<std::string, LibPin> pins;
};

// One scalar delay value out of a pin's timing blocks: the maximum
// cell_rise / cell_fall scalar; table arcs set delay_is_table.
void ParsePinTiming(const std::string& pin_block, LibPin* pin) {
  for (const char* arc : {"cell_rise", "cell_fall"}) {
    std::size_t pos = 0;
    while (true) {
      const std::size_t at = pin_block.find(arc, pos);
      if (at == std::string::npos) break;
      const std::size_t open_paren = pin_block.find('(', at);
      const std::size_t close_paren = pin_block.find(')', at);
      if (open_paren == std::string::npos ||
          close_paren == std::string::npos) {
        break;
      }
      const std::string model =
          TrimText(pin_block.substr(open_paren + 1,
                                    close_paren - open_paren - 1));
      if (model != "scalar") {
        pin->delay_is_table = true;
        pos = close_paren;
        continue;
      }
      const std::size_t values = pin_block.find("values", close_paren);
      if (values == std::string::npos) break;
      const std::size_t quote = pin_block.find('"', values);
      const std::size_t quote_end =
          quote == std::string::npos
              ? std::string::npos
              : pin_block.find('"', quote + 1);
      if (quote_end == std::string::npos) break;
      const double value = std::strtod(
          pin_block.substr(quote + 1, quote_end - quote - 1).c_str(),
          nullptr);
      pin->delay = std::max(pin->delay, value);
      pos = quote_end;
    }
  }
}

// Rewrites a Liberty function to genlib syntax (& to *, | to +,
// ~ to !, juxtaposition to *), or returns nullopt when the
// function cannot be expressed: a token that is not an operator,
// a parenthesis, a constant, or one of the cell's input pins.
std::optional<std::string> LibertyFunctionToGenlib(
    const std::string& function, const LibCell& cell) {
  std::vector<std::string> tokens;
  for (std::size_t i = 0; i < function.size();) {
    const char c = function[i];
    if (std::isspace(static_cast<unsigned char>(c))) {
      ++i;
      continue;
    }
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
      std::size_t j = i;
      while (j < function.size() &&
             (std::isalnum(static_cast<unsigned char>(function[j])) ||
              function[j] == '_')) {
        ++j;
      }
      tokens.push_back(function.substr(i, j - i));
      i = j;
      continue;
    }
    if (c == '&' || c == '|' || c == '+' || c == '!' || c == '~' ||
        c == '^' || c == '(' || c == ')' || c == '*') {
      // '+' is OR in the Liberty dialect that writes A+B (as
      // opposed to A|B); genlib writes OR as '+' either way.
      tokens.push_back(std::string(
          1, c == '~' ? '!' : c == '&' ? '*' : c == '|' ? '+' : c));
      ++i;
      continue;
    }
    return std::nullopt;
  }
  const auto is_operand_end = [](const std::string& token) {
    return token == ")" ||
           (!token.empty() &&
            (std::isalnum(static_cast<unsigned char>(token[0])) ||
             token[0] == '_'));
  };
  const auto is_operand_start = [](const std::string& token) {
    return token == "(" || token == "!" ||
           (!token.empty() &&
            (std::isalnum(static_cast<unsigned char>(token[0])) ||
             token[0] == '_'));
  };
  std::string out;
  std::string previous;
  for (const std::string& token : tokens) {
    if (!previous.empty() && is_operand_end(previous) &&
        is_operand_start(token)) {
      out += "*";  // Liberty juxtaposition is AND
    }
    if (is_operand_end(token) && token != ")") {
      if (token != "0" && token != "1") {
        const auto pin = cell.pins.find(token);
        if (pin == cell.pins.end() || !pin->second.is_input) {
          return std::nullopt;  // a state variable, not an input
        }
      }
    }
    out += token;
    previous = token;
  }
  if (out.empty()) return std::nullopt;
  return out;
}

// Redirects stdout and stderr to a scratch file for its lifetime
// and reads the captured text back on destruction.
class OutputCapture {
 public:
  OutputCapture() {
    static std::atomic<unsigned long> counter{0};
    path_ = std::filesystem::temp_directory_path() /
            ("eos_abc_capture_" + std::to_string(counter.fetch_add(1)) +
             ".txt");
    std::fflush(stdout);
    std::fflush(stderr);
    saved_out_ = ::dup(STDOUT_FILENO);
    saved_err_ = ::dup(STDERR_FILENO);
    const int fd =
        ::open(path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
      ::dup2(fd, STDOUT_FILENO);
      ::dup2(fd, STDERR_FILENO);
      ::close(fd);
      active_ = true;
    }
  }
  ~OutputCapture() { Finish(); }
  OutputCapture(const OutputCapture&) = delete;
  OutputCapture& operator=(const OutputCapture&) = delete;

  // Restores the real output streams and returns the captured
  // text. Idempotent; the destructor calls it when the caller did
  // not.
  std::string Finish() {
    if (finished_) {
      return text_;
    }
    finished_ = true;
    std::fflush(stdout);
    std::fflush(stderr);
    if (saved_out_ >= 0) {
      ::dup2(saved_out_, STDOUT_FILENO);
      ::close(saved_out_);
      saved_out_ = -1;
    }
    if (saved_err_ >= 0) {
      ::dup2(saved_err_, STDERR_FILENO);
      ::close(saved_err_);
      saved_err_ = -1;
    }
    if (active_) {
      std::ifstream in(path_);
      std::stringstream text;
      text << in.rdbuf();
      text_ = text.str();
    }
    std::error_code ec;
    std::filesystem::remove(path_, ec);
    return text_;
  }

 private:
  std::filesystem::path path_;
  int saved_out_ = -1;
  int saved_err_ = -1;
  bool active_ = false;
  bool finished_ = false;
  std::string text_;
};

}  // namespace

struct AbcEngine::Impl {
  Abc_Frame_t* frame = nullptr;
  bool owns_frame = false;

  Impl() {
    bool expected = false;
    if (!g_engine_alive.compare_exchange_strong(expected, true)) {
      // Another engine owns the upstream frame. Leave frame null;
      // every operation then fails with kInvalidState.
      return;
    }
    owns_frame = true;
    Abc_Start();
    frame = Abc_FrameGetGlobalFrame();
  }

  ~Impl() {
    if (owns_frame) {
      Abc_Stop();
      g_engine_alive.store(false);
    }
  }

  // Restarts the upstream frame. Conversions restart first, so a
  // failed read leaves an empty network behind instead of the
  // previous call's: ABC's interpreter continues after a failed
  // command and its return value reflects the last step, so
  // without the restart a stale network could be converted and
  // compared as if it were the requested one.
  void Restart() {
    if (!owns_frame) {
      return;
    }
    Abc_Stop();
    Abc_Start();
    frame = Abc_FrameGetGlobalFrame();
  }

  Status Execute(const std::string& command, const std::string& what) {
    if (frame == nullptr) {
      return Status::Error(
          ErrorCode::kInvalidState,
          "another AbcEngine owns the upstream ABC frame; ABC "
          "sessions must not overlap");
    }
    if (Cmd_CommandExecute(frame, command.c_str()) != 0) {
      return Status::Error(ErrorCode::kIoError,
                           "ABC command failed (" + what + "): " + command);
    }
    return Status::Ok();
  }
};

AbcEngine::AbcEngine() : impl_(new Impl) {}
AbcEngine::~AbcEngine() = default;
AbcEngine::AbcEngine(AbcEngine&&) noexcept = default;
AbcEngine& AbcEngine::operator=(AbcEngine&&) noexcept = default;

Status AbcEngine::MapForDelay(const std::string& input_verilog,
                              const std::string& genlib_path,
                              const std::string& output_verilog) {
  if (!std::filesystem::exists(input_verilog)) {
    return IoError("input netlist not found", input_verilog);
  }
  if (!std::filesystem::exists(genlib_path)) {
    return IoError("genlib library not found", genlib_path);
  }
  // Delay mapping flow: read the library and the netlist, strash
  // to an AIG (structural hashing folds double inversions and
  // shared logic), rewrite and balance the AIG, then map for delay
  // and write the mapped netlist back out as Verilog. The netlist
  // is read with "read -m": with a library loaded, ABC's Verilog
  // reader treats a gate netlist as a mapped network and refuses
  // it without the mapped flag (the reader says so itself).
  // The read runs as its own checked step on a restarted frame:
  // ABC's command interpreter continues a semicolon script after
  // a failed step, and mapping a stale or empty network must
  // never be mistaken for mapping the requested one.
  impl_->Restart();
  {
    std::error_code ec;
    std::filesystem::remove(output_verilog, ec);
  }
  Status status = impl_->Execute(
      "read_library " + Quote(genlib_path) + "; read -m " +
          Quote(input_verilog),
      "netlist read");
  if (!status.ok()) {
    return status;
  }
  const std::string script =
      "strash; rewrite; balance; map; write_verilog " +
      Quote(output_verilog);
  status = impl_->Execute(script, "delay mapping");
  if (!status.ok()) {
    return status;
  }
  if (!std::filesystem::exists(output_verilog)) {
    return IoError("ABC wrote no mapped netlist", output_verilog);
  }
  return Status::Ok();
}

Result<std::vector<std::string>> AbcEngine::LibertyToGenlib(
    const std::string& liberty_path, const std::string& genlib_path) {
  const std::string text = ReadTextFile(liberty_path);
  if (text.empty()) {
    return Status::Error(ErrorCode::kIoError,
                         "Liberty file is empty or unreadable: " +
                             liberty_path);
  }
  std::map<std::string, LibCell> cells;
  ForEachBlock(text, "cell", [&](const std::string& name,
                                 const std::string& block) {
    LibCell cell;
    cell.name = name;
    FindScalar(block, "area", &cell.area);
    cell.sequential = block.find("ff (") != std::string::npos ||
                      block.find("ff(") != std::string::npos ||
                      block.find("latch (") != std::string::npos ||
                      block.find("latch(") != std::string::npos;
    ForEachBlock(block, "pin", [&](const std::string& pin_name,
                                   const std::string& pin_block) {
      LibPin pin;
      const std::size_t at = pin_block.find("direction");
      if (at != std::string::npos) {
        const std::size_t colon = pin_block.find(':', at);
        const std::size_t semi = pin_block.find(';', at);
        if (colon != std::string::npos && semi != std::string::npos) {
          const std::string dir_word =
              TrimText(pin_block.substr(colon + 1, semi - colon - 1));
          pin.is_input = dir_word == "input" || dir_word == "inout";
          pin.is_output = dir_word == "output" || dir_word == "inout";
        }
      }
      FindScalar(pin_block, "capacitance", &pin.capacitance);
      pin.function = FindQuoted(pin_block, "function");
      ParsePinTiming(pin_block, &pin);
      cell.pins[pin_name] = pin;
    });
    cells[name] = cell;
  });
  if (cells.empty()) {
    return Status::Error(ErrorCode::kIoError,
                         "no cells found in Liberty file: " + liberty_path);
  }
  std::vector<std::string> emitted;
  std::stringstream genlib;
  for (const auto& entry : cells) {
    const LibCell& cell = entry.second;
    if (cell.sequential) continue;
    const LibPin* output = nullptr;
    std::string output_name;
    std::size_t outputs = 0;
    for (const auto& pin_entry : cell.pins) {
      if (pin_entry.second.is_output && !pin_entry.second.is_input) {
        ++outputs;
        output = &pin_entry.second;
        output_name = pin_entry.first;
      }
    }
    if (outputs != 1 || output == nullptr || output->function.empty() ||
        output->delay_is_table || output->delay < 0.0) {
      continue;
    }
    const auto function = LibertyFunctionToGenlib(output->function, cell);
    if (!function.has_value()) continue;
    const double area = cell.area > 0.0 ? cell.area : 1.0;
    genlib << "GATE " << cell.name << " " << area << " " << output_name
           << "=" << *function << ";\n";
    genlib << "PIN * INV 1 999 " << output->delay << " 0.0 "
           << output->delay << " 0.0;\n";
    emitted.push_back(cell.name);
  }
  if (emitted.empty()) {
    return Status::Error(
        ErrorCode::kUnsupported,
        "no Liberty cell qualifies for genlib mapping (combinational, "
        "one output pin, function over its inputs, scalar delay)");
  }
  {
    std::ofstream out(genlib_path);
    if (!out) {
      return Status::Error(ErrorCode::kIoError,
                           "cannot write genlib file: " + genlib_path);
    }
    out << genlib.str();
    out.close();
    if (!out) {
      return Status::Error(ErrorCode::kIoError,
                           "cannot write genlib file: " + genlib_path);
    }
  }
  return emitted;
}

Status AbcEngine::MapTechnology(const std::string& input_network,
                                const std::string& genlib_path,
                                const std::string& output_verilog,
                                MapTarget target) {
  if (impl_->frame == nullptr) {
    return Status::Error(
        ErrorCode::kInvalidState,
        "another AbcEngine owns the upstream ABC frame; ABC "
        "sessions must not overlap");
  }
  if (!std::filesystem::exists(input_network)) {
    return IoError("input network not found", input_network);
  }
  if (!std::filesystem::exists(genlib_path)) {
    return IoError("genlib library not found", genlib_path);
  }
  // Each step runs as its own checked command, as in
  // OptimizeGeneric: the interpreter continues a semicolon
  // script after a failed step, so a single script could map a
  // stale network and report a result that never happened.
  // The read runs unmapped: this method's input is the bridge
  // form, not a mapped gate netlist (that is MapForDelay's).
  impl_->Restart();
  {
    std::error_code ec;
    std::filesystem::remove(output_verilog, ec);
  }
  Status status = impl_->Execute("read_library " + Quote(genlib_path),
                                 "genlib read");
  if (!status.ok()) return status;
  status = impl_->Execute("read " + Quote(input_network), "network read");
  if (!status.ok()) return status;
  for (const char* step : {"strash", "rewrite", "balance"}) {
    status = impl_->Execute(step, "mapping-time optimization");
    if (!status.ok()) return status;
  }
  status = impl_->Execute(target == MapTarget::kArea ? "map -a" : "map",
                          "technology mapping");
  if (!status.ok()) return status;
  status = impl_->Execute("write_verilog " + Quote(output_verilog),
                          "mapped netlist write");
  if (!status.ok()) return status;
  if (!std::filesystem::exists(output_verilog)) {
    return IoError("ABC wrote no mapped netlist", output_verilog);
  }
  return Status::Ok();
}

// Parses one print_stats line: "  and =%7d ... lev =%3d".
// Returns false when either number is absent, so a caller never
// mistakes an unreadable stats line for a measured zero.
bool ParsePrintStats(const std::string& text, AbcNetworkStats& stats) {
  const auto parse_after = [&text](const std::string& key, int& out) {
    const std::size_t at = text.find(key);
    if (at == std::string::npos) return false;
    std::size_t i = at + key.size();
    while (i < text.size() && text[i] == ' ') ++i;
    if (i >= text.size() || text[i] < '0' || text[i] > '9') return false;
    int value = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
      value = value * 10 + (text[i] - '0');
      ++i;
    }
    out = value;
    return true;
  };
  return parse_after("and =", stats.and_count) &&
         parse_after("lev =", stats.levels);
}

std::string GenericScriptSteps(const std::string& script) {
  if (script == "default") {
    return "balance; rewrite; refactor; resub; balance";
  }
  if (script == "balance") return "balance";
  if (script == "rewrite") return "rewrite";
  if (script == "none") return "";
  return "";
}

bool IsKnownGenericScript(const std::string& script) {
  return script == "default" || script == "balance" ||
         script == "rewrite" || script == "none";
}

Result<AbcGenericResult> AbcEngine::OptimizeGeneric(
    const std::string& input_verilog, const std::string& script,
    const std::string& output_verilog) {
  if (impl_->frame == nullptr) {
    return Status::Error(
        ErrorCode::kInvalidState,
        "another AbcEngine owns the upstream ABC frame; ABC "
        "sessions must not overlap");
  }
  if (!IsKnownGenericScript(script)) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "unknown generic script: " + script);
  }
  if (!std::filesystem::exists(input_verilog)) {
    return IoError("input netlist not found", input_verilog);
  }
  // Each step runs as its own checked command: the interpreter
  // continues a semicolon script after a failed step, so a single
  // script could optimize a stale network and report numbers for
  // a run that never happened.
  impl_->Restart();
  {
    std::error_code ec;
    std::filesystem::remove(output_verilog, ec);
  }
  Status status = impl_->Execute("read " + Quote(input_verilog), "netlist read");
  if (!status.ok()) return status;
  status = impl_->Execute("strash", "structural hashing");
  if (!status.ok()) return status;
  AbcGenericResult result;
  {
    OutputCapture capture;
    status = impl_->Execute("print_stats", "baseline stats");
    const std::string captured = capture.Finish();
    if (!status.ok()) return status;
    if (!ParsePrintStats(captured, result.before)) {
      return Status::Error(ErrorCode::kInvalidState,
                           "ABC print_stats output could not be parsed");
    }
  }
  const std::string steps = GenericScriptSteps(script);
  std::size_t start = 0;
  while (start < steps.size()) {
    const std::size_t semi = steps.find(';', start);
    std::string step = steps.substr(
        start, semi == std::string::npos ? std::string::npos : semi - start);
    const std::size_t first = step.find_first_not_of(" \t");
    const std::size_t last = step.find_last_not_of(" \t");
    if (first != std::string::npos) {
      step = step.substr(first, last - first + 1);
      status = impl_->Execute(step, "generic script step");
      if (!status.ok()) return status;
    }
    if (semi == std::string::npos) break;
    start = semi + 1;
  }
  {
    OutputCapture capture;
    status = impl_->Execute("print_stats", "optimized stats");
    const std::string captured = capture.Finish();
    if (!status.ok()) return status;
    if (!ParsePrintStats(captured, result.after)) {
      return Status::Error(ErrorCode::kInvalidState,
                           "ABC print_stats output could not be parsed");
    }
  }
  status = impl_->Execute("write_verilog " + Quote(output_verilog),
                          "optimized netlist write");
  if (!status.ok()) return status;
  if (!std::filesystem::exists(output_verilog)) {
    return IoError("ABC wrote no optimized netlist", output_verilog);
  }
  return result;
}

Result<EquivalenceVerdict> AbcEngine::ProveEquivalent(
    const std::string& first_verilog, const std::string& second_verilog,
    const std::string& genlib_path) {
  if (impl_->frame == nullptr) {
    return Status::Error(
        ErrorCode::kInvalidState,
        "another AbcEngine owns the upstream ABC frame; ABC "
        "sessions must not overlap");
  }
  // The proof runs on AIGER intermediates: cec's own file reader
  // cannot read a mapped Verilog netlist (it reads with the
  // mapped flag off, and the Verilog reader refuses a gate
  // netlist once a library is loaded), so each netlist is first
  // read as mapped, strashed to an AIG, and written as AIGER,
  // whose symbol table carries the input and output names cec
  // matches on. A failure anywhere in that chain leaves no
  // conclusion in the captured output, which classifies as
  // kUnknown below.
  static std::atomic<unsigned long> counter{0};
  const unsigned long id = counter.fetch_add(1);
  const std::string first_aig =
      (std::filesystem::temp_directory_path() /
       ("eos_abc_cec_first_" + std::to_string(id) + ".aig"))
          .string();
  const std::string second_aig =
      (std::filesystem::temp_directory_path() /
       ("eos_abc_cec_second_" + std::to_string(id) + ".aig"))
          .string();
  // The steps run as separate commands, each checked: ABC's
  // command interpreter continues a semicolon script after a
  // failed step, so a single script could strash and compare a
  // stale network left over from the previous step and report a
  // proof that never ran. A failed conversion is a proof that did
  // not conclude: kUnknown.
  {
    std::error_code ec;
    std::filesystem::remove(first_aig, ec);
    std::filesystem::remove(second_aig, ec);
  }
  const auto convert = [this, &genlib_path](const std::string& verilog,
                                            const std::string& aig) {
    impl_->Restart();
    const std::string script = "read_library " + Quote(genlib_path) +
                               "; read -m " + Quote(verilog) +
                               "; strash; write_aiger " + Quote(aig);
    return impl_->Execute(script, "netlist conversion").ok() &&
           std::filesystem::exists(aig);
  };
  if (!convert(first_verilog, first_aig) ||
      !convert(second_verilog, second_aig)) {
    std::error_code ec;
    std::filesystem::remove(first_aig, ec);
    std::filesystem::remove(second_aig, ec);
    return EquivalenceVerdict::kUnknown;
  }
  std::string captured;
  {
    OutputCapture capture;
    const std::string script = "cec " + Quote(first_aig) + " " +
                               Quote(second_aig);
    // The command's own return value cannot carry the verdict
    // (see the banner); it is deliberately ignored here and the
    // captured prover output below decides.
    (void)impl_->Execute(script, "equivalence proof");
    captured = capture.Finish();
  }
  {
    std::error_code ec;
    std::filesystem::remove(first_aig, ec);
    std::filesystem::remove(second_aig, ec);
  }
  // The prover's conclusions, in its own words at this pin:
  // "Networks are equivalent ..." (fraiging adds "after
  // structural hashing" before the period) and "Networks are NOT
  // EQUIVALENT.". The negative is checked first; the positive is
  // matched without its trailing period for the reason above.
  if (captured.find("Networks are NOT EQUIVALENT") != std::string::npos) {
    return EquivalenceVerdict::kNotEquivalent;
  }
  if (captured.find("Networks are equivalent") != std::string::npos) {
    return EquivalenceVerdict::kEquivalent;
  }
  return EquivalenceVerdict::kUnknown;
}

}  // namespace eos::synth
