# Provenance: tcl (linenoise)

Upstream: linenoise by Salvatore Sanfilippo (antirez).
Pinned version or commit: commit
a473823d74b93eab2ba83480df16ed37617493f2 (2026-05-02, "Add bracketed
paste folding"), pinned 2026-10-29 for the interactive shell's line
editing.
Licence: BSD-2-Clause (the upstream LICENSE file is kept in this
directory).
Imported: 2026-10-29, copied from a pristine shallow checkout of the
pinned commit. No upstream file was modified during the import.

## Inventory (2 imported linenoise files, plus the licence)

- src/linenoise.c (upstream linenoise.c, SHA-256
  4bc28faf2a46ccaea11d07aa056e21aa2e9a748b4dc6962346a3f658593103a8)
- src/linenoise.h (upstream linenoise.h, SHA-256
  5f94c6295e3b62e0f8b4b62b0a2f351433d0b6b21029607aec0c9abbc60acff7)
- LICENSE (upstream LICENSE, BSD-2-Clause)

The linenoise sources compile into the tcl module's library target;
they are not part of the module's public API. EOS-owned shell code in
src/tcl.cc calls the linenoise API for the interactive loop only:
history, tab completion of `::eos::` command names, and a persistent
history file. Piped input keeps working: linenoise reads piped stdin
with a plain line reader and prints no prompt there.
