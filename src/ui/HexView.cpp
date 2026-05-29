// SPDX-License-Identifier: GPL-3.0-or-later
#include "HexView.h"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QUndoCommand>

#include "core/BufferModel.h"

namespace {
constexpr int kHGap = 12;
constexpr int kVPad = 4;

class ByteEditCommand : public QUndoCommand {
public:
    ByteEditCommand(BufferModel *m, qsizetype off, quint8 oldV, quint8 newV)
        : QUndoCommand(QString("edit 0x%1: 0x%2 → 0x%3")
                           .arg(off, 0, 16)
                           .arg(oldV, 2, 16, QChar('0'))
                           .arg(newV, 2, 16, QChar('0'))),
          m_model(m), m_off(off), m_old(oldV), m_new(newV) {}
    void redo() override { if (m_model) m_model->setByteAt(m_off, m_new); }
    void undo() override { if (m_model) m_model->setByteAt(m_off, m_old); }

private:
    QPointer<BufferModel> m_model;
    qsizetype m_off;
    quint8 m_old, m_new;
};

class CopyCommand : public QUndoCommand {
public:
    CopyCommand(BufferModel *m, qsizetype src, qsizetype length, qsizetype dst)
        : QUndoCommand(QString("copy 0x%1..0x%2 → 0x%3")
                           .arg(src, 0, 16)
                           .arg(src + length - 1, 0, 16)
                           .arg(dst, 0, 16)),
          m_model(m), m_dst(dst)
    {
        if (!m) return;
        m_payload.reserve(length);
        m_dstOriginal.reserve(length);
        for (qsizetype i = 0; i < length; ++i) {
            m_payload.push_back(m->byteAt(src + i));     // what we'll write
            m_dstOriginal.push_back(m->byteAt(dst + i)); // for undo
        }
    }
    void redo() override
    {
        if (!m_model) return;
        const qsizetype n = m_payload.size();
        for (qsizetype i = 0; i < n; ++i)
            m_model->setByteAt(m_dst + i, m_payload[i]);
    }
    void undo() override
    {
        if (!m_model) return;
        const qsizetype n = m_dstOriginal.size();
        for (qsizetype i = 0; i < n; ++i)
            m_model->setByteAt(m_dst + i, m_dstOriginal[i]);
    }

private:
    QPointer<BufferModel> m_model;
    qsizetype m_dst;
    QList<quint8> m_payload;
    QList<quint8> m_dstOriginal;
};

class FillCommand : public QUndoCommand {
public:
    FillCommand(BufferModel *m, qsizetype start, qsizetype length,
                const QByteArray &pattern)
        : QUndoCommand(QString("fill 0x%1..0x%2 with %3-byte pattern")
                           .arg(start, 0, 16)
                           .arg(start + length - 1, 0, 16)
                           .arg(pattern.size())),
          m_model(m), m_start(start), m_pattern(pattern)
    {
        if (!m) return;
        m_old.reserve(length);
        for (qsizetype i = 0; i < length; ++i)
            m_old.push_back(m->byteAt(start + i));
    }
    void redo() override
    {
        if (!m_model || m_pattern.isEmpty()) return;
        const qsizetype n = m_old.size();
        const qsizetype p = m_pattern.size();
        for (qsizetype i = 0; i < n; ++i)
            m_model->setByteAt(m_start + i,
                               static_cast<quint8>(m_pattern[i % p]));
    }
    void undo() override
    {
        if (!m_model) return;
        const qsizetype n = m_old.size();
        for (qsizetype i = 0; i < n; ++i)
            m_model->setByteAt(m_start + i, m_old[i]);
    }

private:
    QPointer<BufferModel> m_model;
    qsizetype m_start;
    QByteArray m_pattern;
    QList<quint8> m_old;
};
}  // namespace

HexView::HexView(QWidget *parent) : QAbstractScrollArea(parent)
{
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSize(10);
    setFont(f);
    viewport()->setBackgroundRole(QPalette::Base);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setFocusPolicy(Qt::StrongFocus);
    m_undo.setUndoLimit(2000);
}

void HexView::setModel(BufferModel *model)
{
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model) {
        connect(m_model, &BufferModel::reset, this, &HexView::onModelReset);
        connect(m_model, &BufferModel::dataChanged, this, &HexView::onModelChanged);
    }
    m_undo.clear();
    m_cursorOffset = 0;
    m_nibbleHi = true;
    onModelReset();
}

void HexView::setBytesPerRow(int n)
{
    if (n <= 0 || n == m_bytesPerRow) return;
    m_bytesPerRow = n;
    updateScrollbars();
    viewport()->update();
}

