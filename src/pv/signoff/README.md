# signoff (L4 engines)

Signoff verdicts from the external PV evidence reports: the
parsed record behind the Signoff panel (full-flow GUI unit
F13).

Status: implemented in the F13 panel unit. The module parses
`docs/gcd-pv-results.md` (KLayout DRC, antenna, and the
KLayout-extraction LVS path) and `docs/gcd-lvs-magic.md`
(the Magic-extraction LVS path) into one verdict record:
DRC per-family counts, LVS CORRECT/INCORRECT/NOT COMPARED
per path, and the named discrepancies D1 and D2 with the
successor residual, each with its report path. It runs no
engine and reads no layout: the reports are the evidence,
produced outside EOS by the pinned KLayout, Magic, and
Netgen runs. A later unit owns the in-tree DRC engine, then
connectivity LVS; this module only reads what the external
runs already measured.

The stable public API lives in include/ and is versioned per
the EOS Code Structure document.
