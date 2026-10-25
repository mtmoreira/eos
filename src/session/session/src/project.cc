// session: the Project object and the project file (L5 session).
#include "eos/session/project.h"

#include <charconv>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace eos::session {
namespace {

using foundation::ErrorCode;

Status IoError(const std::string& what) {
  return Status::Error(ErrorCode::kIoError, what);
}
Status BadFormat(const std::string& what) {
  return Status::Error(ErrorCode::kInvalidArgument, what);
}

// ------------------------------------------------------------------
// The report bindings text, format version 1: a line-oriented text
// in the same style as the ledger snapshot text, with the same
// percent-escaping and a trailing FNV-1a 64 checksum line covering
// every preceding byte. One "binding" line per binding, followed by
// one "scenario" line per scenario the binding snapshot. Parsing is
// strict: bad magic, unsupported version, checksum mismatch,
// truncation, a malformed record, a duplicate binding name or a
// duplicate artifact id are all errors. The text is syntax only:
// whether a binding's artifact exists is judged against the
// restored ledger by LoadProject, which drops and counts the rest.
// ------------------------------------------------------------------

std::uint64_t Fnv1a(const char* data, std::size_t size) {
  std::uint64_t hash = 14695981039346656037ull;
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= static_cast<unsigned char>(data[i]);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string Hex64(std::uint64_t value) {
  char buf[17];
  std::snprintf(buf, sizeof(buf), "%016llx",
                static_cast<unsigned long long>(value));
  return std::string(buf, 16);
}

bool RawByte(unsigned char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '/' ||
         c == '+' || c == ':' || c == '[' || c == ']' || c == '-';
}

std::string Escape(const std::string& in) {
  if (in.empty()) {
    return "%";
  }
  std::string out;
  for (const unsigned char c : in) {
    if (RawByte(c)) {
      out.push_back(static_cast<char>(c));
    } else {
      char buf[4];
      std::snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

int HexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

Result<std::string> Unescape(const std::string& token) {
  if (token == "%") {
    return std::string();
  }
  std::string out;
  for (std::size_t i = 0; i < token.size(); ++i) {
    if (token[i] != '%') {
      out.push_back(token[i]);
      continue;
    }
    if (i + 2 > token.size() - 1) {
      return BadFormat("truncated escape in bindings string");
    }
    const int hi = HexDigit(token[i + 1]);
    const int lo = HexDigit(token[i + 2]);
    if (hi < 0 || lo < 0) {
      return BadFormat("bad escape in bindings string");
    }
    out.push_back(static_cast<char>(hi * 16 + lo));
    i += 2;
  }
  return out;
}

template <typename T>
bool ParseNum(const std::string& token, T& out) {
  const char* first = token.data();
  const char* last = first + token.size();
  const auto result = std::from_chars(first, last, out);
  return result.ec == std::errc() && result.ptr == last;
}

// Splits a line on single spaces. Escaped strings never contain a
// raw space, so tokens are exact.
std::vector<std::string> SplitTokens(const std::string& line) {
  std::vector<std::string> tokens;
  std::size_t pos = 0;
  while (true) {
    const std::size_t sp = line.find(' ', pos);
    if (sp == std::string::npos) {
      tokens.push_back(line.substr(pos));
      return tokens;
    }
    tokens.push_back(line.substr(pos, sp - pos));
    pos = sp + 1;
  }
}

std::string BindingsToText(const std::vector<ReportBinding>& bindings) {
  std::string out = "EOSBINDINGS 1\n";
  for (const ReportBinding& binding : bindings) {
    out += "binding " + std::to_string(binding.artifact_id) + " " +
           std::to_string(binding.bound_revision) + " " +
           (binding.parasitics.has_value()
                ? std::to_string(
                      static_cast<std::int32_t>(*binding.parasitics))
                : std::string("-1")) +
           " " + Escape(binding.name) + " " +
           Escape(binding.timing_model) + " " +
           Escape(binding.tool_build) + "\n";
    for (const ScenarioSnapshot& scenario : binding.scenarios) {
      out += "scenario " + std::to_string(scenario.id) + " " +
             std::to_string(
                 static_cast<std::int32_t>(scenario.parasitics)) +
             " " + Escape(scenario.name) + " " + Escape(scenario.mode) +
             " " + Escape(scenario.corner) + "\n";
    }
  }
  return out + "end " + Hex64(Fnv1a(out.data(), out.size())) + "\n";
}

Result<std::vector<ReportBinding>> BindingsFromText(
    const std::string& text) {
  // The checksum line is the last line; it covers every byte before
  // it, exactly as the ledger text does.
  if (text.empty() || text.back() != '\n') {
    return BadFormat("bindings text is truncated");
  }
  const std::size_t end_pos = text.rfind("\nend ");
  if (end_pos == std::string::npos) {
    return BadFormat("bindings text has no checksum line");
  }
  const std::string body = text.substr(0, end_pos + 1);
  const std::string tail =
      text.substr(end_pos + 1, text.size() - end_pos - 2);
  const std::vector<std::string> tail_tokens = SplitTokens(tail);
  if (tail_tokens.size() != 2 || tail_tokens[0] != "end" ||
      tail_tokens[1].size() != 16) {
    return BadFormat("malformed bindings checksum line");
  }
  std::uint64_t stored = 0;
  for (const char c : tail_tokens[1]) {
    const int digit = HexDigit(c);
    if (digit < 0) {
      return BadFormat("malformed bindings checksum line");
    }
    stored = stored * 16 + static_cast<std::uint64_t>(digit);
  }
  if (Fnv1a(body.data(), body.size()) != stored) {
    return BadFormat("bindings checksum mismatch: text is corrupt");
  }

  std::vector<ReportBinding> bindings;
  std::istringstream lines(body);
  std::string line;
  if (!std::getline(lines, line) || line != "EOSBINDINGS 1") {
    if (line.rfind("EOSBINDINGS ", 0) == 0) {
      return Status::Error(ErrorCode::kUnsupported,
                           "unsupported bindings format version");
    }
    return BadFormat("not a bindings text");
  }
  while (std::getline(lines, line)) {
    const std::vector<std::string> t = SplitTokens(line);
    if (t.empty()) {
      return BadFormat("empty line in bindings text");
    }
    if (t[0] == "binding") {
      if (t.size() != 7) {
        return BadFormat("malformed binding line in bindings text");
      }
      ReportBinding binding;
      std::int32_t parasitics = 0;
      if (!ParseNum(t[1], binding.artifact_id) ||
          !ParseNum(t[2], binding.bound_revision) ||
          !ParseNum(t[3], parasitics)) {
        return BadFormat("malformed binding line in bindings text");
      }
      if (parasitics == -1) {
        binding.parasitics.reset();
      } else if (parasitics == 0 || parasitics == 1) {
        binding.parasitics =
            static_cast<db::ParasiticsLevel>(parasitics);
      } else {
        return BadFormat("bad parasitics level in bindings text");
      }
      const Result<std::string> name = Unescape(t[4]);
      const Result<std::string> model = Unescape(t[5]);
      const Result<std::string> build = Unescape(t[6]);
      if (!name.ok() || !model.ok() || !build.ok()) {
        return BadFormat("bad string in bindings text");
      }
      binding.name = name.value();
      binding.timing_model = model.value();
      binding.tool_build = build.value();
      if (binding.name.empty()) {
        return BadFormat("binding with an empty name in bindings text");
      }
      for (const ReportBinding& existing : bindings) {
        if (existing.name == binding.name) {
          return BadFormat("duplicate binding name in bindings text: " +
                           binding.name);
        }
        if (existing.artifact_id == binding.artifact_id) {
          return BadFormat("duplicate binding artifact in bindings text");
        }
      }
      bindings.push_back(std::move(binding));
    } else if (t[0] == "scenario") {
      if (bindings.empty()) {
        return BadFormat("scenario line before any binding line");
      }
      if (t.size() != 6) {
        return BadFormat("malformed scenario line in bindings text");
      }
      ScenarioSnapshot scenario;
      std::int32_t parasitics = 0;
      if (!ParseNum(t[1], scenario.id) || !ParseNum(t[2], parasitics) ||
          (parasitics != 0 && parasitics != 1)) {
        return BadFormat("malformed scenario line in bindings text");
      }
      scenario.parasitics =
          static_cast<db::ParasiticsLevel>(parasitics);
      const Result<std::string> name = Unescape(t[3]);
      const Result<std::string> mode = Unescape(t[4]);
      const Result<std::string> corner = Unescape(t[5]);
      if (!name.ok() || !mode.ok() || !corner.ok()) {
        return BadFormat("bad string in bindings text");
      }
      scenario.name = name.value();
      scenario.mode = mode.value();
      scenario.corner = corner.value();
      bindings.back().scenarios.push_back(std::move(scenario));
    } else {
      return BadFormat("unknown line in bindings text: " + t[0]);
    }
  }
  return bindings;
}

// ------------------------------------------------------------------
// The project file envelope: a magic line, then length-prefixed
// sections in order (checkpoint, ledger, reports). The reports
// section is absent in project files written before bindings became
// project state; those load with an empty binding set.
// ------------------------------------------------------------------

struct ProjectSections {
  std::string checkpoint_text;
  std::string ledger_text;
  std::optional<std::string> reports_text;
};

std::string BuildProjectText(const ProjectSections& sections) {
  std::string out = "EOSPROJ 1\n";
  out += "checkpoint " + std::to_string(sections.checkpoint_text.size()) +
         "\n";
  out += sections.checkpoint_text;
  out += "ledger " + std::to_string(sections.ledger_text.size()) + "\n";
  out += sections.ledger_text;
  if (sections.reports_text.has_value()) {
    out += "reports " + std::to_string(sections.reports_text->size()) +
           "\n";
    out += *sections.reports_text;
  }
  return out;
}

// Splits a project file back into its embedded texts. Purely
// syntactic: the embedded texts validate themselves afterwards.
Result<ProjectSections> SplitProjectText(const std::string& content) {
  std::size_t pos = 0;
  auto next_line = [&content, &pos]() -> Result<std::string> {
    const std::size_t nl = content.find('\n', pos);
    if (nl == std::string::npos) {
      return BadFormat("project file is truncated");
    }
    std::string line = content.substr(pos, nl - pos);
    pos = nl + 1;
    return line;
  };
  const Result<std::string> magic = next_line();
  if (!magic.ok()) {
    return magic.status();
  }
  if (magic.value() != "EOSPROJ 1") {
    if (magic.value().rfind("EOSPROJ ", 0) == 0) {
      return Status::Error(ErrorCode::kUnsupported,
                           "unsupported project file format version");
    }
    return BadFormat("not an EOS project file");
  }
  auto section = [&next_line, &content, &pos](
                     const char* name) -> Result<std::string> {
    const Result<std::string> header = next_line();
    if (!header.ok()) {
      return header.status();
    }
    const std::string prefix = std::string(name) + " ";
    if (header.value().rfind(prefix, 0) != 0) {
      return BadFormat(std::string("project file missing ") + name +
                       " section");
    }
    std::size_t size = 0;
    try {
      std::size_t used = 0;
      size = std::stoull(header.value().substr(prefix.size()), &used);
      if (used != header.value().size() - prefix.size()) {
        return BadFormat("malformed project section size");
      }
    } catch (...) {
      return BadFormat("malformed project section size");
    }
    if (size > content.size() - pos) {
      return BadFormat("project file is truncated in a section body");
    }
    std::string body = content.substr(pos, size);
    pos += size;
    return body;
  };
  ProjectSections sections;
  const Result<std::string> checkpoint_text = section("checkpoint");
  if (!checkpoint_text.ok()) {
    return checkpoint_text.status();
  }
  sections.checkpoint_text = checkpoint_text.value();
  const Result<std::string> ledger_text = section("ledger");
  if (!ledger_text.ok()) {
    return ledger_text.status();
  }
  sections.ledger_text = ledger_text.value();
  if (pos != content.size()) {
    // Anything after the ledger section must be the reports section,
    // and it must run exactly to the end of the file.
    const Result<std::string> reports_text = section("reports");
    if (!reports_text.ok()) {
      return reports_text.status();
    }
    sections.reports_text = reports_text.value();
    if (pos != content.size()) {
      return BadFormat("trailing content after the project file sections");
    }
  }
  return sections;
}

}  // namespace

void Project::Reset() {
  design.reset();
  scenarios = db::ScenarioSet();
  constraints = db::ConstraintSet();
  ledger = Ledger();
  report_bindings.clear();
}

Status Project::SaveProject(const std::string& path) const {
  if (!design.has_value()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "cannot save a project with no design open");
  }
  db::CheckpointData data;
  data.design = design->ExportState();
  data.scenarios = scenarios.ExportState();
  data.constraints = constraints.ExportState();
  const Result<std::string> checkpoint_text = db::Checkpoint::Serialize(data);
  if (!checkpoint_text.ok()) {
    return checkpoint_text.status();
  }
  const Result<std::string> ledger_text = SnapshotToText(ledger.Snapshot());
  if (!ledger_text.ok()) {
    return ledger_text.status();
  }
  ProjectSections sections;
  sections.checkpoint_text = checkpoint_text.value();
  sections.ledger_text = ledger_text.value();
  sections.reports_text = BindingsToText(report_bindings);
  const std::string content = BuildProjectText(sections);
  const std::string tmp_path = path + ".tmp";
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return IoError("cannot open project temp file: " + tmp_path);
    }
    out << content;
    out.flush();
    if (!out) {
      std::remove(tmp_path.c_str());
      return IoError("failed writing project temp file: " + tmp_path);
    }
  }
  if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
    std::remove(tmp_path.c_str());
    return IoError("cannot move project file into place: " + path);
  }
  return Status::Ok();
}

