// odb facade implementation: the only EOS-authored translation unit
// in eda_db_odb. Everything upstream of this file is imported code
// (see PROVENANCE.md); this file translates between the EOS surface
// in include/eos/db/odb.h and the upstream odb API, and converts
// upstream failures (null returns and exceptions) into EOS Status
// values, so no upstream type or exception crosses the boundary.
#include "eos/db/odb.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "odb/db.h"
#include "odb/defin.h"
#include "odb/defout.h"
#include "odb/lefin.h"
#include "utl/Logger.h"

namespace eos::db {
namespace {

using foundation::ErrorCode;

Status IoError(const std::string& what, const std::string& path) {
  return Status::Error(ErrorCode::kIoError, what + ": " + path);
}

// A plain Verilog identifier: [A-Za-z_][A-Za-z0-9_$]*.
bool IsPlainIdentifier(const std::string& name) {
  if (name.empty()) {
    return false;
  }
  const auto first_ok = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  };
  const auto rest_ok = [&first_ok](char c) {
    return first_ok(c) || (c >= '0' && c <= '9') || c == '$';
  };
  if (!first_ok(name[0])) {
    return false;
  }
  for (std::size_t i = 1; i < name.size(); ++i) {
    if (!rest_ok(name[i])) {
      return false;
    }
  }
  return true;
}

// Names in the database may carry DEF-style backslash escapes
// (a DEF reader keeps "net\[0\]" with its backslashes in the stored
// name). The name itself has no backslashes: an escape quotes the
// next character literally. Strip the escapes first, so the name
// written out matches the name every other format (SPEF, SDC,
// Verilog written by other tools) uses for the same object.
std::string UnescapedName(const std::string& name) {
  std::string out;
  for (std::size_t i = 0; i < name.size(); ++i) {
    if (name[i] == '\\' && i + 1 < name.size()) {
      ++i;
    }
    out += name[i];
  }
  return out;
}

// The name as a Verilog identifier: itself when plain, otherwise an
// escaped identifier (backslash, the name, a terminating space).
std::string VerilogName(const std::string& raw_name) {
  const std::string name = UnescapedName(raw_name);
  if (IsPlainIdentifier(name)) {
    return name;
  }
  return "\\" + name + " ";
}

bool IsSupply(odb::dbSigType type) { return type.isSupply(); }

// Parses a bus-bit name "base[N]" (already unescaped) into its
// base and index. Anything else (no brackets, empty base, a
// non-numeric or negative index, trailing characters) is not a
// bus bit.
std::optional<std::pair<std::string, long>> ParseBusBit(
    const std::string& name) {
  const std::size_t open = name.rfind('[');
  if (open == std::string::npos || open == 0 || name.back() != ']') {
    return std::nullopt;
  }
  const std::string digits = name.substr(open + 1, name.size() - open - 2);
  if (digits.empty()) {
    return std::nullopt;
  }
  long index = 0;
  for (const char c : digits) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    index = index * 10 + (c - '0');
  }
  return std::make_pair(name.substr(0, open), index);
}

}  // namespace

struct OdbDatabase::Impl {
  utl::Logger logger;
  odb::dbDatabase* db = nullptr;
  // The technology created by the first LEF read; later LEF reads
  // and DEF reads take it as an explicit argument in the ead3236
  // API, where a technology is a first-class object.
  odb::dbTech* tech = nullptr;
  int lef_count = 0;

  Impl() : db(odb::dbDatabase::create()) { db->setLogger(&logger); }
  ~Impl() {
    if (db != nullptr) {
      odb::dbDatabase::destroy(db);
    }
  }

  odb::dbBlock* TopBlock() const {
    if (db == nullptr || db->getChip() == nullptr) {
      return nullptr;
    }
    return db->getChip()->getBlock();
  }

