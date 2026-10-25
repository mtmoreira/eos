// ledger: the state ledger (L5 session).
#include "eos/session/ledger.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <system_error>
#include <utility>

namespace eos::session {

Status Ledger::BeginTransaction(const TransactionSpec& spec) {
  if (pending_.has_value()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "a transaction is already pending");
  }
  if (spec.input_revision != current_revision_) {
    return Status::Error(
        ErrorCode::kStaleRevision,
        "transaction input revision is not the current revision");
  }
  for (const ArtifactId id : spec.invalidates) {
    if (FindArtifact(id) == nullptr) {
      return Status::Error(ErrorCode::kNotFound,
                           "transaction invalidates an unknown artifact");
    }
  }
  pending_ = spec;
  return Status::Ok();
}

Result<Revision> Ledger::CommitPending(ProofStatus proof) {
  if (!pending_.has_value()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "no transaction is pending");
  }
  TransactionRecord record;
  record.id = next_txn_++;
  record.input_revision = pending_->input_revision;
  record.output_revision = current_revision_ + 1;
  record.rollback_checkpoint = pending_->input_revision;
  record.label = pending_->label;
  record.tool_build = pending_->tool_build;
  record.changed_objects = pending_->changed_objects;
  record.before_metrics = pending_->before_metrics;
  record.after_metrics = pending_->after_metrics;
  record.invalidated = pending_->invalidates;
  record.proof = proof;
  record.active = true;
  history_.push_back(std::move(record));
  current_revision_ += 1;
  pending_.reset();
  return current_revision_;
}

Status Ledger::AbortPending() {
  if (!pending_.has_value()) {
    return Status::Error(ErrorCode::kInvalidState,
                         "no transaction is pending");
  }
  pending_.reset();
  return Status::Ok();
}

ArtifactId Ledger::RegisterArtifact(ArtifactKind kind, std::string name,
                                    ProofStatus proof) {
  ArtifactRecord record;
  record.id = next_artifact_++;
  record.kind = kind;
  record.name = std::move(name);
  record.bound_revision = current_revision_;
  record.proof = proof;
  artifacts_.push_back(std::move(record));
  return artifacts_.back().id;
}

const ArtifactRecord* Ledger::FindArtifact(ArtifactId id) const {
  for (const ArtifactRecord& record : artifacts_) {
    if (record.id == id) {
      return &record;
    }
  }
  return nullptr;
}

bool Ledger::ArtifactValid(ArtifactId id) const {
  const ArtifactRecord* artifact = FindArtifact(id);
  if (artifact == nullptr || artifact->bound_revision > current_revision_) {
    return false;
  }
  for (const TransactionRecord& txn : history_) {
    if (!txn.active || txn.output_revision > current_revision_) {
      continue;
    }
    if (txn.input_revision < artifact->bound_revision) {
      continue;
    }
    if (std::find(txn.invalidated.begin(), txn.invalidated.end(), id) !=
        txn.invalidated.end()) {
      return false;
    }
  }
  return true;
}

bool Ledger::ArtifactCurrent(ArtifactId id) const {
  const ArtifactRecord* artifact = FindArtifact(id);
  return artifact != nullptr &&
         artifact->bound_revision == current_revision_ &&
         ArtifactValid(id);
}

Status Ledger::SetArtifactProof(ArtifactId id, ProofStatus proof) {
  for (ArtifactRecord& record : artifacts_) {
    if (record.id == id) {
      record.proof = proof;
      return Status::Ok();
    }
  }
  return Status::Error(ErrorCode::kNotFound, "unknown artifact");
}

Status Ledger::RollbackTo(Revision target) {
  if (pending_.has_value()) {
    return Status::Error(
        ErrorCode::kInvalidState,
        "cannot roll back while a transaction is pending");
  }
  if (target > current_revision_) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "rollback target is beyond the current revision");
  }
  for (TransactionRecord& txn : history_) {
    if (txn.output_revision > target) {
      txn.active = false;
    }
  }
  current_revision_ = target;
  return Status::Ok();
}

LedgerSnapshot Ledger::Snapshot() const {
  LedgerSnapshot snapshot;
  snapshot.current_revision = current_revision_;
  snapshot.next_txn = next_txn_;
  snapshot.next_artifact = next_artifact_;
  snapshot.pending = pending_;
  snapshot.history = history_;
  snapshot.artifacts = artifacts_;
  return snapshot;
}

