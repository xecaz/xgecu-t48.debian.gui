// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMainWindow>
#include <QPointer>

#include "core/Programmer.h"

class BufferModel;
class ChipDatabase;
class HexView;
class QAction;
class QLabel;
class QProgressDialog;
class ZifSocketView;
struct ChipEntry;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onSelectChip();
    void onDetect();
    void onDetected(const DeviceInfo &info);
    void onDetectionFailed(const QString &message);
    void onChipOpened(const QString &name, quint32 codeSize, quint32 dataSize, bool canErase);
    void onChipOpenFailed(const QString &message);
    void onProgress(qint64 done, qint64 total);
    void onReadFinished(const ReadResult &result);
    void onVerifyFinished(const VerifyResult &result);
    void onChipIdFinished(const ChipIdResult &result);
    void onProgrammerError(const QString &message);
    void onReadClicked();
    void onVerifyClicked();
    void onDetectIdClicked();
    void onBlankCheckClicked();
    void onEraseClicked();
    void onEraseFinished(bool ok, const QString &message);
    void onWriteClicked();
    void onWriteFinished(const WriteResult &result);

private:
    void buildUi();
    void updateActions();

    ChipDatabase *m_db;
    BufferModel *m_buffer;
    Programmer *m_programmer;

    HexView *m_hex;
    ZifSocketView *m_zif;
    QLabel *m_deviceLabel;
    QLabel *m_chipLabel;
    QAction *m_readAct = nullptr;
    QAction *m_verifyAct = nullptr;
    QAction *m_detectIdAct = nullptr;
    QAction *m_blankCheckAct = nullptr;
    QAction *m_eraseAct = nullptr;
    QAction *m_writeAct = nullptr;
    QProgressDialog *m_progress = nullptr;
    enum class Op { None, Read, Verify, BlankCheck, Write } m_op = Op::None;
    quint32 m_chipCodeSize = 0;
    bool m_canErase = false;
    const ChipEntry *m_currentChip = nullptr;
    bool m_deviceConnected = false;
    bool m_chipOpened = false;
};
