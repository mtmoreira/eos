# gui (L6 interface)

The GUI adapter over the command registry, thin by design: every GUI
action issues a registry command and renders its machine-readable
result, so the CLI and the GUI cannot drift.

Status: unit U1 of the minimal Elaboration slice (owner approval
2026-10-31, Qt decided 2026-10-31). U1 delivers the skeleton: the
window, the stage bar (Elaboration active, later stages greyed),
the Read/Elaborate/Generic synth stepper with per-step outputs from
the session record, the console echo log, and the registry wiring
(SessionView). Hierarchy browser (U2), block diagram and navigation
(U3), leaf schematic (U4), right panels (U5), console command entry
(U6), and packaging (U7) are later units, not this one.

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
