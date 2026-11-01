# rsz module provenance

Upstream: OpenROAD v2.0, commit a5a56584 (candidate tree
`openroad-v2.0-pin`), BSD-3-Clause. The imported files are
the R1 spike port copies (patch list P-0 through P-32, see
PATCHES.md), not verbatim upstream files; the table below
records the upstream SHA-256 for each file so the port
origin is auditable. All upstream SHAs below were
re-verified against the candidate tree at staging time.

## Upstream files (SHA-256 of the upstream file)

| EOS path | Upstream path | SHA-256 |
| --- | --- | --- |
| include/rsz/BufferedNet.hh | src/rsz/include/rsz/BufferedNet.hh | 4420c063c2c4b6f020ed818078f4abfc703e2fec57ea3476d4f2e2c111689674 |
| include/rsz/MakeResizer.hh | src/rsz/include/rsz/MakeResizer.hh | d4f6bb161d0e507bd68a18a576f58794ab866a52820fd3350396a510d3346dff |
| include/rsz/Resizer.hh | src/rsz/include/rsz/Resizer.hh | 02c8ea9772cb33ab6ad2d585f6d8a5dc40bdf47c3258d89f0572b8bf5ca80066 |
| include/rsz/SteinerTree.hh | src/rsz/include/rsz/SteinerTree.hh | 2b8a8a54cb0636a29ec2871be022cb8c6a03f0b6e3acfd2e0a0a7bee48f5eeb8 |
| src/BufferedNet.cc | src/rsz/src/BufferedNet.cc | deaa761fc9f87f8f46350a035289d2a1598cb67c480a6f314c0f1783ae125041 |
| src/EstimateWireParasitics.cc | src/rsz/src/EstimateWireParasitics.cc | 082077baf87ef62ccffcc75d21c078c62cb7e97966149e8b3ae448e96065d43d |
| src/MakeResizer.cc | src/rsz/src/MakeResizer.cc | 692797401d9018665db0be0c17723e07913d35be0915e583dfc4fafbb8d1f802 |
| src/Rebuffer.cc | src/rsz/src/Rebuffer.cc | b924ae9103991eea4d3b32b67e6c18a90ed70812150a35265e3b18f374bc6c65 |
| src/Resizer.cc | src/rsz/src/Resizer.cc | 0c3ddc514aab7c68e044db9a4f80af0fcecdc9b2d4637f9ddda6b5253559bedd |
| src/SteinerTree.cc | src/rsz/src/SteinerTree.cc | 7bb541cee2019069865c416ccfeba9f8ab4975eca8d20e9b0fad0bfd9be02426 |

## Adapted files (not verbatim; see PATCHES.md)

All ten files above are port copies with P-0 through P-32
applied as dispositioned in PATCHES.md. The only additional
import-time adaptation is SteinerTree.hh's
`#include "flute.h"` becoming `#include "pdr/flute.h"`
(public-header reachability).

## EOS-owned files (not upstream)

CMakeLists.txt, README.md, PATCHES.md, this file, and the
unit test under tests/unit/pd/rsz/.

## Deliberately not imported

Resizer.tcl, Resizer.i (upstream Tcl/SWIG surface; EOS
registry replaces it), Unused.cc (dead code), upstream
CMakeLists (GPL header comment; EOS writes its own
CMakeLists per module).

## Licence

BSD-3-Clause only. The import-time grep over the exact
PR3 import set finds no GPL text in any imported file.
No file under src/analysis/timing/sta is modified.
