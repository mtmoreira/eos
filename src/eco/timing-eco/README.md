# timing-eco (L4 engines)

The v1 timing ECO adapters: gate sizing and buffer insertion over
the ODB facade, for use inside an ECO transaction.

Status: implemented in Phase 2 unit 2.4. Sizing swaps an instance
to a same-pin drive variant (function preserved by construction);
buffer insertion splits a net, moving every load behind a new
buffer placed at the driver instance's location (the disclosed v1
placement simplification; no legalizer runs yet). Port-driven or
port-loaded nets are refused in v1. The stable public API lives
in include/eos/eco/timing_eco.h.
