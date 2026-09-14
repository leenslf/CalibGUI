#ifndef CALIBGUI_THEME_H
#define CALIBGUI_THEME_H

#include <QString>

// Color system shared by every CalibGUI screen: a neutral gray scale for
// backgrounds/borders/text, plus a single accent color for anything actionable or
// selected. New UI should draw from these tokens rather than hardcoding hex values
// inline, so the whole app reads as one consistent palette.
static const QString kColorSurface       Q_DECL_UNUSED = "#ffffff";
static const QString kColorBorder        Q_DECL_UNUSED = "#cbd2d9";
static const QString kColorHover         Q_DECL_UNUSED = "#e4e7eb";
static const QString kColorText          Q_DECL_UNUSED = "#1f2933";
static const QString kColorTextMuted     Q_DECL_UNUSED = "#52606d";
static const QString kColorAccent        Q_DECL_UNUSED = "#2f6fed";
static const QString kColorAccentHover   Q_DECL_UNUSED = "#2557c7";
static const QString kColorAccentPressed Q_DECL_UNUSED = "#1d439c";
static const QString kColorAccentMuted   Q_DECL_UNUSED = "#a9c2f5";
static const QString kColorDanger        Q_DECL_UNUSED = "#c0392b";
static const QString kColorDangerMuted   Q_DECL_UNUSED = "#fbeae9";

// Widget styles shared across screens, built from the color tokens above. New UI
// should reuse these rather than hand-rolling another QPushButton/QLabel stylesheet.
static const QString kPrimaryButtonStyle Q_DECL_UNUSED =
  "QPushButton {"
  "  background-color: " + kColorAccent + "; color: white; border: none;"
  "  border-radius: 6px; padding: 10px 28px; font-weight: bold; font-size: 14px;"
  "}"
  "QPushButton:hover:!disabled { background-color: " + kColorAccentHover + "; }"
  "QPushButton:pressed { background-color: " + kColorAccentPressed + "; }"
  "QPushButton:disabled { background-color: " + kColorAccentMuted + "; color: #eef2fb; }";

static const QString kSecondaryButtonStyle Q_DECL_UNUSED =
  "QPushButton {"
  "  background-color: transparent; color: " + kColorTextMuted + ";"
  "  border: 1px solid " + kColorBorder + "; border-radius: 6px;"
  "  padding: 6px 20px; font-size: 13px;"
  "}"
  "QPushButton:hover { background-color: " + kColorHover + "; }"
  "QPushButton:pressed { background-color: " + kColorBorder + "; }";

static const QString kIconButtonStyle Q_DECL_UNUSED =
  "QPushButton {"
  "  background-color: " + kColorSurface + "; color: " + kColorTextMuted + ";"
  "  border: 1px solid " + kColorBorder + ";"
  "  border-radius: 6px; font-size: 16px; padding: 2px;"
  "}"
  "QPushButton:hover { background-color: " + kColorHover + "; }"
  "QPushButton:pressed { background-color: " + kColorBorder + "; }";

// Same footprint as kIconButtonStyle but in the danger color, for destructive
// icon-only actions (e.g. clearing saved calibration frames).
static const QString kIconButtonDangerStyle Q_DECL_UNUSED =
  "QPushButton {"
  "  background-color: " + kColorSurface + "; color: " + kColorDanger + ";"
  "  border: 1px solid " + kColorDanger + ";"
  "  border-radius: 6px; font-size: 16px; padding: 2px;"
  "}"
  "QPushButton:hover:!disabled { background-color: " + kColorDangerMuted + "; }"
  "QPushButton:pressed { background-color: " + kColorDanger + "; color: white; }"
  "QPushButton:disabled { color: " + kColorBorder + "; border-color: " + kColorBorder + "; }";

// Tighter padding/font than kSecondaryButtonStyle, for a slim header row that
// shouldn't compete visually with the main action buttons below the video feed.
static const QString kCompactButtonStyle Q_DECL_UNUSED =
  "QPushButton {"
  "  background-color: transparent; color: " + kColorTextMuted + ";"
  "  border: 1px solid " + kColorBorder + "; border-radius: 5px;"
  "  padding: 3px 12px; font-size: 12px;"
  "}"
  "QPushButton:hover { background-color: " + kColorHover + "; }"
  "QPushButton:pressed { background-color: " + kColorBorder + "; }";

