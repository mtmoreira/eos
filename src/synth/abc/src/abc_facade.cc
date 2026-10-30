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

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

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
