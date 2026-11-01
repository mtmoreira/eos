# pdr module provenance

Upstream: OpenROAD v2.0, commit a5a56584 (candidate tree
`openroad-v2.0-pin`), BSD-3-Clause. The rsz spike port tree and the
R1 step 1 driver copies are intermediate working copies, not sources
of record for the verbatim files; the adapted files record their
driver-copy origin in PATCHES.md.

## Verbatim files (SHA-256 over the staged bytes)

| staged file | upstream file | SHA-256 |
| --- | --- | --- |
| include/pdr/graph.h | src/pdr/src/graph.h | d70655700fb20f34ed35f0adaedc4087f0d31029fed28a0fee33c9a20c38c71c |
| include/pdr/node.h | src/pdr/src/node.h | 67d4a6365140e42e12d94090c7fc42f1c933105d7b6ba69b78b35711a958a1cf |
| include/pdr/edge.h | src/pdr/src/edge.h | 0d4c8d468dcfd9d6e60424a278bcbc0f4d1ae353befa5d0e3d3f20d70ed78c78 |
| include/pdr/flute.h | src/stt/flute.h | bd67a5d4c9a91d13644ea319dc0aad9f97085f4522606d00b438a5701e20277d |
| src/flute.cpp | src/stt/flute.cpp | ed294bb68386d1dd9dfe6664a3bcf935df05f63cb695e2e0615f8946b8ede4eb |
| data/POWV9.dat | src/stt/etc/POWV9.dat | 876eb7fd9b6e2f98fc4e5679545f7d563cf32dd44fd4b2c4b97893066bb3b699 |
| data/POST9.dat | src/stt/etc/POST9.dat | 20455530e0eb52c5b0b0405e3ae25c20f9f1e9af00e2a70290136e1243f94d01 |

All values above were computed by the staging script over the staged
bytes (the .dat values additionally asserted against the stage-1
draft; the script fails the stage if any differs) and re-verified
at import time in the PR2 tree.

## EOS-owned files (not upstream; see PATCHES.md)

| file | origin |
| --- | --- |
| tools/make_dat_var.py | EOS-owned FLUTE table generator, replacing upstream's GPL-3.0 MakeDatVar.tcl by owner decision 2026-11-01; verified byte-identical output on both tables |
| CMakeLists.txt, README.md, PATCHES.md, this file, tests | EOS-owned module scaffolding and unit test |

## Adapted files (not verbatim; see PATCHES.md)

| staged file | origin |
| --- | --- |
| src/graph.cpp, src/node.cpp, src/edge.cpp | R1 step 1 driver copies, driver comment stripped |
| src/pdrev.cpp | R1 step 1 driver copy, highlightGraph declaration dropped |
| include/pdr/pdrev.h | upstream header minus gui/highlightSteinerTree declarations |

## Licence

BSD-3-Clause only. Upstream's GPL-3.0 table generator
`src/stt/etc/MakeDatVar.tcl` is deliberately NOT imported (owner
decision 2026-11-01; see README.md and PATCHES.md) and is replaced
by the EOS-owned `tools/make_dat_var.py`. The import-time grep over
the exact PR2 import set finds no GPL text in any imported file.
