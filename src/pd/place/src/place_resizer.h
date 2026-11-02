// place_resizer: EOS-owned construction of the timing-driven
// resizer for the place runner, kept in its own translation unit
// on purpose. The v2.0 resizer (rsz/Resizer.hh) pulls in the
// pdr FLUTE header, which defines stt::Branch/stt::Tree in the
// old array-based form, while the ead3236 Steiner-tree header
// (stt/SteinerTreeBuilder.h) defines stt::Branch/stt::Tree in the
// new vector-based form. The two headers cannot coexist in one
// translation unit: including both is a hard compile error, not
// a warning. The place runner needs both engines (it constructs
// the Steiner-tree builder for the global router and the resizer
// for timing-driven placement), so the resizer construction lives
// here, in a unit that includes only the resizer headers, and the
// runner holds the result as an opaque pointer through these two
// functions. This is EOS-owned glue, not a patch to any upstream
// file and not a stand-in for the engine: the real resizer is
// constructed and initialized, just in a separate unit.
// See src/pd/place/PATCHES.md.
#pragma once

namespace odb {
class dbDatabase;
}  // namespace odb

namespace sta {
class dbSta;
}  // namespace sta

namespace utl {
class Logger;
}  // namespace utl

namespace rsz {
class Resizer;
}  // namespace rsz

namespace eos::pd {

// Constructs a resizer and initializes it for the given database
// and STA session (routing alpha 0.3, the global router's
// default). Returns nullptr only if construction fails; the
// caller owns the result and must pass it to
// DeleteTimingResizer.
rsz::Resizer* CreateTimingResizer(utl::Logger* logger,
                                  odb::dbDatabase* db, sta::dbSta* sta);

// Frees a resizer created by CreateTimingResizer.
void DeleteTimingResizer(rsz::Resizer* resizer);

}  // namespace eos::pd
