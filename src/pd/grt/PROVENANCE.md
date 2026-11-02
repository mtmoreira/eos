# grt: provenance

Upstream: OpenROAD at commit ead3236e56d57 (the same commit that
supplied the in-tree OpenDB), imported as EOS's own code under unit
P2 (placement) of the place-and-route phase. The pin tree itself is
never modified; the files below are copies into this module, with
the port adaptations listed in PATCHES.md. No git submodule, no
external process at runtime.

## Licence audit (run at import time, 2026-11-01)

Every file in the import set was grepped for licence text
(GNU/General Public/Lesser/Affero/Mozilla/Eclipse/Apache/MIT/Boost
phrases) and every LICENSE file under the copied trees was read,
including src/grt/src/fastroute/LICENSE (BSD 3-Clause, Iowa State
University, 2018). Result: BSD-3-Clause only. No GPL, LGPL, AGPL,
MPL, EPL, MIT, or Apache text was found in any imported file. The
only "GPL" strings in the sources are the gpl engine namespace and
the utl::GPL logger tool id (in the place module), not licence
text, and the only "gnuplot" hit is a comment about plot output
format in flute.cpp (in the stt module). The upstream
MakeWireParasitics files and the Tcl/SWIG factories are not
imported (see PATCHES.md), so no other licence surface enters
with this module.

## Imported files (SHA-256 of the upstream file at ead3236)

