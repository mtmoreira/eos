// session: the Project object and the project file (L5 session).
#include "eos/session/project.h"

#include <cstdio>
#include <fstream>
#include <sstream>
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

// Builds the whole project file text from its two embedded texts.
// Each section is a header line ("checkpoint <bytes>" /
// "ledger <bytes>") followed by exactly that many bytes of text, so
// embedding needs no escaping and a truncated file is detectable.
Result<std::string> BuildProjectText(const std::string& checkpoint_text,
                                     const std::string& ledger_text) {
  std::string out = "EOSPROJ 1\n";
  out += "checkpoint " + std::to_string(checkpoint_text.size()) + "\n";
  out += checkpoint_text;
  out += "ledger " + std::to_string(ledger_text.size()) + "\n";
  out += ledger_text;
  return out;
}

// Splits a project file back into its two embedded texts. Purely
// syntactic: the embedded texts validate themselves afterwards.
Result<std::pair<std::string, std::string>> SplitProjectText(
    const std::string& content) {
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
  const Result<std::string> checkpoint_text = section("checkpoint");
  if (!checkpoint_text.ok()) {
    return checkpoint_text.status();
  }
  const Result<std::string> ledger_text = section("ledger");
  if (!ledger_text.ok()) {
    return ledger_text.status();
  }
  if (pos != content.size()) {
    return BadFormat("trailing content after the project file sections");
  }
  return std::make_pair(checkpoint_text.value(), ledger_text.value());
}

}  // namespace

void Project::Reset() {
  design.reset();
  scenarios = db::ScenarioSet();
  constraints = db::ConstraintSet();
  ledger = Ledger();
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
  const Result<std::string> content =
      BuildProjectText(checkpoint_text.value(), ledger_text.value());
  if (!content.ok()) {
    return content.status();
  }
  const std::string tmp_path = path + ".tmp";
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return IoError("cannot open project temp file: " + tmp_path);
    }
    out << content.value();
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

Result<db::RestoreInfo> Project::LoadProject(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return IoError("cannot open project file: " + path);
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  if (in.bad()) {
    return IoError("failed reading project file: " + path);
  }
  const Result<std::pair<std::string, std::string>> sections =
      SplitProjectText(buffer.str());
  if (!sections.ok()) {
    return sections.status();
  }
  // Parse and validate everything before any live state changes.
  db::RestoreInfo info;
  const Result<db::CheckpointData> data =
      db::Checkpoint::FromString(sections.value().first, &info);
  if (!data.ok()) {
    return data.status();
  }
  const Result<LedgerSnapshot> snapshot =
      SnapshotFromText(sections.value().second);
  if (!snapshot.ok()) {
    return snapshot.status();
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
  // Everything validated: adopt in one step.
  design = std::move(new_design).value();
  scenarios = std::move(new_scenarios).value();
  constraints = std::move(new_constraints).value();
  ledger = std::move(new_ledger);
  return info;
}

}  // namespace eos::session
