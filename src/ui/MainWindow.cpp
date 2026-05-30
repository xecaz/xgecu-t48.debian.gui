// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QLineEdit>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QUndoStack>
#include <QVBoxLayout>

#include "ChipSelectDialog.h"
#include "FuseEditorWidget.h"
#include "HexView.h"
#include "PreferencesDialog.h"
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
        "/usr/share/xgecu-gui/" + name,
        "/usr/local/share/xgecu-gui/" + name,
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
    setAcceptDrops(true);
    // Ask the WM for minimise + maximise buttons (next to the X) in the
    // title bar — defaults vary by desktop environment.
    setWindowFlags(windowFlags()
                   | Qt::WindowMinimizeButtonHint
                   | Qt::WindowMaximizeButtonHint
                   | Qt::WindowCloseButtonHint);

    m_db = new ChipDatabase(this);
    m_programmer = new Programmer(this);
    for (MemArea a : { MemArea::Code, MemArea::Data }) {
        AreaPane p;
        p.buffer = new BufferModel(this);
        m_panes.insert(a, p);
    }
    m_buffer = m_panes[MemArea::Code].buffer;  // initial alias

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
    connect(m_programmer, &Programmer::fusesAvailable, this, &MainWindow::onFusesAvailable);
    connect(m_programmer, &Programmer::fusesRead, this, &MainWindow::onFusesRead);
    connect(m_programmer, &Programmer::fuseWriteFinished, this, &MainWindow::onFuseWriteFinished);
    connect(m_programmer, &Programmer::error, this, &MainWindow::onProgrammerError);
    for (MemArea a : { MemArea::Code, MemArea::Data }) {
        connect(m_panes[a].buffer, &BufferModel::reset,
                this, &MainWindow::updateActions);
        connect(m_panes[a].buffer, &BufferModel::dirtyChanged,
                this, [this, a] { updateTabLabel(a); });
    }

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
    fileMenu->addAction("&Preferences…", QKeySequence(QKeySequence::Preferences),
                        this, &MainWindow::onPreferences);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", QKeySequence::Quit, qApp, &QCoreApplication::quit);

    auto *editMenu = menuBar()->addMenu("&Edit");
    // Hex view created later in this function — wire actions lazily.
    m_undoAct = editMenu->addAction("&Undo", QKeySequence::Undo,
                                    this, [this] { m_hex->undoStack()->undo(); });
    m_redoAct = editMenu->addAction("&Redo", QKeySequence::Redo,
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
    editMenu->addSeparator();
    editMenu->addAction("&Fill…", this, [this] {
        if (m_buffer->size() == 0) return;
        const qsizetype bufLast = m_buffer->size() - 1;
        const bool hasSel = m_hex->hasSelection();
        const qsizetype defStart = hasSel ? m_hex->selectionStart()
                                          : m_hex->cursorOffset();
        const qsizetype defEnd = hasSel
            ? defStart + m_hex->selectionLength() - 1
            : bufLast;

        QDialog dlg(this);
        dlg.setWindowTitle("Fill");
        auto *form = new QFormLayout(&dlg);
        auto *startEdit = new QLineEdit(
            QString("0x%1").arg(defStart, 0, 16).toUpper(), &dlg);
        auto *endEdit = new QLineEdit(
            QString("0x%1").arg(defEnd, 0, 16).toUpper(), &dlg);
        auto *patEdit = new QLineEdit("FF", &dlg);
        patEdit->setPlaceholderText("hex bytes, e.g. FF  or  EA F1 00 2C");
        auto *summary = new QLabel(&dlg);
        summary->setWordWrap(true);
        form->addRow(QString("Start (max 0x%1):").arg(bufLast, 0, 16).toUpper(),
                     startEdit);
        form->addRow("End (inclusive):", endEdit);
        form->addRow("Pattern (hex, repeats):", patEdit);
        form->addRow(summary);

        auto parseOffset = [bufLast](const QString &t, qsizetype *out) {
            const QString s = t.trimmed();
            if (s.isEmpty()) return false;
            bool ok = false;
            const qsizetype v = s.startsWith("0x", Qt::CaseInsensitive)
                ? s.mid(2).toLongLong(&ok, 16)
                : s.toLongLong(&ok, 10);
            if (!ok || v < 0 || v > bufLast) return false;
            *out = v;
            return true;
        };
        auto parsePattern = [](const QString &t) {
            QString hex;
            for (QChar c : t) if (!c.isSpace()) hex.append(c);
            if (hex.isEmpty() || hex.size() % 2) return QByteArray();
            QByteArray bytes = QByteArray::fromHex(hex.toLatin1());
            return (bytes.size() == hex.size() / 2) ? bytes : QByteArray();
        };
        auto refresh = [&] {
            qsizetype s = 0, e = 0;
            const bool sOk = parseOffset(startEdit->text(), &s);
            const bool eOk = parseOffset(endEdit->text(), &e);
            const QByteArray pat = parsePattern(patEdit->text());
            QString msg;
            if (!sOk || !eOk) msg = "Enter start and end as decimal or 0x… hex.";
            else if (e < s) msg = "End must be ≥ start.";
            else if (pat.isEmpty()) msg = "Pattern must be hex bytes (even digit count).";
            else msg = QString("Will write %1 bytes (pattern repeats %2 time(s)).")
                          .arg(e - s + 1)
                          .arg(double(e - s + 1) / pat.size(), 0, 'g', 3);
            summary->setText(msg);
        };
        QObject::connect(startEdit, &QLineEdit::textChanged, &dlg, refresh);
        QObject::connect(endEdit, &QLineEdit::textChanged, &dlg, refresh);
        QObject::connect(patEdit, &QLineEdit::textChanged, &dlg, refresh);
        refresh();

        auto *btns = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        btns->button(QDialogButtonBox::Ok)->setText("Fill");
        form->addRow(btns);
        QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

        if (dlg.exec() != QDialog::Accepted) return;
        qsizetype s = 0, e = 0;
        if (!parseOffset(startEdit->text(), &s)
            || !parseOffset(endEdit->text(), &e) || e < s) {
            QMessageBox::warning(this, "Fill", "Invalid range.");
            return;
        }
        const QByteArray pat = parsePattern(patEdit->text());
        if (pat.isEmpty()) {
            QMessageBox::warning(this, "Fill", "Invalid pattern.");
            return;
        }
        const qsizetype n = m_hex->fillRange(s, e - s + 1, pat);
        statusBar()->showMessage(
            QString("Filled %1 bytes (0x%2..0x%3) with %4-byte pattern")
                .arg(n).arg(s, 0, 16).arg(e, 0, 16).arg(pat.size()).toUpper(),
            5000);
    });
    editMenu->addAction("&Copy range…", this, [this] {
        if (m_buffer->size() == 0) return;
        const qsizetype bufLast = m_buffer->size() - 1;
        const bool hasSel = m_hex->hasSelection();
        const qsizetype defSrcStart = hasSel ? m_hex->selectionStart()
                                             : m_hex->cursorOffset();
        const qsizetype defSrcEnd = hasSel
            ? defSrcStart + m_hex->selectionLength() - 1
            : qMin<qsizetype>(defSrcStart + 0xF, bufLast);
        const qsizetype defDst = m_hex->cursorOffset();

        QDialog dlg(this);
        dlg.setWindowTitle("Copy range");
        auto *form = new QFormLayout(&dlg);
        auto *srcStart = new QLineEdit(
            QString("0x%1").arg(defSrcStart, 0, 16).toUpper(), &dlg);
        auto *srcEnd = new QLineEdit(
            QString("0x%1").arg(defSrcEnd, 0, 16).toUpper(), &dlg);
        auto *dstOff = new QLineEdit(
            QString("0x%1").arg(defDst, 0, 16).toUpper(), &dlg);
        auto *summary = new QLabel(&dlg);
        summary->setWordWrap(true);
        form->addRow("Source start:", srcStart);
        form->addRow("Source end (inclusive):", srcEnd);
        form->addRow("Destination start:", dstOff);
        form->addRow(summary);

        auto parseOffset = [bufLast](const QString &t, qsizetype *out) {
            const QString s = t.trimmed();
            if (s.isEmpty()) return false;
            bool ok = false;
            const qsizetype v = s.startsWith("0x", Qt::CaseInsensitive)
                ? s.mid(2).toLongLong(&ok, 16)
                : s.toLongLong(&ok, 10);
            if (!ok || v < 0 || v > bufLast) return false;
            *out = v;
            return true;
        };
        auto refresh = [&] {
            qsizetype ss = 0, se = 0, ds = 0;
            const bool sOk = parseOffset(srcStart->text(), &ss);
            const bool eOk = parseOffset(srcEnd->text(), &se);
            const bool dOk = parseOffset(dstOff->text(), &ds);
            QString msg;
            if (!sOk || !eOk || !dOk)
                msg = "Enter offsets as decimal or 0x… hex.";
            else if (se < ss)
                msg = "Source end must be ≥ source start.";
            else {
                const qsizetype len = se - ss + 1;
                const qsizetype dstEnd = ds + len - 1;
                if (dstEnd > bufLast)
                    msg = QString("Destination would run past end of buffer "
                                  "(0x%1 > 0x%2). Copy will be truncated to "
                                  "%3 bytes.")
                              .arg(dstEnd, 0, 16).arg(bufLast, 0, 16)
                              .arg(bufLast - ds + 1).toUpper();
                else
                    msg = QString("Copy %1 bytes  (0x%2..0x%3 → 0x%4..0x%5).")
                              .arg(len)
                              .arg(ss, 0, 16).arg(se, 0, 16)
                              .arg(ds, 0, 16).arg(dstEnd, 0, 16).toUpper();
            }
            summary->setText(msg);
        };
        QObject::connect(srcStart, &QLineEdit::textChanged, &dlg, refresh);
        QObject::connect(srcEnd, &QLineEdit::textChanged, &dlg, refresh);
        QObject::connect(dstOff, &QLineEdit::textChanged, &dlg, refresh);
        refresh();

        auto *btns = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        btns->button(QDialogButtonBox::Ok)->setText("Copy");
        form->addRow(btns);
        QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;

        qsizetype ss = 0, se = 0, ds = 0;
        if (!parseOffset(srcStart->text(), &ss)
            || !parseOffset(srcEnd->text(), &se)
            || !parseOffset(dstOff->text(), &ds) || se < ss) {
            QMessageBox::warning(this, "Copy range", "Invalid range.");
            return;
        }
        const qsizetype n = m_hex->copyRange(ss, se - ss + 1, ds);
        statusBar()->showMessage(
            QString("Copied %1 bytes from 0x%2 to 0x%3")
                .arg(n).arg(ss, 0, 16).arg(ds, 0, 16).toUpper(), 5000);
    });
    editMenu->addAction("&Find bytes…", QKeySequence::Find, this, [this] {
        if (m_buffer->size() == 0) return;

        QDialog dlg(this);
        dlg.setWindowTitle("Find bytes");
        auto *outer = new QVBoxLayout(&dlg);
        auto *form = new QFormLayout;
        outer->addLayout(form);

        auto *patEdit = new QLineEdit(m_lastFindText, &dlg);
        patEdit->setPlaceholderText("pattern…");
        form->addRow("Pattern:", patEdit);

        auto *modeRow = new QHBoxLayout;
        auto *hexBtn = new QRadioButton("Hex bytes", &dlg);
        auto *asciiBtn = new QRadioButton("ASCII text", &dlg);
        auto *decBtn = new QRadioButton("Decimal bytes", &dlg);
        modeRow->addWidget(hexBtn);
        modeRow->addWidget(asciiBtn);
        modeRow->addWidget(decBtn);
        modeRow->addStretch(1);
        switch (m_lastFindMode) {
        case 1: asciiBtn->setChecked(true); break;
        case 2: decBtn->setChecked(true); break;
        default: hexBtn->setChecked(true); break;
        }
        form->addRow("Mode:", modeRow);

        auto *preview = new QLabel(&dlg);
        preview->setWordWrap(true);
        preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow("Bytes:", preview);

        auto parsePattern = [](const QString &text, int mode) -> QByteArray {
            const QString s = text.trimmed();
            if (s.isEmpty()) return {};
            if (mode == 1) {
                // ASCII: literal bytes (Latin-1).
                return s.toLatin1();
            }
            // Hex (mode 0) or Decimal (mode 2): tokenize on whitespace AND
            // commas; each token is one byte.
            QByteArray out;
            QString tok;
            auto flush = [&](bool *ok) {
                if (tok.isEmpty()) { *ok = true; return; }
                bool numOk = false;
                const int v = (mode == 0) ? tok.toInt(&numOk, 16)
                                          : tok.toInt(&numOk, 10);
                if (!numOk || v < 0 || v > 255) { *ok = false; return; }
                out.append(static_cast<char>(v));
                tok.clear();
                *ok = true;
            };
            // For hex with no separators (e.g. "DEADBEEF") take pairs of digits.
            if (mode == 0) {
                QString stripped;
                for (QChar c : s)
                    if (!c.isSpace() && c != ',') stripped.append(c);
                bool allHex = true;
                for (QChar c : stripped)
                    if (!QChar::isLetterOrNumber(c.unicode())
                        || (!c.isDigit() && c.toUpper() < 'A'
                            || c.toUpper() > 'F')) { allHex = false; break; }
                if (allHex && stripped.size() % 2 == 0) {
                    QByteArray bytes = QByteArray::fromHex(stripped.toLatin1());
                    if (bytes.size() == stripped.size() / 2)
                        return bytes;
                }
            }
            // Token mode (works for both hex and decimal once split).
            bool ok = true;
            for (QChar c : s) {
                if (c.isSpace() || c == ',') {
                    flush(&ok);
                    if (!ok) return {};
                } else {
                    tok.append(c);
                }
            }
            flush(&ok);
            if (!ok) return {};
            return out;
        };

        auto modeFromUi = [&] {
            return hexBtn->isChecked() ? 0
                 : asciiBtn->isChecked() ? 1
                 : 2;
        };
        auto refresh = [&] {
            const QByteArray bytes = parsePattern(patEdit->text(), modeFromUi());
            if (bytes.isEmpty()) {
                preview->setText("<i>(invalid or empty pattern)</i>");
            } else {
                preview->setText(
                    QString("%1 byte%2: %3")
                        .arg(bytes.size())
                        .arg(bytes.size() == 1 ? "" : "s")
                        .arg(QString::fromLatin1(bytes.toHex(' ')).toUpper()));
            }
        };
        QObject::connect(patEdit, &QLineEdit::textChanged, &dlg, refresh);
        QObject::connect(hexBtn, &QRadioButton::toggled, &dlg, refresh);
        QObject::connect(asciiBtn, &QRadioButton::toggled, &dlg, refresh);
        QObject::connect(decBtn, &QRadioButton::toggled, &dlg, refresh);
        refresh();

        auto *btns = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        btns->button(QDialogButtonBox::Ok)->setText("Find");
        outer->addWidget(btns);
        QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

        if (dlg.exec() != QDialog::Accepted) return;
        const QByteArray needle = parsePattern(patEdit->text(), modeFromUi());
        if (needle.isEmpty()) {
            QMessageBox::warning(this, "Find", "Invalid or empty pattern.");
            return;
        }
        m_lastFindBytes = needle;
        m_lastFindText = patEdit->text();
        m_lastFindMode = modeFromUi();
        m_lastFindHit = -1;  // fresh search; allow first wrap to register cleanly

        const qsizetype from = m_hex->cursorOffset() + 1;
        const qsizetype hit = m_hex->findBytes(needle, from);
        if (hit < 0) {
            statusBar()->showMessage(
                QString("Not found: \"%1\"").arg(m_lastFindText), 6000);
            return;
        }
        m_hex->selectRange(hit, needle.size());
        const bool wrapped = (hit < from);
        m_lastFindHit = hit;
        statusBar()->showMessage(
            wrapped
                ? QString("Search wrapped to 0x%1 — \"%2\"")
                      .arg(hit, 0, 16).arg(m_lastFindText).toUpper()
                : QString("Match at 0x%1").arg(hit, 0, 16).toUpper(),
            6000);
    });
    editMenu->addAction("Find &next", QKeySequence("F3"), this, [this] {
        if (m_lastFindBytes.isEmpty() || m_buffer->size() == 0) return;
        const qsizetype from = m_hex->cursorOffset() + 1;
        const qsizetype hit = m_hex->findBytes(m_lastFindBytes, from);
        if (hit < 0) {
            statusBar()->showMessage(
                QString("Not found: \"%1\"").arg(m_lastFindText), 6000);
            return;
        }
        const bool wrapped = (hit < from);
        const bool sameAsLast = (hit == m_lastFindHit);
        m_lastFindHit = hit;
        m_hex->selectRange(hit, m_lastFindBytes.size());

        QString msg;
        if (wrapped && sameAsLast)
            msg = QString("Only one match — \"%1\" at 0x%2")
                      .arg(m_lastFindText)
                      .arg(hit, 0, 16).toUpper();
        else if (wrapped)
            msg = QString("Search wrapped to 0x%1 — no more instances of \"%2\" after cursor")
                      .arg(hit, 0, 16).arg(m_lastFindText).toUpper();
        else
            msg = QString("Match at 0x%1").arg(hit, 0, 16).toUpper();
        statusBar()->showMessage(msg, 6000);
    });
    m_undoAct->setEnabled(false);
    m_redoAct->setEnabled(false);
    // Tab construction runs later in buildUi(); QTimer::singleShot defers the
    // initial undo-stack wiring until the views exist. After that,
    // onCurrentTabChanged() owns re-wiring whenever the user switches tab.
    QTimer::singleShot(0, this, [this] { onCurrentTabChanged(0); });

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
    m_writeAct = deviceMenu->addAction("&Write chip…");
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

    // Ctrl+M as a keyboard convenience for toggling maximise. The title-bar
    // maximise button (requested via the WindowMaximizeButtonHint flag above)
    // is the primary UI affordance.
    auto *maxAct = new QAction(this);
    maxAct->setShortcut(QKeySequence("Ctrl+M"));
    maxAct->setShortcutContext(Qt::ApplicationShortcut);
    addAction(maxAct);
    connect(maxAct, &QAction::triggered, this, [this] {
        setWindowState(windowState() ^ Qt::WindowMaximized);
    });


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
        loadBufferFromPath(path);
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
    m_tabs = new QTabWidget(this);
    for (MemArea a : { MemArea::Code, MemArea::Data }) {
        auto &pane = m_panes[a];
        pane.view = new HexView(this);
        pane.view->setModel(pane.buffer);
        const QString label = (a == MemArea::Code) ? "Code" : "Data (EEPROM)";
        pane.tabIndex = m_tabs->addTab(pane.view, label);
    }
    // Data tab hidden until a chip with data memory is bound.
    m_tabs->setTabVisible(m_panes[MemArea::Data].tabIndex, false);
    m_hex = m_panes[MemArea::Code].view;  // initial alias

    // Fuses tab: hidden until a chip that exposes config fuses is opened.
    m_fuseEditor = new FuseEditorWidget(this);
    m_fuseTabIndex = m_tabs->addTab(m_fuseEditor, "Fuses");
    m_tabs->setTabVisible(m_fuseTabIndex, false);
    connect(m_fuseEditor, &FuseEditorWidget::readRequested,
            m_programmer, &Programmer::readFuses);
    connect(m_fuseEditor, &FuseEditorWidget::writeRequested,
            this, &MainWindow::onFuseWriteRequested);

    m_zif = new ZifSocketView(this);
    split->addWidget(m_tabs);
    split->addWidget(m_zif);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    root->addWidget(split, 1);

    connect(m_tabs, &QTabWidget::currentChanged,
            this, &MainWindow::onCurrentTabChanged);
}

