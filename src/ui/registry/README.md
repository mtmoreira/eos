# registry (L6 interface)

The command registry every interface binds to.

Status: implemented (Phase 1). CommandRegistry with the Plan Section 12 diagnostic taxonomy, a CommandContext holding the open design, scenarios, constraints, ledger and collections, and the core command set (28 commands: design lifecycle and construction, scenarios, constraints with coverage and rebind, glob-based collections, ledger transactions and report artifacts, checkpoint save; project restore answers unsupported_feature until the session layer composes it in increment 5). Collections are live ID handles: names resolve at list time, missing objects read as <deleted>.
The stable public API of this module lives in include/ and is versioned per the EOS Code Structure document.
