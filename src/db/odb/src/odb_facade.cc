// odb facade implementation: the only EOS-authored translation unit
// in eda_db_odb. Everything upstream of this file is imported code
// (see PROVENANCE.md); this file translates between the EOS surface
// in include/eos/db/odb.h and the upstream odb API, and converts
// upstream failures (null returns and exceptions) into EOS Status
// values, so no upstream type or exception crosses the boundary.
#include "eos/db/odb.h"

#include <exception>
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
