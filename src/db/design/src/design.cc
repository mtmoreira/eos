// design: the Design object graph (L2 design-data).
#include "eos/db/design.h"

#include <utility>

namespace eos::db {

Design::Design(std::string name) : name_(std::move(name)) {
  const Result<ObjectId> allocated = allocator_.Allocate(ObjectKind::kDesign);
  id_ = allocated.ok() ? allocated.value() : ObjectId();
}

Result<ObjectId> Design::CreateCell(const std::string& name, bool is_macro) {
  if (cell_names_.count(name) != 0) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "duplicate cell name: " + name);
  }
  const Result<ObjectId> allocated = allocator_.Allocate(ObjectKind::kCell);
  if (!allocated.ok()) {
    return allocated.status();
  }
  const ObjectId id = allocated.value();
  cell_by_id_[id.raw()] = cells_.size();
  cells_.push_back(CellRecord{id, name, is_macro});
  cell_names_[name] = id;
  return id;
}

Result<ObjectId> Design::CreateInstance(const std::string& name,
                                        ObjectId cell) {
  if (instance_names_.count(name) != 0) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "duplicate instance name: " + name);
  }
  if (cell.kind() != ObjectKind::kCell || FindCell(cell) == nullptr) {
    return Status::Error(ErrorCode::kNotFound,
                         "instance references an unknown cell");
  }
  const Result<ObjectId> allocated =
      allocator_.Allocate(ObjectKind::kInstance);
  if (!allocated.ok()) {
    return allocated.status();
  }
  const ObjectId id = allocated.value();
  instance_by_id_[id.raw()] = instances_.size();
  instances_.push_back(InstanceRecord{id, name, cell});
  instance_names_[name] = id;
  return id;
}

Result<ObjectId> Design::CreateNet(const std::string& name) {
  if (net_names_.count(name) != 0) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "duplicate net name: " + name);
  }
  const Result<ObjectId> allocated = allocator_.Allocate(ObjectKind::kNet);
  if (!allocated.ok()) {
    return allocated.status();
  }
  const ObjectId id = allocated.value();
  net_by_id_[id.raw()] = nets_.size();
  nets_.push_back(NetRecord{id, name});
  net_names_[name] = id;
  return id;
}

Result<ObjectId> Design::CreatePort(const std::string& name,
                                     Direction direction) {
  if (port_names_.count(name) != 0) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "duplicate port name: " + name);
  }
  const Result<ObjectId> port_alloc =
      allocator_.Allocate(ObjectKind::kPort);
  if (!port_alloc.ok()) {
    return port_alloc.status();
  }
  const Result<ObjectId> term_alloc =
      allocator_.Allocate(ObjectKind::kTerm);
  if (!term_alloc.ok()) {
    return term_alloc.status();
  }
  const ObjectId port_id = port_alloc.value();
  const ObjectId term_id = term_alloc.value();
  term_by_id_[term_id.raw()] = terms_.size();
  terms_.push_back(TermRecord{term_id, port_id});
  port_by_id_[port_id.raw()] = ports_.size();
  ports_.push_back(PortRecord{port_id, name, direction, term_id});
  port_names_[name] = port_id;
  return port_id;
}

Result<ObjectId> Design::Connect(ObjectId instance,
                                 const std::string& pin_name, ObjectId net) {
  const InstanceRecord* inst = FindInstance(instance);
  if (instance.kind() != ObjectKind::kInstance || inst == nullptr) {
    return Status::Error(ErrorCode::kNotFound, "connect: unknown instance");
  }
  if (net.kind() != ObjectKind::kNet || FindNet(net) == nullptr) {
    return Status::Error(ErrorCode::kNotFound, "connect: unknown net");
  }
  const std::string display = inst->name + "/" + pin_name;
  if (pin_names_.count(display) != 0) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "pin already connected: " + display);
  }
  const Result<ObjectId> allocated = allocator_.Allocate(ObjectKind::kPin);
  if (!allocated.ok()) {
    return allocated.status();
  }
  const ObjectId id = allocated.value();
  pin_by_id_[id.raw()] = pins_.size();
  pins_.push_back(PinRecord{id, display, instance, pin_name, net});
  pin_names_[display] = id;
  return id;
}

ObjectId Design::Find(ObjectKind kind, const std::string& name) const {
  const std::unordered_map<std::string, ObjectId>* map = nullptr;
  switch (kind) {
    case ObjectKind::kCell: map = &cell_names_; break;
    case ObjectKind::kInstance: map = &instance_names_; break;
    case ObjectKind::kNet: map = &net_names_; break;
    case ObjectKind::kPort: map = &port_names_; break;
    case ObjectKind::kPin: map = &pin_names_; break;
    default: return ObjectId();
  }
  const auto it = map->find(name);
  return it == map->end() ? ObjectId() : it->second;
}

bool Design::Exists(ObjectId id) const {
  switch (id.kind()) {
    case ObjectKind::kDesign: return id == id_;
    case ObjectKind::kCell: return FindCell(id) != nullptr;
    case ObjectKind::kInstance: return FindInstance(id) != nullptr;
    case ObjectKind::kNet: return FindNet(id) != nullptr;
    case ObjectKind::kPort: return FindPort(id) != nullptr;
    case ObjectKind::kTerm: return FindTerm(id) != nullptr;
    case ObjectKind::kPin: return FindPin(id) != nullptr;
    default: return false;
  }
}

std::string Design::NameOf(ObjectId id) const {
  switch (id.kind()) {
    case ObjectKind::kDesign: return id == id_ ? name_ : std::string();
    case ObjectKind::kCell: {
      const CellRecord* r = FindCell(id);
      return r ? r->name : std::string();
    }
    case ObjectKind::kInstance: {
      const InstanceRecord* r = FindInstance(id);
      return r ? r->name : std::string();
    }
    case ObjectKind::kNet: {
      const NetRecord* r = FindNet(id);
      return r ? r->name : std::string();
    }
    case ObjectKind::kPort: {
      const PortRecord* r = FindPort(id);
      return r ? r->name : std::string();
    }
    case ObjectKind::kPin: {
      const PinRecord* r = FindPin(id);
      return r ? r->name : std::string();
    }
    default: return std::string();
  }
}

std::size_t Design::Count(ObjectKind kind) const {
  switch (kind) {
    case ObjectKind::kCell: return cells_.size();
    case ObjectKind::kInstance: return instances_.size();
    case ObjectKind::kNet: return nets_.size();
    case ObjectKind::kPort: return ports_.size();
    case ObjectKind::kTerm: return terms_.size();
    case ObjectKind::kPin: return pins_.size();
    default: return 0;
  }
}

const CellRecord* Design::FindCell(ObjectId id) const {
  return FindIn(cells_, cell_by_id_, id);
}
const InstanceRecord* Design::FindInstance(ObjectId id) const {
  return FindIn(instances_, instance_by_id_, id);
}
const NetRecord* Design::FindNet(ObjectId id) const {
  return FindIn(nets_, net_by_id_, id);
}
const PortRecord* Design::FindPort(ObjectId id) const {
  return FindIn(ports_, port_by_id_, id);
}
const TermRecord* Design::FindTerm(ObjectId id) const {
  return FindIn(terms_, term_by_id_, id);
}
const PinRecord* Design::FindPin(ObjectId id) const {
  return FindIn(pins_, pin_by_id_, id);
}

}  // namespace eos::db