Status Ledger::Restore(const LedgerSnapshot& snapshot) {
  auto reject = [](const char* what) {
    return Status::Error(ErrorCode::kInvalidArgument, what);
  };
  // History: strictly increasing ids, dense revision steps, the
  // rollback checkpoint is the input revision by construction.
  TxnId max_txn = 0;
  Revision max_output = 0;
  Revision last_active_output = 0;
  for (const TransactionRecord& txn : snapshot.history) {
    if (txn.id <= max_txn || txn.output_revision != txn.input_revision + 1 ||
        txn.rollback_checkpoint != txn.input_revision) {
      return reject("ledger snapshot: malformed transaction record");
    }
    max_txn = txn.id;
    if (txn.output_revision > max_output) {
      max_output = txn.output_revision;
    }
    if (txn.active) {
      last_active_output = txn.output_revision;
    }
  }
  if (snapshot.current_revision != last_active_output) {
    return reject(
        "ledger snapshot: current revision is not the last active output");
  }
  if (snapshot.next_txn <= max_txn) {
    return reject("ledger snapshot: txn counter would reuse an id");
  }
  // Artifacts: strictly increasing ids, bound at a revision the
  // history actually reached.
  ArtifactId max_artifact = 0;
  for (const ArtifactRecord& artifact : snapshot.artifacts) {
    if (artifact.id <= max_artifact ||
        artifact.bound_revision > max_output) {
      return reject("ledger snapshot: malformed artifact record");
    }
    max_artifact = artifact.id;
  }
  if (snapshot.next_artifact <= max_artifact) {
    return reject("ledger snapshot: artifact counter would reuse an id");
  }
  auto known = [&snapshot](ArtifactId id) {
    for (const ArtifactRecord& artifact : snapshot.artifacts) {
      if (artifact.id == id) {
        return true;
      }
    }
    return false;
  };
  for (const TransactionRecord& txn : snapshot.history) {
    for (const ArtifactId id : txn.invalidated) {
      if (!known(id)) {
        return reject("ledger snapshot: record invalidates unknown artifact");
      }
    }
  }
  if (snapshot.pending.has_value()) {
    if (snapshot.pending->input_revision != snapshot.current_revision) {
      return reject(
          "ledger snapshot: pending transaction starts at a stale revision");
    }
    for (const ArtifactId id : snapshot.pending->invalidates) {
      if (!known(id)) {
        return reject(
            "ledger snapshot: pending transaction invalidates unknown "
            "artifact");
      }
    }
  }
  current_revision_ = snapshot.current_revision;
  next_txn_ = snapshot.next_txn;
  next_artifact_ = snapshot.next_artifact;
  pending_ = snapshot.pending;
  history_ = snapshot.history;
  artifacts_ = snapshot.artifacts;
  return Status::Ok();
}

}  // namespace eos::session

namespace eos::session {
// ---- snapshot text serialization --------------------------------------
//
// Format version 1, line oriented. Metric values print with %.17g so
// a double round-trips exactly. Records keep Snapshot() order;
// a transaction's detail lines (changed objects, metrics,
// invalidations) follow its "txn" line and belong to it.

namespace {

using foundation::ErrorCode;

Status BadLedgerText(const std::string& what) {
  return Status::Error(ErrorCode::kInvalidArgument, what);
}

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
      return BadLedgerText("truncated escape in ledger string");
    }
    const int hi = HexDigit(token[i + 1]);
    const int lo = HexDigit(token[i + 2]);
    if (hi < 0 || lo < 0) {
      return BadLedgerText("bad escape in ledger string");
    }
    out.push_back(static_cast<char>(hi * 16 + lo));
    i += 2;
  }
  return out;
}

template <typename T>
bool ParseNum(const std::string& token, T& out, int base = 10) {
  const char* first = token.data();
  const char* last = first + token.size();
  const auto result = std::from_chars(first, last, out, base);
  return result.ec == std::errc() && result.ptr == last;
}

std::string FormatDouble(double value) {
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%.17g", value);
  return buf;
}

