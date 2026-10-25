// checkpoint: saves and restores design-data checkpoints (L2).
#include "eos/db/checkpoint.h"

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

namespace eos::db {
namespace {

using foundation::ErrorCode;

Status IoError(const std::string& what) {
  return Status::Error(ErrorCode::kIoError, what);
}
Status BadFormat(const std::string& what) {
  return Status::Error(ErrorCode::kInvalidArgument, what);
}

// ---- checksum -------------------------------------------------------

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

// ---- string escaping ------------------------------------------------
// Tokens are space separated, so a stored string may never contain a
// raw space. Bytes outside [A-Za-z0-9._/+:\[\]-] are written %XX; the
// empty string is written as a bare "%".

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
      return BadFormat("truncated escape in checkpoint string");
    }
    const int hi = HexDigit(token[i + 1]);
    const int lo = HexDigit(token[i + 2]);
    if (hi < 0 || lo < 0) {
      return BadFormat("bad escape in checkpoint string");
    }
    out.push_back(static_cast<char>(hi * 16 + lo));
    i += 2;
  }
  return out;
}

// ---- scalar parsing -------------------------------------------------

template <typename T>
bool ParseNum(const std::string& token, T& out, int base = 10) {
  const char* first = token.data();
  const char* last = first + token.size();
  const auto result = std::from_chars(first, last, out, base);
  return result.ec == std::errc() && result.ptr == last;
}

std::vector<std::string> Split(const std::string& line) {
  std::vector<std::string> tokens;
  std::size_t pos = 0;
  while (pos < line.size()) {
    const std::size_t space = line.find(' ', pos);
    if (space == std::string::npos) {
      tokens.push_back(line.substr(pos));
      break;
    }
    tokens.push_back(line.substr(pos, space - pos));
    pos = space + 1;
  }
  return tokens;
}

// ---- serialization --------------------------------------------------

void AppendDesign(std::string& out, const DesignState& d) {
  out += "section design\n";
  out += "name " + Escape(d.name) + "\n";
  out += "id " + std::to_string(d.id.raw()) + "\n";
  for (std::uint32_t k = 0; k < d.allocator_next.size(); ++k) {
    out += "alloc " + std::to_string(k) + " " +
           std::to_string(d.allocator_next[k]) + "\n";
  }
  for (const CellRecord& r : d.cells) {
    out += "cell " + std::to_string(r.id.raw()) + (r.is_macro ? " 1 " : " 0 ") +
           Escape(r.name) + "\n";
  }
  for (const InstanceRecord& r : d.instances) {
    out += "instance " + std::to_string(r.id.raw()) + " " +
           std::to_string(r.cell.raw()) + " " + Escape(r.name) + "\n";
  }
  for (const NetRecord& r : d.nets) {
    out += "net " + std::to_string(r.id.raw()) + " " + Escape(r.name) + "\n";
  }
  for (const TermRecord& r : d.terms) {
    out += "term " + std::to_string(r.id.raw()) + " " +
           std::to_string(r.port.raw()) + "\n";
  }
  for (const PortRecord& r : d.ports) {
    out += "port " + std::to_string(r.id.raw()) + " " +
           std::to_string(static_cast<std::int32_t>(r.direction)) + " " +
           std::to_string(r.term.raw()) + " " + Escape(r.name) + "\n";
  }
  for (const PinRecord& r : d.pins) {
    out += "pin " + std::to_string(r.id.raw()) + " " +
           std::to_string(r.instance.raw()) + " " +
           std::to_string(r.net.raw()) + " " + Escape(r.pin_name) + "\n";
  }
}

void AppendScenarios(std::string& out, const ScenarioState& s) {
  out += "section scenarios\n";
  out += "next " + std::to_string(s.next_id) + "\n";
  for (const Scenario& r : s.scenarios) {
    out += "scenario " + std::to_string(r.id) + (r.active ? " 1 " : " 0 ") +
           std::to_string(static_cast<std::int32_t>(r.parasitics)) + " " +
           Escape(r.name) + " " + Escape(r.mode) + " " + Escape(r.corner) +
           "\n";
  }
}

void AppendConstraints(std::string& out, const ConstraintState& c) {
  out += "section constraints\n";
  out += "next " + std::to_string(c.next_id) + "\n";
  for (const Constraint& r : c.constraints) {
    out += "constraint " + std::to_string(r.id) + " " +
           std::to_string(static_cast<std::int32_t>(r.kind)) + " " +
           std::to_string(static_cast<std::int32_t>(r.status)) + " " +
           std::to_string(r.bound_object.raw()) + " " + Escape(r.name) + " " +
           Escape(r.target_spec) + "\n";
  }
}

