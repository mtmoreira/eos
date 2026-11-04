# pdn: provenance

Upstream: OpenROAD at commit ead3236e56d57 (the same commit that supplied the in-tree OpenDB), imported as EOS-owned code under unit P7 (finish) of the place-and-route phase, as the P7 support import for the basic power grid (see src/pd/finish). The pin tree itself is never modified; the files below are copies into this module, with the port adaptations listed in PATCHES.md. No git submodule, no external process at runtime.

## Licence audit (run at import time, 2026-11-03)

Every file in the import set was grepped for licence text (GNU/General Public/Lesser/Affero/Mozilla/Eclipse/Apache/MIT/Boost phrases, case-insensitive, plus SPDX identifiers). Result: BSD-3-Clause only: every imported file carries the Regents of the University of California BSD 3-Clause header (pdn ships no separate LICENSE file upstream; the per-file headers are the licence). No GPL, LGPL, AGPL, MPL, EPL, MIT, or Apache text was found in any imported file. The string "boost" appears only as C++ #include lines for the Boost headers (geometry, geometry/index/rtree), which are a declared system dependency (see third_party_manifest.yaml and cmake/EosDependencies.cmake), not vendored licence text. The upstream Tcl/SWIG factory (MakePdnGen.cc, PdnGen.i, PdnGen-py.i, pdn.tcl) and all tests are not imported (see PATCHES.md), so no other licence surface enters with this module.

## Imported files (SHA-256 of the upstream file at ead3236)

| EOS path (relative to src/pd/pdn) | Upstream path (relative to OpenROAD) | SHA-256 |
|---|---|---|
| include/pdn/PdnGen.hh | src/pdn/include/pdn/PdnGen.hh | a2136027bb1357402a14ea154d22c37571c5434b19cccfad554ad03e6e62bd30 |
| src/PdnGen.cc | src/pdn/src/PdnGen.cc | 4fef45dddc498491331d9296f4cf7c7b824b0a5a39836180d8d1a24d44e80e97 |
| src/connect.cpp | src/pdn/src/connect.cpp | cdc596ebfd99e98e8b4b19240aa0e228ecac3d5e5195f78506237a8bb8b09565 |
| src/connect.h | src/pdn/src/connect.h | 15b967a4ab5d445116cb5dce5c51c3564fead9da9a3790567c6e2000199a1d71 |
| src/domain.cpp | src/pdn/src/domain.cpp | 4fff129cf02637d6ed3c776e801750094105ceaecc56177a2bb03ef37e926129 |
| src/domain.h | src/pdn/src/domain.h | 2a6e309ae9b63d0f6123448350bad45512827df568aa9c7167ebaa8f35e1a496 |
| src/grid.cpp | src/pdn/src/grid.cpp | d99fae751ce6505f4e771b9836969e6639410488e0aa5037ef8378a82aa8e12f |
| src/grid.h | src/pdn/src/grid.h | bd3552e36e78edb644202a3e9e08b7d1dd121aa013d111a506261b0713cdab76 |
| src/grid_component.cpp | src/pdn/src/grid_component.cpp | d3c4938601d6f457e874e75f154be83b612afe22068f2344e5f48b511aed32f9 |
| src/grid_component.h | src/pdn/src/grid_component.h | b000b9daec7a79b1ffb2fe001b6777d1f794e9a23b1dcd363e4e85a2b0551cf9 |
| src/power_cells.cpp | src/pdn/src/power_cells.cpp | 8cd9ab95ae3a725324dff09496818b6d48fbff544ace241d225de02a7f70abcb |
| src/power_cells.h | src/pdn/src/power_cells.h | 1c7a9287fb8e578cd88057976d8a3ad5bb5529ac0d672661c3427904c63f833b |
| src/renderer.cpp | src/pdn/src/renderer.cpp | db11d5b8148188a689aab084609e1f187ba6cefd66838c99ed9ed32c9f1432d8 |
| src/renderer.h | src/pdn/src/renderer.h | 49aa05186b42f514abbc9fcacc96ef55a8f40d8f252de245c71dcc0e69eebd8f |
| src/rings.cpp | src/pdn/src/rings.cpp | cc45c0dbfb3228cde276a1a203f514a2a07cf6744a7b4acae7a6558db8c45760 |
| src/rings.h | src/pdn/src/rings.h | c27bc9c76e4e301c49e15a4314c38ad4bc5dd4bd9744684162956b3aba69a156 |
| src/shape.cpp | src/pdn/src/shape.cpp | d85bf569147af95e11eb82edaac01aecdaab4b8b7dd4a214d18f96ac32df8ae2 |
| src/shape.h | src/pdn/src/shape.h | 328a2b4fbcbcdb92dd92725262f4bf9ef26b361f3165ef383f08b950a2954fb9 |
| src/sroute.cpp | src/pdn/src/sroute.cpp | fbc068e56e4d301eae222f0f82ebd54b91d0a3f42886631c05e3fc610c6702fd |
| src/sroute.h | src/pdn/src/sroute.h | 880872eb800226b571000ee8834e459ac9b9b34a2cf11e44ee4ba00358d94ab7 |
| src/straps.cpp | src/pdn/src/straps.cpp | 9168b0afff5a98a030c47c2c399d440d207e6c2f3043f8c7f49148dba2df7de2 |
| src/straps.h | src/pdn/src/straps.h | 69430cef90dc3b333b67572d4311893263d17cabb31bc5ee453e1beb071dbe73 |
| src/techlayer.cpp | src/pdn/src/techlayer.cpp | 5557bd160a5b64518afc8acee286c5b94ac838b149d86556c57a8a8b62e80e98 |
| src/techlayer.h | src/pdn/src/techlayer.h | 77eed5a8e17dbb2b63a292737bf3e334aa9497fcd0580b5423bbdaec94ac946e |
| src/via.cpp | src/pdn/src/via.cpp | 6d6f531bf829c39684a9359f55725cd2f3e0d3eaa758331cf76c9b961c4fb8a2 |
| src/via.h | src/pdn/src/via.h | b949222d91a806f9f0d0aea47fcbca5d7cc8acb2a46c51698eae09f77df649bd |
| src/via_repair.cpp | src/pdn/src/via_repair.cpp | cbcd33b0adf63fda1d4c7b6bb33b11f80812ef3d8fe64681ec2f26e3714dd482 |
| src/via_repair.h | src/pdn/src/via_repair.h | cfd6b2b25e508632ca1f317cd6359c9c8f643482d10833a85ed8c1c6a359bacc |

## Not imported

- src/pdn/src/MakePdnGen.cc, src/pdn/include/pdn/MakePdnGen.hh, src/pdn/src/PdnGen.i, src/pdn/src/PdnGen-py.i, src/pdn/src/pdn.tcl: the Tcl/SWIG factory and bindings. EOS constructs pdn::PdnGen directly in the finish runner (src/pd/finish) and registers eos::finish through its own registry, not through engine Tcl bindings.
- src/pdn/messages.txt: upstream message-code input for OpenROAD's message system. EOS's Logger already carries the PDN tool id (see src/db/odb/include/utl/Logger.h), so no codegen is needed.
- src/pdn/test/*: upstream regression tests (Nangate45/ASAP7/Sky130 collateral). Not imported; P7 tests live in tests/unit/pd/finish/.
- src/pdn/src/CMakeLists.txt: upstream build file. EOS writes its own (see CMakeLists.txt).
