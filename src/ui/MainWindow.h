// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QMainWindow>
#include <QPointer>

#include "core/Programmer.h"

class BufferModel;
class ChipDatabase;
class HexView;
class QAction;
class QLabel;
class QProgressDialog;
class QTabWidget;
class ZifSocketView;
struct ChipEntry;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

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
    void loadBufferFromPath(const QString &path);

    struct AreaPane {
        BufferModel *buffer = nullptr;
        HexView *view = nullptr;
        quint32 chipSize = 0;
        int tabIndex = -1;
    };

    AreaPane &paneFor(MemArea a) { return m_panes[a]; }
    AreaPane paneFor(MemArea a) const { return m_panes.value(a); }
    MemArea currentArea() const;
    quint32 currentChipSize() const { return paneFor(currentArea()).chipSize; }
    void onCurrentTabChanged(int index);

    ChipDatabase *m_db;
    Programmer *m_programmer;

    QHash<MemArea, AreaPane> m_panes;
    QTabWidget *m_tabs = nullptr;
    // Active-tab convenience aliases; refreshed by onCurrentTabChanged so
    // every existing callsite that operates on "the current buffer / view"
    // keeps working without per-action plumbing.
    BufferModel *m_buffer = nullptr;
    HexView *m_hex = nullptr;
    ZifSocketView *m_zif;
    QLabel *m_deviceLabel;
    QLabel *m_chipLabel;
    QAction *m_readAct = nullptr;
    QAction *m_verifyAct = nullptr;
    QAction *m_detectIdAct = nullptr;
    QAction *m_blankCheckAct = nullptr;
    QAction *m_eraseAct = nullptr;
    QAction *m_writeAct = nullptr;
    QAction *m_undoAct = nullptr;
    QAction *m_redoAct = nullptr;
    QMetaObject::Connection m_undoConn;
    QMetaObject::Connection m_redoConn;
    QProgressDialog *m_progress = nullptr;
    enum class Op { None, Read, Verify, BlankCheck, Write } m_op = Op::None;
    bool m_canErase = false;
    QByteArray m_lastFindBytes;
    QString m_lastFindText;
    int m_lastFindMode = 0;       // 0 = Hex, 1 = ASCII, 2 = Decimal
    qsizetype m_lastFindHit = -1; // last hit offset, for "only one match" detect
    const ChipEntry *m_currentChip = nullptr;
    bool m_deviceConnected = false;
    bool m_chipOpened = false;
};