  std::vector<odb::dbLib*> Libs() const {
    std::vector<odb::dbLib*> libs;
    for (odb::dbLib* lib : db->getLibs()) {
      libs.push_back(lib);
    }
    return libs;
  }

  odb::dbMaster* FindMaster(const std::string& name) const {
    for (odb::dbLib* lib : Libs()) {
      if (odb::dbMaster* master = lib->findMaster(name.c_str())) {
        return master;
      }
    }
    return nullptr;
  }
};

OdbDatabase::OdbDatabase() : impl_(new Impl) {}
OdbDatabase::~OdbDatabase() = default;
OdbDatabase::OdbDatabase(OdbDatabase&&) noexcept = default;
OdbDatabase& OdbDatabase::operator=(OdbDatabase&&) noexcept = default;

Status OdbDatabase::ReadLef(const std::string& path) {
  try {
    odb::lefin reader(impl_->db, &impl_->logger, false);
    odb::dbLib* lib = nullptr;
    if (impl_->lef_count == 0) {
      lib = reader.createTechAndLib("eos_tech", "eos_lib", path.c_str());
      if (lib != nullptr) {
        impl_->tech = lib->getTech();
        if (impl_->tech == nullptr) {
          impl_->tech = impl_->db->getTech();
        }
      }
    } else {
      const std::string name = "eos_lib_" + std::to_string(impl_->lef_count);
      lib = reader.createLib(impl_->tech, name.c_str(), path.c_str());
    }
    if (lib == nullptr) {
      return IoError("could not read LEF file", path);
    }
    ++impl_->lef_count;
    return Status::Ok();
  } catch (const std::exception&) {
    return IoError("could not read LEF file", path);
  }
}

Status OdbDatabase::ReadDef(const std::string& path) {
  if (impl_->lef_count == 0) {
    return Status::Error(ErrorCode::kInvalidState,
                         "read DEF before any LEF: " + path);
  }
  try {
    odb::defin reader(impl_->db, &impl_->logger);
    std::vector<odb::dbLib*> libs = impl_->Libs();
    odb::dbChip* chip = reader.createChip(libs, path.c_str(), impl_->tech);
    if (chip == nullptr || chip->getBlock() == nullptr) {
      return IoError("could not read DEF file", path);
    }
    return Status::Ok();
  } catch (const std::exception&) {
    return IoError("could not read DEF file", path);
  }
}

Status OdbDatabase::WriteDef(const std::string& path) const {
  odb::dbBlock* block = impl_->TopBlock();
  if (block == nullptr) {
    return Status::Error(ErrorCode::kInvalidState,
                         "write DEF before any DEF has been read: " + path);
  }
  try {
    odb::defout writer(&impl_->logger);
    if (!writer.writeBlock(block, path.c_str())) {
      return IoError("could not write DEF file", path);
    }
    return Status::Ok();
  } catch (const std::exception&) {
    return IoError("could not write DEF file", path);
  }
}

Status OdbDatabase::ReadDb(const std::string& path) {
  if (impl_->TopBlock() != nullptr || impl_->lef_count > 0) {
    return Status::Error(
        ErrorCode::kInvalidState,
        "read a database into a database that already holds content: " +
            path);
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return IoError("could not read database file", path);
  }
  try {
    impl_->db->read(file);
  } catch (const std::exception&) {
    return IoError("could not read database file", path);
  }
  if (impl_->TopBlock() == nullptr) {
    return IoError("database file holds no chip", path);
  }
  return Status::Ok();
}

Status OdbDatabase::WriteDb(const std::string& path) const {
  if (impl_->TopBlock() == nullptr) {
    return Status::Error(ErrorCode::kInvalidState,
                         "write a database before any has been read: " +
                             path);
  }
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return IoError("could not write database file", path);
  }
  try {
    impl_->db->write(file);
    file.close();
    if (!file) {
      return IoError("could not write database file", path);
    }
  } catch (const std::exception&) {
    return IoError("could not write database file", path);
  }
  return Status::Ok();
}