| EOS path (relative to src/pd/grt) | Upstream path (relative to OpenROAD) | SHA-256 |
|---|---|---|
| include/grt/GRoute.h | src/grt/include/grt/GRoute.h | 1aa6424d55a3a8f2a19894e19ea5a0bd3c5e16945145d209c55db713083b34d5 |
| include/grt/GlobalRouter.h | src/grt/include/grt/GlobalRouter.h | a3a81c2d7f24d0aab6bb012d9731187abe2e71ba1b6910f641c290a4223c553b |
| include/grt/RoutePt.h | src/grt/include/grt/RoutePt.h | 7c261c03a3a10c01623a895434c85e5194ae88c35fae2d25d4a2984e5a4affbb |
| include/fastroute/AbstractFastRouteRenderer.h | src/grt/src/fastroute/include/AbstractFastRouteRenderer.h | 01496dc5b436ad70f87fb6b3385119dfca8bb4b160da17004b3a27ec1e3acd42 |
| include/fastroute/AbstractMakeWireParasitics.h | src/grt/src/fastroute/include/AbstractMakeWireParasitics.h | 0c771a870dd1b1b5c11fb0553d7d895ad7a64083dc9459d7780f27779251cc98 |
| include/fastroute/DataType.h | src/grt/src/fastroute/include/DataType.h | f972f0c2d7998d733d1241135c2cdd8339012d55cc3f75f3a379820301048823 |
| include/fastroute/FastRoute.h | src/grt/src/fastroute/include/FastRoute.h | b2a804d247fb21f7eb5fff38a3ea34f9c608f22ab7b91685d4eee6b22bcb3f97 |
| include/fastroute/FastRouteRenderer.h | src/grt/src/fastroute/include/FastRouteRenderer.h | 8cfb22385dd01f71868bd99cd9eaea8a3e690e1094f11ceabf0822607fa6dd0b |
| src/AbstractGrouteRenderer.h | src/grt/src/AbstractGrouteRenderer.h | 5e278e89a29ae513858cc4deb2011c8fa5182b26bf9e4fa16e213ffa0e20d85f |
| src/AbstractRoutingCongestionDataSource.h | src/grt/src/AbstractRoutingCongestionDataSource.h | 6cf462515256cad66c4e43c020721358319afbacdc76b4c956207d75ad471ddb |
| src/Grid.h | src/grt/src/Grid.h | 9d28b3b4be5245f073fde611d66daab47398bf39a95e52821cb10bae272e1485 |
| src/Grid.cpp | src/grt/src/Grid.cpp | ae29d29eb371d05a24588ac5291158798a3baf001ddb4453c25b741bbe6c7151 |
| src/Net.h | src/grt/src/Net.h | 9a2bb97e6b7efde6b2f1c6b1f45649090d7d79323b2770f8fc666507628a15c1 |
| src/Net.cpp | src/grt/src/Net.cpp | cf9de9016b96cf43cd9f0e239bbdd4c2315497df063fa64aac64d55cbdae2d70 |
| src/Pin.h | src/grt/src/Pin.h | 46a13f2af8227177d0c425346f8af3f05ca4f95d4294a990eb8d90cc047bccc0 |
| src/Pin.cpp | src/grt/src/Pin.cpp | 276db539e4c0d8672502d082a435bd9b5dbb03e08d0e35469da961eec7d98192 |
| src/RepairAntennas.h | src/grt/src/RepairAntennas.h | 1cfc32050180abc9c2fbc72b1f49fe9a6b74eb580792efec3458049d64f38f23 |
| src/RepairAntennas.cpp | src/grt/src/RepairAntennas.cpp | ae397a899be36e60b5e5e25b53037f6444d57fc08cb23003b66b817c1d0e161d |
| src/RoutingTracks.h | src/grt/src/RoutingTracks.h | a38b240b9619788881aa1caaee7926306c8dbcade8b2960acfce169ed69dc110 |
| src/GrouteRenderer.h | src/grt/src/GrouteRenderer.h | ae7ee3b54876f157d8acb0db994897bdc3d514d4e51e8ed161a017c2e6d7c50c |
| src/GrouteRenderer.cpp | src/grt/src/GrouteRenderer.cpp | a1233dd548e19a07b4f720b19f0f64e004ea4ae2dd4e43712eb5f82344a1ea5d |
| src/heatMap.h | src/grt/src/heatMap.h | 3b3310e07ab61f40f2d89ba89b402b35eb6798a8307eaa5d06218610fa2aa661 |
| src/heatMap.cpp | src/grt/src/heatMap.cpp | 1db18fdef1f9bf782e5523ffa45ad746287e22dbfdb86ab8822baacf6b44b7a1 |
| src/GlobalRouter.cpp | src/grt/src/GlobalRouter.cpp | 031141dd04e73419fd13d4fb78aa32a9d91ad68114f61bb046ca905acf369344 |
| src/fastroute/FastRoute.cpp | src/grt/src/fastroute/src/FastRoute.cpp | 99a1e35653f7654de03e6bf017812a2da77b57566bc2f843f6b131af0fff946e |
| src/fastroute/FastRouteRenderer.cpp | src/grt/src/fastroute/src/FastRouteRenderer.cpp | 1fb2a588d4fb24b696174e64f3d2e5299ed4d9026d747f0a130ecdd3574e6814 |
| src/fastroute/RSMT.cpp | src/grt/src/fastroute/src/RSMT.cpp | 1f5c0af99937327cd2853ec3b5c58ce3b776c81efce13871e0afd4ad5a9d7c2b |
| src/fastroute/RipUp.cpp | src/grt/src/fastroute/src/RipUp.cpp | c8229e918f6d09454cc12a3d031532dbcdc71fb9219efeb60b5729ced75450fe |
| src/fastroute/maze.cpp | src/grt/src/fastroute/src/maze.cpp | bb069d89914f8e5c3722e9ffe273af88efd10dfd859ea77114b985cfeca7e9b1 |
| src/fastroute/maze3D.cpp | src/grt/src/fastroute/src/maze3D.cpp | 6e49b9b44c2cbce68c7fdc3b4e2778adc5173ca4b8a67f85121e1ee454ff19fb |
| src/fastroute/route.cpp | src/grt/src/fastroute/src/route.cpp | 4b8591df9297360e0b66460a1854dff76cfd37731e7c5f2596ca6c634da197ea |
| src/fastroute/utility.cpp | src/grt/src/fastroute/src/utility.cpp | 6d05bf85fcd9de2fa54352cd4cf70d842802efa5e5a5234bc59236e4c0eb3190 |

## Not imported

- src/grt/src/MakeGlobalRouter.cpp, MakeWireParasitics.h/.cpp:
  see PATCHES.md. The Tcl factory is replaced by the EOS-owned
  src/MakeGlobalRouter.cc in this module; the wire-parasitics
  builder is deferred to P4/P6.
- All .tcl and .i (SWIG) surfaces: EOS registers commands through
  its own registry, not through engine Tcl bindings.
- Upstream CMakeLists: EOS writes its own (see CMakeLists.txt).
