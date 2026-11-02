# cts: provenance

Upstream: OpenROAD at commit ead3236e56d57362a8dc8b25658fb41a191eba17
(the same commit that supplied the in-tree OpenDB), imported as
EOS's own code under unit P3 (CTS) of the place-and-route phase:
TritonCTS clock tree synthesis, driven by the EOS-owned runner in
src/cts.cc. The pin tree itself is never modified; the files below
are copies into this module, with the port adaptations listed in
PATCHES.md. No git submodule, no external process at runtime.

## Pin verification (run at import time, 2026-11-02)

The local pin tree at ~/workspace/openroad-ead3236 is a tarball
without .git, so the pin is verified two ways, not asserted: (1)
its BUILD-RECIPE.md names OpenROAD
ead3236e56d57362a8dc8b25658fb41a191eba17 (the ORFS v3.0 pin);
(2) sampled SHA-256 hashes from that tree match the merged P2
PROVENANCE exactly (gpl replace.cpp, gpl timingBase.cpp, grt
GlobalRouter.cpp). The cts files below come from that same tree.
No other commit was substituted.

## Licence audit (run at import time, 2026-11-02)

Every file in the import set was grepped for licence text
(GNU/General Public/Affero/Mozilla/Eclipse/Apache/MIT phrases)
and the BSD headers were read (BSD 3-Clause, UC Regents 2019).
Result: BSD-3-Clause only. No copyleft text was found. The Tcl
factory (MakeTritoncts.cpp), the GUI renderer (CtsGraphics),
all .tcl/.i surfaces, and the upstream test directory are not
imported.

## Imported files (SHA-256 of the upstream file at ead3236)

| EOS path (relative to src/pd/cts) | Upstream path (relative to OpenROAD) | SHA-256 |
|---|---|---|
| src/Clock.cpp | src/cts/src/Clock.cpp | 99e23f6e9795c32f3be993ee97b16601c6ff15434ae2d8a6745608ff098fc7e3 |
| src/Clock.h | src/cts/src/Clock.h | 0895c1e654860607aeff596375ffcc6387fa452bab921620934264ce17902658 |
| src/Clustering.cpp | src/cts/src/Clustering.cpp | b48c1053587cef09acf15cf977238f0ccc9b44d8ddf2c64f04c7c6e7db90d8dd |
| src/Clustering.h | src/cts/src/Clustering.h | 45d4e9a89e9e1afe64c59c7427a3ab96461d5c852e2db7032ce91e5f9ec2016e |
| src/CtsObserver.h | src/cts/src/CtsObserver.h | c8404437bbed240f645822e720ef18d873aedbd34873fbe415e573f9b5d9cc20 |
| src/CtsOptions.h | src/cts/src/CtsOptions.h | ef0c2f8501ec0f1c5a843949c080ee96866239b71abb9b60f3f89b641e0c2ad2 |
| src/HTreeBuilder.cpp | src/cts/src/HTreeBuilder.cpp | 7bc5156b2b962acb84e8d227ebd04c417337a39c835b20ba2df5b4d8a11e4320 |
| src/HTreeBuilder.h | src/cts/src/HTreeBuilder.h | 68418a2864e9bb2efb7c624748e385eb0c4efdb56d467c7030e8d888259a7f06 |
| src/LevelBalancer.cpp | src/cts/src/LevelBalancer.cpp | dd0d2a04f45039991f0e5b0313ebb08ce3bfd6018d7b4329fc7756b8e34f2aaf |
| src/LevelBalancer.h | src/cts/src/LevelBalancer.h | 3c9959c690e201f722b07d56cea71646bce3dfbb72cfce7b8fff18c5bc6cabab |
| src/SinkClustering.cpp | src/cts/src/SinkClustering.cpp | 4267496e53a8da3c13e3c3036c85a6e931d8f97f43b7d0f373060f9dbd732d44 |
| src/SinkClustering.h | src/cts/src/SinkClustering.h | 50f047fb1d9e1ababecc7d936ca231ae371878b6d31e0ae4f37f15ba59dee99b |
| src/TechChar.cpp | src/cts/src/TechChar.cpp | 3dc41b06192404f3d282930adc3f159170e3a108d7bfbfcc45f36b910802d836 |
| src/TechChar.h | src/cts/src/TechChar.h | eeb268db3cb090bf65b8eda8d0f92b5e5f7c6944898d1fb720ff1086066c38f2 |
| src/TreeBuilder.cpp | src/cts/src/TreeBuilder.cpp | a01843b3af13cc59239b09deca24cb632debbd9b2740faef38ad79b3c721be52 |
| src/TreeBuilder.h | src/cts/src/TreeBuilder.h | ec3c3d939dc748bb350bbf900f35c0fbc0e0de7c2dcfcac4a321b6080243aa9b |
| src/TritonCTS.cpp | src/cts/src/TritonCTS.cpp | 5f6b7b50a88a09c02621cccfed040060a17726aabad4223258e252fb009e7558 |
| src/Util.h | src/cts/src/Util.h | 7f0ef0fb3bcabbaad7658e0a24827c3dc4cb5d5f0f6e65226a87e49c7a8d33ba |
| include/cts/TritonCTS.h | src/cts/include/cts/TritonCTS.h | 9be71ce2789df7a0f4e6883aabaf03723c9e6f57cbb13fff652c9d366267efaa |