std::string BuildBody(const CheckpointData& data) {
  std::string out = "EOSCKPT " + std::to_string(Checkpoint::kFormatVersion) + "\n";
  AppendDesign(out, data.design);
  AppendScenarios(out, data.scenarios);
  AppendConstraints(out, data.constraints);
  return out;
}

// Validates module-internal consistency and returns the built design
// for cross-checks. Shared by Save and Load.
Result<Design> ValidateModules(const CheckpointData& data) {
  const Result<Design> design = Design::FromState(data.design);
  if (!design.ok()) {
    return design.status();
  }
  const Result<ScenarioSet> scenarios = ScenarioSet::FromState(data.scenarios);
  if (!scenarios.ok()) {
    return scenarios.status();
  }
  const Result<ConstraintSet> constraints =
      ConstraintSet::FromState(data.constraints);
  if (!constraints.ok()) {
    return constraints.status();
  }
  return design;
}

// ---- parsing ----------------------------------------------------------

struct LineReader {
  std::vector<std::string> lines;
  std::size_t pos = 0;
  const std::string* Next() {
    return pos < lines.size() ? &lines[pos++] : nullptr;
  }
  bool AtEnd() const { return pos >= lines.size(); }
};

Status ParseDesign(LineReader& reader, CheckpointData& data) {
  bool have_name = false;
  bool have_id = false;
  while (const std::string* line = reader.Next()) {
    if (line->rfind("section ", 0) == 0) {
      reader.pos -= 1;
      break;
    }
    const std::vector<std::string> t = Split(*line);
    if (t.empty()) {
      return BadFormat("empty line in design section");
    }
    if (t[0] == "name" && t.size() == 2) {
      const Result<std::string> v = Unescape(t[1]);
      if (!v.ok()) return v.status();
      data.design.name = v.value();
      have_name = true;
    } else if (t[0] == "id" && t.size() == 2) {
      std::uint32_t raw = 0;
      if (!ParseNum(t[1], raw)) return BadFormat("bad design id");
      data.design.id = ObjectId(raw);
      have_id = true;
    } else if (t[0] == "alloc" && t.size() == 3) {
      std::uint32_t kind = 0;
      std::uint32_t next = 0;
      if (!ParseNum(t[1], kind) || !ParseNum(t[2], next) ||
          kind >= data.design.allocator_next.size()) {
        return BadFormat("bad alloc line");
      }
      data.design.allocator_next[kind] = next;
    } else if (t[0] == "cell" && t.size() == 4) {
      CellRecord r;
      std::uint32_t raw = 0;
      std::uint32_t macro = 0;
      if (!ParseNum(t[1], raw) || !ParseNum(t[2], macro) || macro > 1) {
        return BadFormat("bad cell line");
      }
      const Result<std::string> v = Unescape(t[3]);
      if (!v.ok()) return v.status();
      r.id = ObjectId(raw);
      r.is_macro = macro == 1;
      r.name = v.value();
      data.design.cells.push_back(std::move(r));
    } else if (t[0] == "instance" && t.size() == 4) {
      InstanceRecord r;
      std::uint32_t raw = 0;
      std::uint32_t cell = 0;
      if (!ParseNum(t[1], raw) || !ParseNum(t[2], cell)) {
        return BadFormat("bad instance line");
      }
      const Result<std::string> v = Unescape(t[3]);
      if (!v.ok()) return v.status();
      r.id = ObjectId(raw);
      r.cell = ObjectId(cell);
      r.name = v.value();
      data.design.instances.push_back(std::move(r));
    } else if (t[0] == "net" && t.size() == 3) {
      NetRecord r;
      std::uint32_t raw = 0;
      if (!ParseNum(t[1], raw)) return BadFormat("bad net line");
      const Result<std::string> v = Unescape(t[2]);
      if (!v.ok()) return v.status();
      r.id = ObjectId(raw);
      r.name = v.value();
      data.design.nets.push_back(std::move(r));
    } else if (t[0] == "term" && t.size() == 3) {
      TermRecord r;
      std::uint32_t raw = 0;
      std::uint32_t port = 0;
      if (!ParseNum(t[1], raw) || !ParseNum(t[2], port)) {
        return BadFormat("bad term line");
      }
      r.id = ObjectId(raw);
      r.port = ObjectId(port);
      data.design.terms.push_back(r);
    } else if (t[0] == "port" && t.size() == 5) {
      PortRecord r;
      std::uint32_t raw = 0;
      std::int32_t dir = 0;
      std::uint32_t term = 0;
      if (!ParseNum(t[1], raw) || !ParseNum(t[2], dir) || dir < 0 || dir > 2 ||
          !ParseNum(t[3], term)) {
        return BadFormat("bad port line");
      }
      const Result<std::string> v = Unescape(t[4]);
      if (!v.ok()) return v.status();
      r.id = ObjectId(raw);
      r.direction = static_cast<Direction>(dir);
      r.term = ObjectId(term);
      r.name = v.value();
      data.design.ports.push_back(std::move(r));
    } else if (t[0] == "pin" && t.size() == 5) {
      PinRecord r;
      std::uint32_t raw = 0;
      std::uint32_t inst = 0;
      std::uint32_t net = 0;
      if (!ParseNum(t[1], raw) || !ParseNum(t[2], inst) ||
          !ParseNum(t[3], net)) {
        return BadFormat("bad pin line");
      }
      const Result<std::string> v = Unescape(t[4]);
      if (!v.ok()) return v.status();
      r.id = ObjectId(raw);
      r.instance = ObjectId(inst);
      r.net = ObjectId(net);
      r.pin_name = v.value();
      data.design.pins.push_back(std::move(r));
    } else {
      return BadFormat("unrecognized line in design section: " + t[0]);
    }
  }
  if (!have_name || !have_id) {
    return BadFormat("design section missing name or id");
  }
  // Pin display names are derived, never stored: rebuild them so
  // FromState's consistency check compares like with like.
  for (PinRecord& pin : data.design.pins) {
    for (const InstanceRecord& inst : data.design.instances) {
      if (inst.id == pin.instance) {
        pin.name = inst.name + "/" + pin.pin_name;
        break;
      }
    }
  }
  return Status::Ok();
}

