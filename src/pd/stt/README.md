# stt (L4 engines)

OpenROAD Steiner-tree builder (SteinerTreeBuilder, FLUTE, and the
ead3236 Prim-Dijkstra), imported as a P2 support module for grt.
FLUTE lookup tables are generated at build time by the EOS-owned
tools/make_dat_var.py from the pdr module's committed .dat files;
the upstream MakeDatVar.tcl is GPL-3.0 and is not imported. The
ead3236 pd.cpp is imported here rather than reusing the v2.0 pdr
overload, because the two have different signatures and Tree
layouts (see PROVENANCE.md). See PATCHES.md (no engine changes).
