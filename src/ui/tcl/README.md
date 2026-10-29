# tcl (L6 interface)

The EOS Tcl dialect over the command registry.

Status: implemented (Phase 1). Every registry command is bound as ::eos::<name> in a Tcl 8.6 interpreter, dispatching to the same registry, so semantics are identical across interfaces. Command failure becomes a Tcl error carrying "EOS <diag_name>: <message>". When the build finds no Tcl development package, the module compiles a stub whose Create reports unsupported, so the build never breaks on a missing optional dependency.

Interactive loop: the shell reads through linenoise (vendored in src/, see PROVENANCE.md), so a terminal session gets history (arrow keys, Ctrl-C cancels the line without leaving the shell) and tab completion of ::eos:: command names, with history persisted to .eos_history in the directory the shell is launched from, saved after every command so any exit path keeps it. Piped stdin reads plainly with no prompt and touches no history. Option-name completion is not implemented yet.
The stable public API of this module lives in include/ and is versioned per the EOS Code Structure document.
