// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QAbstractButton>
#include <QCheckBox>
#include <QInputDialog>
#include <QTimer>
#include <QUndoStack>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

#include "ChipSelectDialog.h"
#include "HexView.h"
#include "ZifSocketView.h"
#include "core/BufferModel.h"
#include "core/ChipDatabase.h"

namespace {
QString findMiniproFile(const QString &name)
{
    // Try alongside the binary first, then a few well-known dev locations.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/" + name,
        appDir + "/../third_party/minipro/" + name,
        QDir::currentPath() + "/third_party/minipro/" + name,
        QDir::currentPath() + "/../third_party/minipro/" + name,
        "/usr/local/share/minipro/" + name,
        "/usr/share/minipro/" + name,
    };
    for (const QString &p : candidates) {
        if (QFile::exists(p))
            return QFileInfo(p).absoluteFilePath();
    }
    return {};
}
}  // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("XGecu T-48/T-56/TL866II+ by Xecaz");
    resize(1280, 800);

    m_db = new ChipDatabase(this);
    m_buffer = new BufferModel(this);
    m_programmer = new Programmer(this);

    QString err;
    if (!m_db->load(":/chips_merged.json", &err)) {
        QMessageBox::warning(this, "Chip database",
            QString("Failed to load chip database: %1").arg(err));
    }

    const QString infoic = findMiniproFile("infoic.xml");
    const QString logicic = findMiniproFile("logicic.xml");
    if (!infoic.isEmpty())
        m_programmer->setInfoicPath(infoic);
    if (!logicic.isEmpty())
        m_programmer->setLogicicPath(logicic);

    buildUi();

    connect(m_programmer, &Programmer::detected, this, &MainWindow::onDetected);
    connect(m_programmer, &Programmer::detectionFailed, this, &MainWindow::onDetectionFailed);
    connect(m_programmer, &Programmer::chipOpened, this, &MainWindow::onChipOpened);
    connect(m_programmer, &Programmer::chipOpenFailed, this, &MainWindow::onChipOpenFailed);
    connect(m_programmer, &Programmer::progress, this, &MainWindow::onProgress);
    connect(m_programmer, &Programmer::readFinished, this, &MainWindow::onReadFinished);
    connect(m_programmer, &Programmer::verifyFinished, this, &MainWindow::onVerifyFinished);
    connect(m_programmer, &Programmer::chipIdFinished, this, &MainWindow::onChipIdFinished);
    connect(m_programmer, &Programmer::eraseFinished, this, &MainWindow::onEraseFinished);
    connect(m_programmer, &Programmer::writeFinished, this, &MainWindow::onWriteFinished);
    connect(m_programmer, &Programmer::error, this, &MainWindow::onProgrammerError);
    connect(m_buffer, &BufferModel::reset, this, &MainWindow::updateActions);

    statusBar()->showMessage(
        QString("Loaded %1 chips (%2 supported, %3 Windows-only)%4")
            .arg(m_db->chips().size())
            .arg(m_db->supportedCount())
            .arg(m_db->chips().size() - m_db->supportedCount())
            .arg(infoic.isEmpty() ? "  — infoic.xml NOT FOUND" : ""));

    QMetaObject::invokeMethod(this, &MainWindow::onDetect, Qt::QueuedConnection);
}

