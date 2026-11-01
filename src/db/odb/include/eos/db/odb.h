// odb: the EOS facade over the imported OpenDB database (L2
// design-data).
//
// OpenDB is adopted as EOS's physical database core: its code lives
// in this module as EOS's own code (see PROVENANCE.md) and builds as
// the eda_db_odb target. This header is the only surface other
// modules may use. It exposes no upstream odb types: callers create
// a database, read LEF and DEF into it, write DEF back out, and ask
// for the top block's name and its instance and net counts. The
// upstream database, its logger and every upstream object stay
// behind the implementation in src/odb_facade.cc.
//
// This header is a stable surface.
#ifndef EOS_DB_ODB_H_
#define EOS_DB_ODB_H_

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "eos/foundation/errors.h"

// Forward declaration for the raw-database accessor below: the
// facade itself never exposes upstream types in its API, except
// this one accessor, which exists so L4 engine modules can operate
// on the live database the session owns.
namespace odb {
class dbDatabase;
}  // namespace odb

namespace eos::db {

using foundation::Result;
using foundation::Status;

// One instance of the top block, as the ECO adapters see it: its
// name, its master's name, and its placed location in database
// units (the DEF distance units of the design).
struct OdbInstanceInfo {
  std::string name;
  std::string master;
  int x = 0;
  int y = 0;
};

// One signal pin of a master cell, with its direction.
struct OdbMasterPin {
  std::string name;
  bool is_input = false;
  bool is_output = false;
};

// One connection on a net. A top-level port connection has an
// empty instance name and is_port set; is_driver is set for the
// connections that drive the net (an instance output pin, or a
// top-level input port).
struct OdbNetConnection {
  std::string instance;
  std::string pin;
  bool is_port = false;
  bool is_driver = false;
};

// One physical database. Move-only; the destructor releases the
// upstream database. ReadLef may be called more than once (each call
// adds a library); ReadDef creates the chip and its top block from
// the libraries read so far. WriteDef writes the top block created
// by ReadDef. Counts describe the top block, or zero when no DEF
// has been read.
//
// ReadDb and WriteDb handle the database's own binary form (the
// .odb files implementation flows save and reopen). ReadDb loads a
// whole database (technology, libraries and chip) into an empty
// facade; WriteDb saves the whole database. WriteVerilog writes the
// top block as a flat gate-level Verilog netlist, for consumers
// (the timing service) whose input is a netlist: power and ground
// connections are omitted, and so are instances with no signal
// connection at all (filler, tap and decap cells), because neither
// can carry a timing arc and the physical-only cells are usually
// absent from the Liberty library. Names that are not plain
// Verilog identifiers are written as escaped identifiers, and
// DEF-style backslash escapes stored in database names are
// normalized away first, so the netlist names objects exactly as
// the other formats (SPEF, SDC) name them. A port named "base[N]"
// whose net carries the same name is written as one bit of a real
// bus port ("output [31:0] base"), because parasitics formats
// resolve port pins through the bus; internal bus-bit nets stay
// escaped scalars, which is how those formats name nets.
class OdbDatabase {
 public:
  OdbDatabase();
  ~OdbDatabase();

  OdbDatabase(const OdbDatabase&) = delete;
  OdbDatabase& operator=(const OdbDatabase&) = delete;
  OdbDatabase(OdbDatabase&&) noexcept;
  OdbDatabase& operator=(OdbDatabase&&) noexcept;

  // Reads one LEF file as a new technology plus library. Fails with
  // kIoError when the file cannot be read or does not parse.
  Status ReadLef(const std::string& path);
  // Reads one DEF file, creating the chip and top block. Fails with
  // kIoError when the file cannot be read or does not parse, and
  // with kInvalidState when no LEF has been read yet.
  Status ReadDef(const std::string& path);
  // Writes the top block as DEF. Fails with kInvalidState when no
  // DEF has been read, and with kIoError when the file cannot be
  // written.
  Status WriteDef(const std::string& path) const;
  // Reads a whole database from its binary form. Fails with
  // kInvalidState when this database already holds a chip or any
  // LEF library, and with kIoError when the file cannot be read or
  // does not parse (including a file written by an incompatible
  // database version).
  Status ReadDb(const std::string& path);
  // Writes the whole database in its binary form. Fails with
  // kInvalidState when no DEF or database has been read, and with
  // kIoError when the file cannot be written.
  Status WriteDb(const std::string& path) const;
  // Writes the top block as a flat gate-level Verilog netlist, per
  // the rules in this header's banner. Fails with kInvalidState
  // when no DEF or database has been read, and with kIoError when
  // the file cannot be written.
  Status WriteVerilog(const std::string& path) const;