bool ParseDouble(const std::string& token, double& out) {
  if (token.empty()) {
    return false;
  }
  char* end = nullptr;
  out = std::strtod(token.c_str(), &end);
  return end == token.c_str() + token.size();
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

void AppendMetricLines(std::string& out, const char* tag,
                       const MetricMap& metrics) {
  for (const auto& entry : metrics) {
    out += tag;
    out += " " + Escape(entry.first) + " " + FormatDouble(entry.second.value) +
           " " + Escape(entry.second.unit) + "\n";
  }
}

void AppendSpec(std::string& out, const char* prefix,
                const TransactionSpec& spec) {
  const std::string p(prefix);
  out += p + "_input " + std::to_string(spec.input_revision) + "\n";
  out += p + "_label " + Escape(spec.label) + "\n";
  out += p + "_build " + Escape(spec.tool_build) + "\n";
  out += p + "_changed";
  for (const ObjectId id : spec.changed_objects) {
    out += " " + std::to_string(id.raw());
  }
  out += "\n";
  AppendMetricLines(out, (p + "_before").c_str(), spec.before_metrics);
  AppendMetricLines(out, (p + "_after").c_str(), spec.after_metrics);
  out += p + "_invalidates";
  for (const ArtifactId id : spec.invalidates) {
    out += " " + std::to_string(id);
  }
  out += "\n";
}

Status ParseMetricLine(const std::vector<std::string>& t, MetricMap& metrics) {
  if (t.size() != 4) {
    return BadLedgerText("bad metric line in ledger text");
  }
  const Result<std::string> name = Unescape(t[1]);
  const Result<std::string> unit = Unescape(t[3]);
  double value = 0.0;
  if (!name.ok() || !unit.ok() || !ParseDouble(t[2], value)) {
    return BadLedgerText("bad metric line in ledger text");
  }
  MetricValue metric;
  metric.value = value;
  metric.unit = unit.value();
  metrics[name.value()] = metric;
  return Status::Ok();
}

Status ParseIdList(const std::vector<std::string>& t,
                   std::vector<ObjectId>& out) {
  for (std::size_t i = 1; i < t.size(); ++i) {
    std::uint32_t raw = 0;
    if (!ParseNum(t[i], raw)) {
      return BadLedgerText("bad object id list in ledger text");
    }
    out.push_back(ObjectId(raw));
  }
  return Status::Ok();
}

Status ParseArtifactList(const std::vector<std::string>& t,
                         std::vector<ArtifactId>& out) {
  for (std::size_t i = 1; i < t.size(); ++i) {
    ArtifactId id = 0;
    if (!ParseNum(t[i], id)) {
      return BadLedgerText("bad artifact id list in ledger text");
    }
    out.push_back(id);
  }
  return Status::Ok();
}

}  // namespace

Result<std::string> SnapshotToText(const LedgerSnapshot& snapshot) {
  std::string out = "EOSLEDGER 1\n";
  out += "current " + std::to_string(snapshot.current_revision) + "\n";
  out += "next_txn " + std::to_string(snapshot.next_txn) + "\n";
  out += "next_artifact " + std::to_string(snapshot.next_artifact) + "\n";
  out += snapshot.pending.has_value() ? "pending 1\n" : "pending 0\n";
  if (snapshot.pending.has_value()) {
    AppendSpec(out, "pending", *snapshot.pending);
  }
  for (const ArtifactRecord& artifact : snapshot.artifacts) {
    out += "artifact " + std::to_string(artifact.id) + " " +
           std::to_string(static_cast<std::int32_t>(artifact.kind)) + " " +
           std::to_string(artifact.bound_revision) + " " +
           std::to_string(static_cast<std::int32_t>(artifact.proof)) + " " +
           Escape(artifact.name) + "\n";
  }
  for (const TransactionRecord& txn : snapshot.history) {
    out += "txn " + std::to_string(txn.id) + " " +
           std::to_string(txn.input_revision) + " " +
           std::to_string(txn.output_revision) + " " +
           std::to_string(static_cast<std::int32_t>(txn.proof)) +
           (txn.active ? " 1 " : " 0 ") + Escape(txn.label) + " " +
           Escape(txn.tool_build) + "\n";
    out += "txn_changed";
    for (const ObjectId id : txn.changed_objects) {
      out += " " + std::to_string(id.raw());
    }
    out += "\n";
    AppendMetricLines(out, "txn_before", txn.before_metrics);
    AppendMetricLines(out, "txn_after", txn.after_metrics);
    out += "txn_invalidates";
    for (const ArtifactId id : txn.invalidated) {
      out += " " + std::to_string(id);
    }
    out += "\n";
  }
  return out + "end " + Hex64(Fnv1a(out.data(), out.size())) + "\n";
}