Status OdbDatabase::WriteVerilog(const std::string& path) const {
  odb::dbBlock* block = impl_->TopBlock();
  if (block == nullptr) {
    return Status::Error(
        ErrorCode::kInvalidState,
        "write Verilog before any DEF or database has been read: " + path);
  }
  try {
    // Ports: the block terms on signal nets. Power and ground terms
    // are not logical ports of the netlist.
    struct Port {
      std::string name;
      std::string net_name;
      odb::dbIoType::Value direction;
    };
    std::vector<Port> ports;
    std::set<std::string> port_nets;
    for (odb::dbBTerm* term : block->getBTerms()) {
      odb::dbNet* net = term->getNet();
      if (net == nullptr || IsSupply(net->getSigType()) ||
          IsSupply(term->getSigType())) {
        continue;
      }
      ports.push_back({term->getName(), net->getName(),
                       term->getIoType().getValue()});
      port_nets.insert(net->getName());
    }
    // Instances: signal connections only. An instance left with no
    // signal connection (filler, tap, decap and other physical-only
    // cells) is omitted, as this header's banner states.
    struct Connection {
      std::string pin;
      std::string net;
    };
    struct Instance {
      std::string master;
      std::string name;
      std::vector<Connection> connections;
    };
    std::vector<Instance> instances;
    std::set<std::string> used_nets;
    for (odb::dbInst* inst : block->getInsts()) {
      Instance out;
      out.master = inst->getMaster()->getName();
      out.name = inst->getName();
      for (odb::dbITerm* term : inst->getITerms()) {
        odb::dbMTerm* master_term = term->getMTerm();
        odb::dbNet* net = term->getNet();
        if (net == nullptr || IsSupply(net->getSigType()) ||
            IsSupply(master_term->getSigType())) {
          continue;
        }
        out.connections.push_back({master_term->getName(), net->getName()});
        used_nets.insert(net->getName());
      }
      if (!out.connections.empty()) {
        instances.push_back(std::move(out));
      }
    }

    // Bus-bit ports: a port named "base[N]" whose net carries the
    // same name is one bit of a bus, and it is written as a real
    // bus port ("output [31:0] base"), not as a scalar escaped
    // port, because parasitics formats resolve a port pin through
    // the bus: a scalar port named base[N] annotates nothing in
    // SPEF (measured on the Phase 0 gcd: 194 port pins unmatched).
    // Internal bus-bit nets stay escaped scalars; SPEF names those
    // as nets and they match as written. A group whose base is not
    // a plain identifier, or that collides with a scalar port of
    // the same name, falls back to scalar ports.
    struct BusGroup {
      std::string base;
      odb::dbIoType::Value direction;
      long lo = 0;
      long hi = 0;
    };
    std::map<std::size_t, std::size_t> group_of;  // port index -> group
    std::vector<BusGroup> groups;
    std::map<std::string, std::string> bit_expr;  // net name -> base[N]
    {
      std::map<std::pair<std::string, odb::dbIoType::Value>, std::size_t>
          by_base;
      std::set<std::string> scalar_names;
      for (const Port& port : ports) {
        if (!ParseBusBit(UnescapedName(port.name)).has_value()) {
          scalar_names.insert(UnescapedName(port.name));
        }
      }
      for (std::size_t i = 0; i < ports.size(); ++i) {
        const std::string name = UnescapedName(ports[i].name);
        const auto bit = ParseBusBit(name);
        if (!bit.has_value() || !IsPlainIdentifier(bit->first) ||
            scalar_names.count(bit->first) > 0) {
          continue;
        }
        // The grouping is stated for ports whose net is the port:
        // the port's own net must carry the same (unescaped) name.
        if (UnescapedName(ports[i].net_name) != name) {
          continue;
        }
        const auto key = std::make_pair(bit->first, ports[i].direction);
        const auto [it, inserted] = by_base.try_emplace(key, groups.size());
        if (inserted) {
          groups.push_back(
              BusGroup{bit->first, ports[i].direction, bit->second,
                       bit->second});
        } else {
          BusGroup& group = groups[it->second];
          group.lo = std::min(group.lo, bit->second);
          group.hi = std::max(group.hi, bit->second);
        }
        group_of[i] = it->second;
        bit_expr[name] = bit->first + "[" + std::to_string(bit->second) + "]";
      }
    }
    const auto net_expr = [&bit_expr](const std::string& raw) {
      const auto it = bit_expr.find(UnescapedName(raw));
      return it == bit_expr.end() ? VerilogName(raw) : it->second;
    };

    std::ofstream out(path);
    if (!out) {
      return IoError("could not write Verilog file", path);
    }
    out << "module " << VerilogName(block->getName()) << " (";
    {
      bool first = true;
      std::set<std::size_t> emitted_groups;
      for (std::size_t i = 0; i < ports.size(); ++i) {
        std::string header;
        const auto git = group_of.find(i);
        if (git != group_of.end()) {
          if (!emitted_groups.insert(git->second).second) {
            continue;
          }
          header = VerilogName(groups[git->second].base);
        } else {
          header = VerilogName(ports[i].name);
        }
        if (!first) {
          out << ", ";
        }
        first = false;
        out << header;
      }
    }
    out << ");\n";
    {
      std::set<std::size_t> declared_groups;
      for (std::size_t i = 0; i < ports.size(); ++i) {
        const char* direction = "inout";
        if (ports[i].direction == odb::dbIoType::INPUT) {
          direction = "input";
        } else if (ports[i].direction == odb::dbIoType::OUTPUT) {
          direction = "output";
        }
        const auto git = group_of.find(i);
        if (git != group_of.end()) {
          if (!declared_groups.insert(git->second).second) {
            continue;
          }
          const BusGroup& group = groups[git->second];
          out << "  " << direction << " [" << group.hi << ":" << group.lo
              << "] " << VerilogName(group.base) << ";\n";
        } else {
          out << "  " << direction << " " << VerilogName(ports[i].name)
              << ";\n";
        }
      }
    }
    // Wires for the referenced signal nets that are not ports (a
    // port name already declares its net).
    for (const std::string& net : used_nets) {
      if (port_nets.find(net) == port_nets.end()) {
        out << "  wire " << VerilogName(net) << ";\n";
      }
    }
    for (const Instance& inst : instances) {
      out << "  " << VerilogName(inst.master) << " "
          << VerilogName(inst.name) << " (";
      for (std::size_t i = 0; i < inst.connections.size(); ++i) {
        if (i > 0) {
          out << ", ";
        }
        out << "." << VerilogName(inst.connections[i].pin) << "("
            << net_expr(inst.connections[i].net) << ")";
      }
      out << ");\n";
    }
    out << "endmodule\n";
    out.close();
    if (!out) {
      return IoError("could not write Verilog file", path);
    }
    return Status::Ok();
  } catch (const std::exception&) {
    return IoError("could not write Verilog file", path);
  }
}