void HexView::gotoOffset(qsizetype offset)
{
    if (!m_model) return;
    moveCursor(qBound<qsizetype>(0, offset, m_model->size() - 1), false);
}

qsizetype HexView::selectionStart() const
{
    if (!hasSelection()) return m_cursorOffset;
    return qMin(m_anchor, m_cursorOffset);
}

qsizetype HexView::selectionLength() const
{
    if (!hasSelection()) return 0;
    // Selection is half-open: [start, end); end = max + 1.
    return qAbs(m_cursorOffset - m_anchor) + 1;
}

void HexView::clearSelection()
{
    if (m_anchor < 0) return;
    m_anchor = -1;
    viewport()->update();
}

void HexView::selectRange(qsizetype start, qsizetype length)
{
    if (!m_model || length <= 0) { clearSelection(); return; }
    const qsizetype bytes = m_model->size();
    start = qBound<qsizetype>(0, start, bytes - 1);
    qsizetype end = qBound<qsizetype>(0, start + length - 1, bytes - 1);
    m_anchor = start;
    m_cursorOffset = end;
    ensureCursorVisible();
    viewport()->update();
}

qsizetype HexView::fillRange(qsizetype start, qsizetype length,
                              const QByteArray &pattern)
{
    if (!m_model || length <= 0 || pattern.isEmpty()) return 0;
    const qsizetype bytes = m_model->size();
    start = qMax<qsizetype>(0, start);
    if (start >= bytes) return 0;
    if (start + length > bytes) length = bytes - start;
    m_undo.push(new FillCommand(m_model, start, length, pattern));
    return length;
}

qsizetype HexView::copyRange(qsizetype src, qsizetype length, qsizetype dst)
{
    if (!m_model || length <= 0 || src < 0 || dst < 0) return 0;
    const qsizetype bytes = m_model->size();
    if (src >= bytes || dst >= bytes) return 0;
    // Clamp by both source and destination ends.
    length = qMin(length, bytes - src);
    length = qMin(length, bytes - dst);
    if (length <= 0) return 0;
    m_undo.push(new CopyCommand(m_model, src, length, dst));
    return length;
}

qsizetype HexView::findBytes(const QByteArray &needle, qsizetype from) const
{
    if (!m_model || needle.isEmpty()) return -1;
    const qsizetype n = m_model->size();
    if (n == 0 || needle.size() > n) return -1;
    auto search = [&](qsizetype begin, qsizetype end) -> qsizetype {
        const qsizetype last = end - needle.size();
        for (qsizetype i = begin; i <= last; ++i) {
            bool match = true;
            for (qsizetype j = 0; j < needle.size(); ++j) {
                if (m_model->byteAt(i + j)
                    != static_cast<quint8>(needle[j])) {
                    match = false;
                    break;
                }
            }
            if (match) return i;
        }
        return -1;
    };
    qsizetype hit = search(qMax<qsizetype>(0, from), n);
    if (hit >= 0) return hit;
    // Wrap-around: search [0, from + needle.size()).
    return search(0, qMin(n, from + needle.size() - 1));
}

int HexView::rowHeight() const { return fontMetrics().height() + kVPad; }
int HexView::addrColumnPx() const { return fontMetrics().horizontalAdvance("00000000") + kHGap; }
int HexView::hexByteWidth() const { return fontMetrics().horizontalAdvance("FF "); }
int HexView::hexColumnLeft() const { return addrColumnPx(); }
int HexView::asciiColumnLeft() const {
    return hexColumnLeft() + hexByteWidth() * m_bytesPerRow + kHGap;
}
int HexView::firstVisibleRow() const { return verticalScrollBar()->value(); }

void HexView::updateScrollbars()
{
    const qsizetype bytes = m_model ? m_model->size() : 0;
    const int rows = static_cast<int>((bytes + m_bytesPerRow - 1) / m_bytesPerRow);
    const int visibleRows = viewport()->height() / rowHeight();
    verticalScrollBar()->setRange(0, std::max(0, rows - visibleRows));
    verticalScrollBar()->setPageStep(std::max(1, visibleRows));
    verticalScrollBar()->setSingleStep(1);
}

void HexView::ensureCursorVisible()
{
    const int rh = rowHeight();
    if (rh <= 0) return;
    const int row = static_cast<int>(m_cursorOffset / m_bytesPerRow);
    const int firstRow = firstVisibleRow();
    const int visibleRows = viewport()->height() / rh;
    if (row < firstRow)
        verticalScrollBar()->setValue(row);
    else if (row >= firstRow + visibleRows)
        verticalScrollBar()->setValue(row - visibleRows + 1);
}