MemArea MainWindow::currentArea() const
{
    if (!m_tabs) return MemArea::Code;
    return (m_tabs->currentIndex() == m_panes[MemArea::Data].tabIndex)
        ? MemArea::Data : MemArea::Code;
}

void MainWindow::updateTabLabel(MemArea area)
{
    if (!m_tabs) return;
    const auto &pane = m_panes[area];
    if (pane.tabIndex < 0) return;
    const QString base = (area == MemArea::Data) ? "Data (EEPROM)" : "Code";
    const bool dirty = pane.buffer && pane.buffer->hasDirtyBytes();
    m_tabs->setTabText(pane.tabIndex, dirty ? ("● " + base) : base);
}

void MainWindow::onCurrentTabChanged(int)
{
    const MemArea a = currentArea();
    m_buffer = m_panes[a].buffer;
    m_hex = m_panes[a].view;
    // Re-bind undo/redo enabled state to the active tab's stack.
    if (m_undoConn) disconnect(m_undoConn);
    if (m_redoConn) disconnect(m_redoConn);
    if (m_undoAct && m_hex) {
        m_undoConn = connect(m_hex->undoStack(), &QUndoStack::canUndoChanged,
                             m_undoAct, &QAction::setEnabled);
        m_undoAct->setEnabled(m_hex->undoStack()->canUndo());
    }
    if (m_redoAct && m_hex) {
        m_redoConn = connect(m_hex->undoStack(), &QUndoStack::canRedoChanged,
                             m_redoAct, &QAction::setEnabled);
        m_redoAct->setEnabled(m_hex->undoStack()->canRedo());
    }
    updateActions();
}

