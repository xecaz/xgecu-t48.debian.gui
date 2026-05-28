// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QTest>

#include "core/ChipDatabase.h"

class TestChipDatabase : public QObject {
    Q_OBJECT
private slots:
    void loadsAndIndexes()
    {
        const QByteArray pathEnv = qgetenv("CHIPS_JSON");
        QVERIFY2(!pathEnv.isEmpty(), "CHIPS_JSON env var not set");
        ChipDatabase db;
        QString err;
        QVERIFY2(db.load(QString::fromLocal8Bit(pathEnv), &err),
                 qPrintable(err));
        QVERIFY(db.chips().size() > 10000);
        QVERIFY(db.supportedCount() > 5000);

        const ChipEntry *atmega = db.find("ATMEL", "ATMEGA328P", "DIP28");
        QVERIFY(atmega != nullptr);
        QVERIFY(atmega->supported);
        QCOMPARE(atmega->pinCount, 28);

        const ChipEntry *winOnly = db.find("AUTO", "AUTO-EMMC_4B_1.8V", "BGA100");
        QVERIFY(winOnly != nullptr);
        QVERIFY(!winOnly->supported);
    }
};

QTEST_MAIN(TestChipDatabase)
#include "test_chip_database.moc"
