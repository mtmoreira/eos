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

}  // namespace
}  // namespace eos::db
