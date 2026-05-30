// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

#include "Theme.h"

// Application-wide appearance: current theme + two font sizes (UI and the
// monospace hex editor). Setters apply immediately, persist to QSettings, and
// emit changed() so custom-painted widgets repaint. A Meyers singleton; access
// via ThemeManager::instance().
class ThemeManager : public QObject {
    Q_OBJECT
public:
    static ThemeManager &instance();

    const Theme &theme() const { return m_theme; }
    ThemeId currentId() const { return m_theme.id; }
    int uiFontSize() const { return m_uiFont; }
    int hexFontSize() const { return m_hexFont; }

    void setThemeId(ThemeId id);
    void setUiFontSize(int pt);
    void setHexFontSize(int pt);

    // Read persisted preferences and apply them once (single changed()).
    void loadFromSettings();

    static Theme makeTheme(ThemeId id);
    static ThemeId idFromName(const QString &name);
    static QString nameFromId(ThemeId id);

signals:
    void changed();

private:
    explicit ThemeManager(QObject *parent = nullptr) : QObject(parent) {}
    void apply();  // style + palette + UI font

    Theme m_theme = makeTheme(ThemeId::Light);
    int m_uiFont = 10;
    int m_hexFont = 10;
};
