# session (L5 session)

Owns the Session and Project objects every flow runs inside.

Status: Project implemented (Phase 1 increment 5; report bindings became project state in Phase 2 unit 2.0). A Project owns the open design, the scenario set, the constraint set, the state ledger and the report bindings as one unit, and saves and loads the single-file project checkpoint (magic "EOSPROJ 1") that embeds the L2 checkpoint text, the ledger snapshot text and the report bindings text: validated, atomic save; all-or-nothing load that reports constraints marked unresolved on restore, exactly as the L2 checkpoint load does, and report bindings restored or dropped (a binding whose ledger artifact is absent from the restored ledger is dropped and counted, never silently kept). Project files written before bindings became project state have no reports section and load with an empty binding set. The interactive Session object arrives with the flows that need it.
The stable public API of this module lives in include/ and is versioned per the EOS Code Structure document.