void MainWindow::buildUi()
{
    auto *fileMenu = menuBar()->addMenu("&File");
    auto *openAct = fileMenu->addAction("&Open buffer…");
    auto *saveAct = fileMenu->addAction("&Save buffer…");
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", QKeySequence::Quit, qApp, &QCoreApplication::quit);

    auto *editMenu = menuBar()->addMenu("&Edit");
    // Hex view created later in this function — wire actions lazily.
    auto *undoAct = editMenu->addAction("&Undo", QKeySequence::Undo,
                                        this, [this] { m_hex->undoStack()->undo(); });
    auto *redoAct = editMenu->addAction("&Redo", QKeySequence::Redo,
                                        this, [this] { m_hex->undoStack()->redo(); });
    editMenu->addSeparator();
    editMenu->addAction("&Go to offset…", QKeySequence("Ctrl+G"),
                        this, [this] {
        if (m_buffer->size() == 0) return;
        bool ok = false;
        const QString txt = QInputDialog::getText(this, "Go to offset",
            QString("Offset (decimal or 0x… hex, max 0x%1):")
                .arg(m_buffer->size() - 1, 0, 16),
            QLineEdit::Normal, "0x", &ok);
        if (!ok) return;
        bool numOk = false;
        const QString s = txt.trimmed();
        const qsizetype off = s.startsWith("0x", Qt::CaseInsensitive)
            ? s.mid(2).toLongLong(&numOk, 16)
            : s.toLongLong(&numOk, 10);
        if (numOk) m_hex->gotoOffset(off);
    });
    undoAct->setEnabled(false);
    redoAct->setEnabled(false);
    connect(this, &MainWindow::destroyed, this, [undoAct, redoAct]{
        Q_UNUSED(undoAct); Q_UNUSED(redoAct);
    });
    // Defer hex-view connections until it's constructed.
    QTimer::singleShot(0, this, [this, undoAct, redoAct] {
        if (!m_hex) return;
        connect(m_hex->undoStack(), &QUndoStack::canUndoChanged,
                undoAct, &QAction::setEnabled);
        connect(m_hex->undoStack(), &QUndoStack::canRedoChanged,
                redoAct, &QAction::setEnabled);
        undoAct->setEnabled(m_hex->undoStack()->canUndo());
        redoAct->setEnabled(m_hex->undoStack()->canRedo());
    });

    auto *deviceMenu = menuBar()->addMenu("&Device");
    auto *selectAct = deviceMenu->addAction("&Select chip…");
    selectAct->setShortcut(QKeySequence("Ctrl+L"));
    auto *detectAct = deviceMenu->addAction("&Detect programmer");
    m_readAct = deviceMenu->addAction("&Read chip…");
    m_readAct->setShortcut(QKeySequence("Ctrl+R"));
    m_readAct->setEnabled(false);
    m_verifyAct = deviceMenu->addAction("&Verify chip…");
    m_verifyAct->setShortcut(QKeySequence("Ctrl+Y"));
    m_verifyAct->setEnabled(false);
    m_detectIdAct = deviceMenu->addAction("Detect chip &ID…");
    m_detectIdAct->setShortcut(QKeySequence("Ctrl+I"));
    m_detectIdAct->setEnabled(false);
    m_blankCheckAct = deviceMenu->addAction("&Blank check…");
    m_blankCheckAct->setShortcut(QKeySequence("Ctrl+B"));
    m_blankCheckAct->setEnabled(false);
    deviceMenu->addSeparator();
    m_eraseAct = deviceMenu->addAction("&Erase chip…");
    m_eraseAct->setShortcut(QKeySequence("Ctrl+E"));
    m_eraseAct->setEnabled(false);
    m_writeAct = deviceMenu->addAction("&Write buffer to chip…");
    m_writeAct->setShortcut(QKeySequence("Ctrl+W"));
    m_writeAct->setEnabled(false);

    auto *helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&About", this, [this] {
        QMessageBox::about(this, "About xgecu-gui",
            "<b>xgecu-gui</b><br>Debian GUI for the XGecu T48 / T56 / TL866II+ "
            "device programmers, built on the open-source minipro library."
            "<br><br>Created by Xecaz with a healthy dose of Claude Code in 2026!"
            "<br><br>License: GPL-3.0-or-later.");
    });

    auto *tb = addToolBar("Main");
    tb->setMovable(false);
    tb->addAction(selectAct);
    tb->addSeparator();
    tb->addAction(detectAct);
    tb->addAction(m_readAct);
    tb->addAction(m_verifyAct);
    tb->addAction(m_detectIdAct);
    tb->addAction(m_blankCheckAct);
    tb->addAction(m_eraseAct);
    tb->addAction(m_writeAct);

    connect(selectAct, &QAction::triggered, this, &MainWindow::onSelectChip);
    connect(detectAct, &QAction::triggered, this, &MainWindow::onDetect);
    connect(m_readAct, &QAction::triggered, this, &MainWindow::onReadClicked);
    connect(m_verifyAct, &QAction::triggered, this, &MainWindow::onVerifyClicked);
    connect(m_detectIdAct, &QAction::triggered, this, &MainWindow::onDetectIdClicked);
    connect(m_blankCheckAct, &QAction::triggered, this, &MainWindow::onBlankCheckClicked);
    connect(m_eraseAct, &QAction::triggered, this, &MainWindow::onEraseClicked);
    connect(m_writeAct, &QAction::triggered, this, &MainWindow::onWriteClicked);
    connect(openAct, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Open buffer");
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, "Open", f.errorString());
            return;
        }
        m_buffer->setData(f.readAll());
    });
    connect(saveAct, &QAction::triggered, this, [this] {
        if (m_buffer->size() == 0) {
            QMessageBox::information(this, "Save", "Buffer is empty.");
            return;
        }
        const QString path = QFileDialog::getSaveFileName(this, "Save buffer");
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, "Save", f.errorString());
            return;
        }
        f.write(m_buffer->data());
        m_buffer->markClean();
    });

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(8, 8, 8, 8);

    m_deviceLabel = new QLabel("Programmer: detecting…", this);
    m_chipLabel = new QLabel("Chip: (none selected)", this);
    auto *banner = new QHBoxLayout;
    banner->addWidget(m_deviceLabel);
    banner->addStretch(1);
    banner->addWidget(m_chipLabel);
    root->addLayout(banner);

    auto *split = new QSplitter(Qt::Horizontal, this);
    m_hex = new HexView(this);
    m_hex->setModel(m_buffer);
    m_zif = new ZifSocketView(this);
    split->addWidget(m_hex);
    split->addWidget(m_zif);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    root->addWidget(split, 1);
}

