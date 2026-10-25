// Unit tests: db/design.
#include "eos/db/design.h"

#include <gtest/gtest.h>

namespace eos::db {
namespace {

TEST(Design, CarriesItsOwnPersistentIdAndName) {
  Design design("gcd");
  EXPECT_EQ(design.name(), "gcd");
  EXPECT_EQ(design.id().kind(), foundation::ObjectKind::kDesign);
  EXPECT_FALSE(design.id().is_null());
  EXPECT_TRUE(design.Exists(design.id()));
  EXPECT_EQ(design.NameOf(design.id()), "gcd");
}

TEST(Design, CreatesObjectsWithPersistentIdsOfTheRightKind) {
  Design design("top");
  const Result<ObjectId> cell = design.CreateCell("bufx2");
  const Result<ObjectId> inst = design.CreateInstance("u1", cell.value());
  const Result<ObjectId> net = design.CreateNet("n1");
  const Result<ObjectId> port = design.CreatePort("clk", Direction::kInput);
  ASSERT_TRUE(cell.ok());
  ASSERT_TRUE(inst.ok());
  ASSERT_TRUE(net.ok());
  ASSERT_TRUE(port.ok());
  EXPECT_EQ(cell.value().kind(), foundation::ObjectKind::kCell);
  EXPECT_EQ(inst.value().kind(), foundation::ObjectKind::kInstance);
  EXPECT_EQ(net.value().kind(), foundation::ObjectKind::kNet);
  EXPECT_EQ(port.value().kind(), foundation::ObjectKind::kPort);
  EXPECT_EQ(design.Count(foundation::ObjectKind::kCell), 1u);
  EXPECT_EQ(design.Count(foundation::ObjectKind::kInstance), 1u);
  EXPECT_EQ(design.Count(foundation::ObjectKind::kNet), 1u);
  EXPECT_EQ(design.Count(foundation::ObjectKind::kPort), 1u);
}

TEST(Design, PortCreationPairsATerm) {
  Design design("top");
  const Result<ObjectId> port = design.CreatePort("data_in", Direction::kInput);
  ASSERT_TRUE(port.ok());
  const PortRecord* rec = design.FindPort(port.value());
  ASSERT_NE(rec, nullptr);
  EXPECT_EQ(rec->direction, Direction::kInput);
  ASSERT_FALSE(rec->term.is_null());
  const TermRecord* term = design.FindTerm(rec->term);
  ASSERT_NE(term, nullptr);
  EXPECT_EQ(term->port, port.value());
  EXPECT_EQ(design.Count(foundation::ObjectKind::kTerm), 1u);
}

TEST(Design, DuplicateNamesAreRejectedPerKind) {
  Design design("top");
  ASSERT_TRUE(design.CreateNet("n1").ok());
  const Result<ObjectId> dup = design.CreateNet("n1");
  ASSERT_FALSE(dup.ok());
  EXPECT_EQ(dup.status().code(), ErrorCode::kInvalidArgument);
  // The same name in a different kind is a different object.
  const Result<ObjectId> cell = design.CreateCell("n1");
  EXPECT_TRUE(cell.ok());
}

TEST(Design, InstanceRequiresAKnownCell) {
  Design design("top");
  const ObjectId bogus =
      foundation::ObjectId::Make(foundation::ObjectKind::kCell, 77);
  const Result<ObjectId> inst = design.CreateInstance("u1", bogus);
  ASSERT_FALSE(inst.ok());
  EXPECT_EQ(inst.status().code(), ErrorCode::kNotFound);
}

TEST(Design, ConnectCreatesAPinBetweenInstanceAndNet) {
  Design design("top");
  const ObjectId cell = design.CreateCell("dff").value();
  const ObjectId inst = design.CreateInstance("u1", cell).value();
  const ObjectId net = design.CreateNet("clk_net").value();
  const Result<ObjectId> pin = design.Connect(inst, "CK", net);
  ASSERT_TRUE(pin.ok());
  EXPECT_EQ(pin.value().kind(), foundation::ObjectKind::kPin);
  const PinRecord* rec = design.FindPin(pin.value());
  ASSERT_NE(rec, nullptr);
  EXPECT_EQ(rec->instance, inst);
  EXPECT_EQ(rec->net, net);
  EXPECT_EQ(rec->pin_name, "CK");
  EXPECT_EQ(rec->name, "u1/CK");
  EXPECT_EQ(design.Find(foundation::ObjectKind::kPin, "u1/CK"), pin.value());

  const Result<ObjectId> again = design.Connect(inst, "CK", net);
  ASSERT_FALSE(again.ok());
  EXPECT_EQ(again.status().code(), ErrorCode::kInvalidArgument);
}

TEST(Design, ConnectValidatesBothEnds) {
  Design design("top");
  const ObjectId cell = design.CreateCell("dff").value();
  const ObjectId inst = design.CreateInstance("u1", cell).value();
  const Result<ObjectId> bad_net = design.Connect(inst, "CK", cell);
  ASSERT_FALSE(bad_net.ok());
  EXPECT_EQ(bad_net.status().code(), ErrorCode::kNotFound);
  const ObjectId net = design.CreateNet("n").value();
  const Result<ObjectId> bad_inst = design.Connect(net, "CK", net);
  ASSERT_FALSE(bad_inst.ok());
}

TEST(Design, LookupByNameAndIdAgree) {
  Design design("top");
  const ObjectId net = design.CreateNet("reset_n").value();
  EXPECT_EQ(design.Find(foundation::ObjectKind::kNet, "reset_n"), net);
  EXPECT_TRUE(design.Find(foundation::ObjectKind::kNet, "missing").is_null());
  EXPECT_EQ(design.NameOf(net), "reset_n");
  EXPECT_TRUE(design.Exists(net));
  const ObjectId unknown =
      foundation::ObjectId::Make(foundation::ObjectKind::kNet, 999);
  EXPECT_FALSE(design.Exists(unknown));
  EXPECT_EQ(design.NameOf(unknown), "");
  EXPECT_EQ(design.FindNet(unknown), nullptr);
}

}  // namespace
}  // namespace eos::db
