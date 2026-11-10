# waivers (L2 design-data)

Owns waiver records: named approvals for named violations, with
exact matching and no silent drops.

Status: implemented (Immediate waiver-model unit). Every waiver is
stored with its kind, class, target exactly as written, scene
scope, reason, approver, optional expiry date, and the ledger
revision at creation. Matching is exact on kind, class, target,
and scene; there are no wildcards in v1. Status is derived, never
stored: active when the waiver matches at least one current
violation, unmatched when it matches nothing, expired past its
date. A waiver never changes what the engine checks: it is a
reporting disposition only, applied at report time over
violations the engine just computed.
The stable public API of this module lives in include/ and is versioned per the EOS Code Structure document.
