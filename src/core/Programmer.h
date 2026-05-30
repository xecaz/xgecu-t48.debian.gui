// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QThread>

class ProgrammerWorker;

enum class MemArea : quint8 {
    Code = 0,   // -> MP_CODE
    Data = 1,   // -> MP_DATA
};

struct DeviceInfo {
    bool connected = false;
    QString model;
    QString firmwareStr;
    QString deviceCode;
    int versionId = 0;
};

struct ReadResult {
    bool ok = false;
    MemArea area = MemArea::Code;
    QByteArray data;
    QString errorMessage;
};

struct ChipIdResult {
    enum class Status { Unsupported, Match, Mismatch, IoError };
    Status status = Status::IoError;
    quint32 expectedId = 0;       // device->chip_id
    quint32 readId = 0;
    quint8 idType = 0;            // MP_ID_TYPE1..5
    quint8 revisionBits = 0;      // for TYPE3/TYPE4
    quint8 idBytesCount = 0;      // for pretty-printing
    QString identifiedAs;         // when mismatch
    QString errorMessage;
};

struct WriteResult {
    bool ok = false;
    MemArea area = MemArea::Code;
    qint64 bytesWritten = 0;
    QString errorMessage;
    bool verified = false;        // true if auto-verify was requested AND passed
    qint64 verifyMismatches = 0;  // 0 if verified, otherwise count of differences
};

struct VerifyResult {
    bool ok = false;            // false if any difference OR I/O error
    MemArea area = MemArea::Code;
    qint64 bytesChecked = 0;
    qint64 mismatches = 0;
    qint64 firstMismatchOffset = -1;
    quint8 firstChipByte = 0;
    quint8 firstExpectedByte = 0;
    QString errorMessage;       // non-empty only on I/O error / cancel
};

// One config-fuse or lock item, as declared by minipro's fuse_decl_t.
// minipro carries only {name, mask, default} per item — no per-bit names.
struct FuseItem {
    QString name;          // "lfuse", "hfuse", "efuse", "lock", ...
    quint16 mask = 0xFF;   // valid bits; bits outside the mask always read 1
    quint16 def = 0;       // manufacturer default / recommended value
    quint16 value = 0;     // current value (read from chip, or edited in UI)
    bool isLock = false;   // false = config fuse (MP_FUSE_CFG), true = MP_FUSE_LOCK
};

struct FuseSet {
    bool valid = false;     // true if the chip exposes a config/fuse section
    int wordSize = 1;       // bytes per item (1 for AVR, 2 for some PIC)
    bool lockReadable = true;  // false when flags.lock_bit_write_only is set
    QList<FuseItem> items;  // config fuses first, then locks
    QString errorMessage;   // non-empty only on read/write I/O error
};

Q_DECLARE_METATYPE(MemArea)
Q_DECLARE_METATYPE(DeviceInfo)
Q_DECLARE_METATYPE(ReadResult)
Q_DECLARE_METATYPE(VerifyResult)
Q_DECLARE_METATYPE(ChipIdResult)
Q_DECLARE_METATYPE(WriteResult)
Q_DECLARE_METATYPE(FuseItem)
Q_DECLARE_METATYPE(FuseSet)

class Programmer : public QObject {
    Q_OBJECT
public:
    explicit Programmer(QObject *parent = nullptr);
    ~Programmer() override;

    void setInfoicPath(const QString &path);
    void setLogicicPath(const QString &path);

    void detect();
    void openChip(const QString &miniproName);
    void readMemory(MemArea area);
    void verifyMemory(MemArea area, const QByteArray &expected);
    void writeMemory(MemArea area, const QByteArray &data, bool force, bool autoVerify);
    void detectChipId();
    void eraseChip(bool force);
    void readFuses();
    void writeFuses(const FuseSet &fuses);
    void requestCancel();

signals:
    void detected(const DeviceInfo &info);
    void detectionFailed(const QString &message);
    void chipOpened(const QString &miniproName, quint32 codeSize, quint32 dataSize,
                    bool canErase);
    void chipOpenFailed(const QString &message);
    void progress(qint64 done, qint64 total);
    void readFinished(const ReadResult &result);
    void verifyFinished(const VerifyResult &result);
    void chipIdFinished(const ChipIdResult &result);
    void eraseFinished(bool ok, const QString &message);
    void writeFinished(const WriteResult &result);
    // Emitted on openChip: the chip's declared fuse layout (values not yet read).
    // An invalid/empty FuseSet means the chip has no fuses — hide the editor.
    void fusesAvailable(const FuseSet &fuses);
    void fusesRead(const FuseSet &fuses);
    void fuseWriteFinished(bool ok, bool verified, const QString &message);
    void error(const QString &message);

private:
    QThread m_thread;
    ProgrammerWorker *m_worker;
};
