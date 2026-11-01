# gui (L6 interface)

The GUI adapter over the command registry, thin by design: every GUI
action issues a registry command and renders its machine-readable
result, so the CLI and the GUI cannot drift.

Status: units U1, U2, U3, U4, and U5 of the minimal Elaboration slice
(owner approval 2026-10-31, Qt decided 2026-10-31). U1 delivers
the skeleton: the window, the stage bar (Elaboration active,
later stages greyed), the Read/Elaborate/Generic synth stepper
with per-step outputs from the session record, the console echo
log, and the registry wiring (SessionView). U2 delivers the
hierarchy browser plus data model: the synth_hierarchy registry
query over the live elaborated design, the HierarchyModel with
its selection contract for U3, and the browser widget (tree,
expand and collapse, click to select) beside the step outputs.
U3 delivers the block diagram plus navigation: the synth_blocks
registry query over the live elaborated design, the DiagramModel
with its breadcrumb helper, and the diagram widget (blocks with
labeled nets, double-click to descend, breadcrumb to ascend,
cross-selection with the tree both directions) in the center,
replacing the step-output label. U4 delivers the gate-level
schematic at the leaf level: the synth_schematic registry query
over the live elaborated design, the SchematicModel with its
leaf test, and the schematic widget (gates with labeled nets,
breadcrumb to ascend) in the center, replacing the block diagram
exactly at leaf levels, as the bottom of the same navigation
stack. The schematic is structural only: no delay, slew,
capacitance, or slack annotation. U5 delivers the right panels:
the synth_stats registry query over the live design, the
PanelsModel, and the panels widget (Sources, Results, Opt log,
Messages tabs) in the middle splitter. Clicking a stepper step
shows that step's outputs in the panels (Read shows Sources,
Elaborate shows Results, Generic synth shows the Opt log); the
stepper keeps only its per-step state, and the detailed outputs
live in the panels. Messages are diagnostics only: a rejected
dispatch records one line, a successful dispatch records none.
Console command entry (U6) and packaging (U7) are later units,
not this one.

## Build

Qt6 is optional at configure time. Without it, the module builds
its Qt-free logic and tests, and the eos-gui app target is skipped;
with it, the widgets and the app build too. CI installs Qt6, so CI
always builds the full GUI. Install: Debian/Ubuntu
`qt6-base-dev qt6-base-dev-tools`, macOS `brew install qt`.

Qt is a declared external dependency, never vendored. Its LGPL terms
are flagged for licence review at distribution time (see
PROVENANCE.md); that is a flag, not a licence commitment.

## Run

The eos-gui binary mirrors the eos shell startup: it builds the
registry, registers the core commands, optionally runs script files
through the Tcl shell on the shared context, then opens the window
on the same registry and context. Without a Tcl build, script files
are ignored with a warning and the window still opens on an empty
session. Console command entry arrives in U6; in U1 the console
shows GUI-issued commands (none yet from the UI itself, so a
script-loaded session shows state in the stepper and center while
the console starts empty).

## Design notes

- Widgets use plain callbacks, never Qt signals, so no Q_OBJECT and
  no moc step is needed in this module.
- SessionView is the only session access widgets use. Widgets never
  keep design state of their own.
- No mock numbers anywhere in the app: every number shown comes
  from the live session record.
