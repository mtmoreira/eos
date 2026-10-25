# session (L5 session)

Owns the Session and Project objects every flow runs inside.

Status: Project implemented (Phase 1 increment 5). A Project owns the open design, the scenario set, the constraint set and the state ledger as one unit, and saves and loads the single-file project checkpoint (magic "EOSPROJ 1") that embeds the L2 checkpoint text and the ledger snapshot text: validated, atomic save; all-or-nothing load that reports constraints marked unresolved on restore, exactly as the L2 checkpoint load does. The interactive Session object arrives with the flows that need it.
The stable public API of this module lives in include/ and is versioned per the EOS Code Structure document.
