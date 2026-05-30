// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

enum class ThemeId { Light, Dark, Hacker, Amber };

// A complete appearance: the standard QPalette plus the accent colors the
// custom-painted widgets (HexView, ZifSocketView, ChipSelectDialog) need —
// every value that used to be a hardcoded literal in those widgets.
struct Theme {
    ThemeId id = ThemeId::Light;
    QString name = "Light";
    QPalette palette;

    QColor dirtyByte;        // HexView: unsaved-edit bytes
    QColor socketBody;       // ZIF socket body
    QColor socketText;       // ZIF "IC↑" arrow + label
    QColor leverStem;        // ZIF lever rod
    QColor leverBallHi;      // ZIF lever ball radial-gradient highlight
    QColor leverBallLo;      // ZIF lever ball radial-gradient shadow
    QColor leverBallOutline; // ZIF lever ball outline
    QColor pinIdle;          // ZIF unused pins
    QColor pinActive;        // ZIF pins under the chip body
    QColor pinText;          // ZIF pin-number text
    QColor chipBody;         // ZIF chip overlay (keeps alpha)
    QColor pin1Marker;       // ZIF pin-1 dot + caption
    QColor unsupportedText;  // ChipSelectDialog: Windows-only rows
};
