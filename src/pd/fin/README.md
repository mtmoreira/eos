# fin (L4 engines)

OpenROAD chip finish (Finale metal density fill), imported as
EOS-owned code from the OpenDB pin commit ead3236.

Status: implemented under unit P7 of the place-and-route phase, as
the finish runner's metal-fill engine (see src/pd/finish). The
imported files are byte-identical copies of the upstream files;
see PROVENANCE.md for the SHA-256 table and PATCHES.md for the
EOS-owned adaptations (none touch an imported file). The Tcl/SWIG
factory is not imported: the finish runner constructs fin::Finale
directly.

The stable public API for consumers is fin/Finale.h (Finale::init,
Finale::densityFill); the finish module is the only consumer.
