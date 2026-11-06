// gui: the EOS startup splash screen (internal, Qt only).
//
// Shown on eos-gui startup, before the main window, and closed
// once the main window is ready AND the banner has been visible
// for at least 2 seconds (see kMinimumVisibleMs): the owner
// asked for the minimum so the banner is actually seen even
// when the window is ready sooner. That minimum is the only
// artificial wait in the splash. The status line reports real
// startup stages set by RunGui (session start, interface build,
// window open), never a fake progress sequence. The artwork is
// the committed dark logo (docs/logo/eos-logo-dark.webp,
// converted to PNG in resources/; see README.md), rendered from
// its actual bytes, never a redraw. No Q_OBJECT here, matching
// the rest of the module: no signals or slots, so no moc step
// is needed.
#ifndef EOS_UI_GUI_SPLASH_SCREEN_H_
#define EOS_UI_GUI_SPLASH_SCREEN_H_

#include <QElapsedTimer>
#include <QSplashScreen>
#include <QString>

class QPaintEvent;
class QPixmap;
class QShowEvent;
class QWidget;

namespace eos::ui {

// The banner: dark background, the neon eos logo, the
// EDA OPEN SYSTEM subtitle, a progress bar, and a status line.
// Progress is set only by SetStage calls from the real startup
// path; the splash never advances itself.
class SplashScreen : public QSplashScreen {
 public:
  // A null logo disables the artwork (the splash still shows
  // the subtitle, bar, and status); RunGui skips the splash
  // entirely when the logo resource fails to load. QSplashScreen
  // is always top-level, so this takes no parent widget.
  explicit SplashScreen(const QPixmap& logo);

  // Reports one real startup stage and its share of startup.
  // percent is clamped to [0, 100]; the bar repaints at once.
  void SetStage(const QString& message, int percent);

  // Minimum time the splash stays visible from when it first
  // appears, in milliseconds. The owner asked for 2 seconds so
  // the banner is actually seen even when the main window is
  // ready sooner. Close at max(window-ready time, shown time
  // plus this minimum). This is the only artificial wait in
  // the splash: stages still report real startup steps only.
  static constexpr int kMinimumVisibleMs = 2000;

  // Milliseconds still to wait before closing, given the
  // milliseconds elapsed since the splash first appeared.
  // Zero means the minimum already passed: close at once.
  // Never negative; a negative elapsed time counts as zero.
  static int RemainingVisibleMs(int elapsed_ms);

  // Closes the splash once the minimum visible time has
  // passed: at once when 2 seconds already elapsed, otherwise
  // after the remainder, without blocking the GUI thread (the
  // wait is a single-shot timer on the running event loop).
  // The main window is already shown when this is called (see
  // RunGui). The splash deletes itself after closing.
  void FinishWhenReady(QWidget* main_window);

  // Splash pixmap size, fixed so the layout below is stable.
  static constexpr int kWidth = 800;
  static constexpr int kHeight = 500;

 protected:
  void paintEvent(QPaintEvent* event) override;
  // Starts the visible-time clock on first appearance.
  void showEvent(QShowEvent* event) override;

 private:
  QPixmap logo_;
  QString message_;
  int percent_ = 0;
  QElapsedTimer shown_timer_;
};

}  // namespace eos::ui

#endif  // EOS_UI_GUI_SPLASH_SCREEN_H_
