// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <atomic>

#include "Programmer.h"  // DeviceInfo, ReadResult

struct minipro_handle;
typedef struct minipro_handle minipro_handle_t;
struct cmdopts_s;
typedef struct cmdopts_s cmdopts_t;

class ProgrammerWorker : public QObject {
    Q_OBJECT
public:
    explicit ProgrammerWorker(QObject *parent = nullptr);
    ~ProgrammerWorker() override;

    void requestCancel() { m_cancel.store(true); }

public slots:
    void setInfoicPath(const QString &path) { m_infoicPath = path; }
    void setLogicicPath(const QString &path) { m_logicicPath = path; }
    void detect();
    void openChip(const QString &miniproName);
    void readCode();
    void verifyCode(const QByteArray &expected);
    void detectChipId();
    void eraseChip(bool force);
    void writeCode(const QByteArray &data, bool force, bool autoVerify);

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
    void closeHandle();

    minipro_handle_t *m_handle = nullptr;
    cmdopts_t *m_cmdopts = nullptr;
    QString m_infoicPath;
    QString m_logicicPath;
    QString m_currentChip;
    std::atomic<bool> m_cancel{false};
};