Status ParseScenarios(LineReader& reader, CheckpointData& data) {
  bool have_next = false;
  while (const std::string* line = reader.Next()) {
    if (line->rfind("section ", 0) == 0) {
      reader.pos -= 1;
      break;
    }
    const std::vector<std::string> t = Split(*line);
    if (t.empty()) {
      return BadFormat("empty line in scenarios section");
    }
    if (t[0] == "next" && t.size() == 2) {
      if (!ParseNum(t[1], data.scenarios.next_id)) {
        return BadFormat("bad scenarios next line");
      }
      have_next = true;
    } else if (t[0] == "scenario" && t.size() == 7) {
      Scenario r;
      std::uint32_t active = 0;
      std::int32_t parasitics = 0;
      if (!ParseNum(t[1], r.id) || !ParseNum(t[2], active) || active > 1 ||
          !ParseNum(t[3], parasitics) || parasitics < 0 || parasitics > 1) {
        return BadFormat("bad scenario line");
      }
      const Result<std::string> name = Unescape(t[4]);
      const Result<std::string> mode = Unescape(t[5]);
      const Result<std::string> corner = Unescape(t[6]);
      if (!name.ok() || !mode.ok() || !corner.ok()) {
        return BadFormat("bad scenario string field");
      }
      r.active = active == 1;
      r.parasitics = static_cast<ParasiticsLevel>(parasitics);
      r.name = name.value();
      r.mode = mode.value();
      r.corner = corner.value();
      data.scenarios.scenarios.push_back(std::move(r));
    } else {
      return BadFormat("unrecognized line in scenarios section: " + t[0]);
    }
  }
  if (!have_next) {
    return BadFormat("scenarios section missing next line");
  }
  return Status::Ok();
}

Status ParseConstraints(LineReader& reader, CheckpointData& data) {
  bool have_next = false;
  while (const std::string* line = reader.Next()) {
    const std::vector<std::string> t = Split(*line);
    if (t.empty()) {
      return BadFormat("empty line in constraints section");
    }
    if (t[0] == "next" && t.size() == 2) {
      if (!ParseNum(t[1], data.constraints.next_id)) {
        return BadFormat("bad constraints next line");
      }
      have_next = true;
    } else if (t[0] == "constraint" && t.size() == 7) {
      Constraint r;
      std::int32_t kind = 0;
      std::int32_t status = 0;
      std::uint32_t bound = 0;
      if (!ParseNum(t[1], r.id) || !ParseNum(t[2], kind) || kind < 0 ||
          kind > 7 || !ParseNum(t[3], status) || status < 0 || status > 2 ||
          !ParseNum(t[4], bound)) {
        return BadFormat("bad constraint line");
      }
      const Result<std::string> name = Unescape(t[5]);
      const Result<std::string> spec = Unescape(t[6]);
      if (!name.ok() || !spec.ok()) {
        return BadFormat("bad constraint string field");
      }
      r.kind = static_cast<ConstraintKind>(kind);
      r.status = static_cast<BindingStatus>(status);
      r.bound_object = ObjectId(bound);
      r.name = name.value();
      r.target_spec = spec.value();
      data.constraints.constraints.push_back(std::move(r));
    } else {
      return BadFormat("unrecognized line in constraints section: " + t[0]);
    }
  }
  if (!have_next) {
    return BadFormat("constraints section missing next line");
  }
  return Status::Ok();
}

}  // namespace