bool OdbDatabase::has_top_block() const { return impl_->TopBlock() != nullptr; }

std::string OdbDatabase::TopBlockName() const {
  odb::dbBlock* block = impl_->TopBlock();
  return block == nullptr ? std::string() : block->getName();
}

std::size_t OdbDatabase::InstanceCount() const {
  odb::dbBlock* block = impl_->TopBlock();
  return block == nullptr ? 0 : block->getInsts().size();
}

std::size_t OdbDatabase::NetCount() const {
  odb::dbBlock* block = impl_->TopBlock();
  return block == nullptr ? 0 : block->getNets().size();
}

std::size_t OdbDatabase::MasterCount() const {
  std::size_t total = 0;
  for (odb::dbLib* lib : impl_->Libs()) {
    total += lib->getMasters().size();
  }
  return total;
}

namespace {

Status NotFound(const std::string& what, const std::string& name) {
  return Status::Error(ErrorCode::kNotFound, what + " not found: " + name);
}

}  // namespace

std::vector<std::string> OdbDatabase::MasterNames() const {
  std::vector<std::string> names;
  for (odb::dbLib* lib : impl_->Libs()) {
    for (odb::dbMaster* master : lib->getMasters()) {
      names.push_back(master->getName());
    }
  }
  return names;
}

