# extract: provenance

Upstream: OpenROAD at commit ead3236e56d57362a8dc8b25658fb41a191eba17
(the same commit that supplied the in-tree OpenDB), imported as
EOS-owned code under unit P6 (extraction) of the place-and-route
phase: OpenRCX parasitic extraction, driven by the EOS-owned runner
in src/extract.cc. The pin tree itself is never modified; the files
below are copies into this module, with the port adaptations listed
in PATCHES.md. No git submodule, no external process at runtime.

## Pin verification (run at import time, 2026-11-03)

The local pin tree at ~/workspace/openroad-ead3236 is a tarball
without .git, so the pin is verified two ways, not asserted: (1)
its BUILD-RECIPE.md names OpenROAD
ead3236e56d57362a8dc8b25658fb41a191eba17 (the ORFS v3.0 pin);
(2) sampled SHA-256 hashes from that tree match the merged P2
PROVENANCE exactly (gpl replace.cpp, gpl timingBase.cpp, grt
GlobalRouter.cpp). The rcx files below come from that same tree.
No other commit was substituted.

## Licence audit (run at import time, 2026-11-03)

Every file in the import set was grepped for licence text
(GNU/General Public/Affero/Mozilla/Eclipse/Apache/MIT phrases)
and the BSD headers were read (BSD 3-Clause, UC Regents / Nefelus).
Result: BSD-3-Clause only. No copyleft text was found in any
imported file. All 23 imported files carry the BSD header.
The upstream LICENSE file (BSD-3) is copied as LICENSE in this
module. The Tcl factory (MakeOpenRCX.cpp), all .tcl/.i
surfaces, the upstream test directory, calibration, doc, and
example directories are not imported: the EOS runner constructs
rcx::Ext directly and the EOS registry registers
eos::extract_parasitics.

## Imported files (SHA-256 of the upstream file at ead3236)

| EOS path (relative to src/pv/extract) | Upstream path (relative to OpenROAD) | SHA-256 |
|---|---|---|
| include/rcx/ext.h | src/rcx/include/rcx/ext.h | d8539d16a81a50f679df1d1889aef052c052de94b8bb49afce6d095ed6e83cc0 |
| include/rcx/ext2dBox.h | src/rcx/include/rcx/ext2dBox.h | 1564f1c76605c89528a15df4d4b27f251c01ff0164105f89512783857f853840 |
| include/rcx/extRCap.h | src/rcx/include/rcx/extRCap.h | 62b1366a7bfba1c09f5cf8888b3819247d6c7b078ac415547ee781eb2b3f14be |
| include/rcx/extSpef.h | src/rcx/include/rcx/extSpef.h | 548c909857413cb010327210fb0814efabac8d57f5686474fe7948e2ab7079cf |
| include/rcx/extprocess.h | src/rcx/include/rcx/extprocess.h | e663a4e6163d4a9c9e535276a338f7dd0db19bbc34074f98624aa7ef6c886215 |
| src/ext.cpp | src/rcx/src/ext.cpp | c9fd170342f78598a954af500ea2bd09e25aa27ebab7884a387cb80e02774f2d |
| src/extBench.cpp | src/rcx/src/extBench.cpp | 27019bf5586a06bb692f8317d257b7ffb92d3d52fb4282a8b2baabe2c2d07065 |
| src/extBenchDB.cpp | src/rcx/src/extBenchDB.cpp | 0469a99b06c60b4f43598c48e41c13092a0ccae490e7cd4b424b19aa6cf617de |
| src/extCC.cpp | src/rcx/src/extCC.cpp | af3f328cef16af1fe6ffb0ae5b5bacbd2a0233404232b3708f8f9fcf508e99d7 |
| src/extCoords.cpp | src/rcx/src/extCoords.cpp | d6f90dae78b03ae8bf778d0fd2861a4dff35aff6aaec3845ae2fdec0c2a6d0aa |
| src/extDebugPrint.cpp | src/rcx/src/extDebugPrint.cpp | fcfc14a6aff56c48c07ee68b1d45c55d6c3c7bdfc89d78a18eca17e848985498 |
| src/extFlow.cpp | src/rcx/src/extFlow.cpp | c51feea5404ab85e38f05cfae594bb04107e79d3ed81358b8ed66188bd2a32b4 |
| src/extRCmodel.cpp | src/rcx/src/extRCmodel.cpp | 4ec72808edf43d424cd96b1e86a5c9dc5d25890cfdc9663fb1083be81bd1ffce |
| src/extSpef.cpp | src/rcx/src/extSpef.cpp | 6921133bd55e74fbae0e76e10c4ff54de8a41af9d9e5cc0ebef3496bd2856ac3 |
| src/extSpefIn.cpp | src/rcx/src/extSpefIn.cpp | ba294e9c6f43418380608a716bb994e96d0cd7a30a409302adfdafc93daccf78 |
| src/extmain.cpp | src/rcx/src/extmain.cpp | 20c0ad588c0e97771e2780a42c267c8492b7293ba768d81029678b576542e637 |
| src/extmeasure.cpp | src/rcx/src/extmeasure.cpp | a5a76b7b2410e99c4ab05568c01f825a20b9d73f004bee433a266d61966911ad |
| src/extmeasure_res.cpp | src/rcx/src/extmeasure_res.cpp | 914f804bc05a651bffae23236b1bc7ced98b8da85c74c8c790878f121218055b |
| src/extprocess.cpp | src/rcx/src/extprocess.cpp | 74dca0f5d25b968c79212d4d960525fb2685b097edfe1107a2257232f7f8b93d |
| src/extstats.cpp | src/rcx/src/extstats.cpp | c55013f5751b4a2cf33b3713204fa9624adb6bdb9de4dc9f8f80c06aa2d1b7c2 |
| src/name.cpp | src/rcx/src/name.cpp | a462d549c066d74b02fb4682d73c402e7cc963d866ec9d2cf4fd0619fb021b39 |
| src/netRC.cpp | src/rcx/src/netRC.cpp | 8961f3ae2e20aaf290131017041438eba2dde1711517171220b272e8e5c58638 |
| src/name.h | src/rcx/src/name.h | d87112934c7f117e944f8dcc2f04d0febe7080750a6c8a80d716996c2c242b24 |
