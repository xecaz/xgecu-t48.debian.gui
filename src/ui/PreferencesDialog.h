// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

#include "Theme.h"

class QComboBox;
class QSpinBox;

// Appearance preferences: theme + UI/hex font sizes. Changes preview live via
// ThemeManager (which also persists them); Cancel restores the values that
// were active when the dialog opened.
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

private:
    void restoreOriginals();

    QComboBox *m_themeBox = nullptr;
    QSpinBox *m_uiFont = nullptr;
    QSpinBox *m_hexFont = nullptr;

    ThemeId m_origTheme;
    int m_origUiFont;
    int m_origHexFont;
};
