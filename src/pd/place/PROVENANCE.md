# place: provenance

Upstream: OpenROAD at commit ead3236e56d57 (the same commit that
supplied the in-tree OpenDB), imported as EOS's own code under
unit P2 (placement) of the place-and-route phase: RePlAce global
placement (gpl) and OpenROAD detailed placement (dpl), driven by
the EOS-owned runner in src/place.cc. The pin tree itself is never
modified; the files below are copies into this module, with the
port adaptations listed in PATCHES.md. No git submodule, no
external process at runtime.

## Licence audit (run at import time, 2026-11-01)

Every file in the import set was grepped for licence text
(GNU/General Public/Lesser/Affero/Mozilla/Eclipse/Apache/MIT/Boost
phrases) and the LICENSE files under src/gpl and src/dpl were read
(BSD 3-Clause, UC Regents 2015-2018 for gpl). Result: BSD-3-
Clause only. No copyleft text was found. The "GPL" strings in
these sources are the gpl namespace and the utl::GPL logger tool
id, not licence text. The Tcl factories (MakeReplace, MakeOpendp)
and all .tcl/.i surfaces are not imported, and gpuSolver.cu is
excluded (CPU-only build).

## Imported files (SHA-256 of the upstream file at ead3236)

| EOS path (relative to src/pd/place) | Upstream path (relative to OpenROAD) | SHA-256 |
|---|---|---|
| src/gpl/fft.cpp | src/gpl/src/fft.cpp | 727006ffa4b35af25fbbbe4f60a2cc656e9bf8c8b60e472f6301b953444f5eeb |
| src/gpl/fft.h | src/gpl/src/fft.h | 2d8d2ecadd59f6d69a1f54a6c6b92442e1695f27ea652b0868f58d38a53c04b2 |
| src/gpl/fftsg.cpp | src/gpl/src/fftsg.cpp | 30ff8c92d1474b8ee5f0454edcfda3b89cc381da3c67565ad2049f1866cb8d51 |
| src/gpl/fftsg2d.cpp | src/gpl/src/fftsg2d.cpp | 0124d2960d56c83d9a8c2657788393d6e91af4a66d58bf2f9c7c907d3b1de238 |
| src/gpl/graphics.cpp | src/gpl/src/graphics.cpp | ea7606fc364c74b645c2039b8685352dfaa8911df1b92d759af04e6eec8c2616 |
| src/gpl/graphics.h | src/gpl/src/graphics.h | 5981fa186cd9cb5530b09db568c0a1c24f07335f57f50eb81224539fc79e1f85 |
| src/gpl/initialPlace.cpp | src/gpl/src/initialPlace.cpp | 18703cf13b64238f4797785c9b7644eb605acacbdc84a7f881ea8c0f1af951a9 |
| src/gpl/initialPlace.h | src/gpl/src/initialPlace.h | 591e4833a50f80cb62bc3ede74ad3edf27a51c0ca5da0d86c51799f24da5f7ca |
| src/gpl/mbff.cpp | src/gpl/src/mbff.cpp | 08dfc472e4c090c327f6f59f66e5875c6ad473b4ed7f7e77bf377c91fe8da9f7 |
| src/gpl/mbff.h | src/gpl/src/mbff.h | 60ba2111f1c70a49891df97e655e87d9b0f7fe85537365daa2926dc22b6e647f |
| src/gpl/nesterovBase.cpp | src/gpl/src/nesterovBase.cpp | 5dce574402c15dec79409ae448e12fa5d8e4d601ae7012dcfae5c4347f7afe10 |
| src/gpl/nesterovBase.h | src/gpl/src/nesterovBase.h | 587ff398a821e4d87740101d143c9881f934ae6d35cf3a129a2b977196779d55 |
| src/gpl/nesterovPlace.cpp | src/gpl/src/nesterovPlace.cpp | d08ef5dd28265d0acf24c48fd4a7385ba79fdbed5fdbd53b66c78b3c40ca4cc2 |
| src/gpl/nesterovPlace.h | src/gpl/src/nesterovPlace.h | a24322d57e3a65b7a934bb8e0bc6292ab6d74a9d5e31a455d9a2cae70b7a9d5c |
| src/gpl/placerBase.cpp | src/gpl/src/placerBase.cpp | d5c52897f669140b19171747d8c14e958eae271e8d57f6c4f8c3fb888fe998e9 |
| src/gpl/placerBase.h | src/gpl/src/placerBase.h | 2fdf71c2fe8d1e80381983cf192b99d42c4ebb25b96a305eed1f2084e91ff20f |
| src/gpl/point.cpp | src/gpl/src/point.cpp | 7aa2741a582eb185ec3bbb19be4de372f320b1db0b525bd15b4e07acafbaad0a |
| src/gpl/point.h | src/gpl/src/point.h | 8e83045c37c81e35a5c14c1502c4efabc550d02d1c8c06cfaae2955823c33d1d |
| src/gpl/replace.cpp | src/gpl/src/replace.cpp | e35ca9ec56c4cc80ce1d10e1df9d21bf00d2463dc69bfb6c18a6a4a64d3a4ded |
| src/gpl/routeBase.cpp | src/gpl/src/routeBase.cpp | 32db3ec712f1ba1d6f7cb47fc5979e08bd41715c826a67ec82ce7b276434ec00 |
| src/gpl/routeBase.h | src/gpl/src/routeBase.h | 6e31cfa3427c2cae7a7b80e7f37c1aa3e388a8e61a8c482e5648ed633fa44f4d |
| src/gpl/solver.cpp | src/gpl/src/solver.cpp | 9c286df9d425e1ac0d2d4963b3b0955d6968a7c0d6da51378edf933f8066cc9f |
| src/gpl/solver.h | src/gpl/src/solver.h | 6f2e32cc9446223b595d890ea3a7ffb9e872d2fdf5920d38f2f16f379d67d413 |
| src/gpl/timingBase.cpp | src/gpl/src/timingBase.cpp | 93a30ed54e003189253623f59164b4f91a4c0bb6200f39d0d36d79c768c13e16 |
| src/gpl/timingBase.h | src/gpl/src/timingBase.h | 16d1f53bffdeea335eb41ba87e5a109b265feff473d19c5a167ca44e9960aead |
| src/dpl/CheckPlacement.cpp | src/dpl/src/CheckPlacement.cpp | 12df08f1230b7cb92ad17d1d1893661746b96a0676ce5b35e78cd5dae3ff7c28 |
| src/dpl/DplObserver.h | src/dpl/src/DplObserver.h | 81deb007afcf7305c22d9e64b401a761fabe250261037112c6c02eb83d77ef6a |
| src/dpl/FillerPlacement.cpp | src/dpl/src/FillerPlacement.cpp | e0f838badb7ae7ede7aa536a35074971ef4c3206bd63d43315fb72f6d7f7590e |
| src/dpl/Graphics.cpp | src/dpl/src/Graphics.cpp | 745d020428ac45e2f50910aa43afc08fb849b4b684a172964390baab4e8779df |
| src/dpl/Graphics.h | src/dpl/src/Graphics.h | ebcad3afddcffeb76dc17e4c96ac89f1c4df27c4f1591bd5bae2da65b545c730 |
| src/dpl/Grid.cpp | src/dpl/src/Grid.cpp | be11f3fccff82a3c89fb10795d3a7de584999b0b69b30f108c2508ecc3c6229a |
| src/dpl/Opendp.cpp | src/dpl/src/Opendp.cpp | 2c7003acb1c86e6b193fa911bb98281cdf88f5343e5c750d658432505538b6bb |
| src/dpl/OptMirror.cpp | src/dpl/src/OptMirror.cpp | b9664c956f9df8b70e96b969068a334cefacd50c4b38a1f22c6a2e079c7e149e |
| src/dpl/Place.cpp | src/dpl/src/Place.cpp | ea0221912d122aa682b23cafbfbffb11428f55f3ebe36af38a9dda125c651168 |
| src/dpl/dbToOpendp.cpp | src/dpl/src/dbToOpendp.cpp | 907adc0c5b7edbd2897a83b40469910af15912a1e97907dc2d99b4071af0c9d3 |
| include/gpl/Replace.h | src/gpl/include/gpl/Replace.h | f51984c42b7ff57838eae4133592551a8ea5b808492e11c6a4b79a9586504c71 |
| include/dpl/Opendp.h | src/dpl/include/dpl/Opendp.h | 594eea49fba4def7b2d0b119308799fc0a30c4e3bb29f5fe5c7211b2bbdd669d |
