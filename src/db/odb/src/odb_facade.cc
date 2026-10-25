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

#include "opendb/db.h"
#include "opendb/defin.h"
#include "opendb/defout.h"
#include "opendb/lefin.h"
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
      lib = reader.createTechAndLib("eos_lib", path.c_str());
    } else {
      const std::string name = "eos_lib_" + std::to_string(impl_->lef_count);
      lib = reader.createLib(name.c_str(), path.c_str());
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
    odb::dbChip* chip = reader.createChip(libs, path.c_str());
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
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return IoError("could not read database file", path);
  }
  try {
    impl_->db->read(file);
    std::fclose(file);
  } catch (const std::exception&) {
    std::fclose(file);
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
  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    return IoError("could not write database file", path);
  }
  try {
    impl_->db->write(file);
    std::fclose(file);
  } catch (const std::exception&) {
    std::fclose(file);
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

}  // namespace eos::db
