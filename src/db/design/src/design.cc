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

Design::Design(std::string name, ObjectId id)
    : name_(std::move(name)), id_(id) {}

DesignState Design::ExportState() const {
  DesignState state;
  state.name = name_;
  state.id = id_;
  for (std::uint32_t k = 0; k < state.allocator_next.size(); ++k) {
    state.allocator_next[k] =
        allocator_.next_index(static_cast<ObjectKind>(k));
  }
  state.cells = cells_;
  state.instances = instances_;
  state.nets = nets_;
  state.ports = ports_;
  state.terms = terms_;
  state.pins = pins_;
  return state;
}

Result<Design> Design::FromState(const DesignState& state) {
  if (state.id.is_null() || state.id.kind() != ObjectKind::kDesign) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "design state has no valid design ID");
  }
  Design design(state.name, state.id);
  auto reject = [](const char* what) {
    return Status::Error(ErrorCode::kInvalidArgument, what);
  };
  // Cells.
  for (const CellRecord& r : state.cells) {
    if (r.id.kind() != ObjectKind::kCell ||
        design.cell_by_id_.count(r.id.raw()) != 0 ||
        design.cell_names_.count(r.name) != 0) {
      return reject("design state: bad or duplicate cell");
    }
    design.cell_by_id_[r.id.raw()] = design.cells_.size();
    design.cells_.push_back(r);
    design.cell_names_[r.name] = r.id;
  }
  // Instances.
  for (const InstanceRecord& r : state.instances) {
    if (r.id.kind() != ObjectKind::kInstance ||
        design.instance_by_id_.count(r.id.raw()) != 0 ||
        design.instance_names_.count(r.name) != 0 ||
        design.FindCell(r.cell) == nullptr) {
      return reject("design state: bad or duplicate instance");
    }
    design.instance_by_id_[r.id.raw()] = design.instances_.size();
    design.instances_.push_back(r);
    design.instance_names_[r.name] = r.id;
  }
  // Nets.
  for (const NetRecord& r : state.nets) {
    if (r.id.kind() != ObjectKind::kNet ||
        design.net_by_id_.count(r.id.raw()) != 0 ||
        design.net_names_.count(r.name) != 0) {
      return reject("design state: bad or duplicate net");
    }
    design.net_by_id_[r.id.raw()] = design.nets_.size();
    design.nets_.push_back(r);
    design.net_names_[r.name] = r.id;
  }
  // Terms first, then ports, so port to term pairing can be checked.
  for (const TermRecord& r : state.terms) {
    if (r.id.kind() != ObjectKind::kTerm ||
        design.term_by_id_.count(r.id.raw()) != 0) {
      return reject("design state: bad or duplicate term");
    }
    design.term_by_id_[r.id.raw()] = design.terms_.size();
    design.terms_.push_back(r);
  }
  for (const PortRecord& r : state.ports) {
    if (r.id.kind() != ObjectKind::kPort ||
        design.port_by_id_.count(r.id.raw()) != 0 ||
        design.port_names_.count(r.name) != 0 ||
        design.FindTerm(r.term) == nullptr) {
      return reject("design state: bad or duplicate port");
    }
    design.port_by_id_[r.id.raw()] = design.ports_.size();
    design.ports_.push_back(r);
    design.port_names_[r.name] = r.id;
  }
  for (const TermRecord& r : state.terms) {
    const PortRecord* port = design.FindPort(r.port);
    if (port == nullptr || port->term != r.id) {
      return reject("design state: term and port do not pair up");
    }
  }
  // Pins.
  for (const PinRecord& r : state.pins) {
    const InstanceRecord* inst = design.FindInstance(r.instance);
    if (r.id.kind() != ObjectKind::kPin ||
        design.pin_by_id_.count(r.id.raw()) != 0 || inst == nullptr ||
        design.FindNet(r.net) == nullptr ||
        r.name != inst->name + "/" + r.pin_name ||
        design.pin_names_.count(r.name) != 0) {
      return reject("design state: bad or duplicate pin");
    }
    design.pin_by_id_[r.id.raw()] = design.pins_.size();
    design.pins_.push_back(r);
    design.pin_names_[r.name] = r.id;
  }
  // Allocator: every kind's next index must be ahead of every used
  // index of that kind, so restored allocation can never reuse an ID.
  std::array<std::uint32_t, 8> max_used{};
  max_used[static_cast<std::uint32_t>(ObjectKind::kDesign)] =
      state.id.index();
  auto note = [&max_used](ObjectId id) {
    const auto k = static_cast<std::uint32_t>(id.kind());
    if (id.index() > max_used[k]) {
      max_used[k] = id.index();
    }
  };
  for (const CellRecord& r : state.cells) note(r.id);
  for (const InstanceRecord& r : state.instances) note(r.id);
  for (const NetRecord& r : state.nets) note(r.id);
  for (const PortRecord& r : state.ports) note(r.id);
  for (const TermRecord& r : state.terms) note(r.id);
  for (const PinRecord& r : state.pins) note(r.id);
  for (std::uint32_t k = 1; k < max_used.size(); ++k) {
    const auto kind = static_cast<ObjectKind>(k);
    const std::uint32_t next = state.allocator_next[k];
    if (next <= max_used[k] || next > ObjectId::kMaxIndex + 1) {
      return reject("design state: allocator index would reuse an ID");
    }
    const Status restored = design.allocator_.RestoreNext(kind, next);
    if (!restored.ok()) {
      return restored;
    }
  }
  return design;
}

}  // namespace eos::db
