// gui: the EOS startup splash screen (internal, Qt only).
#include "splash_screen.h"

#include <QFont>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QRect>
#include <QShowEvent>
#include <QString>
#include <QTimer>

namespace eos::ui {

namespace {

// The version string on the banner, matching the approved
// banner artwork. The project CMake version is still 0.0.0;
// update this constant when the project adopts a real version.
const char* kVersion = "Version 0.1";
const char* kSubtitle = "EDA OPEN SYSTEM";

int ClampPercent(int percent) {
  if (percent < 0) return 0;
  if (percent > 100) return 100;
  return percent;
}

}  // namespace

SplashScreen::SplashScreen(const QPixmap& logo)
    : QSplashScreen(QPixmap()), logo_(logo) {
  setFixedSize(kWidth, kHeight);
}

void SplashScreen::SetStage(const QString& message, int percent) {
  message_ = message;
  percent_ = ClampPercent(percent);
  repaint();
}

int SplashScreen::RemainingVisibleMs(int elapsed_ms) {
  if (elapsed_ms < 0) elapsed_ms = 0;
  const int remaining = kMinimumVisibleMs - elapsed_ms;
  return remaining > 0 ? remaining : 0;
}

void SplashScreen::FinishWhenReady(QWidget* main_window) {
  const int elapsed_ms =
      shown_timer_.isValid() ? static_cast<int>(shown_timer_.elapsed()) : 0;
  const int remaining = RemainingVisibleMs(elapsed_ms);
  if (remaining <= 0) {
    finish(main_window);
    deleteLater();
    return;
  }
  // The single-shot timer needs no Q_OBJECT on this class: the
  // receiver is only a QObject lifetime guard, and the work is
  // a lambda. If the splash is destroyed first, Qt cancels
  // the timer instead of calling into a dead object.
  QTimer::singleShot(remaining, this, [this, main_window]() {
    finish(main_window);
    deleteLater();
  });
}

void SplashScreen::showEvent(QShowEvent* event) {
  QSplashScreen::showEvent(event);
  if (!shown_timer_.isValid()) shown_timer_.start();
}

void SplashScreen::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  // Background sampled from the logo's own corner pixel, so the
  // square logo blends into the canvas with no visible seam,
  // whatever the exact dark shade of the committed logo is.
  QColor background(10, 12, 16);
  if (!logo_.isNull()) {
    background = logo_.toImage().pixelColor(0, 0);
  }
  painter.fillRect(rect(), background);

  if (!logo_.isNull()) {
    const int side = 300;
    const QPixmap scaled =
        logo_.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const int x = (kWidth - scaled.width()) / 2;
    painter.drawPixmap(x, 20, scaled);
  }

  QFont subtitle_font = font();
  subtitle_font.setPointSize(15);
  subtitle_font.setLetterSpacing(QFont::PercentageSpacing, 250);
  painter.setFont(subtitle_font);
  painter.setPen(Qt::white);
  painter.drawText(QRect(0, 345, kWidth, 30), Qt::AlignCenter, kSubtitle);

  const int bar_x = 150;
  const int bar_w = kWidth - 2 * bar_x;
  const int bar_y = 400;
  const int bar_h = 6;
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(42, 47, 58));
  painter.drawRoundedRect(bar_x, bar_y, bar_w, bar_h, 3, 3);
  painter.setBrush(QColor(0, 229, 255));
  const int fill_w = bar_w * percent_ / 100;
  if (fill_w > 0) {
    painter.drawRoundedRect(bar_x, bar_y, fill_w, bar_h, 3, 3);
  }

  QFont status_font = font();
  status_font.setPointSize(11);
  painter.setFont(status_font);
  painter.setPen(QColor(199, 205, 216));
  painter.drawText(QRect(0, 425, kWidth, 25), Qt::AlignCenter, message_);

  QFont version_font = font();
  version_font.setPointSize(10);
  painter.setFont(version_font);
  painter.setPen(QColor(138, 145, 158));
  painter.drawText(QRect(0, 465, kWidth - 20, 20), Qt::AlignRight, kVersion);
}

}  // namespace eos::ui