Status Checkpoint::Save(const std::string& path, const CheckpointData& data) {
  const Result<Design> design = ValidateModules(data);
  if (!design.ok()) {
    return design.status();
  }
  for (const Constraint& constraint : data.constraints.constraints) {
    if (constraint.status == BindingStatus::kBound &&
        !design.value().Exists(constraint.bound_object)) {
      return BadFormat(
          "checkpoint save refused: constraint bound to an object the "
          "design does not contain");
    }
  }
  const std::string body = BuildBody(data);
  const std::string content =
      body + "end " + Hex64(Fnv1a(body.data(), body.size())) + "\n";
  const std::string tmp_path = path + ".tmp";
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return IoError("cannot open checkpoint temp file: " + tmp_path);
    }
    out << content;
    out.flush();
    if (!out) {
      std::remove(tmp_path.c_str());
      return IoError("failed writing checkpoint temp file: " + tmp_path);
    }
  }
  if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
    std::remove(tmp_path.c_str());
    return IoError("cannot move checkpoint into place: " + path);
  }
  return Status::Ok();
}

Result<CheckpointData> Checkpoint::Load(const std::string& path,
                                        RestoreInfo* info) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return IoError("cannot open checkpoint file: " + path);
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  if (in.bad()) {
    return IoError("failed reading checkpoint file: " + path);
  }
  const std::string content = buffer.str();

  // The checksum line is the last line; the checksum covers every byte
  // before it, including the newline that precedes it.
  const std::size_t end_pos = content.rfind("\nend ");
  if (end_pos == std::string::npos) {
    return BadFormat("checkpoint has no checksum line");
  }
  const std::string end_line = content.substr(end_pos + 1);
  const std::vector<std::string> end_tokens = Split(
      end_line.substr(0, end_line.find('\n')));
  std::uint64_t stored = 0;
  if (end_tokens.size() != 2 || end_tokens[0] != "end" ||
      !ParseNum(end_tokens[1], stored, 16)) {
    return BadFormat("malformed checkpoint checksum line");
  }
  const std::string body = content.substr(0, end_pos + 1);
  if (Fnv1a(body.data(), body.size()) != stored) {
    return BadFormat("checkpoint checksum mismatch: file is corrupt");
  }

  LineReader reader;
  std::size_t pos = 0;
  while (pos < body.size()) {
    const std::size_t nl = body.find('\n', pos);
    if (nl == std::string::npos) {
      reader.lines.push_back(body.substr(pos));
      break;
    }
    reader.lines.push_back(body.substr(pos, nl - pos));
    pos = nl + 1;
  }
  if (reader.lines.empty()) {
    return BadFormat("empty checkpoint");
  }
  const std::string& magic = reader.lines[0];
  const std::string prefix = "EOSCKPT ";
  if (magic.rfind(prefix, 0) != 0) {
    return BadFormat("not an EOS checkpoint file");
  }
  int version = 0;
  if (!ParseNum(magic.substr(prefix.size()), version)) {
    return BadFormat("malformed checkpoint version line");
  }
  if (version != kFormatVersion) {
    return Status::Error(ErrorCode::kUnsupported,
                         "unsupported checkpoint format version");
  }
  reader.pos = 1;

  CheckpointData data;
  const std::string* section = reader.Next();
  if (section == nullptr || *section != "section design") {
    return BadFormat("checkpoint missing design section");
  }
  Status status = ParseDesign(reader, data);
  if (!status.ok()) {
    return status;
  }
  section = reader.Next();
  if (section == nullptr || *section != "section scenarios") {
    return BadFormat("checkpoint missing scenarios section");
  }
  status = ParseScenarios(reader, data);
  if (!status.ok()) {
    return status;
  }
  section = reader.Next();
  if (section == nullptr || *section != "section constraints") {
    return BadFormat("checkpoint missing constraints section");
  }
  status = ParseConstraints(reader, data);
  if (!status.ok()) {
    return status;
  }
  if (!reader.AtEnd()) {
    return BadFormat("trailing content after constraints section");
  }

  const Result<Design> design = ValidateModules(data);
  if (!design.ok()) {
    return design.status();
  }
  // Cross-check: a constraint bound to an object the design does not
  // contain is marked unresolved and counted, never silently kept.
  std::size_t flipped = 0;
  for (Constraint& constraint : data.constraints.constraints) {
    if (constraint.status == BindingStatus::kBound &&
        !design.value().Exists(constraint.bound_object)) {
      constraint.status = BindingStatus::kUnresolved;
      constraint.bound_object = ObjectId();
      flipped += 1;
    }
  }
  if (info != nullptr) {
    info->constraints_marked_unresolved = flipped;
  }
  return data;
}

}  // namespace eos::db