Result<ProjectRestoreInfo> Project::LoadProject(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return IoError("cannot open project file: " + path);
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  if (in.bad()) {
    return IoError("failed reading project file: " + path);
  }
  const Result<ProjectSections> sections = SplitProjectText(buffer.str());
  if (!sections.ok()) {
    return sections.status();
  }
  // Parse and validate everything before any live state changes.
  ProjectRestoreInfo info;
  const Result<db::CheckpointData> data =
      db::Checkpoint::FromString(sections.value().checkpoint_text,
                                 &info.checkpoint);
  if (!data.ok()) {
    return data.status();
  }
  const Result<LedgerSnapshot> snapshot =
      SnapshotFromText(sections.value().ledger_text);
  if (!snapshot.ok()) {
    return snapshot.status();
  }
  std::vector<ReportBinding> parsed_bindings;
  if (sections.value().reports_text.has_value()) {
    const Result<std::vector<ReportBinding>> parsed =
        BindingsFromText(*sections.value().reports_text);
    if (!parsed.ok()) {
      return parsed.status();
    }
    parsed_bindings = parsed.value();
  }
  Result<db::Design> new_design = db::Design::FromState(data.value().design);
  if (!new_design.ok()) {
    return new_design.status();
  }
  Result<db::ScenarioSet> new_scenarios =
      db::ScenarioSet::FromState(data.value().scenarios);
  if (!new_scenarios.ok()) {
    return new_scenarios.status();
  }
  Result<db::ConstraintSet> new_constraints =
      db::ConstraintSet::FromState(data.value().constraints);
  if (!new_constraints.ok()) {
    return new_constraints.status();
  }
  Ledger new_ledger;
  const Status restored = new_ledger.Restore(snapshot.value());
  if (!restored.ok()) {
    return restored;
  }
  // Link the bindings to the restored ledger: a binding survives
  // only when the ledger holds its artifact, as a report. Every
  // other binding is dropped and counted, never silently kept.
  std::vector<ReportBinding> new_bindings;
  for (ReportBinding& binding : parsed_bindings) {
    const ArtifactRecord* record = new_ledger.FindArtifact(binding.artifact_id);
    if (record != nullptr && record->kind == ArtifactKind::kReport) {
      new_bindings.push_back(std::move(binding));
    }
  }
  info.bindings_restored = new_bindings.size();
  info.bindings_dropped = parsed_bindings.size() - new_bindings.size();
  // Everything validated: adopt in one step.
  design = std::move(new_design).value();
  scenarios = std::move(new_scenarios).value();
  constraints = std::move(new_constraints).value();
  ledger = std::move(new_ledger);
  report_bindings = std::move(new_bindings);
  return info;
}

}  // namespace eos::session
