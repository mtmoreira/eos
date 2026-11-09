# gui (L6 interface)

The GUI adapter over the command registry, thin by design: every GUI
action issues a registry command and renders its machine-readable
result, so the CLI and the GUI cannot drift.

Status: units U1, U2, U3, U4, U5, and U6 of the minimal Elaboration slice
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
Panel rework (after the natural level-based schematic layout):
the center becomes one navigation stack: the block diagram
for non-leaf levels, and the gate-level schematic for the
leaf level, so the center shows the schematic exactly when
the user descends to a leaf and the block diagram otherwise
(the top level keeps its block view). The panels are five
tabs at that point (Issues, Sources, Results, Opt log, Messages), and the
stepper mapping changes to Read shows Sources, Elaborate
shows Issues, Generic synth shows Opt log. The Issues tab shows the elaboration issues from the
synth_issues registry query (the check pass plus a design
walk), severity ordered, with a header stating the counts and
what was checked; clicking a row selects the instance in the
hierarchy, descends the diagram to it, and highlights the
offending gate in the center schematic. The Floorplan stage label
carries the error count while any error exists. The bottom row
gains a single log window beside the console, showing all
engine output plus the output of every console-dispatched
command, with severity colors, severity filters, and a
search box; the Messages tab keeps dispatch history only.
Clicking a block in the diagram selects it and traces its
fanin and fanout nets, like a gate click in the schematic;
double-clicking the block descends into it, and when the
new level is a leaf the center switches to the schematic for
it. (Earlier this rework put the schematic in the right
panels as its own tab; the owner's 2026-11-06 demo feedback
moved it back to the center, so the center holds both views
and the panels stay at five tabs.)
F2 adds the Timing tab to the right panels: the timing_summary
registry query over the last run_timing, the TimingModel, and
the Timing tab rendering the per-scene slack summary, the
clock table, the worst-violator table, the worst paths, the
coverage summary, and the QoR numbers. The tab refreshes only
when the timing inputs change, so a plain refresh never pays
for fresh STA sessions.
Full-flow GUI unit F1 adds the Synthesis view: the Results tab
gains the mapped synthesis section from the synth_summary
registry query (mapped cells by type, worst slack and TNS
before and after the loop, the winning strategy, and the
equivalence verdict), and SessionView gains SynthesisStep(),
Done once a mapped design exists, as the stepper state contract
unit F11 wires.
F3 adds the floorplan canvas to the center, as the third page
of the center stack beside the block diagram and the
schematic: the floorplan_summary registry query over the live
database (die and core rectangles, row rectangles, IO pin
positions, and the counts), the FloorplanModel, and the
FloorplanDiagram rendering the die outline, the core outline,
the rows, and the placed IO pins with their names, with the
payload counts beside the canvas. A successful floorplan
dispatch takes the user there, and the Floorplan stage button
takes them there too once a floorplan is loaded; hierarchy
navigation takes them back to the hierarchy views. The model
refreshes on every window refresh: the query is a cheap
database walk, so no timing-key guard is needed.
F4 adds the placement canvas to the center, as the fourth page
of the center stack: the place_summary registry query over the
live database plus the stored place record (die and core
rectangles, placed and total counts, HPWL, overlap count, one
rectangle per placed instance, and one post-place slack per
scene the place run measured), the PlacementModel, and the
PlacementDiagram rendering the die outline, the core outline,
and one rectangle per placed instance, colored by master with
a master legend, with the payload counts beside the canvas. A
successful place dispatch takes the user there, and the Place
stage button takes them there too once a placement is loaded;
hierarchy navigation, the Floorplan stage button, and a
successful close_design take them back. The model refreshes on
every window refresh: the query is a cheap database walk plus
the stored place record, so no timing-key guard is needed.
U6 delivers console command entry plus session persistence: the
console gains an entry line that takes one registry command per
Return and dispatches it through the session, so the stepper,
tree, diagram, schematic, and panels update after each command;
the checkpoint indicator at the right of the stepper row shows
the current checkpoint path (none until a checkpoint or restore
succeeds; a successful close_design clears it); and the session
outlives the window. The same process keeps a terminal REPL on
stdin while the window is open, every terminal line is evaluated
on the GUI thread through the Tcl shell so terminal and GUI
commands can never race, closing the window hides it instead of
killing the session, and typing show in the terminal shows the
window again on the same session. eos::gui_show is the registry
command for the same action, so it works from the GUI console as
well as the terminal; run from the plain eos CLI it launches a
separate eos-gui process that keeps running after the CLI ends,
and closing such a viewer quits it, since no terminal is left to
reopen it from. The terminal REPL uses plain
line input: line editing, history, and tab completion stay in
the eos CLI. Console entry takes registry commands only, not
Tcl; the terminal REPL keeps full Tcl, and exit there quits the
process. If the terminal stdin closes while the window is hidden,
the application quits instead of lingering headless, since a
hidden window with no terminal left cannot be reopened.
Packaging (U7) delivered the minimal install rule above. The
app icon lives in src/ui/gui/icons/ and is covered below.

