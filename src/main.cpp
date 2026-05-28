// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>

#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("xgecu-gui");
    QApplication::setOrganizationName("xgecu-gui");
    QApplication::setApplicationVersion("0.0.1");

    MainWindow w;
    w.show();
    return app.exec();
}
