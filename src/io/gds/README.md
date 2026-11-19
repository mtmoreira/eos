# gds (L1 io)

Reads GDSII stream files into model objects: libraries,
structures, boundaries, paths, and structure references
(`eos/io/gds.h`, `ReadGdsFile`). It is the read side of the
layout file surface whose write side lives in pd/finish
(the EOS-owned stream writer there).

The required input domain is the EOS writer's own output.
Unmodeled elements (AREF, TEXT, NODE, BOX) and unknown
records are skipped by their declared length; malformed
streams fail fast and total, naming the file and the
record offset. The stable public API lives in include/
and is versioned per the EOS Code Structure document.
