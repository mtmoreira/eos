# stt: port adaptations

None to the imported engine files: SteinerTreeBuilder.cpp,
flute.cpp, and pd.cpp are byte-identical to upstream ead3236 (see
PROVENANCE.md for SHA-256). The adaptations in this module are
EOS-owned files around them:

- tools/make_dat_var.py: EOS-owned FLUTE table generator emitting
  namespace stt::flt variables (as flute.cpp declares them) from
  the pdr module's committed .dat files. The upstream
  MakeDatVar.tcl is GPL-3.0 and is not imported. See PROVENANCE.md
  for the pd.cpp (ead3236, not v2.0 pdr) rationale.
- The Tcl factory (MakeSteinerTreeBuilder) and the LinesRenderer
  GUI helper are not imported.
