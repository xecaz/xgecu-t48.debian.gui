// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QThread>

class ProgrammerWorker;

struct DeviceInfo {
    bool connected = false;
    QString model;
    QString firmwareStr;
    QString deviceCode;
    int versionId = 0;
};

struct ReadResult {
    bool ok = false;
    QString area;          // "code"
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
    qint64 bytesWritten = 0;
    QString errorMessage;
    bool verified = false;        // true if auto-verify was requested AND passed
    qint64 verifyMismatches = 0;  // 0 if verified, otherwise count of differences
};

struct VerifyResult {
    bool ok = false;            // false if any difference OR I/O error
    QString area;
    qint64 bytesChecked = 0;
    qint64 mismatches = 0;
    qint64 firstMismatchOffset = -1;
    quint8 firstChipByte = 0;
    quint8 firstExpectedByte = 0;
    QString errorMessage;       // non-empty only on I/O error / cancel
};

Q_DECLARE_METATYPE(DeviceInfo)
Q_DECLARE_METATYPE(ReadResult)
Q_DECLARE_METATYPE(VerifyResult)
Q_DECLARE_METATYPE(ChipIdResult)
Q_DECLARE_METATYPE(WriteResult)

class Programmer : public QObject {
    Q_OBJECT
public:
    explicit Programmer(QObject *parent = nullptr);
    ~Programmer() override;

    void setInfoicPath(const QString &path);
    void setLogicicPath(const QString &path);

    void detect();
    void openChip(const QString &miniproName);
    void readCode();
    void verifyCode(const QByteArray &expected);
    void detectChipId();
    void eraseChip(bool force);
    void writeCode(const QByteArray &data, bool force, bool autoVerify);
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
    void error(const QString &message);

private:
    QThread m_thread;
    ProgrammerWorker *m_worker;
};
