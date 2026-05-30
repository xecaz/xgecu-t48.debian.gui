// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>

#include "ui/MainWindow.h"
#include "ui/ThemeManager.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("xgecu-gui");
    QApplication::setOrganizationName("xgecu-gui");
    QApplication::setApplicationVersion("0.3.0");

    // Style/palette/fonts before any widget exists, so the first paint is
    // already themed (no flash of the OS default).
    ThemeManager::instance().loadFromSettings();

    MainWindow w;
    w.show();
    return app.exec();
}