void MainWindow::loadBufferFromPath(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Open", f.errorString());
        return;
    }
    m_buffer->setData(f.readAll());
    statusBar()->showMessage(
        QString("Loaded %1 (%2 bytes)")
            .arg(QFileInfo(path).fileName()).arg(m_buffer->size()), 8000);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    const QMimeData *m = e->mimeData();
    if (m && m->hasUrls()) {
        for (const QUrl &u : m->urls()) {
            if (u.isLocalFile()) { e->acceptProposedAction(); return; }
        }
    }
}

void MainWindow::dropEvent(QDropEvent *e)
{
    const QMimeData *m = e->mimeData();
    if (!m || !m->hasUrls()) return;
    for (const QUrl &u : m->urls()) {
        if (!u.isLocalFile()) continue;
        loadBufferFromPath(u.toLocalFile());
        e->acceptProposedAction();
        return;  // load the first one only
    }
}

void MainWindow::updateActions()
{
    const bool ready = m_deviceConnected && m_chipOpened;
    const quint32 sz = currentChipSize();
    m_readAct->setEnabled(ready && sz > 0);
    m_verifyAct->setEnabled(ready && sz > 0 && m_buffer && m_buffer->size() > 0);
    m_detectIdAct->setEnabled(ready);
    m_blankCheckAct->setEnabled(ready && sz > 0);
    m_eraseAct->setEnabled(ready && m_canErase);
    m_eraseAct->setToolTip(m_canErase
        ? "Electrically erase the chip (sets every byte to 0xFF)."
        : "This chip is not electrically erasable (UV EPROM, OTP, or similar).");
    const bool bufMatches = (sz > 0 && m_buffer
        && static_cast<quint32>(m_buffer->size()) == sz);
    m_writeAct->setEnabled(ready && bufMatches);
    m_writeAct->setToolTip(bufMatches
        ? "Write the current buffer to the chip."
        : QString("Load a buffer of exactly %1 bytes to enable Write.").arg(sz));
}