// Compact counterpart to kToggleButtonStyle, for the same slim header row.
static const QString kCompactToggleButtonStyle Q_DECL_UNUSED =
  "QPushButton {"
  "  background-color: transparent; color: " + kColorTextMuted + ";"
  "  border: 1px solid " + kColorBorder + "; border-radius: 5px;"
  "  padding: 3px 12px; font-size: 12px;"
  "}"
  "QPushButton:hover:!disabled { background-color: " + kColorHover + "; }"
  "QPushButton:checked { background-color: " + kColorAccent + "; color: white; border: none; }"
  "QPushButton:checked:hover { background-color: " + kColorAccentHover + "; }"
  "QPushButton:disabled { color: " + kColorBorder + "; }";

// Larger, higher-weight variant of kPrimaryButtonStyle for the one action on a
// screen that should visually dominate (e.g. "Kalibre Et").
static const QString kPrimaryButtonLargeStyle Q_DECL_UNUSED =
  "QPushButton {"
  "  background-color: " + kColorAccent + "; color: white; border: none;"
  "  border-radius: 8px; padding: 14px 32px; font-weight: bold; font-size: 16px;"
  "}"
  "QPushButton:hover:!disabled { background-color: " + kColorAccentHover + "; }"
  "QPushButton:pressed { background-color: " + kColorAccentPressed + "; }"
  "QPushButton:disabled { background-color: " + kColorAccentMuted + "; color: #eef2fb; }";

// Checkable button: looks like kSecondaryButtonStyle unchecked, accent-filled when
// checked (undistort preview on), and greyed out when disabled (no calibration file).
static const QString kToggleButtonStyle Q_DECL_UNUSED =
  "QPushButton {"
  "  background-color: transparent; color: " + kColorTextMuted + ";"
  "  border: 1px solid " + kColorBorder + "; border-radius: 6px;"
  "  padding: 6px 20px; font-size: 13px;"
  "}"
  "QPushButton:hover:!disabled { background-color: " + kColorHover + "; }"
  "QPushButton:checked { background-color: " + kColorAccent + "; color: white; border: none; }"
  "QPushButton:checked:hover { background-color: " + kColorAccentHover + "; }"
  "QPushButton:disabled { color: " + kColorBorder + "; }";

// Small circular "i" info button (fixed 22x22, border-radius half that => perfect circle).
static const QString kInfoButtonStyle Q_DECL_UNUSED =
  "QPushButton {"
  "  background-color: " + kColorAccent + "; color: white; border: none;"
  "  border-radius: 11px; font-weight: bold; font-size: 12px;"
  "}"
  "QPushButton:hover { background-color: " + kColorAccentHover + "; }"
  "QPushButton:pressed { background-color: " + kColorAccentPressed + "; }";

static const QString kCameraListStyle Q_DECL_UNUSED =
  "QListWidget {"
  "  background-color: " + kColorSurface + "; border: 1px solid " + kColorBorder + ";"
  "  border-radius: 6px; outline: none;"
  "}"
  "QListWidget::item { padding: 10px 12px; font-size: 13pt; color: " + kColorText + "; }"
  "QListWidget::item:hover:!selected { background-color: " + kColorHover + "; }"
  "QListWidget::item:selected { background-color: " + kColorAccent + "; color: white; }";

static const QString kHeaderLabelStyle Q_DECL_UNUSED =
  "color: " + kColorText + "; font-size: 13pt; font-weight: 600;";

static const QString kStatusLabelStyle Q_DECL_UNUSED =
  "color: " + kColorTextMuted + "; font-size: 11pt; padding: 4px 2px;";

static const QString kDetailLabelStyle Q_DECL_UNUSED =
  "color: " + kColorTextMuted + "; font-size: 11pt; padding: 4px 2px;";

// Smaller/quieter than kDetailLabelStyle, for secondary status text that sits
// alongside buttons in a slim header row (e.g. the saved-frame counter).
static const QString kMutedSmallLabelStyle Q_DECL_UNUSED =
  "color: " + kColorTextMuted + "; font-size: 9pt;";

#endif
