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

#include "eos/foundation/errors.h"

namespace eos::db {

using foundation::Status;

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
  std::string TopBlockName() const;
  std::size_t InstanceCount() const;
  std::size_t NetCount() const;
  // Masters (cells) across every library read so far.
  std::size_t MasterCount() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace eos::db

#endif  // EOS_DB_ODB_H_