std::vector<OdbMasterPin> OdbDatabase::MasterPins(
    const std::string& master_name) const {
  std::vector<OdbMasterPin> pins;
  odb::dbMaster* master = impl_->FindMaster(master_name);
  if (master == nullptr) {
    return pins;
  }
  for (odb::dbMTerm* term : master->getMTerms()) {
    if (IsSupply(term->getSigType())) {
      continue;
    }
    OdbMasterPin pin;
    pin.name = term->getName();
    const odb::dbIoType io = term->getIoType();
    pin.is_input = io == odb::dbIoType::INPUT || io == odb::dbIoType::INOUT;
    pin.is_output = io == odb::dbIoType::OUTPUT || io == odb::dbIoType::INOUT;
    pins.push_back(pin);
  }
  return pins;
}

bool OdbDatabase::HasInstance(const std::string& name) const {
  OdbInstanceInfo info;
  return FindInstance(name, &info);
}

bool OdbDatabase::FindInstance(const std::string& name,
                               OdbInstanceInfo* out) const {
  odb::dbBlock* block = impl_->TopBlock();
  if (block == nullptr) {
    return false;
  }
  odb::dbInst* inst = block->findInst(name.c_str());
  if (inst == nullptr) {
    return false;
  }
  if (out != nullptr) {
    out->name = inst->getName();
    out->master = inst->getMaster()->getName();
    inst->getLocation(out->x, out->y);
  }
  return true;
}

std::vector<OdbInstanceInfo> OdbDatabase::Instances() const {
  std::vector<OdbInstanceInfo> instances;
  odb::dbBlock* block = impl_->TopBlock();
  if (block == nullptr) {
    return instances;
  }
  for (odb::dbInst* inst : block->getInsts()) {
    OdbInstanceInfo info;
    info.name = inst->getName();
    info.master = inst->getMaster()->getName();
    inst->getLocation(info.x, info.y);
    instances.push_back(info);
  }
  return instances;
}

std::vector<std::string> OdbDatabase::NetNames() const {
  std::vector<std::string> names;
  odb::dbBlock* block = impl_->TopBlock();
  if (block == nullptr) {
    return names;
  }
  for (odb::dbNet* net : block->getNets()) {
    names.push_back(net->getName());
  }
  return names;
}

bool OdbDatabase::HasNet(const std::string& name) const {
  odb::dbBlock* block = impl_->TopBlock();
  return block != nullptr && block->findNet(name.c_str()) != nullptr;
}

std::vector<OdbNetConnection> OdbDatabase::NetConnections(
    const std::string& net_name) const {
  std::vector<OdbNetConnection> connections;
  odb::dbBlock* block = impl_->TopBlock();
  if (block == nullptr) {
    return connections;
  }
  odb::dbNet* net = block->findNet(net_name.c_str());
  if (net == nullptr) {
    return connections;
  }
  for (odb::dbITerm* term : net->getITerms()) {
    OdbNetConnection connection;
    connection.instance = term->getInst()->getName();
    connection.pin = term->getMTerm()->getName();
    const odb::dbIoType io = term->getMTerm()->getIoType();
    connection.is_driver =
        io == odb::dbIoType::OUTPUT || io == odb::dbIoType::INOUT;
    connections.push_back(connection);
  }
  for (odb::dbBTerm* term : net->getBTerms()) {
    OdbNetConnection connection;
    connection.pin = term->getName();
    connection.is_port = true;
    connection.is_driver = term->getIoType() == odb::dbIoType::INPUT;
    connections.push_back(connection);
  }
  return connections;
}