## Console and log routing

A command typed in the GUI console runs through the session
and its output goes to the Log window, shown and saved there,
never to the console transcript: the transcript keeps the
command echo plus one outcome line, "ok" on success and the
one-line diagnostic on failure, so a long report never floods
the console the command was typed in. A rejected dispatch
also records its diagnostic in Messages. The terminal REPL
keeps its current behavior: terminal commands print their
output on the terminal, as before.

## Auto-navigation

As commands run, the window takes the user to the surface
showing the command's results, once per command: the session
records a pending destination on a successful dispatch, and
the next refresh consumes it, so a later refresh never yanks
the user back to a tab they already left. A failed command
navigates nowhere, and a command whose results have no
surface of their own leaves the current tab alone.

- elaborate enables the Synthesis stage in the stage bar and
  shows the Results tab, which carries the synthesis section.
  The Synthesis stage stays disabled until elaboration is
  done, and stays enabled once it is; clicking it shows the
  Results tab.
- synth_generic shows the Opt log; synth_map, synth_opt,
  synth_summary, synth_stats, and check_equivalence show the
  Results tab; synth_hierarchy, synth_blocks, and
  synth_schematic show the hierarchy views; synth_issues
  shows the Issues tab.
- run_timing, report_timing, the check-specific timing
  reports, timing_summary, and check_timing show the Timing
  tab. The remaining report commands (report_clock,
  report_constraint, report_analysis_coverage, report_qor,
  report_bottleneck) show the Results tab.
- read_verilog and read_filelist show the Sources tab.
- floorplan shows the floorplan canvas, as before, through
  the center flag rather than this destination.
- The stepper step clicks still follow the step mapping
  (Read shows Sources, Elaborate shows Issues, Generic synth
  shows the Opt log); only a command completion auto-navigates.
- The terminal REPL dispatches through the Tcl shell, not
  through the session dispatch, so terminal commands never
  auto-navigate; the window still refreshes after each one.

## App icon

The GUI shows the EOS logo as its icon, two mechanisms:

- Window icon, every platform: gui_app.cc sets the
  QApplication window icon from the 512 pixel iconset PNG,
  embedded at configure time as a C array (no runtime file
  lookup). This sets the window and task switcher icon.
- macOS Dock icon: only the .app bundle sets it. A bare
  binary launched from a terminal has no bundle, so macOS
  shows the generic executable icon in the Dock no matter
  what the window icon is. The eos-gui-bundle target
  assembles build/apps/eos-gui/eos-gui.app around the
  already-built binary, carrying eos-icon.icns in Resources
  with an Info.plist naming it. Qt stays an external runtime
  dependency and is never bundled, same as the install rule.

The icon artwork derives from the approved dark logo
(docs/logo/eos-logo-dark.webp, never modified): the full
neon eos wordmark on black, with macOS-style rounded
corners. The iconset (16, 32, 64, 128, 256, 512, 1024 pixels)
and the .icns are committed under src/ui/gui/icons/ and are
reproducible from the logo with:

