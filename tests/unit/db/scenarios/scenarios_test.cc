// Unit tests: db/scenarios.
#include "eos/db/scenarios.h"

#include <gtest/gtest.h>

namespace eos::db {
namespace {

TEST(ScenarioSet, CreatesScenariosActiveByDefault) {
  ScenarioSet set;
  const Result<ScenarioId> id =
      set.Create("func_tt", "func", "tt_025c", ParasiticsLevel::kEstimate);
  ASSERT_TRUE(id.ok());
  const Scenario* scenario = set.Find(id.value());
  ASSERT_NE(scenario, nullptr);
  EXPECT_EQ(scenario->name, "func_tt");
  EXPECT_EQ(scenario->mode, "func");
  EXPECT_EQ(scenario->corner, "tt_025c");
  EXPECT_EQ(scenario->parasitics, ParasiticsLevel::kEstimate);
  EXPECT_TRUE(scenario->active);
  EXPECT_EQ(set.ActiveScenarios().size(), 1u);
}

TEST(ScenarioSet, IdentityIsTheTriple) {
  ScenarioSet set;
  ASSERT_TRUE(
      set.Create("a", "func", "ss_125c", ParasiticsLevel::kExtracted).ok());
  const Scenario* found =
      set.FindByTriple("func", "ss_125c", ParasiticsLevel::kExtracted);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->name, "a");
  EXPECT_EQ(set.FindByTriple("func", "ss_125c", ParasiticsLevel::kEstimate),
            nullptr);
  EXPECT_EQ(set.FindByName("a"), found);
  EXPECT_EQ(set.FindByName("missing"), nullptr);
}

TEST(ScenarioSet, RejectsDuplicateNamesAndDuplicateTriples) {
  ScenarioSet set;
  ASSERT_TRUE(
      set.Create("a", "func", "tt", ParasiticsLevel::kEstimate).ok());
  const Result<ScenarioId> dup_name =
      set.Create("a", "test", "ff", ParasiticsLevel::kEstimate);
  ASSERT_FALSE(dup_name.ok());
  EXPECT_EQ(dup_name.status().code(), ErrorCode::kInvalidArgument);
  const Result<ScenarioId> dup_triple =
      set.Create("b", "func", "tt", ParasiticsLevel::kEstimate);
  ASSERT_FALSE(dup_triple.ok());
  EXPECT_EQ(dup_triple.status().code(), ErrorCode::kInvalidArgument);
  EXPECT_EQ(set.size(), 1u);
}

TEST(ScenarioSet, ActiveSetTracksToggles) {
  ScenarioSet set;
  const ScenarioId a =
      set.Create("a", "func", "tt", ParasiticsLevel::kEstimate).value();
  const ScenarioId b =
      set.Create("b", "func", "ss", ParasiticsLevel::kEstimate).value();
  EXPECT_EQ(set.ActiveScenarios().size(), 2u);
  ASSERT_TRUE(set.SetActive(a, false).ok());
  const std::vector<const Scenario*> active = set.ActiveScenarios();
  ASSERT_EQ(active.size(), 1u);
  EXPECT_EQ(active[0]->id, b);
  ASSERT_TRUE(set.SetActive(a, true).ok());
  EXPECT_EQ(set.ActiveScenarios().size(), 2u);

  const Status missing = set.SetActive(999, true);
  ASSERT_FALSE(missing.ok());
  EXPECT_EQ(missing.code(), ErrorCode::kNotFound);
}

// S6 backfill: ExportState/FromState round-trips every scenario,
// its inactive flag, and the next-id counter.
TEST(ScenarioSet, StateRoundTripPreservesScenariosAndNextId) {
  ScenarioSet set;
  const ScenarioId a =
      set.Create("a", "func", "tt", ParasiticsLevel::kEstimate).value();
  const ScenarioId b =
      set.Create("b", "func", "ss", ParasiticsLevel::kExtracted).value();
  ASSERT_TRUE(set.SetActive(b, false).ok());
  const ScenarioState state = set.ExportState();
  Result<ScenarioSet> restored = ScenarioSet::FromState(state);
  ASSERT_TRUE(restored.ok()) << restored.status().message();
  ScenarioSet& back = restored.value();
  EXPECT_EQ(back.size(), 2u);
  const Scenario* ra = back.Find(a);
  ASSERT_NE(ra, nullptr);
  EXPECT_EQ(ra->name, "a");
  EXPECT_TRUE(ra->active);
  const Scenario* rb = back.Find(b);
  ASSERT_NE(rb, nullptr);
  EXPECT_EQ(rb->parasitics, ParasiticsLevel::kExtracted);
  EXPECT_FALSE(rb->active);
  ASSERT_EQ(back.ActiveScenarios().size(), 1u);
  EXPECT_EQ(back.ActiveScenarios()[0]->id, a);
  const Result<ScenarioId> c = back.Create(
      "c", "func", "ff", ParasiticsLevel::kEstimate);
  ASSERT_TRUE(c.ok());
  EXPECT_NE(c.value(), a);
  EXPECT_NE(c.value(), b);
}

}  // namespace
}  // namespace eos::db