void HexView::moveCursor(qsizetype newOffset, bool keepNibble, bool extendSelection)
{
    if (!m_model || m_model->size() == 0) return;
    const qsizetype bytes = m_model->size();
    const qsizetype prev = m_cursorOffset;
    m_cursorOffset = qBound<qsizetype>(0, newOffset, bytes - 1);
    if (!keepNibble) m_nibbleHi = true;
    if (extendSelection) {
        if (m_anchor < 0) m_anchor = prev;
    } else {
        m_anchor = -1;
    }
    ensureCursorVisible();
    viewport()->update();
}

qsizetype HexView::offsetAtPos(const QPoint &p, Column *col) const
{
    if (!m_model || m_model->size() == 0) return -1;
    const int rh = rowHeight();
    if (rh <= 0) return -1;
    const int row = firstVisibleRow() + p.y() / rh;
    const int hexL = hexColumnLeft();
    const int asciiL = asciiColumnLeft();
    const int byteW = hexByteWidth();
    if (p.x() >= hexL && p.x() < asciiL - kHGap) {
        const int c = (p.x() - hexL) / byteW;
        if (c >= 0 && c < m_bytesPerRow) {
            if (col) *col = Column::Hex;
            return qsizetype(row) * m_bytesPerRow + c;
        }
    } else if (p.x() >= asciiL) {
        const int asciiCharW = fontMetrics().horizontalAdvance("M");
        const int c = (p.x() - asciiL) / asciiCharW;
        if (c >= 0 && c < m_bytesPerRow) {
            if (col) *col = Column::Ascii;
            return qsizetype(row) * m_bytesPerRow + c;
        }
    }
    return -1;
}

void HexView::applyByteEdit(qsizetype offset, quint8 newValue)
{
    if (!m_model) return;
    const quint8 oldValue = m_model->byteAt(offset);
    if (oldValue == newValue) return;
    m_undo.push(new ByteEditCommand(m_model, offset, oldValue, newValue));
}

void HexView::typeHexNibble(quint8 nibble)
{
    if (!m_model || m_model->size() == 0) return;
    const quint8 cur = m_model->byteAt(m_cursorOffset);
    quint8 next;
    if (m_nibbleHi) {
        next = static_cast<quint8>((nibble << 4) | (cur & 0x0F));
        applyByteEdit(m_cursorOffset, next);
        m_nibbleHi = false;
    } else {
        next = static_cast<quint8>((cur & 0xF0) | (nibble & 0x0F));
        applyByteEdit(m_cursorOffset, next);
        m_nibbleHi = true;
        moveCursor(m_cursorOffset + 1, false);
    }
    viewport()->update();
}

void HexView::typeAsciiChar(char c)
{
    if (!m_model || m_model->size() == 0) return;
    applyByteEdit(m_cursorOffset, static_cast<quint8>(c));
    moveCursor(m_cursorOffset + 1, false);
}

void HexView::resizeEvent(QResizeEvent *e)
{
    QAbstractScrollArea::resizeEvent(e);
    updateScrollbars();
}

void HexView::onModelReset()
{
    m_cursorOffset = 0;
    m_nibbleHi = true;
    m_undo.clear();
    updateScrollbars();
    viewport()->update();
}

void HexView::onModelChanged(qsizetype, qsizetype) { viewport()->update(); }

void HexView::focusInEvent(QFocusEvent *e)
{
    QAbstractScrollArea::focusInEvent(e);
    viewport()->update();
}

void HexView::focusOutEvent(QFocusEvent *e)
{
    QAbstractScrollArea::focusOutEvent(e);
    viewport()->update();
}

void HexView::mousePressEvent(QMouseEvent *e)
{
    if (!m_model || m_model->size() == 0 || e->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(e);
        return;
    }
    setFocus();
    Column col = m_cursorColumn;
    const qsizetype off = offsetAtPos(e->position().toPoint(), &col);
    if (off < 0) return;
    m_cursorColumn = col;
    const bool shift = (e->modifiers() & Qt::ShiftModifier);
    moveCursor(off, false, shift);
    m_mouseDown = !shift;  // shift-click sets selection end; further drag extends
    if (!shift) m_anchor = -1;  // plain click clears any selection
    // For a drag operation, treat first press as anchor.
    if (e->button() == Qt::LeftButton && !shift)
        m_anchor = off;
    viewport()->update();
}

