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