void MainWindow::updateActions()
{
    const bool ready = m_deviceConnected && m_chipOpened;
    m_readAct->setEnabled(ready);
    m_verifyAct->setEnabled(ready && m_buffer->size() > 0);
    m_detectIdAct->setEnabled(ready);
    m_blankCheckAct->setEnabled(ready && m_chipCodeSize > 0);
    m_eraseAct->setEnabled(ready && m_canErase);
    m_eraseAct->setToolTip(m_canErase
        ? "Electrically erase the chip (sets every byte to 0xFF)."
        : "This chip is not electrically erasable (UV EPROM, OTP, or similar).");
    const bool bufMatches = (m_chipCodeSize > 0 &&
        static_cast<quint32>(m_buffer->size()) == m_chipCodeSize);
    m_writeAct->setEnabled(ready && bufMatches);
    m_writeAct->setToolTip(bufMatches
        ? "Write the current buffer to the chip."
        : QString("Load a buffer of exactly %1 bytes to enable Write.")
              .arg(m_chipCodeSize));
}

void MainWindow::onSelectChip()
{
    ChipSelectDialog dlg(m_db, this);
    if (dlg.exec() != QDialog::Accepted) return;
    m_currentChip = dlg.selectedChip();
    m_zif->setChip(m_currentChip);
    if (!m_currentChip) return;

    m_chipLabel->setText(QString("Chip: %1 / %2 @%3 (%4 pins, %5)")
                             .arg(m_currentChip->manufacturer,
                                  m_currentChip->name,
                                  m_currentChip->package)
                             .arg(m_currentChip->pinCount)
                             .arg(m_currentChip->supported ? "supported"
                                                            : "Windows-only"));

    if (!m_currentChip->supported) {
        QMessageBox::information(
            this, "Unsupported chip",
            "This chip exists in the Windows Xgpro list but is not in the "
            "minipro database used by this app. You'll need the Windows "
            "Xgpro application to program it.");
        m_chipOpened = false;
        updateActions();
        return;
    }
    if (!m_deviceConnected) {
        statusBar()->showMessage("Chip selected; connect the programmer to read.",
                                 5000);
        return;
    }
    m_chipOpened = false;
    updateActions();
    m_programmer->openChip(m_currentChip->miniproName);
}

