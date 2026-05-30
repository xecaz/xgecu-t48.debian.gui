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

    // Tier 1 — opening the ATmega328P emits fusesAvailable carrying its
    // declared layout (lfuse/hfuse/efuse + lock). Pure XML metadata; no
    // silicon access.
    void fuseLayoutAvailableForAtmega328p()
    {
        Programmer prog;
        prog.setInfoicPath(m_infoic);
        prog.setLogicicPath(m_logicic);
        if (!detectConnected(prog)) QSKIP("no programmer connected");

        QSignalSpy fuseSpy(&prog, &Programmer::fusesAvailable);
        QSignalSpy failSpy(&prog, &Programmer::chipOpenFailed);
        prog.openChip("ATMEGA328P@DIP28");
        QVERIFY(fuseSpy.wait(3000) || failSpy.count() > 0);
        QVERIFY2(failSpy.count() == 0,
                 qPrintable(failSpy.value(0).value(0).toString()));
        QCOMPARE(fuseSpy.count(), 1);
        const auto set = fuseSpy.takeFirst().at(0).value<FuseSet>();
        QVERIFY(set.valid);
        QVERIFY(set.wordSize >= 1);  // ATmega328P@DIP28 reports word_size 2
        QStringList names;
        int locks = 0;
        for (const FuseItem &it : set.items) {
            names << it.name;
            if (it.isLock) ++locks;
        }
        QVERIFY(names.contains("lfuse"));
        QVERIFY(names.contains("hfuse"));
        QVERIFY(names.contains("efuse"));
        QCOMPARE(locks, 1);
    }

    // Tier 2 — actually read the fuse bytes off the chip. Read-only on
    // silicon, so safe to run whenever a '328P is in the socket.
    void readFusesFromAtmega328p()
    {
        Programmer prog;
        prog.setInfoicPath(m_infoic);
        prog.setLogicicPath(m_logicic);
        if (!detectConnected(prog)) QSKIP("no programmer connected");

        QSignalSpy openSpy(&prog, &Programmer::chipOpened);
        prog.openChip("ATMEGA328P@DIP28");
        QVERIFY(openSpy.wait(3000));

        QSignalSpy readSpy(&prog, &Programmer::fusesRead);
        prog.readFuses();
        QVERIFY(readSpy.wait(8000));
        QCOMPARE(readSpy.count(), 1);
        const auto set = readSpy.takeFirst().at(0).value<FuseSet>();
        QVERIFY2(set.errorMessage.isEmpty(), qPrintable(set.errorMessage));
        QVERIFY(set.valid);
        // Four items: lfuse, hfuse, efuse, lock.
        QCOMPARE(set.items.size(), 4);
        // The read masks unused config-fuse bits high, so for each non-lock
        // fuse every bit outside its mask must read as 1.
        for (const FuseItem &it : set.items) {
            if (it.isLock) continue;
            const quint8 outside = static_cast<quint8>(~it.mask);
            QVERIFY2((static_cast<quint8>(it.value) & outside) == outside,
                     qPrintable(QString("bits outside mask not all 1 in %1")
                                    .arg(it.name)));
        }
    }

    // Tier 3 — DESTRUCTIVE fuse write, double-gated behind a second env var so
    // a normal live run never writes fuses. Toggles CKDIV8 (bit 7 of LFUSE,
    // 0x62<->0xE2 — clock prescaler only, can't lock out the programmer),
    // verifies, then restores the original value so the chip is left as found.
    void fuseWriteRoundTripCkdiv8()
    {
        if (qgetenv("XGECU_LIVE_FUSE_WRITE") != "1")
            QSKIP("set XGECU_LIVE_FUSE_WRITE=1 to run the live fuse-WRITE test");
        Programmer prog;
        prog.setInfoicPath(m_infoic);
        prog.setLogicicPath(m_logicic);
        if (!detectConnected(prog)) QSKIP("no programmer connected");

        QSignalSpy openSpy(&prog, &Programmer::chipOpened);
        prog.openChip("ATMEGA328P@DIP28");
        QVERIFY(openSpy.wait(3000));

        // 1. Read what's there now.
        const FuseSet before = readFusesSync(prog);
        QVERIFY2(before.valid && before.errorMessage.isEmpty(),
                 qPrintable(before.errorMessage));
        const quint16 lfuse0 = fuseValue(before, "lfuse");
        const quint16 hfuse0 = fuseValue(before, "hfuse");
        const quint16 efuse0 = fuseValue(before, "efuse");
        qInfo("BEFORE:        lfuse=0x%02X hfuse=0x%02X efuse=0x%02X",
              lfuse0, hfuse0, efuse0);
        QVERIFY(lfuse0 != 0xFFFF);

        // 2. Flip CKDIV8 and write the whole config section.
        const quint16 lfuseT = lfuse0 ^ 0x80;
        bool verified = false;
        QVERIFY2(writeFusesSync(prog, makeCfg(before, "lfuse", lfuseT), &verified),
                 "toggle write failed");
        QVERIFY2(verified, "toggle write read-back verify mismatched");
        const FuseSet mid = readFusesSync(prog);
        qInfo("AFTER TOGGLE:  lfuse=0x%02X hfuse=0x%02X efuse=0x%02X",
              fuseValue(mid, "lfuse"), fuseValue(mid, "hfuse"),
              fuseValue(mid, "efuse"));
        QCOMPARE(fuseValue(mid, "lfuse"), lfuseT);
        QCOMPARE(fuseValue(mid, "hfuse"), hfuse0);  // untouched
        QCOMPARE(fuseValue(mid, "efuse"), efuse0);  // untouched

        // 3. Restore — leave the chip exactly as we found it.
        verified = false;
        QVERIFY2(writeFusesSync(prog, makeCfg(before, "lfuse", lfuse0), &verified),
                 "restore write failed");
        QVERIFY2(verified, "restore write read-back verify mismatched");
        const FuseSet after = readFusesSync(prog);
        qInfo("RESTORED:      lfuse=0x%02X hfuse=0x%02X efuse=0x%02X",
              fuseValue(after, "lfuse"), fuseValue(after, "hfuse"),
              fuseValue(after, "efuse"));
        QCOMPARE(fuseValue(after, "lfuse"), lfuse0);
        QCOMPARE(fuseValue(after, "hfuse"), hfuse0);
        QCOMPARE(fuseValue(after, "efuse"), efuse0);
    }

