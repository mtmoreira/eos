// Unit tests: db/checkpoint.
#include "eos/db/checkpoint.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace eos::db {
namespace {

std::string TempPath(const std::string& tag) {
  return "checkpoint_test_" + tag + ".ckpt";
}

void Remove(const std::string& path) {
  std::remove(path.c_str());
  std::remove((path + ".tmp").c_str());
}

std::string ReadAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

void WriteAll(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

// A populated project state: design with one of everything, two
// scenarios (one inactive), three constraints (bound, unresolved,
// empty by design, so their states are intrinsic, not corruption).
struct Fixture {
  Design design{"top"};
  ScenarioSet scenarios;
  ConstraintSet constraints;
  ObjectId clk_port;

  Fixture() {
    const ObjectId cell = design.CreateCell("dff").value();
    clk_port = design.CreatePort("clk", Direction::kInput).value();
    const ObjectId u1 = design.CreateInstance("u1", cell).value();
    const ObjectId net = design.CreateNet("clk net").value();  // space!
    design.Connect(u1, "CK", net);
    scenarios.Create("func_tt", "func", "tt_025c",
                     ParasiticsLevel::kEstimate);
    const ScenarioId slow =
        scenarios
            .Create("func_ss", "func", "ss_125c",
                    ParasiticsLevel::kExtracted)
            .value();
    scenarios.SetActive(slow, false);
    constraints.Add(ConstraintKind::kClock, "clk_100", "clk", design);
    constraints.Add(ConstraintKind::kInputDelay, "din", "missing", design);
    constraints.Add(ConstraintKind::kFalsePath, "fp", "", design);
  }

  CheckpointData Data() const {
    CheckpointData data;
    data.design = design.ExportState();
    data.scenarios = scenarios.ExportState();
    data.constraints = constraints.ExportState();
    return data;
  }
};

TEST(Checkpoint, RoundTripsTheFullState) {
  Fixture f;
  const std::string path = TempPath("roundtrip");
  Remove(path);
  ASSERT_TRUE(Checkpoint::Save(path, f.Data()).ok());

  RestoreInfo info;
  const Result<CheckpointData> loaded = Checkpoint::Load(path, &info);
  ASSERT_TRUE(loaded.ok());
  EXPECT_EQ(info.constraints_marked_unresolved, 0u);
  const CheckpointData& data = loaded.value();

  const Result<Design> design = Design::FromState(data.design);
  ASSERT_TRUE(design.ok());
  EXPECT_EQ(design.value().name(), "top");
  EXPECT_EQ(design.value().Find(foundation::ObjectKind::kPort, "clk"),
            f.clk_port);
  // The net name contains a space: escaping must round-trip it.
  EXPECT_FALSE(
      design.value().Find(foundation::ObjectKind::kNet, "clk net").is_null());
  EXPECT_EQ(design.value().Count(foundation::ObjectKind::kPin), 1u);

  const Result<ScenarioSet> scenarios = ScenarioSet::FromState(data.scenarios);
  ASSERT_TRUE(scenarios.ok());
  EXPECT_EQ(scenarios.value().size(), 2u);
  EXPECT_EQ(scenarios.value().ActiveScenarios().size(), 1u);
  EXPECT_FALSE(scenarios.value().FindByName("func_ss")->active);

  const Result<ConstraintSet> constraints =
      ConstraintSet::FromState(data.constraints);
  ASSERT_TRUE(constraints.ok());
  const CoverageReport coverage = constraints.value().Coverage();
  EXPECT_EQ(coverage.total, 3u);
  EXPECT_EQ(coverage.bound, 1u);
  EXPECT_EQ(coverage.unresolved, 1u);
  EXPECT_EQ(coverage.empty, 1u);
  Remove(path);
}

TEST(Checkpoint, RestoredAllocatorNeverReusesAnId) {
  Fixture f;
  const std::string path = TempPath("allocator");
  Remove(path);
  ASSERT_TRUE(Checkpoint::Save(path, f.Data()).ok());
  const Result<CheckpointData> loaded = Checkpoint::Load(path, nullptr);
  ASSERT_TRUE(loaded.ok());
  Result<Design> design = Design::FromState(loaded.value().design);
  ASSERT_TRUE(design.ok());
  const ObjectId fresh = design.value().CreateNet("brand_new").value();
  // Exactly one net existed before (index 1); the new one is index 2.
  EXPECT_EQ(fresh.index(), 2u);
  Remove(path);
}

TEST(Checkpoint, SaveIsAtomicAndLeavesNoTempFile) {
  Fixture f;
  const std::string path = TempPath("atomic");
  Remove(path);
  ASSERT_TRUE(Checkpoint::Save(path, f.Data()).ok());
  std::ifstream tmp(path + ".tmp");
  EXPECT_FALSE(tmp.good());
  // Saving again over an existing checkpoint replaces it.
  ASSERT_TRUE(Checkpoint::Save(path, f.Data()).ok());
  EXPECT_TRUE(Checkpoint::Load(path, nullptr).ok());
  Remove(path);
}

TEST(Checkpoint, LoadRejectsCorruptionTruncationAndStrangers) {
  Fixture f;
  const std::string path = TempPath("corrupt");
  Remove(path);
  ASSERT_TRUE(Checkpoint::Save(path, f.Data()).ok());
  const std::string good = ReadAll(path);

  // One flipped byte anywhere in the body: checksum mismatch.
  std::string flipped = good;
  flipped[20] = flipped[20] == 'a' ? 'b' : 'a';
  WriteAll(path, flipped);
  const Result<CheckpointData> bad_sum = Checkpoint::Load(path, nullptr);
  ASSERT_FALSE(bad_sum.ok());
  EXPECT_EQ(bad_sum.status().code(), ErrorCode::kInvalidArgument);

  // Truncation: no checksum line at all.
  WriteAll(path, good.substr(0, good.size() / 2));
  EXPECT_FALSE(Checkpoint::Load(path, nullptr).ok());

  // Not a checkpoint.
  WriteAll(path, "hello world\n");
  EXPECT_FALSE(Checkpoint::Load(path, nullptr).ok());

  // Missing file.
  Remove(path);
  const Result<CheckpointData> missing = Checkpoint::Load(path, nullptr);
  ASSERT_FALSE(missing.ok());
  EXPECT_EQ(missing.status().code(), ErrorCode::kIoError);
  Remove(path);
}

TEST(Checkpoint, SaveRefusesAConstraintBoundOutsideTheDesign) {
  Fixture f;
  CheckpointData data = f.Data();
  // Hand-corrupt the state: bind the unresolved constraint to an
  // object the design does not contain.
  for (Constraint& c : data.constraints.constraints) {
    if (c.status == BindingStatus::kUnresolved) {
      c.status = BindingStatus::kBound;
      c.bound_object =
          foundation::ObjectId::Make(foundation::ObjectKind::kNet, 555);
    }
  }
  const std::string path = TempPath("refused");
  Remove(path);
  const Status saved = Checkpoint::Save(path, data);
  ASSERT_FALSE(saved.ok());
  std::ifstream in(path);
  EXPECT_FALSE(in.good());  // nothing was written
  Remove(path);
}

TEST(Checkpoint, LoadMarksDanglingBindingsUnresolvedAndCountsThem) {
  Fixture f;
  const std::string path = TempPath("dangling");
  Remove(path);
  ASSERT_TRUE(Checkpoint::Save(path, f.Data()).ok());

  // Rewrite the file by hand: point the clock constraint's bound
  // object at a net that does not exist, and repair the checksum with
  // a test-local copy of the format's FNV-1a, so Load's own
  // cross-check is what catches it.
  std::string content = ReadAll(path);
  const std::size_t line_start = content.find("\nconstraint 1 ");
  ASSERT_NE(line_start, std::string::npos);
  const std::size_t line_end = content.find('\n', line_start + 1);
  const std::string line =
      content.substr(line_start + 1, line_end - line_start - 1);
  std::vector<std::string> tokens;
  std::size_t p = 0;
  while (p < line.size()) {
    const std::size_t sp = line.find(' ', p);
    if (sp == std::string::npos) {
      tokens.push_back(line.substr(p));
      break;
    }
    tokens.push_back(line.substr(p, sp - p));
    p = sp + 1;
  }
  ASSERT_EQ(tokens.size(), 7u);
  const std::uint32_t fake_net =
      foundation::ObjectId::Make(foundation::ObjectKind::kNet, 555).raw();
  tokens[4] = std::to_string(fake_net);
  std::string joined;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (i != 0) joined += " ";
    joined += tokens[i];
  }
  content.replace(line_start + 1, line_end - line_start - 1, joined);
  const std::size_t end_pos = content.rfind("\nend ");
  ASSERT_NE(end_pos, std::string::npos);
  const std::string body = content.substr(0, end_pos + 1);
  std::uint64_t hash = 14695981039346656037ull;
  for (const char c : body) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 1099511628211ull;
  }
  char hex[17];
  std::snprintf(hex, sizeof(hex), "%016llx",
                static_cast<unsigned long long>(hash));
  WriteAll(path, body + "end " + std::string(hex, 16) + "\n");