void MainWindow::onDetect()
{
    m_deviceLabel->setText("Programmer: detecting…");
    m_programmer->detect();
}

void MainWindow::onDetected(const DeviceInfo &info)
{
    m_deviceConnected = true;
    m_deviceLabel->setText(QString("Programmer: %1  (fw %2)")
                               .arg(info.model, info.firmwareStr));
    statusBar()->showMessage(
        QString("Connected: %1 / firmware %2 / code %3")
            .arg(info.model, info.firmwareStr, info.deviceCode),
        15000);
    // If user already picked a chip, re-bind it now.
    if (m_currentChip && m_currentChip->supported)
        m_programmer->openChip(m_currentChip->miniproName);
    updateActions();
}

void MainWindow::onDetectionFailed(const QString &message)
{
    m_deviceConnected = false;
    m_chipOpened = false;
    m_deviceLabel->setText("Programmer: not detected");
    statusBar()->showMessage(message, 15000);
    updateActions();
}

void MainWindow::onChipOpened(const QString &name, quint32 codeSize,
                              quint32 dataSize, bool canErase)
{
    m_chipOpened = true;
    m_chipCodeSize = codeSize;
    m_canErase = canErase;
    statusBar()->showMessage(
        QString("Chip bound: %1 — code=%2 bytes, data=%3 bytes%4")
            .arg(name).arg(codeSize).arg(dataSize)
            .arg(canErase ? "" : " (not electrically erasable)"),
        15000);
    updateActions();
}

void MainWindow::onChipOpenFailed(const QString &message)
{
    m_chipOpened = false;
    QMessageBox::warning(this, "Open chip failed", message);
    updateActions();
}

void MainWindow::onReadClicked()
{
    if (!m_chipOpened) return;
    if (m_progress) { m_progress->reset(); m_progress->deleteLater(); }
    m_progress = new QProgressDialog("Reading code memory…", "Cancel", 0, 100, this);
    m_progress->setWindowModality(Qt::WindowModal);
    m_progress->setMinimumDuration(0);
    m_progress->setAutoClose(false);
    m_progress->setAutoReset(false);
    connect(m_progress, &QProgressDialog::canceled,
            m_programmer, &Programmer::requestCancel);
    m_progress->show();
    m_op = Op::Read;
    m_programmer->readCode();
}

void MainWindow::onVerifyClicked()
{
    if (!m_chipOpened) return;
    if (m_buffer->size() == 0) {
        QMessageBox::information(this, "Verify",
            "Buffer is empty. Open a file or read the chip first.");
        return;
    }
    if (m_progress) { m_progress->reset(); m_progress->deleteLater(); }
    m_progress = new QProgressDialog("Verifying chip against buffer…",
                                     "Cancel", 0, 100, this);
    m_progress->setWindowModality(Qt::WindowModal);
    m_progress->setMinimumDuration(0);
    m_progress->setAutoClose(false);
    m_progress->setAutoReset(false);
    connect(m_progress, &QProgressDialog::canceled,
            m_programmer, &Programmer::requestCancel);
    m_progress->show();
    m_op = Op::Verify;
    m_programmer->verifyCode(m_buffer->data());
}

void MainWindow::onProgress(qint64 done, qint64 total)
{
    if (!m_progress) return;
    if (total > 0) {
        m_progress->setMaximum(100);
        m_progress->setValue(static_cast<int>(done * 100 / total));
    }
}

