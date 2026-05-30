// SPDX-License-Identifier: GPL-3.0-or-later
#include "PreferencesDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "ThemeManager.h"

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Preferences");

    ThemeManager &tm = ThemeManager::instance();
    m_origTheme = tm.currentId();
    m_origUiFont = tm.uiFontSize();
    m_origHexFont = tm.hexFontSize();

    m_themeBox = new QComboBox(this);
    for (ThemeId id : {ThemeId::Light, ThemeId::Dark, ThemeId::Hacker, ThemeId::Amber})
        m_themeBox->addItem(ThemeManager::nameFromId(id), static_cast<int>(id));
    m_themeBox->setCurrentIndex(m_themeBox->findData(static_cast<int>(m_origTheme)));

    m_uiFont = new QSpinBox(this);
    m_uiFont->setRange(8, 16);
    m_uiFont->setSuffix(" pt");
    m_uiFont->setValue(m_origUiFont);

    m_hexFont = new QSpinBox(this);
    m_hexFont->setRange(8, 20);
    m_hexFont->setSuffix(" pt");
    m_hexFont->setValue(m_origHexFont);

    auto *form = new QFormLayout;
    form->addRow("Theme:", m_themeBox);
    form->addRow("UI font size:", m_uiFont);
    form->addRow("Hex editor font size:", m_hexFont);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
        QDialogButtonBox::RestoreDefaults, this);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons);

    // Live preview: every change applies (and persists) immediately.
    connect(m_themeBox, &QComboBox::currentIndexChanged, this, [this](int) {
        ThemeManager::instance().setThemeId(
            static_cast<ThemeId>(m_themeBox->currentData().toInt()));
    });
    connect(m_uiFont, &QSpinBox::valueChanged, this,
            [](int v) { ThemeManager::instance().setUiFontSize(v); });
    connect(m_hexFont, &QSpinBox::valueChanged, this,
            [](int v) { ThemeManager::instance().setHexFontSize(v); });

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, [this] {
        restoreOriginals();
        reject();
    });
    connect(buttons->button(QDialogButtonBox::RestoreDefaults),
            &QPushButton::clicked, this, [this] {
                m_themeBox->setCurrentIndex(
                    m_themeBox->findData(static_cast<int>(ThemeId::Light)));
                m_uiFont->setValue(10);
                m_hexFont->setValue(10);
            });
}

void PreferencesDialog::restoreOriginals()
{
    ThemeManager &tm = ThemeManager::instance();
    tm.setThemeId(m_origTheme);
    tm.setUiFontSize(m_origUiFont);
    tm.setHexFontSize(m_origHexFont);
}
