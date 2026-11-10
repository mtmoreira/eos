// Unit tests: db/waivers.
#include "eos/db/waivers.h"

#include <gtest/gtest.h>

namespace eos::db {
namespace {

// The known scenes a waiver's scene must be one of.
const std::vector<std::string> kScenes = {"func_tt", "func_ss"};

Result<WaiverId> AddTiming(WaiverSet& set, const std::string& name,
                           const std::string& klass,
                           const std::string& target,
                           const std::string& scene = "") {
  return set.Add(WaiverKind::kTiming, name, klass, target, scene,
                 "accepted", "mtmoreira", "", 7, kScenes);
}

TEST(Waivers, StoresTheRecordWithAllFields) {
  WaiverSet set;
  const Result<WaiverId> added = set.Add(
      WaiverKind::kTiming, "slow-r2", "max_delay", "r2/D", "func_tt",
      "known slow path", "mtmoreira", "2027-01-31", 7, kScenes);
  ASSERT_TRUE(added.ok()) << added.status().message();
  const Waiver* waiver = set.Find(added.value());
  ASSERT_NE(waiver, nullptr);
  EXPECT_EQ(waiver->name, "slow-r2");
  EXPECT_EQ(waiver->kind, WaiverKind::kTiming);
  EXPECT_EQ(waiver->klass, "max_delay");
  EXPECT_EQ(waiver->target, "r2/D");
  EXPECT_EQ(waiver->scene, "func_tt");
  EXPECT_EQ(waiver->reason, "known slow path");
  EXPECT_EQ(waiver->approved_by, "mtmoreira");
  EXPECT_EQ(waiver->expires, "2027-01-31");
  EXPECT_EQ(waiver->created_revision, 7u);
  EXPECT_EQ(set.all().size(), 1u);
}

TEST(Waivers, DuplicateNameFails) {
  WaiverSet set;
  ASSERT_TRUE(AddTiming(set, "w1", "max_delay", "r2/D").ok());
  const Result<WaiverId> dup = AddTiming(set, "w1", "min_delay", "r2/D");
  EXPECT_FALSE(dup.ok());
  EXPECT_EQ(set.all().size(), 1u);
}

TEST(Waivers, UnknownKindFails) {
  WaiverSet set;
  const Result<WaiverId> bad = set.Add(
      static_cast<WaiverKind>(99), "w1", "max_delay", "r2/D", "", "accepted",
      "mtmoreira", "", 0, kScenes);
  EXPECT_FALSE(bad.ok());
}

TEST(Waivers, UnknownClassForTheKindFails) {
  WaiverSet set;
  EXPECT_FALSE(AddTiming(set, "w1", "no_such_class", "r2/D").ok());
  EXPECT_FALSE(set.Add(WaiverKind::kCheck, "w2", "max_delay", "b", "",
                       "accepted", "mtmoreira", "", 0, kScenes)
                   .ok());
  EXPECT_FALSE(set.Add(WaiverKind::kCoverage, "w3", "no_input_delay", "r1/D",
                       "", "accepted", "mtmoreira", "", 0, kScenes)
                   .ok());
  // Each kind accepts its own classes.
  ASSERT_TRUE(AddTiming(set, "w1", "max_delay", "r2/D").ok());
  ASSERT_TRUE(set.Add(WaiverKind::kCheck, "w2", "no_input_delay", "b", "",
                      "accepted", "mtmoreira", "", 0, kScenes)
                  .ok());
  ASSERT_TRUE(set.Add(WaiverKind::kCoverage, "w3", "setup", "r1/D", "",
                      "accepted", "mtmoreira", "", 0, kScenes)
                  .ok());
}

TEST(Waivers, UnknownSceneFails) {
  WaiverSet set;
  const Result<WaiverId> bad =
      AddTiming(set, "w1", "max_delay", "r2/D", "no_such_scene");
  EXPECT_FALSE(bad.ok());
  EXPECT_TRUE(set.all().empty());
}

TEST(Waivers, MalformedExpiryFails) {
  WaiverSet set;
  EXPECT_FALSE(set.Add(WaiverKind::kTiming, "w1", "max_delay", "r2/D", "",
                       "accepted", "mtmoreira", "31-01-2027", 0, kScenes)
                   .ok());
  EXPECT_FALSE(set.Add(WaiverKind::kTiming, "w2", "max_delay", "r2/D", "",
                       "accepted", "mtmoreira", "2027-02-30", 0, kScenes)
                   .ok());
  EXPECT_FALSE(set.Add(WaiverKind::kTiming, "w3", "max_delay", "r2/D", "",
                       "accepted", "mtmoreira", "2027-13-01", 0, kScenes)
                   .ok());
  // A real date stores.
  ASSERT_TRUE(set.Add(WaiverKind::kTiming, "w4", "max_delay", "r2/D", "",
                      "accepted", "mtmoreira", "2027-02-28", 0, kScenes)
                  .ok());
}

TEST(Waivers, EmptyReasonOrApproverFails) {
  WaiverSet set;
  EXPECT_FALSE(set.Add(WaiverKind::kTiming, "w1", "max_delay", "r2/D", "",
                       "", "mtmoreira", "", 0, kScenes)
                   .ok());
  EXPECT_FALSE(set.Add(WaiverKind::kTiming, "w2", "max_delay", "r2/D", "",
                       "accepted", "", "", 0, kScenes)
                   .ok());
  EXPECT_TRUE(set.all().empty());
}

TEST(Waivers, KindWordsRoundTrip) {
  EXPECT_EQ(ParseWaiverKind("timing"), WaiverKind::kTiming);
  EXPECT_EQ(ParseWaiverKind("check"), WaiverKind::kCheck);
  EXPECT_EQ(ParseWaiverKind("coverage"), WaiverKind::kCoverage);
  EXPECT_EQ(ParseWaiverKind("path"), std::nullopt);
  EXPECT_EQ(ParseWaiverKind("bogus"), std::nullopt);
  EXPECT_EQ(WaiverKindWord(WaiverKind::kTiming), "timing");
  EXPECT_EQ(WaiverKindWord(WaiverKind::kCheck), "check");
  EXPECT_EQ(WaiverKindWord(WaiverKind::kCoverage), "coverage");
}

TEST(Waivers, IsoDateValidation) {
  EXPECT_TRUE(ParseIsoDate("2027-01-31"));
  EXPECT_TRUE(ParseIsoDate("2028-02-29"));  // leap year
  EXPECT_FALSE(ParseIsoDate("2027-02-29"));  // not a leap year
  EXPECT_FALSE(ParseIsoDate("2027-2-28"));
  EXPECT_FALSE(ParseIsoDate("2027/02/28"));
  EXPECT_FALSE(ParseIsoDate("2027-02-28x"));
  EXPECT_FALSE(ParseIsoDate(""));
  // Today is a valid date in the same shape.
  EXPECT_TRUE(ParseIsoDate(TodayUtc()));
}

TEST(Waivers, MatchingIsExactOnAllFourFields) {
  WaiverSet set;
  ASSERT_TRUE(AddTiming(set, "w1", "max_delay", "r2/D", "func_tt").ok());
  const WaiverViolation hit{WaiverKind::kTiming, "max_delay", "r2/D",
                            "func_tt"};
  const Waiver* found = set.Match(hit, "2026-11-09");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->name, "w1");
  // Each differing field misses.
  EXPECT_EQ(set.Match({WaiverKind::kTiming, "min_delay", "r2/D", "func_tt"},
                      "2026-11-09"),
            nullptr);
  EXPECT_EQ(set.Match({WaiverKind::kTiming, "max_delay", "r1/D", "func_tt"},
                      "2026-11-09"),
            nullptr);
  EXPECT_EQ(set.Match({WaiverKind::kTiming, "max_delay", "r2/D", "func_ss"},
                      "2026-11-09"),
            nullptr);
  EXPECT_EQ(set.Match({WaiverKind::kCheck, "max_delay", "r2/D", "func_tt"},
                      "2026-11-09"),
            nullptr);
  // An empty waiver scene matches every scene.
  WaiverSet open;
  ASSERT_TRUE(AddTiming(open, "w2", "max_delay", "r2/D").ok());
  EXPECT_NE(open.Match({WaiverKind::kTiming, "max_delay", "r2/D", "func_ss"},
                       "2026-11-09"),
            nullptr);
  EXPECT_NE(open.Match({WaiverKind::kTiming, "max_delay", "r2/D", "func_tt"},
                       "2026-11-09"),
            nullptr);
}

TEST(Waivers, ExpiredWaiverMatchesNothing) {
  WaiverSet set;
  ASSERT_TRUE(set.Add(WaiverKind::kTiming, "w1", "max_delay", "r2/D", "",
                      "accepted", "mtmoreira", "2026-11-08", 0, kScenes)
                  .ok());
  const WaiverViolation hit{WaiverKind::kTiming, "max_delay", "r2/D",
                            "func_tt"};
  // Expired yesterday: no match.
  EXPECT_EQ(set.Match(hit, "2026-11-09"), nullptr);
  // Expiring today is not expired.
  WaiverSet today;
  ASSERT_TRUE(today.Add(WaiverKind::kTiming, "w1", "max_delay", "r2/D", "",
                        "accepted", "mtmoreira", "2026-11-09", 0, kScenes)
                    .ok());
  EXPECT_NE(today.Match(hit, "2026-11-09"), nullptr);
  // Expiring tomorrow is not expired.
  WaiverSet tomorrow;
  ASSERT_TRUE(tomorrow.Add(WaiverKind::kTiming, "w1", "max_delay", "r2/D",
                           "", "accepted", "mtmoreira", "2026-11-10", 0,
                           kScenes)
                    .ok());
  EXPECT_NE(tomorrow.Match(hit, "2026-11-09"), nullptr);
}

TEST(Waivers, StatusDerivesFromMatchesAndDate) {
  WaiverSet set;
  ASSERT_TRUE(AddTiming(set, "w1", "max_delay", "r2/D").ok());
  ASSERT_TRUE(set.Add(WaiverKind::kTiming, "w2", "min_delay", "r2/D", "",
                      "accepted", "mtmoreira", "2026-11-01", 0, kScenes)
                  .ok());
  const std::vector<WaiverViolation> violations = {
      {WaiverKind::kTiming, "max_delay", "r2/D", "func_tt"}};
  const Waiver* active = set.Find("w1");
  const Waiver* expired = set.Find("w2");
  ASSERT_NE(active, nullptr);
  ASSERT_NE(expired, nullptr);
  EXPECT_EQ(set.MatchesFor(*active, violations, "2026-11-09"), 1u);
  EXPECT_EQ(WaiverSet::StatusOf(*active, 1, "2026-11-09"),
            WaiverStatus::kActive);
  EXPECT_EQ(WaiverSet::StatusOf(*active, 0, "2026-11-09"),
            WaiverStatus::kUnmatched);
  EXPECT_EQ(WaiverSet::StatusOf(*expired, 1, "2026-11-09"),
            WaiverStatus::kExpired);
  // No violation list means nothing matches.
  EXPECT_EQ(set.MatchesFor(*active, {}, "2026-11-09"), 0u);
  const WaiverCoverage coverage = set.Coverage(violations, "2026-11-09");
  EXPECT_EQ(coverage.total, 2u);
  EXPECT_EQ(coverage.active, 1u);
  EXPECT_EQ(coverage.unmatched, 0u);
  EXPECT_EQ(coverage.expired, 1u);
}

TEST(Waivers, RemoveByNameAndUnknownName) {
  WaiverSet set;
  ASSERT_TRUE(AddTiming(set, "w1", "max_delay", "r2/D").ok());
  ASSERT_TRUE(AddTiming(set, "w2", "min_delay", "r2/D").ok());
  EXPECT_FALSE(set.Remove("nope"));
  EXPECT_TRUE(set.Remove("w1"));
  EXPECT_EQ(set.all().size(), 1u);
  EXPECT_EQ(set.Find("w1"), nullptr);
  EXPECT_NE(set.Find("w2"), nullptr);
  // Ids are never reused.
  const Result<WaiverId> third = AddTiming(set, "w3", "max_delay", "r1/D");
  ASSERT_TRUE(third.ok());
  EXPECT_EQ(third.value(), 3u);
}

TEST(Waivers, StateRoundTripsAndRejectsBadState) {
  WaiverSet set;
  ASSERT_TRUE(AddTiming(set, "w1", "max_delay", "r2/D", "func_tt").ok());
  ASSERT_TRUE(set.Add(WaiverKind::kCheck, "w2", "no_input_delay", "b", "",
                      "accepted", "mtmoreira", "2027-06-30", 3, kScenes)
                  .ok());
  const WaiverState state = set.ExportState();
  const Result<WaiverSet> restored = WaiverSet::FromState(state);
  ASSERT_TRUE(restored.ok()) << restored.status().message();
  ASSERT_EQ(restored.value().all().size(), 2u);
  EXPECT_EQ(restored.value().Find("w1")->target, "r2/D");
  EXPECT_EQ(restored.value().Find("w2")->expires, "2027-06-30");
  EXPECT_EQ(restored.value().Find("w2")->created_revision, 3u);
  // A new waiver after restore takes the next id, never a reused one.
  WaiverSet copy = restored.value();
  const Result<WaiverId> third = AddTiming(copy, "w3", "max_delay", "r1/D");
  ASSERT_TRUE(third.ok());
  EXPECT_EQ(third.value(), 3u);

  WaiverState bad = state;
  bad.waivers[1].id = bad.waivers[0].id;
  EXPECT_FALSE(WaiverSet::FromState(bad).ok());
  bad = state;
  bad.waivers[1].name = bad.waivers[0].name;
  EXPECT_FALSE(WaiverSet::FromState(bad).ok());
  bad = state;
  bad.waivers[0].klass = "bogus";
  EXPECT_FALSE(WaiverSet::FromState(bad).ok());
  bad = state;
  bad.waivers[0].expires = "2027-02-30";
  EXPECT_FALSE(WaiverSet::FromState(bad).ok());
  bad = state;
  bad.next_id = 1;
  EXPECT_FALSE(WaiverSet::FromState(bad).ok());
}

TEST(Waivers, NoObjectIdInvolved) {
  // Matching is by name string: a target that never binds to a
  // design object still matches by name when the engine reports
  // that pin.
  WaiverSet set;
  ASSERT_TRUE(AddTiming(set, "w1", "max_delay", "ghost/D").ok());
  EXPECT_NE(set.Match({WaiverKind::kTiming, "max_delay", "ghost/D",
                       "func_tt"},
                      "2026-11-09"),
            nullptr);
}

}  // namespace
}  // namespace eos::db
