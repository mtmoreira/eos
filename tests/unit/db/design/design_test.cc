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

// S6 backfill: every Find* accessor returns the record it was
// created from, and nullptr for an unknown id of that kind.
TEST(Design, FindAccessorsReturnEveryRecordKind) {
  Design design("top");
  const ObjectId cell = design.CreateCell("buf").value();
  const ObjectId inst = design.CreateInstance("u1", cell).value();
  const ObjectId net = design.CreateNet("n1").value();
  const ObjectId port = design.CreatePort("p", Direction::kOutput).value();
  const ObjectId pin = design.Connect(inst, "Y", net).value();
  const CellRecord* cell_rec = design.FindCell(cell);
  ASSERT_NE(cell_rec, nullptr);
  EXPECT_EQ(cell_rec->name, "buf");
  const InstanceRecord* inst_rec = design.FindInstance(inst);
  ASSERT_NE(inst_rec, nullptr);
  EXPECT_EQ(inst_rec->cell, cell);
  const NetRecord* net_rec = design.FindNet(net);
  ASSERT_NE(net_rec, nullptr);
  EXPECT_EQ(net_rec->name, "n1");
  const PortRecord* port_rec = design.FindPort(port);
  ASSERT_NE(port_rec, nullptr);
  EXPECT_EQ(port_rec->direction, Direction::kOutput);
  const TermRecord* term_rec = design.FindTerm(port_rec->term);
  ASSERT_NE(term_rec, nullptr);
  EXPECT_EQ(term_rec->port, port);
  const PinRecord* pin_rec = design.FindPin(pin);
  ASSERT_NE(pin_rec, nullptr);
  EXPECT_EQ(pin_rec->instance, inst);
  EXPECT_EQ(pin_rec->net, net);
  EXPECT_EQ(design.Count(foundation::ObjectKind::kPin), 1u);
  EXPECT_EQ(design.Count(foundation::ObjectKind::kTerm), 1u);
  const ObjectId missing =
      foundation::ObjectId::Make(foundation::ObjectKind::kCell, 999);
  EXPECT_EQ(design.FindCell(missing), nullptr);
  EXPECT_EQ(design.FindInstance(missing), nullptr);
  EXPECT_EQ(design.FindPort(missing), nullptr);
  EXPECT_EQ(design.FindTerm(missing), nullptr);
  EXPECT_EQ(design.FindPin(missing), nullptr);
}

// S6 backfill: ExportState/FromState round-trips every record
// kind and the allocator continues without reusing an id.
TEST(Design, StateRoundTripPreservesRecordsAndAllocator) {
  Design design("top");
  const ObjectId cell = design.CreateCell("buf").value();
  const ObjectId inst = design.CreateInstance("u1", cell).value();
  const ObjectId net = design.CreateNet("n1").value();
  const ObjectId port = design.CreatePort("p", Direction::kInput).value();
  const ObjectId pin = design.Connect(inst, "A", net).value();
  const std::uint32_t next_net =
      design.allocator().next_index(foundation::ObjectKind::kNet);
  const DesignState state = design.ExportState();
  Result<Design> restored = Design::FromState(state);
  ASSERT_TRUE(restored.ok()) << restored.status().message();
  Design& back = restored.value();
  EXPECT_EQ(back.name(), "top");
  EXPECT_EQ(back.FindCell(cell)->name, "buf");
  EXPECT_EQ(back.FindInstance(inst)->cell, cell);
  EXPECT_EQ(back.FindNet(net)->name, "n1");
  EXPECT_EQ(back.FindPort(port)->direction, Direction::kInput);
  EXPECT_EQ(back.FindPin(pin)->net, net);
  EXPECT_EQ(back.allocator().next_index(foundation::ObjectKind::kNet),
            next_net);
  const Result<ObjectId> fresh = back.CreateNet("n2");
  ASSERT_TRUE(fresh.ok());
  EXPECT_NE(fresh.value(), net);
}

}  // namespace
}  // namespace eos::db
