// fmt_compat.h: EOS-authored compatibility shim for the imported
// OpenDB code. This file is not upstream code and modifies no
// upstream file; the module CMake force-includes it into the
// eda_db_odb target only (see PROVENANCE.md).
//
// The 2021 upstream sources pass odb enum values (for example
// dbIoType::Value) to utl::Logger format calls. The fmt library of
// that era formatted enums as their underlying integer; fmt 9 and
// later (the fmt 10 in Ubuntu 24.04's libspdlog-dev, the fmt 11
// bundled with spdlog 1.15) refuse enum arguments without an
// explicit formatter. This partial specialization restores the
// original semantics for every enum type in this target: an enum
// formats exactly as its underlying integer, which is what the
// upstream log calls were written to produce.
#ifndef EOS_DB_ODB_FMT_COMPAT_H_
#define EOS_DB_ODB_FMT_COMPAT_H_

#include <type_traits>

#include "spdlog/spdlog.h"

namespace fmt {

template <typename T>
struct formatter<T, char, std::enable_if_t<std::is_enum<T>::value>>
    : formatter<std::underlying_type_t<T>, char> {
  template <typename FormatContext>
  auto format(T value, FormatContext& ctx) const {
    return formatter<std::underlying_type_t<T>, char>::format(
        static_cast<std::underlying_type_t<T>>(value), ctx);
  }
};

}  // namespace fmt

#endif  // EOS_DB_ODB_FMT_COMPAT_H_
