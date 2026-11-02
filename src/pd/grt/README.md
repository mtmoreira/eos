# grt (L4 engines)

OpenROAD global routing: the GlobalRouter and FastRoute engines,
imported as a P2 support module. The placement path links grt as
the routability engine the global placer calls (RouteBase sets
the critical-nets percentage to 0 on that path). The EOS-owned
factory in include/grt/MakeGlobalRouter.h constructs a router
with its heat-map data source; upstream's Tcl factory is not
imported.

Known gap (P2, see PATCHES.md): the wire-parasitics builder
(MakeWireParasitics) is not ported, because its Corner API has no
in-tree equivalent. findRouting with a nonzero critical-nets
percentage, estimateRC, and routeLayerLengths fail loudly rather
than silently doing nothing. P4 global route with critical nets
above 0, and P6 estimateRC, must restore the ported builder
first.

See PROVENANCE.md (import inventory, licence audit) and
PATCHES.md (port adaptations, including the fmt 9.1 formatter and
the sta/Set.hh shim).