void HexView::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_model || m_model->size() == 0 || !(e->buttons() & Qt::LeftButton)) {
        QAbstractScrollArea::mouseMoveEvent(e);
        return;
    }
    Column col = m_cursorColumn;
    qsizetype off = offsetAtPos(e->position().toPoint(), &col);
    if (off < 0) {
        // Clamp to nearest valid offset based on Y position.
        const int rh = rowHeight();
        if (rh > 0) {
            const int y = static_cast<int>(e->position().y());
            int row = firstVisibleRow() + qMax(0, y / rh);
            off = qBound<qsizetype>(0, qsizetype(row) * m_bytesPerRow,
                                    m_model->size() - 1);
        }
    }
    if (off < 0) return;
    if (m_anchor < 0) m_anchor = m_cursorOffset;
    m_cursorColumn = col;
    moveCursor(off, false, /*extendSelection=*/true);
}

void HexView::keyPressEvent(QKeyEvent *e)
{
    if (!m_model || m_model->size() == 0) {
        QAbstractScrollArea::keyPressEvent(e);
        return;
    }
    const qsizetype bytes = m_model->size();
    const int visibleRows = std::max(1, viewport()->height() / rowHeight());

    if (e->matches(QKeySequence::Undo)) { m_undo.undo(); viewport()->update(); return; }
    if (e->matches(QKeySequence::Redo)) { m_undo.redo(); viewport()->update(); return; }

    const bool shift = (e->modifiers() & Qt::ShiftModifier);
    switch (e->key()) {
    case Qt::Key_Left:  moveCursor(m_cursorOffset - 1, false, shift); return;
    case Qt::Key_Right: moveCursor(m_cursorOffset + 1, false, shift); return;
    case Qt::Key_Up:    moveCursor(m_cursorOffset - m_bytesPerRow, false, shift); return;
    case Qt::Key_Down:  moveCursor(m_cursorOffset + m_bytesPerRow, false, shift); return;
    case Qt::Key_PageUp:
        moveCursor(m_cursorOffset - qsizetype(visibleRows) * m_bytesPerRow,
                   false, shift);
        return;
    case Qt::Key_PageDown:
        moveCursor(m_cursorOffset + qsizetype(visibleRows) * m_bytesPerRow,
                   false, shift);
        return;
    case Qt::Key_Home:
        moveCursor((m_cursorOffset / m_bytesPerRow) * m_bytesPerRow,
                   false, shift);
        return;
    case Qt::Key_End: {
        qsizetype eol = (m_cursorOffset / m_bytesPerRow) * m_bytesPerRow
                        + m_bytesPerRow - 1;
        moveCursor(qMin(eol, bytes - 1), false, shift);
        return;
    }
    case Qt::Key_A:
        if (e->modifiers() & Qt::ControlModifier) {
            selectRange(0, m_model->size());
            return;
        }
        break;
    case Qt::Key_Tab:
        m_cursorColumn = (m_cursorColumn == Column::Hex)
                             ? Column::Ascii : Column::Hex;
        m_nibbleHi = true;
        viewport()->update();
        return;
    case Qt::Key_Backspace:
        // Move back and clear nibble state.
        if (m_cursorColumn == Column::Hex && !m_nibbleHi) {
            m_nibbleHi = true;
            viewport()->update();
        } else {
            moveCursor(m_cursorOffset - 1, false);
        }
        return;
    }

    const QString t = e->text();
    if (m_cursorColumn == Column::Hex) {
        if (t.length() == 1) {
            const QChar c = t.at(0);
            int n = -1;
            if (c >= '0' && c <= '9') n = c.unicode() - '0';
            else if (c >= 'a' && c <= 'f') n = c.unicode() - 'a' + 10;
            else if (c >= 'A' && c <= 'F') n = c.unicode() - 'A' + 10;
            if (n >= 0) { typeHexNibble(static_cast<quint8>(n)); return; }
        }
    } else {
        if (t.length() == 1) {
            const QChar c = t.at(0);
            if (c.unicode() >= 0x20 && c.unicode() < 0x7F) {
                typeAsciiChar(static_cast<char>(c.unicode()));
                return;
            }
        }
    }

    QAbstractScrollArea::keyPressEvent(e);
}

