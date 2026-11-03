# extract (L4 engines)

Parasitic extraction producing SPEF for signoff STA.

Status: implemented under unit P6 of the place-and-route phase.
`eos::extract_parasitics <spef_path> [-model <rules file>]` runs the
imported OpenROAD extractor (OpenRCX, from the OpenDB pin commit
ead3236; see PROVENANCE.md and PATCHES.md) over the routed design
in the live database and writes a SPEF file for it. The runner
fails fast on an unrouted design (no signal net carries a wire),
and on a missing or unreadable extraction model file: the
extractor's RC tables come from the rules file, and without one it
extracts nothing, so EOS ships no default model. The SPEF is
judged by the existing extracted STA path (set_spef plus extracted
mode); this module adds the extractor, not a second STA engine.
The stable public API lives in include/eos/pv/extract.h and is
versioned per the EOS Code Structure document.
