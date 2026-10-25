# constraints (L2 design-data)

Owns constraint objects bound to design objects, with no silent drops.

Status: implemented (Phase 1). Every constraint is stored with its target spec as written and a binding status (bound, unresolved, empty); resolution runs at Add and on RebindAll, and Coverage() is the accounting the Phase 1 exit gate uses. Collections-based targets arrive with the interface layer in increment 4.
The stable public API of this module lives in include/ and is versioned per the EOS Code Structure document.