void HexView::paintEvent(QPaintEvent *e)
{
    QPainter p(viewport());
    p.fillRect(e->rect(), palette().base());
    if (!m_model || m_model->size() == 0) {
        p.setPen(palette().placeholderText().color());
        p.drawText(viewport()->rect(), Qt::AlignCenter,
                   "(buffer empty — read a chip or open a file)");
        return;
    }

    const int rh = rowHeight();
    const int firstRow = firstVisibleRow();
    const int visibleRows = viewport()->height() / rh + 1;
    const qsizetype bytes = m_model->size();
    const int totalRows = static_cast<int>((bytes + m_bytesPerRow - 1) / m_bytesPerRow);

    const int addrW = addrColumnPx();
    const int byteW = hexByteWidth();
    const int asciiX = asciiColumnLeft();
    const int asciiCharW = fontMetrics().horizontalAdvance("M");

    const QColor cursorBg = palette().highlight().color();
    const QColor cursorFg = palette().highlightedText().color();
    const QColor nibbleBg = cursorBg.darker(140);
    QColor selBg = cursorBg; selBg.setAlpha(100);
    const QColor dirtyFg(220, 50, 47);   // red for unsaved edits

    const bool sel = hasSelection();
    const qsizetype selA = sel ? qMin(m_anchor, m_cursorOffset) : -1;
    const qsizetype selB = sel ? qMax(m_anchor, m_cursorOffset) : -1;

    for (int row = firstRow; row < std::min(totalRows, firstRow + visibleRows); ++row) {
        const int y = (row - firstRow) * rh + kVPad;
        const qsizetype rowStart = qsizetype(row) * m_bytesPerRow;

        // Address.
        p.setPen(palette().placeholderText().color());
        p.drawText(QPoint(0, y + fontMetrics().ascent()),
                   QString("%1").arg(rowStart, 8, 16, QChar('0')).toUpper());

        // Hex column, byte by byte (so we can highlight the cursor cell).
        for (int i = 0; i < m_bytesPerRow; ++i) {
            const qsizetype off = rowStart + i;
            if (off >= bytes) break;
            const quint8 b = m_model->byteAt(off);
            const QRect cellRect(addrW + i * byteW, y, byteW, rh - 2);
            const bool isCursor = (off == m_cursorOffset);
            const bool isDirty = m_model->isDirty(off);
            const bool isSel = sel && off >= selA && off <= selB;
            if (isSel && !isCursor)
                p.fillRect(cellRect.adjusted(0, 1, -2, -1), selBg);
            if (isCursor && m_cursorColumn == Column::Hex && hasFocus()) {
                // Highlight the cursor cell and the active nibble within it.
                p.fillRect(cellRect.adjusted(0, 1, -2, -1), cursorBg);
                if (!m_nibbleHi) {
                    QRect lo = cellRect.adjusted(byteW / 2 - 2, 1, -2, -1);
                    p.fillRect(lo, nibbleBg);
                }
                p.setPen(cursorFg);
            } else if (isCursor) {
                p.setPen(isDirty ? dirtyFg : palette().windowText().color());
                p.drawRect(cellRect.adjusted(0, 1, -2, -2));
            } else {
                p.setPen(isDirty ? dirtyFg : palette().windowText().color());
            }
            const QString hex = QString("%1").arg(b, 2, 16, QChar('0')).toUpper();
            p.drawText(QPoint(cellRect.left(), y + fontMetrics().ascent()), hex);
        }

        // ASCII column.
        for (int i = 0; i < m_bytesPerRow; ++i) {
            const qsizetype off = rowStart + i;
            if (off >= bytes) break;
            const quint8 b = m_model->byteAt(off);
            const QRect cellRect(asciiX + i * asciiCharW, y, asciiCharW, rh - 2);
            const bool isCursor = (off == m_cursorOffset);
            const bool isDirty = m_model->isDirty(off);
            const bool isSel = sel && off >= selA && off <= selB;
            if (isSel && !isCursor)
                p.fillRect(cellRect.adjusted(0, 1, 0, -1), selBg);
            if (isCursor && m_cursorColumn == Column::Ascii && hasFocus()) {
                p.fillRect(cellRect.adjusted(0, 1, 0, -1), cursorBg);
                p.setPen(cursorFg);
            } else if (isCursor) {
                p.setPen(isDirty ? dirtyFg : palette().windowText().color());
                p.drawRect(cellRect.adjusted(0, 1, -1, -2));
            } else if (isDirty) {
                p.setPen(dirtyFg);
            } else {
                p.setPen(palette().placeholderText().color());
            }
            const QChar ch = (b >= 0x20 && b < 0x7F) ? QChar(b) : QChar('.');
            p.drawText(QPoint(cellRect.left(), y + fontMetrics().ascent()),
                       QString(ch));
        }
    }
}
