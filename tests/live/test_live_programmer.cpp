// SPDX-License-Identifier: GPL-3.0-or-later
//
// Live-hardware smoke tests for the Programmer / Worker. Gated by the
// XGECU_LIVE_TESTS=1 environment variable so they never run in CI by
// accident. All assertions here are READ-ONLY w.r.t. silicon: device
// detect + chip lookup against minipro's XML. No erase, write, or chip-ID
// poll is performed.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

#include "core/Programmer.h"

namespace {
QString findMiniproXml(const QString &name)
{
    const QString src = QStringLiteral(SOURCE_ROOT);
    const QStringList candidates = {
        src + "/third_party/minipro/" + name,
        "/usr/share/xgecu-gui/" + name,
        "/usr/local/share/xgecu-gui/" + name,
    };
    for (const QString &p : candidates) {
        if (QFile::exists(p)) return QFileInfo(p).absoluteFilePath();
    }
    return {};
}
}  // namespace

class TestLiveProgrammer : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        if (qgetenv("XGECU_LIVE_TESTS") != "1")
            QSKIP("set XGECU_LIVE_TESTS=1 to run hardware tests");
        m_infoic = findMiniproXml("infoic.xml");
        m_logicic = findMiniproXml("logicic.xml");
        QVERIFY2(!m_infoic.isEmpty(), "infoic.xml not found");
        QVERIFY2(!m_logicic.isEmpty(), "logicic.xml not found");
    }

    // Tier 1 — programmer opens, reports a sane version and firmware string.
    void detect()
    {
        Programmer prog;
        prog.setInfoicPath(m_infoic);
        prog.setLogicicPath(m_logicic);

        QSignalSpy okSpy(&prog, &Programmer::detected);
        QSignalSpy failSpy(&prog, &Programmer::detectionFailed);
        prog.detect();
        QVERIFY(okSpy.wait(5000) || failSpy.count() > 0);
        if (failSpy.count() > 0)
            QSKIP("no programmer connected");
        QCOMPARE(okSpy.count(), 1);
        const auto info = okSpy.takeFirst().at(0).value<DeviceInfo>();
        QVERIFY(info.connected);
        QVERIFY(!info.model.isEmpty());
        QVERIFY(!info.firmwareStr.isEmpty());
        QVERIFY(info.versionId >= 1 && info.versionId <= 9);
    }

    // Tier 1 — chip lookup against the XML succeeds for a known chip with
    // BOTH code and data memory areas (ATmega328P). We don't actually read
    // silicon; just verify openChip emits chipOpened with the expected
    // sizes. This is the same call path the Data-tab smoke test relies on
    // once the user has the chip in hand.
    void openKnownChipWithDataMemory()
    {
        Programmer prog;
        prog.setInfoicPath(m_infoic);
        prog.setLogicicPath(m_logicic);

        QSignalSpy detectSpy(&prog, &Programmer::detected);
        QSignalSpy detectFailSpy(&prog, &Programmer::detectionFailed);
        prog.detect();
        QVERIFY(detectSpy.wait(5000) || detectFailSpy.count() > 0);
        if (detectFailSpy.count() > 0)
            QSKIP("no programmer connected");

        QSignalSpy okSpy(&prog, &Programmer::chipOpened);
        QSignalSpy failSpy(&prog, &Programmer::chipOpenFailed);
        prog.openChip("ATMEGA328P@DIP28");
        QVERIFY(okSpy.wait(3000) || failSpy.count() > 0);
        QVERIFY2(failSpy.count() == 0,
                 qPrintable(failSpy.value(0).value(0).toString()));
        QCOMPARE(okSpy.count(), 1);
        const auto args = okSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), QString("ATMEGA328P@DIP28"));
        QCOMPARE(args.at(1).toUInt(), 32768u);  // 32 KiB code (Flash)
        QCOMPARE(args.at(2).toUInt(), 1024u);   // 1 KiB data (EEPROM)
    }

private:
    QString m_infoic;
    QString m_logicic;
};

QTEST_GUILESS_MAIN(TestLiveProgrammer)
#include "test_live_programmer.moc"
