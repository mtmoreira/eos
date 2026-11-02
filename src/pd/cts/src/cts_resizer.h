// cts_resizer: the resizer construction for the CTS runner, kept
// in its own unit for the same reason as place's place_resizer:
// this unit includes only the resizer headers, never the ead3236
// Steiner-tree header, so the pdr FLUTE definitions the resizer
// pulls in can never collide with the ead3236 Steiner-tree
// definitions in the runner unit. The runner holds the result as
// an opaque pointer. No upstream file is changed, no engine is
// stubbed: the real resizer is constructed and initialized, just
// in a separate unit.
#ifndef EOS_PD_CTS_RESIZER_H_
#define EOS_PD_CTS_RESIZER_H_

namespace utl {
class Logger;
}  // namespace utl

namespace odb {
class dbDatabase;
}  // namespace odb

namespace sta {
class dbSta;
}  // namespace sta

namespace rsz {
class Resizer;
}  // namespace rsz

namespace eos::pd {

// Constructs and initializes a resizer for the CTS runner's STA
// session. The caller owns the result and frees it with
// DeleteCtsResizer on every exit path, including engine
// exceptions.
rsz::Resizer* CreateCtsResizer(utl::Logger* logger, odb::dbDatabase* db,
                               sta::dbSta* sta);
void DeleteCtsResizer(rsz::Resizer* resizer);

}  // namespace eos::pd

#endif  // EOS_PD_CTS_RESIZER_H_
