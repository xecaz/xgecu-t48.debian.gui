// SPDX-License-Identifier: GPL-3.0-or-later
#include "ThemeManager.h"

#include <QApplication>
#include <QFont>
#include <QSettings>
#include <QStyleFactory>

namespace {
constexpr int kMinUi = 8, kMaxUi = 16;
constexpr int kMinHex = 8, kMaxHex = 20;

// Standard palette roles per theme.
struct Roles {
    QColor window, windowText, base, altBase, button, buttonText, brightText,
        highlight, highlightedText, tooltipBase, placeholder, link, disabled;
};

QPalette buildPalette(const Roles &r)
{
    QPalette p;
    p.setColor(QPalette::Window, r.window);
    p.setColor(QPalette::WindowText, r.windowText);
    p.setColor(QPalette::Base, r.base);
    p.setColor(QPalette::AlternateBase, r.altBase);
    p.setColor(QPalette::Text, r.windowText);
    p.setColor(QPalette::Button, r.button);
    p.setColor(QPalette::ButtonText, r.buttonText);
    p.setColor(QPalette::BrightText, r.brightText);
    p.setColor(QPalette::Highlight, r.highlight);
    p.setColor(QPalette::HighlightedText, r.highlightedText);
    p.setColor(QPalette::ToolTipBase, r.tooltipBase);
    p.setColor(QPalette::ToolTipText, r.windowText);
    p.setColor(QPalette::PlaceholderText, r.placeholder);
    p.setColor(QPalette::Link, r.link);
    for (QPalette::ColorRole role :
         {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, r.disabled);
    return p;
}
}  // namespace

ThemeManager &ThemeManager::instance()
{
    static ThemeManager mgr;
    return mgr;
}

QString ThemeManager::nameFromId(ThemeId id)
{
    switch (id) {
    case ThemeId::Dark:   return "Dark";
    case ThemeId::Hacker: return "Hacker";
    case ThemeId::Amber:  return "Amber";
    case ThemeId::Light:  break;
    }
    return "Light";
}

ThemeId ThemeManager::idFromName(const QString &name)
{
    if (name == "Dark")   return ThemeId::Dark;
    if (name == "Hacker") return ThemeId::Hacker;
    if (name == "Amber")  return ThemeId::Amber;
    return ThemeId::Light;
}

Theme ThemeManager::makeTheme(ThemeId id)
{
    Theme t;
    t.id = id;
    t.name = nameFromId(id);
    t.pinText = QColor("#000000");

    switch (id) {
    case ThemeId::Light:
        t.palette = buildPalette({QColor("#EFEFEF"), QColor("#1A1A1A"),
            QColor("#FFFFFF"), QColor("#F2F2F2"), QColor("#E2E2E2"),
            QColor("#1A1A1A"), QColor("#FFFFFF"), QColor("#3A7BD5"),
            QColor("#FFFFFF"), QColor("#FFFFE1"), QColor("#9A9A9A"),
            QColor("#1A5FB4"), QColor("#A0A0A0")});
        t.dirtyByte = QColor("#DC322F");
        t.socketBody = QColor("#3C5A78");
        t.socketText = QColor("#DCDCE6");
        t.leverStem = QColor("#28282E");
        t.leverBallHi = QColor("#5A5A64");
        t.leverBallLo = QColor("#141419");
        t.leverBallOutline = QColor("#0A0A0F");
        t.pinIdle = QColor("#B4B4B4");
        t.pinActive = QColor("#3CC878");
        t.chipBody = QColor(0x23, 0x23, 0x2D, 0xE6);
        t.pin1Marker = QColor("#FFDC3C");
        t.unsupportedText = QColor("#A0A0A0");
        break;
    case ThemeId::Dark:
        t.palette = buildPalette({QColor("#2D2D30"), QColor("#E0E0E0"),
            QColor("#1E1E1E"), QColor("#262628"), QColor("#3A3A3D"),
            QColor("#E0E0E0"), QColor("#FFFFFF"), QColor("#2A6FD0"),
            QColor("#FFFFFF"), QColor("#3A3A3D"), QColor("#8A8A8A"),
            QColor("#4FA3FF"), QColor("#6A6A6A")});
        t.dirtyByte = QColor("#FF6B68");
        t.socketBody = QColor("#3C5A78");
        t.socketText = QColor("#DCDCE6");
        t.leverStem = QColor("#1A1A20");
        t.leverBallHi = QColor("#5A5A64");
        t.leverBallLo = QColor("#141419");
        t.leverBallOutline = QColor("#0A0A0F");
        t.pinIdle = QColor("#7A7A80");
        t.pinActive = QColor("#3CC878");
        t.chipBody = QColor(0x23, 0x23, 0x2D, 0xE6);
        t.pin1Marker = QColor("#FFDC3C");
        t.unsupportedText = QColor("#8A8A8A");
        break;
    case ThemeId::Hacker:
        t.palette = buildPalette({QColor("#000000"), QColor("#33FF66"),
            QColor("#020602"), QColor("#0A140A"), QColor("#0A1A0A"),
            QColor("#33FF66"), QColor("#AAFFAA"), QColor("#00B33C"),
            QColor("#001400"), QColor("#001A00"), QColor("#1F8A3A"),
            QColor("#33FF99"), QColor("#1A6A2A")});
        t.dirtyByte = QColor("#FF4040");
        t.socketBody = QColor("#062A12");
        t.socketText = QColor("#66FF99");
        t.leverStem = QColor("#063018");
        t.leverBallHi = QColor("#0FA84A");
        t.leverBallLo = QColor("#021A0A");
        t.leverBallOutline = QColor("#001000");
        t.pinIdle = QColor("#0E5524");
        t.pinActive = QColor("#39FF14");
        t.chipBody = QColor(0x03, 0x14, 0x0A, 0xE6);
        t.pin1Marker = QColor("#CCFF33");
        t.unsupportedText = QColor("#1F8A3A");
        break;
    case ThemeId::Amber:
        t.palette = buildPalette({QColor("#0A0500"), QColor("#FFB000"),
            QColor("#070300"), QColor("#140A00"), QColor("#1A1000"),
            QColor("#FFB000"), QColor("#FFE0A0"), QColor("#C77800"),
            QColor("#1A0E00"), QColor("#1A1000"), QColor("#9C6A00"),
            QColor("#FFC850"), QColor("#7A5200")});
        t.dirtyByte = QColor("#FF5522");
        t.socketBody = QColor("#241400");
        t.socketText = QColor("#FFC850");
        t.leverStem = QColor("#1A1000");
        t.leverBallHi = QColor("#B8820A");
        t.leverBallLo = QColor("#0E0700");
        t.leverBallOutline = QColor("#080400");
        t.pinIdle = QColor("#6E4A00");
        t.pinActive = QColor("#FFC000");
        t.chipBody = QColor(0x12, 0x0A, 0x00, 0xE6);
        t.pin1Marker = QColor("#FFE070");
        t.unsupportedText = QColor("#9C6A00");
        break;
    }
    return t;
}

void ThemeManager::apply()
{
    // Native styles ignore large parts of a custom palette; Fusion honors every
    // role, so the themes render completely and consistently.
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication::setPalette(m_theme.palette);
    QFont f = QApplication::font();
    f.setPointSize(m_uiFont);
    QApplication::setFont(f);
}

void ThemeManager::setThemeId(ThemeId id)
{
    m_theme = makeTheme(id);
    QSettings().setValue("appearance/theme", nameFromId(id));
    apply();
    emit changed();
}

void ThemeManager::setUiFontSize(int pt)
{
    m_uiFont = qBound(kMinUi, pt, kMaxUi);
    QSettings().setValue("appearance/uiFontSize", m_uiFont);
    apply();
    emit changed();
}

void ThemeManager::setHexFontSize(int pt)
{
    m_hexFont = qBound(kMinHex, pt, kMaxHex);
    QSettings().setValue("appearance/hexFontSize", m_hexFont);
    emit changed();  // HexView reads hexFontSize() and reapplies its own font
}

void ThemeManager::loadFromSettings()
{
    QSettings s;
    m_theme = makeTheme(idFromName(s.value("appearance/theme", "Light").toString()));
    m_uiFont = qBound(kMinUi, s.value("appearance/uiFontSize", 10).toInt(), kMaxUi);
    m_hexFont = qBound(kMinHex, s.value("appearance/hexFontSize", 10).toInt(), kMaxHex);
    apply();
    emit changed();
}