void MainWindow::onPreferences()
{
    PreferencesDialog dlg(this);
    dlg.exec();
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
    m_canErase = canErase;
    m_panes[MemArea::Code].chipSize = codeSize;
    m_panes[MemArea::Data].chipSize = dataSize;
    // Show / hide Data tab depending on whether the chip has data memory.
    const int dataTabIdx = m_panes[MemArea::Data].tabIndex;
    m_tabs->setTabVisible(dataTabIdx, dataSize > 0);
    if (dataSize == 0 && m_tabs->currentIndex() == dataTabIdx)
        m_tabs->setCurrentIndex(m_panes[MemArea::Code].tabIndex);
    // Reset both buffers — old chip's contents are no longer relevant.
    for (MemArea a : { MemArea::Code, MemArea::Data })
        m_panes[a].buffer->clear();
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

void MainWindow::onFusesAvailable(const FuseSet &fuses)
{
    m_fuseEditor->bindLayout(fuses);
    if (m_fuseTabIndex < 0) return;
    m_tabs->setTabVisible(m_fuseTabIndex, fuses.valid);
    if (!fuses.valid && m_tabs->currentIndex() == m_fuseTabIndex)
        m_tabs->setCurrentIndex(m_panes[MemArea::Code].tabIndex);
}

void MainWindow::onFusesRead(const FuseSet &fuses)
{
    m_fuseEditor->applyRead(fuses);
    statusBar()->showMessage(
        fuses.errorMessage.isEmpty()
            ? QStringLiteral("Fuses read from chip.")
            : ("Fuse read failed: " + fuses.errorMessage),
        10000);
}

void MainWindow::onFuseWriteFinished(bool ok, bool verified, const QString &message)
{
    m_fuseEditor->setBusy(false);
    if (!ok) {
        QMessageBox::warning(this, "Write fuses failed", message);
        statusBar()->showMessage("Fuse write failed.", 10000);
        return;
    }
    if (!verified) {
        QMessageBox::warning(this, "Fuse verify mismatch",
            message.isEmpty()
                ? "Fuses were written, but the read-back did not match."
                : message);
    } else {
        statusBar()->showMessage("Fuses written and verified.", 8000);
    }
    // Re-read so the editor reflects exactly what's now on the chip.
    m_programmer->readFuses();
}

void MainWindow::onFuseWriteRequested(const FuseSet &subset, bool locks)
{
    if (!m_chipOpened) return;
    if (!subset.valid || subset.items.isEmpty()) {
        QMessageBox::information(this, "Nothing to write",
            "There are no values to write in this section.");
        return;
    }

    QString list;
    for (const FuseItem &it : subset.items)
        list += QString("\n    %1 = 0x%2")
                    .arg(it.name)
                    .arg(it.value, (it.mask > 0xFF) ? 4 : 2, 16, QChar('0'));

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    if (locks) {
        box.setWindowTitle("Write lock bits");
        box.setText("About to write LOCK BITS to the chip.");
        box.setInformativeText(
            "Lock bits restrict reading and reprogramming, and can only be "
            "cleared again by a full chip erase. Make sure you have a backup "
            "of the chip's contents first.\n\nValues:" + list);
    } else {
        box.setWindowTitle("Write fuses");
        box.setText("About to write configuration fuses to the chip.");
        box.setInformativeText(
            "Incorrect fuse settings — wrong clock source, or disabling "
            "SPIEN / RSTDISBL — can make the chip unresponsive to the "
            "programmer until recovered via high-voltage programming.\n\n"
            "Values:" + list);
    }
    auto *writeBtn = box.addButton("Write", QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != static_cast<QAbstractButton *>(writeBtn))
        return;

    m_fuseEditor->setBusy(true);
    m_programmer->writeFuses(subset);
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
    m_programmer->readMemory(currentArea());
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
    m_programmer->verifyMemory(currentArea(), m_buffer->data());
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
    auto &pane = m_panes[result.area];
    pane.buffer->setData(result.data);
    if (pane.tabIndex >= 0 && m_tabs->isTabVisible(pane.tabIndex))
        m_tabs->setCurrentIndex(pane.tabIndex);
    const char *areaName = (result.area == MemArea::Data) ? "data" : "code";
    statusBar()->showMessage(
        QString("Read %1 bytes from %2 memory")
            .arg(result.data.size()).arg(areaName), 15000);
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
    const MemArea area = currentArea();
    const quint32 sz = currentChipSize();
    if (static_cast<quint32>(m_buffer->size()) != sz) {
        QMessageBox::information(this, "Write",
            QString("Buffer is %1 bytes, chip %2 memory is %3 bytes. "
                    "Load a matching-size file first.")
                .arg(m_buffer->size())
                .arg(area == MemArea::Data ? "data" : "code")
                .arg(sz));
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
    box.setText(QString("<b>Write %1 buffer to chip?</b>")
                    .arg(area == MemArea::Data ? "data" : "code"));
    box.setInformativeText(
        QString("This will program the %1 memory of <b>%2</b> with the "
                "current %3-byte buffer. "
                "Most EEPROM/flash chips also need to be erased first — use "
                "Erase before Write if you're not overwriting a blank chip."
                "<br><br><i>The pre-write safety check refuses if the chip's "
                "first block reads as all 0xFF unless the buffer itself is "
                "all 0xFF. Use Force to override.</i>")
            .arg(area == MemArea::Data ? "data" : "code")
            .arg(chipDesc).arg(sz));
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
    m_programmer->writeMemory(currentArea(), m_buffer->data(),
                              isForce, verify->isChecked());
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
        m_panes[result.area].buffer->markClean();
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
                "(%2 bytes of code memory) to 0xFF. Erase is a whole-chip "
                "operation — both code AND data memory get cleared.<br><br>"
                "Make sure the right chip is selected and inserted "
                "before continuing.<br><br>"
                "<i>The T48 cannot electrically detect a missing chip. "
                "A safety check reads the first block before erasing; "
                "if it reads as all 0xFF (blank or empty socket) the "
                "erase will be refused.</i>")
            .arg(chipDesc).arg(m_panes[MemArea::Code].chipSize));
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
    const quint32 sz = currentChipSize();
    if (!m_chipOpened || sz == 0) return;
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
    QByteArray blank(static_cast<qsizetype>(sz), char(0xFF));
    m_programmer->verifyMemory(currentArea(), blank);
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