```
python3 src/ui/gui/icons/generate_icon.py
```

That script needs Pillow and writes the iconset plus the
.icns. On macOS the same iconset can be turned into an
.icns with the system tool instead:
`iconutil -c icns src/ui/gui/icons/eos-icon.iconset`.

Build the bundle and launch it from a terminal (the direct
binary path keeps the terminal REPL working, and the Dock
still shows the bundle icon because the binary lives inside
the .app):

```
cmake --build build --target eos-gui-bundle -j$(sysctl -n hw.ncpu)
build/apps/eos-gui/eos-gui.app/Contents/MacOS/eos-gui
```

Launching with `open` also shows the icon but gives the app
no terminal, so the terminal REPL is unavailable that way.
The bare binary keeps working exactly as before and is the
right launch when the Dock icon does not matter:

```
build/apps/eos-gui/eos-gui
```

## Build

Qt6 is optional at configure time. Without it, the module builds
its Qt-free logic and tests, and the eos-gui app target is skipped;
with it, the widgets and the app build too. CI installs Qt6, so CI
always builds the full GUI. Install: Debian/Ubuntu
`qt6-base-dev qt6-base-dev-tools`, macOS `brew install qt`.

Qt is a declared external dependency, never vendored. Its LGPL terms
are flagged for licence review at distribution time (see
PROVENANCE.md); that is a flag, not a licence commitment.

### macOS

Qt6 comes from Homebrew. If `brew install qt` fails with a qt@5
conflict (qt@5 conflicts with qtbase 6.x), unlink first, then
install:

```
brew unlink qt@5
brew install qt
```

Configure with the Homebrew prefix so CMake finds Qt6 and the
other Homebrew dependencies, then build the GUI target:

```
cmake -S. -B build -DCMAKE_PREFIX_PATH=$(brew --prefix)
cmake --build build --target eos-gui -j$(sysctl -n hw.ncpu)
```

The full build uses the same configure line with `cmake --build
build` and no --target argument.

For the guided version of the same build, run
`scripts/build-macos.sh` from the repository root. It checks the
prerequisites first, configures into `build-fresh`, builds
`eos-gui` and `eos-gui-bundle`, and shows a progress bar with one
line per stage, plus an ASCII EOS logo. The full build log goes to
`gui-build.log` (overridable with `--log FILE`) and ends with an
`exit=` line. The script never runs `git pull`, never asks for
sudo, and never deletes anything.

### Linux (Debian/Ubuntu)

```
sudo apt-get install qt6-base-dev qt6-base-dev-tools
cmake -S. -B build
cmake --build build --target eos-gui -j$(nproc)
```

Drop the CMAKE_PREFIX_PATH argument on Linux, or point it at
your dependency prefix. CI installs the same two Qt packages
(see .github/workflows/ci.yml), so CI always builds the full
GUI.

### Install

The eos-gui target has a minimal install rule (binary only, no
bundled Qt: Qt stays an external runtime dependency):

```
cmake --install build --prefix <dir>
```

That places eos-gui in <dir>/bin. Running from the build tree
needs no install step: the binary is build/apps/eos-gui/eos-gui.

## Run

The eos-gui binary mirrors the eos shell startup: it builds the
registry, registers the core commands, optionally runs script files
through the Tcl shell on the shared context, then opens the window
on the same registry and context and keeps the Tcl shell alive as
the terminal REPL. Without a Tcl build, script files are ignored
with a warning, the terminal REPL is unavailable, and the window
still opens on an empty session with its own console entry.
Typing show in the terminal reopens a hidden window; closing the
window never kills the session.

### Startup splash

