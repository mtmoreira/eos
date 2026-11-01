# gui: provenance

Module: src/ui/gui, target eda_ui_gui, layer L6 interface. Adopted
by GUI build unit U1 (owner approval 2026-10-31, minimal
Elaboration slice; Qt decided 2026-10-31).

## Upstream

None. This module contains no imported code. All sources are
EOS-authored.

## EOS-owned files

- include/eos/ui/gui.h and src/gui_session.cc: SessionView over
  the registry, plus the RunGui application entry declaration.
- internal/main_window.h, src/main_window.cc, src/gui_app.cc: Qt
  widgets and application entry, built only when Qt6 is found.
- CMakeLists.txt: EOS-owned build for the module.

## Declared external dependency

Qt6 (Widgets), declared external, never vendored. Install:
Debian/Ubuntu `qt6-base-dev qt6-base-dev-tools`, macOS
`brew install qt`. Qt's LGPL terms are flagged for licence review
at distribution time; this is a flag, not a licence commitment.

## Licence audit

No imported code, so no import licence applies. EOS's own licence
is undecided (deferred per the plan); this module makes no licence
commitment.
