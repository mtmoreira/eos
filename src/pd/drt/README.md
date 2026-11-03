# drt (L4 engines)

OpenROAD detailed routing (TritonRoute) at ead3236, imported in
P5 as EOS's own code: FlexDR (detailed route with rip-up and
reroute), FlexTA (track assignment), FlexPA (pin access), FlexRP
(region preparation), FlexGR, and FlexGC (design-rule checking
markers). The EOS-owned driver is src/pd/route, which runs the
global router first and then this engine; this module has no
registry command of its own.

What the engine reports, as measured on the run: the violation
count is the number of DRC markers the detailed stage leaves in
the design (TritonRoute::getNumDRVs), and the EOS runner reports
it together with the loop iterations used against the iteration
bound (see src/pd/route/README.md).

What it does not do: distributed routing. The dst backend is not
imported (see PATCHES.md section 2); the EOS runner always runs
single-host with a null distributed backend, and any distributed
call fails loudly instead of silently. There is no Tcl factory:
EOS constructs TritonRoute directly.

Preconditions are the route runner's (see src/pd/route/README.md):
technology data, a mapped design, a floorplanned block, placed
instances, a Liberty library, and guides from the global stage.