On every eos-gui startup, a splash banner shows before the main
window: dark background, the neon eos logo, the EDA OPEN SYSTEM
subtitle, a progress bar, a status line, and the Version 0.1
marker (the version string on the banner matches the approved
banner artwork; the project CMake version is still 0.0.0).
The status line reports real startup stages only: Starting
session, Building interface, Opening window. The splash closes
once the main window is ready AND the banner has been visible
for at least 2 seconds (close at max(window-ready time, shown
time plus 2 seconds)); the wait never blocks the GUI thread.
That 2-second minimum is the only artificial wait in the
splash: there is no timed animation and no fake stage. Script files still run first (see
Launching below); the splash appears at GUI startup after they
finish. The splash shows only at startup: closing and reopening
the window does not show it again, and it never touches the
terminal REPL, the console entry, or the checkpoint indicator.

Logo asset: resources/eos-logo-dark.png is derived from the
committed dark logo docs/logo/eos-logo-dark.webp (PR 74; that
file is never modified). Qt does not reliably read webp, so the
splash uses the PNG. The conversion is PIL decoding the webp to
RGB PNG, verified pixel-exact at 1600x1600 (webp 153,876 bytes,
sha256 starting 99eebe99; PNG 760,419 bytes, sha256 starting
e2d094ac). The PNG is embedded in the binary through
resources/splash.qrc (Qt resource path
:/eos/eos-logo-dark.png), so a bare binary launched from any
directory carries its artwork. If the resource fails to load,
startup skips the splash entirely and proceeds as before. The
resource object lives in a static library, so gui_app.cc calls
the generated initializer qInitResources_splash to force the
linker to pull it in; without that call the linker drops the
unreferenced object and the splash is skipped. To
regenerate the PNG after any logo change, decode the webp with
PIL and re-verify pixel equality before committing.

### Launching

No script file: the window opens on an empty session (steps
Pending, empty tree, "No design loaded"). Drive it from the
console entry line, one registry command per Return, or from the
terminal REPL (below).

With a script file: pass script files as arguments and they run
through the Tcl shell on the shared context before the window
opens, so the window starts on the loaded session:

```
build/apps/eos-gui/eos-gui <script.tcl> [...]
```

demo/synth/elab-demo.tcl is a four-command Elaboration script
for the optloop demo (see demo/synth/README.md).

### Console entry

The console entry line takes one registry command per Return and
dispatches it through the session, so the stepper, tree, diagram,
schematic, and panels update after each command. The transcript
above the entry shows every submitted command followed by its
outcome: the result output on success, the error text on
failure. A pasted submission holding several commands runs each
in turn, each with its own outcome in the transcript. The eos::
prefix is optional (create_scenario and eos::create_scenario
mean the same here). Arguments are whitespace-separated;
double-quoted arguments support backslash escapes for quote and
backslash, and an unterminated quote is rejected. Lines starting
with # and empty lines are ignored. Console entry takes registry
commands only, not Tcl: there is no variable, loop, or procedure
support here; full Tcl lives in the terminal REPL and in script
files.

### Terminal REPL

While the window is open, the same process keeps a terminal REPL
on stdin (prompt eos> on a terminal). Every line is evaluated
through the Tcl shell on the GUI thread, so terminal and GUI
commands can never race, and the window refreshes after each
one. Terminal behaviors:

- show: shows a hidden window again on the same session.
- exit: quits the process.
- Closing the window hides it; the session keeps running.
- If terminal stdin closes while the window is hidden, the
  application quits instead of lingering headless.
- The terminal REPL uses plain line input: line editing,
  history, and tab completion stay in the eos CLI.

Without a Tcl build, the terminal REPL is unavailable and the
window still opens on the shared context with its own console
entry.

### Checkpoint indicator

The right end of the stepper row shows the current checkpoint
path ("Checkpoint: none" until a checkpoint or restore
succeeds). A successful close_design clears it.

## Design notes

- Widgets use plain callbacks, never Qt signals, so no Q_OBJECT and
  no moc step is needed in this module.
- SessionView is the only session access widgets use. Widgets never
  keep design state of their own.
- No mock numbers anywhere in the app: every number shown comes
  from the live session record.