Status OdbDatabase::SwapInstanceMaster(const std::string& instance,
                                       const std::string& new_master) {
  odb::dbBlock* block = impl_->TopBlock();
  if (block == nullptr) {
    return Status::Error(ErrorCode::kInvalidState,
                         "swap a master before any DEF has been read");
  }
  odb::dbInst* inst = block->findInst(instance.c_str());
  if (inst == nullptr) {
    return NotFound("instance", instance);
  }
  odb::dbMaster* master = impl_->FindMaster(new_master);
  if (master == nullptr) {
    return NotFound("master", new_master);
  }
  // The pin sets must match exactly (name and direction), so every
  // existing connection carries over by construction.
  const auto pin_set = [](odb::dbMaster* m) {
    std::set<std::pair<std::string, int>> pins;
    for (odb::dbMTerm* term : m->getMTerms()) {
      if (IsSupply(term->getSigType())) {
        continue;
      }
      pins.insert({term->getName(), term->getIoType().getValue()});
    }
    return pins;
  };
  if (pin_set(inst->getMaster()) != pin_set(master)) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "master " + new_master +
                             " does not carry the same signal pins as " +
                             inst->getMaster()->getName());
  }
  try {
    if (!inst->swapMaster(master)) {
      return Status::Error(ErrorCode::kInvalidState,
                           "master swap failed for instance " + instance);
    }
  } catch (const std::exception&) {
    return Status::Error(ErrorCode::kInvalidState,
                         "master swap failed for instance " + instance);
  }
  return Status::Ok();
}

Status OdbDatabase::CreateInstance(const std::string& name,
                                   const std::string& master_name, int x,
                                   int y) {
  odb::dbBlock* block = impl_->TopBlock();
  if (block == nullptr) {
    return Status::Error(ErrorCode::kInvalidState,
                         "create an instance before any DEF has been read");
  }
  if (block->findInst(name.c_str()) != nullptr) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "instance name already taken: " + name);
  }
  odb::dbMaster* master = impl_->FindMaster(master_name);
  if (master == nullptr) {
    return NotFound("master", master_name);
  }
  try {
    odb::dbInst* inst = odb::dbInst::create(block, master, name.c_str());
    if (inst == nullptr) {
      return Status::Error(ErrorCode::kInvalidState,
                           "could not create instance " + name);
    }
    inst->setLocation(x, y);
    inst->setPlacementStatus(odb::dbPlacementStatus::PLACED);
  } catch (const std::exception&) {
    return Status::Error(ErrorCode::kInvalidState,
                         "could not create instance " + name);
  }
  return Status::Ok();
}

Status OdbDatabase::CreateNet(const std::string& name) {
  odb::dbBlock* block = impl_->TopBlock();
  if (block == nullptr) {
    return Status::Error(ErrorCode::kInvalidState,
                         "create a net before any DEF has been read");
  }
  if (block->findNet(name.c_str()) != nullptr) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "net name already taken: " + name);
  }
  try {
    if (odb::dbNet::create(block, name.c_str()) == nullptr) {
      return Status::Error(ErrorCode::kInvalidState,
                           "could not create net " + name);
    }
  } catch (const std::exception&) {
    return Status::Error(ErrorCode::kInvalidState,
                         "could not create net " + name);
  }
  return Status::Ok();
}

