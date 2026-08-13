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

#endif