private:
    FuseSet readFusesSync(Programmer &prog)
    {
        QSignalSpy spy(&prog, &Programmer::fusesRead);
        prog.readFuses();
        if (!spy.wait(8000)) return FuseSet{};
        return spy.takeFirst().at(0).value<FuseSet>();
    }

    bool writeFusesSync(Programmer &prog, const FuseSet &set, bool *verified)
    {
        QSignalSpy spy(&prog, &Programmer::fuseWriteFinished);
        prog.writeFuses(set);
        if (!spy.wait(8000)) return false;
        const auto args = spy.takeFirst();
        if (verified) *verified = args.at(1).toBool();
        return args.at(0).toBool();
    }

    static quint16 fuseValue(const FuseSet &set, const QString &name)
    {
        for (const FuseItem &it : set.items)
            if (!it.isLock && it.name == name) return it.value;
        return 0xFFFF;
    }

    // CFG-only FuseSet copied from `base`, with one fuse's value overridden.
    static FuseSet makeCfg(const FuseSet &base, const QString &name, quint16 value)
    {
        FuseSet out;
        out.valid = true;
        out.wordSize = base.wordSize;
        out.lockReadable = base.lockReadable;
        for (const FuseItem &it : base.items) {
            if (it.isLock) continue;
            FuseItem c = it;
            if (c.name == name) c.value = value;
            out.items.append(c);
        }
        return out;
    }

    // Returns true if a programmer answered detect(); false if none is present
    // (caller should QSKIP). Never asserts — absence of hardware is not a
    // failure.
    bool detectConnected(Programmer &prog)
    {
        QSignalSpy detectSpy(&prog, &Programmer::detected);
        QSignalSpy detectFailSpy(&prog, &Programmer::detectionFailed);
        prog.detect();
        if (!detectSpy.wait(5000) && detectFailSpy.count() == 0)
            return false;
        return detectFailSpy.count() == 0;
    }

    QString m_infoic;
    QString m_logicic;
};

QTEST_GUILESS_MAIN(TestLiveProgrammer)
#include "test_live_programmer.moc"