Status OdbDatabase::ConnectInstancePin(const std::string& instance,
                                       const std::string& pin,
                                       const std::string& net_name) {
  odb::dbBlock* block = impl_->TopBlock();
  if (block == nullptr) {
    return Status::Error(ErrorCode::kInvalidState,
                         "connect a pin before any DEF has been read");
  }
  odb::dbInst* inst = block->findInst(instance.c_str());
  if (inst == nullptr) {
    return NotFound("instance", instance);
  }
  odb::dbITerm* term = inst->findITerm(pin.c_str());
  if (term == nullptr) {
    return NotFound("pin " + instance + "/", pin);
  }
  odb::dbNet* net = block->findNet(net_name.c_str());
  if (net == nullptr) {
    return NotFound("net", net_name);
  }
  if (term->getNet() != nullptr) {
    return Status::Error(ErrorCode::kInvalidState,
                         "pin " + instance + "/" + pin +
                             " is already connected; disconnect it first");
  }
  try {
    term->connect(net);
  } catch (const std::exception&) {
    return Status::Error(ErrorCode::kInvalidState,
                         "could not connect pin " + instance + "/" + pin);
  }
  return Status::Ok();
}

Status OdbDatabase::DisconnectInstancePin(const std::string& instance,
                                          const std::string& pin) {
  odb::dbBlock* block = impl_->TopBlock();
  if (block == nullptr) {
    return Status::Error(ErrorCode::kInvalidState,
                         "disconnect a pin before any DEF has been read");
  }
  odb::dbInst* inst = block->findInst(instance.c_str());
  if (inst == nullptr) {
    return NotFound("instance", instance);
  }
  odb::dbITerm* term = inst->findITerm(pin.c_str());
  if (term == nullptr) {
    return NotFound("pin " + instance + "/", pin);
  }
  if (term->getNet() == nullptr) {
    return Status::Error(ErrorCode::kInvalidState,
                         "pin " + instance + "/" + pin +
                             " is not connected");
  }
  try {
    term->disconnect();
  } catch (const std::exception&) {
    return Status::Error(ErrorCode::kInvalidState,
                         "could not disconnect pin " + instance + "/" + pin);
  }
  return Status::Ok();
}

Status OdbDatabase::DeleteInstance(const std::string& instance) {
  odb::dbBlock* block = impl_->TopBlock();
  if (block == nullptr) {
    return Status::Error(ErrorCode::kInvalidState,
                         "delete an instance before any DEF has been read");
  }
  odb::dbInst* inst = block->findInst(instance.c_str());
  if (inst == nullptr) {
    return NotFound("instance", instance);
  }
  try {
    // Copy the term list first: disconnecting mutates it.
    std::vector<odb::dbITerm*> terms;
    for (odb::dbITerm* term : inst->getITerms()) {
      terms.push_back(term);
    }
    for (odb::dbITerm* term : terms) {
      if (term->getNet() != nullptr) {
        term->disconnect();
      }
    }
    odb::dbInst::destroy(inst);
  } catch (const std::exception&) {
    return Status::Error(ErrorCode::kInvalidState,
                         "could not delete instance " + instance);
  }
  return Status::Ok();
}

Status OdbDatabase::DeleteNet(const std::string& net_name) {
  odb::dbBlock* block = impl_->TopBlock();
  if (block == nullptr) {
    return Status::Error(ErrorCode::kInvalidState,
                         "delete a net before any DEF has been read");
  }
  odb::dbNet* net = block->findNet(net_name.c_str());
  if (net == nullptr) {
    return NotFound("net", net_name);
  }
  try {
    std::vector<odb::dbITerm*> iterms;
    for (odb::dbITerm* term : net->getITerms()) {
      iterms.push_back(term);
    }
    for (odb::dbITerm* term : iterms) {
      term->disconnect();
    }
    std::vector<odb::dbBTerm*> bterms;
    for (odb::dbBTerm* term : net->getBTerms()) {
      bterms.push_back(term);
    }
    for (odb::dbBTerm* term : bterms) {
      term->disconnect();
    }
    odb::dbNet::destroy(net);
  } catch (const std::exception&) {
    return Status::Error(ErrorCode::kInvalidState,
                         "could not delete net " + net_name);
  }
  return Status::Ok();
}

}  // namespace eos::db
