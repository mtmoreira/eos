// design: the Design object graph (L2 design-data).
//
// The design is the set of objects every other layer refers to:
// cells, instances, nets, ports, terms and pins, each with a persistent
// ID from the L0 allocator. Names are unique per kind within a design
// (the v1 model is flat; hierarchy arrives with Phase 4 expansion and
// will scope names then, without changing IDs).
//
// This module stores and checks consistency only. It runs no algorithm
// and mutates nothing on its own: callers (L4 engines, through L5
// transactions) create objects, this module guarantees the graph stays
// well-formed: no duplicate names, no dangling references, no pin
// created twice.
//
// Scope note: objects are append-only in this increment. Deletion, with
// its invalidation consequences, arrives with the ECO work in Phase 2.
//
// This header is a stable surface.
#ifndef EOS_DB_DESIGN_H_
#define EOS_DB_DESIGN_H_

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "eos/foundation/errors.h"
#include "eos/foundation/ids.h"

namespace eos::db {

using foundation::ErrorCode;
using foundation::ObjectId;
using foundation::ObjectKind;
using foundation::Result;
using foundation::Status;

enum class Direction : std::int32_t {
  kInput = 0,
  kOutput = 1,
  kInout = 2,
};

struct CellRecord {
  ObjectId id;
  std::string name;
  bool is_macro = false;
};

struct InstanceRecord {
  ObjectId id;
  std::string name;
  ObjectId cell;  // the cell this is an instance of
};

struct NetRecord {
  ObjectId id;
  std::string name;
};

struct PortRecord {
  ObjectId id;
  std::string name;
  Direction direction = Direction::kInout;
  ObjectId term;  // the physical terminal paired with this port
};

struct TermRecord {
  ObjectId id;
  ObjectId port;  // the port this terminal belongs to
};

struct PinRecord {
  ObjectId id;
  std::string name;  // "<instance>/<pin>", the display and lookup name
  ObjectId instance;
  std::string pin_name;
  ObjectId net;
};

// The complete serializable state of a Design: every record plus the
// allocator's next index per kind (index 0 of the array is unused;
// kinds are ObjectKind values 1 to 7). The checkpoint module writes
// this; FromState validates it fully before building anything.
struct DesignState {
  std::string name;
  ObjectId id;
  std::array<std::uint32_t, 8> allocator_next{};
  std::vector<CellRecord> cells;
  std::vector<InstanceRecord> instances;
  std::vector<NetRecord> nets;
  std::vector<PortRecord> ports;
  std::vector<TermRecord> terms;
  std::vector<PinRecord> pins;
};

class Design {
 public:
  explicit Design(std::string name);

  ObjectId id() const { return id_; }
  const std::string& name() const { return name_; }

  // Creation. Each fails with kInvalidArgument on a duplicate name
  // (per kind), and with kNotFound or kInvalidArgument when a
  // referenced object does not exist or has the wrong kind.
  Result<ObjectId> CreateCell(const std::string& name, bool is_macro = false);
  Result<ObjectId> CreateInstance(const std::string& name, ObjectId cell);
  Result<ObjectId> CreateNet(const std::string& name);
  // Creating a port also creates its paired term.
  Result<ObjectId> CreatePort(const std::string& name, Direction direction);
  // Connects one pin of an instance to a net.
  Result<ObjectId> Connect(ObjectId instance, const std::string& pin_name,
                           ObjectId net);

  // Lookup. Find returns a null ObjectId when absent; Exists and the
  // record accessors return false or nullptr for unknown IDs.
  ObjectId Find(ObjectKind kind, const std::string& name) const;
  bool Exists(ObjectId id) const;
  std::string NameOf(ObjectId id) const;
  std::size_t Count(ObjectKind kind) const;

  const CellRecord* FindCell(ObjectId id) const;
  const InstanceRecord* FindInstance(ObjectId id) const;
  const NetRecord* FindNet(ObjectId id) const;
  const PortRecord* FindPort(ObjectId id) const;
  const TermRecord* FindTerm(ObjectId id) const;
  const PinRecord* FindPin(ObjectId id) const;

  // The allocator is part of checkpoint state (checkpoint/restore must
  // continue allocation without reuse), so it is exposed read/write.
  foundation::IdAllocator& allocator() { return allocator_; }
  const foundation::IdAllocator& allocator() const { return allocator_; }

  // Checkpoint support: ExportState snapshots everything FromState
  // needs; FromState validates the snapshot (unique IDs and names,
  // kind-correct IDs, no dangling references, allocator indices ahead
  // of every used index) and only then builds. A bad snapshot is an
  // error, never a half-built design.
  DesignState ExportState() const;
  static Result<Design> FromState(const DesignState& state);

 private:
  // Builds a design with a given identity instead of allocating one;
  // only FromState uses this, after validation.
  Design(std::string name, ObjectId id);
  template <typename Record>
  const Record* FindIn(const std::vector<Record>& records,
                       const std::unordered_map<std::uint32_t, std::size_t>&
                           by_id,
                       ObjectId id) const {
    const auto it = by_id.find(id.raw());
    if (it == by_id.end()) {
      return nullptr;
    }
    return &records[it->second];
  }

  std::string name_;
  ObjectId id_;
  foundation::IdAllocator allocator_;

  std::vector<CellRecord> cells_;
  std::vector<InstanceRecord> instances_;
  std::vector<NetRecord> nets_;
  std::vector<PortRecord> ports_;
  std::vector<TermRecord> terms_;
  std::vector<PinRecord> pins_;

  std::unordered_map<std::string, ObjectId> cell_names_;
  std::unordered_map<std::string, ObjectId> instance_names_;
  std::unordered_map<std::string, ObjectId> net_names_;
  std::unordered_map<std::string, ObjectId> port_names_;
  std::unordered_map<std::string, ObjectId> pin_names_;

  std::unordered_map<std::uint32_t, std::size_t> cell_by_id_;
  std::unordered_map<std::uint32_t, std::size_t> instance_by_id_;
  std::unordered_map<std::uint32_t, std::size_t> net_by_id_;
  std::unordered_map<std::uint32_t, std::size_t> port_by_id_;
  std::unordered_map<std::uint32_t, std::size_t> term_by_id_;
  std::unordered_map<std::uint32_t, std::size_t> pin_by_id_;
};

}  // namespace eos::db

#endif  // EOS_DB_DESIGN_H_
