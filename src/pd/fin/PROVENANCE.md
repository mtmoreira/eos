# fin: provenance

Upstream: OpenROAD at commit ead3236e56d57 (the same commit that supplied the in-tree OpenDB), imported as EOS-owned code under unit P7 (finish) of the place-and-route phase. The pin tree itself is never modified; the files below are copies into this module, with the port adaptations listed in PATCHES.md. No git submodule, no external process at runtime.

## Licence audit (run at import time, 2026-11-03)

Every file in the import set was grepped for licence text (GNU/General Public/Lesser/Affero/Mozilla/Eclipse/Apache/MIT/Boost phrases, case-insensitive, plus SPDX identifiers) and the LICENSE file was read (src/fin/LICENSE, BSD 3-Clause, The Regents of the University of California, 2018-2023). Result: BSD-3-Clause only. No GPL, LGPL, AGPL, MPL, EPL, MIT, or Apache text was found in any imported file. The string "boost" appears only as C++ #include lines for the Boost headers (property_tree, polygon, lexical_cast), which are a declared system dependency (see third_party_manifest.yaml and cmake/EosDependencies.cmake), not vendored licence text. The upstream Tcl/SWIG factories (MakeFinale.cpp, finale.i, finale-py.i, finale.tcl) and all tests are not imported (see PATCHES.md), so no other licence surface enters with this module.

## Imported files (SHA-256 of the upstream file at ead3236)

| EOS path (relative to src/pd/fin) | Upstream path (relative to OpenROAD) | SHA-256 |
|---|---|---|
| include/fin/Finale.h | src/fin/include/fin/Finale.h | 459983abfce65299b8130d3e3699fad1a4a94b66d34cc23c85c0f037fb99123b |
| src/Finale.cpp | src/fin/src/Finale.cpp | 61fd30f53b5193f13195460b3057eaa84322278705b73f7d9d04f9f17828d626 |
| src/DensityFill.cpp | src/fin/src/DensityFill.cpp | 0f6bb4b7db6ca1ead4a5c6b687925c090e7db40e14a5380f9de5e305802b71ef |
| src/DensityFill.h | src/fin/src/DensityFill.h | c3416f188470df2b294b0881159033d7b3410861fc105c158a754de24d4b3aad |
| src/graphics.cpp | src/fin/src/graphics.cpp | 92340246bf067dc6b5b8c1d58e47707d369bdb9a7b389d119dab76b187e0c242 |
| src/graphics.h | src/fin/src/graphics.h | 1b6f592bbc2ac5a35debf5fa257eb0bca04f6b1665c30ff258860ea7412d4717 |
| src/polygon.h | src/fin/src/polygon.h | 8e9a91b5a4fea345f9b6fcab0d36f1662bd723c50b633f77266b99da6205ba7b |
| LICENSE | src/fin/LICENSE | e4c62605dedc27267ba0b1b987f6f86355b4e20362ebdeb07a59a792a260d29b |

## Not imported

- src/fin/src/MakeFinale.cpp, src/fin/include/fin/MakeFinale.h, src/fin/src/finale.i, src/fin/src/finale-py.i, src/fin/src/finale.tcl: the Tcl/SWIG factory and bindings. EOS constructs fin::Finale directly in the finish runner (src/pd/finish) and registers eos::finish through its own registry, not through engine Tcl bindings.
- src/fin/messages.txt: upstream message-code input for OpenROAD's message system. EOS's Logger already carries the FIN tool id (see src/db/odb/include/utl/Logger.h), so no codegen is needed.
- src/fin/test/*: upstream regression tests (gcd_fill etc., Sky130 collateral). Not imported; P7 tests live in tests/unit/pd/finish/.
- Upstream CMakeLists: EOS writes its own (see CMakeLists.txt).