void MainWindow::onReadFinished(const ReadResult &result)
{
    if (m_progress) {
        m_progress->reset();
        m_progress->deleteLater();
        m_progress = nullptr;
    }
    m_op = Op::None;
    if (!result.ok) {
        QMessageBox::warning(this, "Read failed", result.errorMessage);
        return;
    }
    m_buffer->setData(result.data);
    statusBar()->showMessage(
        QString("Read %1 bytes from code memory").arg(result.data.size()),
        15000);
    updateActions();
}

void MainWindow::onVerifyFinished(const VerifyResult &result)
{
    if (m_progress) {
        m_progress->reset();
        m_progress->deleteLater();
        m_progress = nullptr;
    }
    const bool isBlank = (m_op == Op::BlankCheck);
    m_op = Op::None;
    if (!result.errorMessage.isEmpty()) {
        QMessageBox::warning(this,
            isBlank ? "Blank check failed" : "Verify failed",
            result.errorMessage);
        return;
    }
    if (result.ok) {
        if (isBlank) {
            QMessageBox::information(this, "Blank check OK",
                QString("Chip is blank — all %1 bytes are 0xFF.")
                    .arg(result.bytesChecked));
            statusBar()->showMessage(
                QString("Blank check OK — %1 bytes 0xFF").arg(result.bytesChecked),
                15000);
        } else {
            QMessageBox::information(this, "Verify OK",
                QString("All %1 bytes match.").arg(result.bytesChecked));
            statusBar()->showMessage(
                QString("Verify OK — %1 bytes match").arg(result.bytesChecked),
                15000);
        }
    } else {
        if (isBlank) {
            QMessageBox::warning(this, "Not blank",
                QString("%1 byte%2 are not 0xFF.\n\nFirst non-blank byte at "
                        "offset 0x%3: 0x%4")
                    .arg(result.mismatches)
                    .arg(result.mismatches == 1 ? "" : "s")
                    .arg(result.firstMismatchOffset, 0, 16)
                    .arg(result.firstChipByte, 2, 16, QChar('0')));
            statusBar()->showMessage(
                QString("Not blank — %1 non-0xFF bytes").arg(result.mismatches),
                15000);
        } else {
            QMessageBox::warning(this, "Verify mismatch",
                QString("%1 byte%2 differ.\n\nFirst mismatch at offset 0x%3:\n"
                        "  buffer = 0x%4\n  chip   = 0x%5")
                    .arg(result.mismatches)
                    .arg(result.mismatches == 1 ? "" : "s")
                    .arg(result.firstMismatchOffset, 0, 16)
                    .arg(result.firstExpectedByte, 2, 16, QChar('0'))
                    .arg(result.firstChipByte, 2, 16, QChar('0')));
            statusBar()->showMessage(
                QString("Verify FAILED — %1 mismatches").arg(result.mismatches),
                15000);
        }
    }
}