  RestoreInfo info;
  const Result<CheckpointData> loaded = Checkpoint::Load(path, &info);
  ASSERT_TRUE(loaded.ok());
  EXPECT_EQ(info.constraints_marked_unresolved, 1u);
  const Result<ConstraintSet> constraints =
      ConstraintSet::FromState(loaded.value().constraints);
  ASSERT_TRUE(constraints.ok());
  const Constraint* clock = constraints.value().Find(1);
  ASSERT_NE(clock, nullptr);
  EXPECT_EQ(clock->status, BindingStatus::kUnresolved);
  EXPECT_TRUE(clock->bound_object.is_null());
  EXPECT_EQ(clock->target_spec, "clk");  // the spec survives for rebind
  Remove(path);
}

TEST(Checkpoint, TextFormRoundTripsAndMatchesTheFileForm) {
  Fixture f;
  const Result<std::string> text = Checkpoint::Serialize(f.Data());
  ASSERT_TRUE(text.ok());

  RestoreInfo info;
  const Result<CheckpointData> parsed =
      Checkpoint::FromString(text.value(), &info);
  ASSERT_TRUE(parsed.ok());
  EXPECT_EQ(info.constraints_marked_unresolved, 0u);
  const Result<Design> design = Design::FromState(parsed.value().design);
  ASSERT_TRUE(design.ok());
  EXPECT_EQ(design.value().name(), "top");
  EXPECT_EQ(design.value().Count(foundation::ObjectKind::kPin), 1u);

  // Save writes exactly the Serialize text, and Load reads it back.
  const std::string path = TempPath("textform");
  Remove(path);
  ASSERT_TRUE(Checkpoint::Save(path, f.Data()).ok());
  EXPECT_EQ(ReadAll(path), text.value());
  const Result<CheckpointData> loaded = Checkpoint::Load(path, nullptr);
  ASSERT_TRUE(loaded.ok());
  Remove(path);
}

