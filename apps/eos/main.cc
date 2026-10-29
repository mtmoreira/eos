// eos: the EOS tool binary. Main and startup only, no logic: build the
// registry, register the core commands, open the Tcl shell over a
// fresh command context, then run script files or the interactive
// loop.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "eos/ui/registry.h"
#include "eos/ui/tcl_shell.h"

int main(int argc, char** argv) {
  eos::ui::CommandRegistry registry;
  eos::ui::RegisterCoreCommands(registry);
  eos::ui::CommandContext context;
  eos::foundation::Result<std::unique_ptr<eos::ui::TclShell>> shell =
      eos::ui::TclShell::Create(registry, context);
  if (!shell.ok()) {
    std::fprintf(stderr, "eos: %s\n", shell.status().message().c_str());
    return 1;
  }
  if (argc > 1) {
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
      files.emplace_back(argv[i]);
    }
    return shell.value()->RunFiles(files);
  }
  return shell.value()->RunInteractive();
}
