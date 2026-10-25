# reports (L5 session)

The report registry and the export freshness gate.

Status: implemented (Phase 1 increment 5.2). ReportRegistry::RegisterReport creates a ledger report artifact and its immutable ReportBinding together: the binding snapshots the bound revision, the active scenario set (ids and mode/corner/parasitics triples), the single parasitics level covered when the active set is uniform, the timing model tag and the tool build. ReportStatus reads validity, freshness and proof live from the ledger. The export gate (Evaluate) returns every blocker: report missing, artifact invalid, artifact stale (valid but not current), proof not pass, and incomplete constraint coverage. Bindings are session state and are not part of the project checkpoint; a restore starts with an empty registry.
The stable public API of this module lives in include/ and is versioned per the EOS Code Structure document.
