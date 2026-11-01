// eos-gui: the EOS GUI binary. Main and startup only, no logic:
// build the registry, register the core commands, optionally run
// script files through the Tcl shell on the shared context, then
// open the Qt GUI on the same registry and context.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "eos/ui/gui.h"
#include "eos/ui/registry.h"
#include "eos/ui/tcl_shell.h"

int main(int argc, char** argv) {
  eos::ui::CommandRegistry registry;
  eos::ui::RegisterCoreCommands(registry);
  eos::ui::CommandContext context;
  if (argc > 1) {
    eos::foundation::Result<std::unique_ptr<eos::ui::TclShell>> shell =
        eos::ui::TclShell::Create(registry, context);
    if (!shell.ok()) {
      std::fprintf(stderr,
                   "eos-gui: no Tcl shell, ignoring script files: %s\n",
                   shell.status().message().c_str());
    } else {
      std::vector<std::string> files(argv + 1, argv + argc);
      const int rc = shell.value()->RunFiles(files);
      if (rc != 0) return rc;
    }
  }
  return eos::ui::RunGui(argc, argv, registry, context);
}
