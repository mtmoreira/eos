# odb (L2 design-data)

OpenDB, adopted as the physical database core under the EOS design
API.

Status: re-pinned 2026-10-28 (fix track unit REPIN) to OpenROAD
commit ead3236e56d57362a8dc8b25658fb41a191eba17, the OpenROAD pin
of ORFS v3.0; see PROVENANCE.md for the exact commit, the file
inventory, the exclusions and the patch list (zero patches). The
previous pin was OpenROAD v2.0, imported 2026-10-25 (Phase 2 unit
2.1); the re-pin replaces the module tree wholesale, and EOS
WriteDb checkpoint files move from schema minor 51 to minor 74
with it (old checkpoints do not survive; none are stored).

The stable public API of this module lives in include/eos/db/odb.h:
an OdbDatabase is created empty, LEF files are read as technology
plus libraries, a DEF file is read into a chip and top block, the
top block can be written back out as DEF, a whole database can be
read and written in its binary form, and the facade reports the
top block name and its instance and net counts. No upstream odb
type appears in the facade; nothing above L2 calls upstream types
directly. The upstream public headers are available inside the
module under include/odb/, and the upstream Logger under
include/utl/, for the module's own implementation files.
