# scenarios (L2 design-data)

Owns scenario and mode and corner objects that analyses are evaluated under.

Status: implemented (Phase 1). A scenario is its (mode, corner, parasitics level) triple; the set enforces unique names and unique triples, and tracks the active set that report bindings snapshot.
The stable public API of this module lives in include/ and is versioned per the EOS Code Structure document.