  bool has_top_block() const;
  // Raw access to the live upstream database, for L4 engine
  // modules that must mutate it in place (place-and-route unit
  // P1's floorplan runner is the first). All other consumers use
  // the facade methods; a raw pointer must never outlive this
  // object, and engines must not hold it past their run.
  odb::dbDatabase* RawDatabase() const;
  std::string TopBlockName() const;
  std::size_t InstanceCount() const;
  std::size_t NetCount() const;
  // Masters (cells) across every library read so far.
  std::size_t MasterCount() const;

  // ECO support (Phase 2 unit 2.4): the queries and mutations the
  // ECO adapters are built on. Queries return empty results for
  // unknown names; mutations report the failure as a Status and
  // change nothing on failure. Nothing here exposes an upstream
  // type, and a mutation never renames or deletes an object.
  std::vector<std::string> MasterNames() const;
  // The signal pins of one master, in master order. Empty when the
  // master is unknown.
  std::vector<OdbMasterPin> MasterPins(const std::string& master) const;
  bool HasInstance(const std::string& name) const;
  bool FindInstance(const std::string& name, OdbInstanceInfo* out) const;
  std::vector<OdbInstanceInfo> Instances() const;
  bool HasNet(const std::string& name) const;
  // The names of every net in the top block, in database order.
  // Empty when no DEF has been read. Added for the closure loop
  // (Phase 2 unit 2.5), which enumerates nets to propose buffer
  // candidates the way Instances() enumerates instances for sizing.
  std::vector<std::string> NetNames() const;
  // The signal connections of one net, ports included. Empty when
  // the net is unknown.
  std::vector<OdbNetConnection> NetConnections(
      const std::string& net_name) const;

  // Swaps an instance to another master. Fails with kNotFound for
  // an unknown instance or master, and with kInvalidArgument when
  // the two masters do not carry the same signal pin names and
  // directions (a swap must preserve every connection by
  // construction, so a pin mismatch is refused, never adapted).
  Status SwapInstanceMaster(const std::string& instance,
                            const std::string& new_master);
  // Creates a placed instance of a master at the given location
  // (database units). Fails with kNotFound for an unknown master
  // and with kInvalidArgument when the name is taken.
  Status CreateInstance(const std::string& name, const std::string& master,
                        int x, int y);
  // Creates a signal net. Fails with kInvalidArgument when the
  // name is taken.
  Status CreateNet(const std::string& name);
  // Connects one instance pin to a net. Fails with kNotFound for
  // an unknown instance, pin or net, and with kInvalidState when
  // the pin is already connected (disconnect it first).
  Status ConnectInstancePin(const std::string& instance,
                            const std::string& pin,
                            const std::string& net_name);
  // Disconnects one instance pin from its net. Fails with
  // kNotFound for an unknown instance or pin, and with
  // kInvalidState when the pin is not connected.
  Status DisconnectInstancePin(const std::string& instance,
                               const std::string& pin);
  // Deletes one instance, disconnecting every pin first. Added for
  // the resynthesis adapter (Phase 2 unit 2.6), which splices a
  // remapped cone into the design: the cone's old instances are
  // deleted and the remapped ones created in their place. Fails
  // with kNotFound for an unknown instance. Nets the instance was
  // connected to are left in place (possibly undriven or
  // unloadable afterwards); deleting a net is DeleteNet's job.
  Status DeleteInstance(const std::string& instance);
  // Deletes one net, disconnecting every pin (instance or port)
  // connected to it first. Fails with kNotFound for an unknown net.
  Status DeleteNet(const std::string& net_name);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace eos::db

#endif  // EOS_DB_ODB_H_