TEST(Checkpoint, FromStringRejectsCorruptText) {
  Fixture f;
  const Result<std::string> text = Checkpoint::Serialize(f.Data());
  ASSERT_TRUE(text.ok());

  std::string flipped = text.value();
  flipped[20] = flipped[20] == 'a' ? 'b' : 'a';
  const Result<CheckpointData> bad = Checkpoint::FromString(flipped, nullptr);
  ASSERT_FALSE(bad.ok());
  EXPECT_EQ(bad.status().code(), ErrorCode::kInvalidArgument);

  EXPECT_FALSE(Checkpoint::FromString("hello world\n", nullptr).ok());
  EXPECT_FALSE(
      Checkpoint::FromString(text.value().substr(0, 40), nullptr).ok());
}

TEST(Checkpoint, SerializeRefusesTheSameStatesSaveRefuses) {
  Fixture f;
  CheckpointData data = f.Data();
  for (Constraint& c : data.constraints.constraints) {
    if (c.status == BindingStatus::kUnresolved) {
      c.status = BindingStatus::kBound;
      c.bound_object =
          foundation::ObjectId::Make(foundation::ObjectKind::kNet, 555);
    }
  }
  EXPECT_FALSE(Checkpoint::Serialize(data).ok());
}

}  // namespace
}  // namespace eos::db