Result<LedgerSnapshot> SnapshotFromText(const std::string& text) {
  const std::size_t end_pos = text.rfind("\nend ");
  if (end_pos == std::string::npos) {
    return BadLedgerText("ledger text has no checksum line");
  }
  const std::string end_line = text.substr(end_pos + 1);
  const std::vector<std::string> end_tokens =
      Split(end_line.substr(0, end_line.find('\n')));
  std::uint64_t stored = 0;
  if (end_tokens.size() != 2 || end_tokens[0] != "end" ||
      !ParseNum(end_tokens[1], stored, 16)) {
    return BadLedgerText("malformed ledger checksum line");
  }
  const std::string body = text.substr(0, end_pos + 1);
  if (Fnv1a(body.data(), body.size()) != stored) {
    return BadLedgerText("ledger checksum mismatch: text is corrupt");
  }

  std::vector<std::string> lines;
  std::size_t pos = 0;
  while (pos < body.size()) {
    const std::size_t nl = body.find('\n', pos);
    if (nl == std::string::npos) {
      lines.push_back(body.substr(pos));
      break;
    }
    lines.push_back(body.substr(pos, nl - pos));
    pos = nl + 1;
  }
  if (lines.empty() || lines[0] != "EOSLEDGER 1") {
    if (!lines.empty() && lines[0].rfind("EOSLEDGER ", 0) == 0) {
      return Status::Error(ErrorCode::kUnsupported,
                           "unsupported ledger text format version");
    }
    return BadLedgerText("not EOS ledger text");
  }

  LedgerSnapshot snapshot;
  bool have_current = false;
  bool have_next_txn = false;
  bool have_next_artifact = false;
  bool have_pending = false;
  TransactionSpec pending;
  bool pending_input = false;
  bool pending_label = false;
  bool pending_build = false;
  for (std::size_t i = 1; i < lines.size(); ++i) {
    const std::vector<std::string> t = Split(lines[i]);
    if (t.empty()) {
      return BadLedgerText("empty line in ledger text");
    }
    if (t[0] == "current" && t.size() == 2 &&
        ParseNum(t[1], snapshot.current_revision)) {
      have_current = true;
    } else if (t[0] == "next_txn" && t.size() == 2 &&
               ParseNum(t[1], snapshot.next_txn)) {
      have_next_txn = true;
    } else if (t[0] == "next_artifact" && t.size() == 2 &&
               ParseNum(t[1], snapshot.next_artifact)) {
      have_next_artifact = true;
    } else if (t[0] == "pending" && t.size() == 2 &&
               (t[1] == "0" || t[1] == "1")) {
      have_pending = true;
      if (t[1] == "1") {
        snapshot.pending = pending;
      } else {
        snapshot.pending.reset();
      }
    } else if (t[0] == "pending_input" && t.size() == 2 &&
               ParseNum(t[1], pending.input_revision)) {
      pending_input = true;
      if (snapshot.pending.has_value()) snapshot.pending->input_revision =
          pending.input_revision;
    } else if (t[0] == "pending_label" && t.size() == 2) {
      const Result<std::string> v = Unescape(t[1]);
      if (!v.ok()) return v.status();
      pending.label = v.value();
      pending_label = true;
      if (snapshot.pending.has_value()) snapshot.pending->label = v.value();
    } else if (t[0] == "pending_build" && t.size() == 2) {
      const Result<std::string> v = Unescape(t[1]);
      if (!v.ok()) return v.status();
      pending.tool_build = v.value();
      pending_build = true;
      if (snapshot.pending.has_value()) {
        snapshot.pending->tool_build = v.value();
      }
    } else if (t[0] == "pending_changed") {
      std::vector<ObjectId> ids;
      const Status s = ParseIdList(t, ids);
      if (!s.ok()) return s;
      pending.changed_objects = ids;
      if (snapshot.pending.has_value()) {
        snapshot.pending->changed_objects = ids;
      }
    } else if (t[0] == "pending_before") {
      MetricMap metrics = pending.before_metrics;
      const Status s = ParseMetricLine(t, metrics);
      if (!s.ok()) return s;
      pending.before_metrics = metrics;
      if (snapshot.pending.has_value()) {
        snapshot.pending->before_metrics = metrics;
      }
    } else if (t[0] == "pending_after") {
      MetricMap metrics = pending.after_metrics;
      const Status s = ParseMetricLine(t, metrics);
      if (!s.ok()) return s;
      pending.after_metrics = metrics;
      if (snapshot.pending.has_value()) {
        snapshot.pending->after_metrics = metrics;
      }
    } else if (t[0] == "pending_invalidates") {
      std::vector<ArtifactId> ids;
      const Status s = ParseArtifactList(t, ids);
      if (!s.ok()) return s;
      pending.invalidates = ids;
      if (snapshot.pending.has_value()) snapshot.pending->invalidates = ids;
    } else if (t[0] == "artifact" && t.size() == 6) {
      ArtifactRecord artifact;
      std::int32_t kind = 0;
      std::int32_t proof = 0;
      if (!ParseNum(t[1], artifact.id) || !ParseNum(t[2], kind) ||
          kind < 0 || kind > 1 ||
          !ParseNum(t[3], artifact.bound_revision) ||
          !ParseNum(t[4], proof) || proof < 0 || proof > 2) {
        return BadLedgerText("bad artifact line in ledger text");
      }
      const Result<std::string> name = Unescape(t[5]);
      if (!name.ok()) return name.status();
      artifact.kind = static_cast<ArtifactKind>(kind);
      artifact.proof = static_cast<ProofStatus>(proof);
      artifact.name = name.value();
      snapshot.artifacts.push_back(std::move(artifact));
    } else if (t[0] == "txn" && t.size() == 8) {
      TransactionRecord txn;
      std::int32_t proof = 0;
      std::uint32_t active = 0;
      if (!ParseNum(t[1], txn.id) || !ParseNum(t[2], txn.input_revision) ||
          !ParseNum(t[3], txn.output_revision) || !ParseNum(t[4], proof) ||
          proof < 0 || proof > 2 || !ParseNum(t[5], active) || active > 1) {
        return BadLedgerText("bad txn line in ledger text");
      }
      const Result<std::string> label = Unescape(t[6]);
      const Result<std::string> build = Unescape(t[7]);
      if (!label.ok() || !build.ok()) {
        return BadLedgerText("bad txn string field in ledger text");
      }
      txn.rollback_checkpoint = txn.input_revision;
      txn.proof = static_cast<ProofStatus>(proof);
      txn.active = active == 1;
      txn.label = label.value();
      txn.tool_build = build.value();
      snapshot.history.push_back(std::move(txn));
    } else if (t[0] == "txn_changed" && !snapshot.history.empty()) {
      const Status s =
          ParseIdList(t, snapshot.history.back().changed_objects);
      if (!s.ok()) return s;
    } else if (t[0] == "txn_before" && !snapshot.history.empty()) {
      const Status s =
          ParseMetricLine(t, snapshot.history.back().before_metrics);
      if (!s.ok()) return s;
    } else if (t[0] == "txn_after" && !snapshot.history.empty()) {
      const Status s =
          ParseMetricLine(t, snapshot.history.back().after_metrics);
      if (!s.ok()) return s;
    } else if (t[0] == "txn_invalidates" && !snapshot.history.empty()) {
      const Status s =
          ParseArtifactList(t, snapshot.history.back().invalidated);
      if (!s.ok()) return s;
    } else {
      return BadLedgerText("unrecognized line in ledger text: " + t[0]);
    }
  }
  if (!have_current || !have_next_txn || !have_next_artifact ||
      !have_pending) {
    return BadLedgerText("ledger text is missing its header fields");
  }
  if (snapshot.pending.has_value() &&
      (!pending_input || !pending_label || !pending_build)) {
    return BadLedgerText("ledger text has an incomplete pending spec");
  }
  return snapshot;
}
}  // namespace eos::session