void MainWindow::onWriteClicked()
{
    if (!m_chipOpened) return;
    if (static_cast<quint32>(m_buffer->size()) != m_chipCodeSize) {
        QMessageBox::information(this, "Write",
            QString("Buffer is %1 bytes, chip code memory is %2 bytes. "
                    "Load a matching-size file first.")
                .arg(m_buffer->size()).arg(m_chipCodeSize));
        return;
    }
    const QString chipDesc = m_currentChip
        ? QString("%1 / %2 @%3").arg(m_currentChip->manufacturer,
                                      m_currentChip->name,
                                      m_currentChip->package)
        : QString("(chip)");
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle("Write chip?");
    box.setText("<b>Write buffer to chip?</b>");
    box.setInformativeText(
        QString("This will program <b>%1</b> with the current %2-byte buffer. "
                "Most EEPROM/flash chips also need to be erased first — use "
                "Erase before Write if you're not overwriting a blank chip."
                "<br><br><i>The pre-write safety check refuses if the chip's "
                "first block reads as all 0xFF unless the buffer itself is "
                "all 0xFF. Use Force to override.</i>")
            .arg(chipDesc).arg(m_chipCodeSize));
    auto *force = new QCheckBox("Force — skip the pre-write blank check", &box);
    auto *verify = new QCheckBox("Auto-verify after write (recommended)", &box);
    verify->setChecked(true);
    // QMessageBox only lets us add one checkbox via setCheckBox; place the
    // verify option as the primary, expose force via the action label.
    box.setCheckBox(verify);
    auto *writeBtn = box.addButton("Write", QMessageBox::DestructiveRole);
    auto *forceBtn = box.addButton("Write (force)", QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    const bool isForce = (box.clickedButton() == static_cast<QAbstractButton *>(forceBtn));
    if (box.clickedButton() != static_cast<QAbstractButton *>(writeBtn) && !isForce)
        return;
    delete force;  // we used buttons instead

    if (m_progress) { m_progress->reset(); m_progress->deleteLater(); }
    m_progress = new QProgressDialog(
        verify->isChecked() ? "Writing chip…" : "Writing chip…",
        "Cancel", 0, 100, this);
    m_progress->setWindowModality(Qt::WindowModal);
    m_progress->setMinimumDuration(0);
    m_progress->setAutoClose(false);
    m_progress->setAutoReset(false);
    connect(m_progress, &QProgressDialog::canceled,
            m_programmer, &Programmer::requestCancel);
    m_progress->show();
    m_op = Op::Write;
    m_programmer->writeCode(m_buffer->data(), isForce, verify->isChecked());
}

void MainWindow::onWriteFinished(const WriteResult &result)
{
    if (m_progress) {
        m_progress->reset();
        m_progress->deleteLater();
        m_progress = nullptr;
    }
    m_op = Op::None;
    if (result.ok) {
        m_buffer->markClean();
        QMessageBox::information(this, "Write complete",
            QString("Wrote %1 bytes%2.")
                .arg(result.bytesWritten)
                .arg(result.verified ? " — auto-verify OK" : ""));
        statusBar()->showMessage(
            QString("Write OK — %1 bytes%2").arg(result.bytesWritten)
                .arg(result.verified ? ", verified" : ""), 15000);
    } else {
        QMessageBox::warning(this, "Write failed",
            result.errorMessage.isEmpty() ? "Unknown error" : result.errorMessage);
    }
    updateActions();
}

void MainWindow::onEraseClicked()
{
    if (!m_chipOpened || !m_canErase) return;
    const QString chipDesc = m_currentChip
        ? QString("%1 / %2 @%3").arg(m_currentChip->manufacturer,
                                      m_currentChip->name,
                                      m_currentChip->package)
        : QString("(chip)");
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle("Erase chip?");
    box.setText(QString("<b>Erase the chip in the socket?</b>"));
    box.setInformativeText(
        QString("This will permanently set every byte of <b>%1</b> "
                "(%2 bytes of code memory) to 0xFF.<br><br>"
                "Make sure the right chip is selected and inserted "
                "before continuing.<br><br>"
                "<i>The T48 cannot electrically detect a missing chip. "
                "A safety check reads the first block before erasing; "
                "if it reads as all 0xFF (blank or empty socket) the "
                "erase will be refused.</i>")
            .arg(chipDesc).arg(m_chipCodeSize));
    auto *force = new QCheckBox("Force — skip the pre-erase blank check", &box);
    box.setCheckBox(force);
    auto *eraseBtn = box.addButton("Erase", QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != static_cast<QAbstractButton *>(eraseBtn)) return;

    statusBar()->showMessage("Erasing chip…", 0);
    m_eraseAct->setEnabled(false);
    m_programmer->eraseChip(force->isChecked());
}

void MainWindow::onEraseFinished(bool ok, const QString &message)
{
    statusBar()->clearMessage();
    if (!ok) {
        QMessageBox::warning(this, "Erase failed",
            message.isEmpty() ? "Unknown error" : message);
        updateActions();
        return;
    }
    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle("Erase complete");
    box.setText("Chip erased.");
    box.setInformativeText("Run Blank check now to confirm every byte is 0xFF?");
    auto *runBlankBtn = box.addButton("Run Blank Check", QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Close);
    box.setDefaultButton(runBlankBtn);
    box.exec();
    updateActions();
    if (box.clickedButton() == static_cast<QAbstractButton *>(runBlankBtn))
        onBlankCheckClicked();
}

void MainWindow::onBlankCheckClicked()
{
    if (!m_chipOpened || m_chipCodeSize == 0) return;
    if (m_progress) { m_progress->reset(); m_progress->deleteLater(); }
    m_progress = new QProgressDialog("Blank check…", "Cancel", 0, 100, this);
    m_progress->setWindowModality(Qt::WindowModal);
    m_progress->setMinimumDuration(0);
    m_progress->setAutoClose(false);
    m_progress->setAutoReset(false);
    connect(m_progress, &QProgressDialog::canceled,
            m_programmer, &Programmer::requestCancel);
    m_progress->show();
    m_op = Op::BlankCheck;
    // Blank check = verify against an all-0xFF reference of the chip's size.
    QByteArray blank(static_cast<qsizetype>(m_chipCodeSize), char(0xFF));
    m_programmer->verifyCode(blank);
}

void MainWindow::onDetectIdClicked()
{
    if (!m_chipOpened) return;
    statusBar()->showMessage("Reading chip ID…", 2000);
    m_programmer->detectChipId();
}

void MainWindow::onChipIdFinished(const ChipIdResult &result)
{
    using S = ChipIdResult::Status;
    const int hexDigits = result.idBytesCount ? result.idBytesCount * 2 : 8;
    auto hexStr = [hexDigits](quint32 v) {
        return QString("0x%1").arg(v, hexDigits, 16, QChar('0')).toUpper();
    };
    switch (result.status) {
    case S::Unsupported:
        QMessageBox::information(this, "Chip ID",
            "This chip family has no readable signature/ID protocol "
            "(e.g. UV EPROMs, 24Cxx, 93Cxx). No way to identify it "
            "electronically — you must select the correct part manually.");
        statusBar()->showMessage("Chip has no ID protocol", 8000);
        break;
    case S::Match:
        QMessageBox::information(this, "Chip ID OK",
            QString("Read chip ID: %1\nMatches the selected chip.")
                .arg(hexStr(result.readId)));
        statusBar()->showMessage(
            QString("Chip ID %1 OK").arg(hexStr(result.readId)), 8000);
        break;
    case S::Mismatch: {
        QString msg = QString("Chip ID mismatch!\n\n"
                              "Expected: %1\nRead:     %2")
                          .arg(hexStr(result.expectedId), hexStr(result.readId));
        if (!result.identifiedAs.isEmpty())
            msg += QString("\n\nThe inserted chip looks like: %1")
                       .arg(result.identifiedAs);
        else
            msg += "\n\n(no matching chip found in database)";
        QMessageBox::warning(this, "Chip ID mismatch", msg);
        statusBar()->showMessage("Chip ID mismatch", 8000);
        break;
    }
    case S::IoError:
        QMessageBox::warning(this, "Detect chip ID failed",
            result.errorMessage.isEmpty() ? "Unknown error" : result.errorMessage);
        break;
    }
}

void MainWindow::onProgrammerError(const QString &message)
{
    if (m_progress) {
        m_progress->reset();
        m_progress->deleteLater();
        m_progress = nullptr;
    }
    m_op = Op::None;
    QMessageBox::warning(this, "Programmer error", message);
}
