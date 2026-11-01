// gui: the Qt application entry point (Qt only).
#include <QApplication>

#include "eos/ui/gui.h"
#include "main_window.h"

namespace eos::ui {

int RunGui(int argc, char** argv, CommandRegistry& registry,
           CommandContext& context) {
  QApplication app(argc, argv);
  SessionView view(registry, context);
  MainWindow window(view);
  window.show();
  return app.exec();
}

}  // namespace eos::ui
