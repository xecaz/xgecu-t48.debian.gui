// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include "core/BufferModel.h"

class TestBufferModel : public QObject {
    Q_OBJECT
private slots:
    void setAndGet()
    {
        BufferModel b;
        QSignalSpy spy(&b, &BufferModel::reset);
        b.setData(QByteArray("\x01\x02\x03\xFF", 4));
        QCOMPARE(b.size(), qsizetype(4));
        QCOMPARE(b.byteAt(0), quint8(0x01));
        QCOMPARE(b.byteAt(3), quint8(0xFF));
        QCOMPARE(spy.count(), 1);
    }

    void resizeFills()
    {
        BufferModel b;
        b.resize(16, char(0xAB));
        QCOMPARE(b.size(), qsizetype(16));
        QCOMPARE(b.byteAt(7), quint8(0xAB));
    }

    void byteEditEmitsChange()
    {
        BufferModel b;
        b.resize(4, 0);
        QSignalSpy spy(&b, &BufferModel::dataChanged);
        b.setByteAt(2, 0x42);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(b.byteAt(2), quint8(0x42));
        // No-op when same value.
        b.setByteAt(2, 0x42);
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TestBufferModel)
#include "test_buffer_model.moc"
