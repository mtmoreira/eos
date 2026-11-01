# dbsta (L4 engines)

The OpenROAD dbSta network bridge: sta::dbNetwork, the adapter
between OpenDB and the in-tree OpenSTA network API, ported from
the OpenDB pin commit ead3236, plus the gui stub and the
EOS-owned ord shim surface.

Status: adopted in place-and-route unit P1, as the bridge the
floorplan engine (ifp) needs. The port copies, their
adaptations to the in-tree STA API and the licence audit are
documented in PROVENANCE.md and PATCHES.md. dbSta itself,
dbReadVerilog and dbSdcNetwork are deliberately not imported
in P1: no P1 consumer needs them.

The stable public API of this module is the ported db_sta
header (include/db_sta/dbNetwork.hh) and the ord shim header
(include/ord/OpenRoad.hh), versioned per the EOS Code Structure document.
