// Unit tests: db/constraints.
#include "eos/db/constraints.h"

#include <gtest/gtest.h>

namespace eos::db {
namespace {

// A small design: port clk, instance u1 of cell dff with pin CK on net
// clk_net, net data_net.
struct Fixture {
  Design design{"top"};
  ObjectId clk_port;
  ObjectId u1;
  Fixture() {
    const ObjectId cell = design.CreateCell("dff").value();
    clk_port = design.CreatePort("clk", Direction::kInput).value();
    u1 = design.CreateInstance("u1", cell).value();
    const ObjectId clk_net = design.CreateNet("clk_net").value();
    design.CreateNet("data_net");
    design.Connect(u1, "CK", clk_net);
  }
};

TEST(Constraints, BindsToAPortByName) {
  Fixture f;
  ConstraintSet set;
  const ConstraintId id =
      set.Add(ConstraintKind::kClock, "clk_100", "clk", f.design);
  const Constraint* c = set.Find(id);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->status, BindingStatus::kBound);
  EXPECT_EQ(c->bound_object, f.clk_port);
  EXPECT_EQ(c->target_spec, "clk");
}

TEST(Constraints, BindsToAPinByPath) {
  Fixture f;
  ConstraintSet set;
  const ConstraintId id =
      set.Add(ConstraintKind::kClock, "clk_at_pin", "u1/CK", f.design);
  const Constraint* c = set.Find(id);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->status, BindingStatus::kBound);
  EXPECT_EQ(c->bound_object.kind(), foundation::ObjectKind::kPin);
}

TEST(Constraints, UnresolvedTargetsAreRetainedNeverDropped) {
  Fixture f;
  ConstraintSet set;
  const ConstraintId id =
      set.Add(ConstraintKind::kInputDelay, "din", "no_such_port", f.design);
  const Constraint* c = set.Find(id);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->status, BindingStatus::kUnresolved);
  EXPECT_TRUE(c->bound_object.is_null());
  EXPECT_EQ(c->target_spec, "no_such_port");
  EXPECT_EQ(set.all().size(), 1u);
  ASSERT_EQ(set.Unresolved().size(), 1u);
  EXPECT_EQ(set.Unresolved()[0]->id, id);
}

TEST(Constraints, EmptyTargetIsRecordedAsEmpty) {
  Fixture f;
  ConstraintSet set;
  const ConstraintId id =
      set.Add(ConstraintKind::kFalsePath, "fp", "", f.design);
  const Constraint* c = set.Find(id);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->status, BindingStatus::kEmpty);
}

TEST(Constraints, CoverageCountsEveryStatus) {
  Fixture f;
  ConstraintSet set;
  set.Add(ConstraintKind::kClock, "c1", "clk", f.design);
  set.Add(ConstraintKind::kInputDelay, "c2", "missing", f.design);
  set.Add(ConstraintKind::kFalsePath, "c3", "", f.design);
  const CoverageReport report = set.Coverage();
  EXPECT_EQ(report.total, 3u);
  EXPECT_EQ(report.bound, 1u);
  EXPECT_EQ(report.unresolved, 1u);
  EXPECT_EQ(report.empty, 1u);
  EXPECT_FALSE(report.complete());
}

TEST(Constraints, CoverageIsCompleteOnlyWhenAllBound) {
  Fixture f;
  ConstraintSet set;
  EXPECT_FALSE(set.Coverage().complete());
  set.Add(ConstraintKind::kClock, "c1", "clk", f.design);
  set.Add(ConstraintKind::kMaxDelay, "c2", "data_net", f.design);
  const CoverageReport report = set.Coverage();
  EXPECT_EQ(report.bound, 2u);
  EXPECT_TRUE(report.complete());
}

TEST(Constraints, RebindResolvesWhatTheDesignGained) {
  Fixture f;
  ConstraintSet set;
  const ConstraintId id =
      set.Add(ConstraintKind::kClock, "late_clk", "late_port", f.design);
  ASSERT_EQ(set.Find(id)->status, BindingStatus::kUnresolved);

  // The design gains the object (an import completing, a rename), and
  // the constraint binds on rebind, with no re-entry by the user.
  const ObjectId late =
      f.design.CreatePort("late_port", Direction::kInput).value();
  set.RebindAll(f.design);
  const Constraint* c = set.Find(id);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->status, BindingStatus::kBound);
  EXPECT_EQ(c->bound_object, late);
  EXPECT_TRUE(set.Coverage().complete());
}

}  // namespace
}  // namespace eos::db
