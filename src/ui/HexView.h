// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractScrollArea>
#include <QPointer>
#include <QUndoStack>

class BufferModel;

class HexView : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit HexView(QWidget *parent = nullptr);

    void setModel(BufferModel *model);
    QUndoStack *undoStack() { return &m_undo; }

    int bytesPerRow() const { return m_bytesPerRow; }
    void setBytesPerRow(int n);

    qsizetype cursorOffset() const { return m_cursorOffset; }
    void gotoOffset(qsizetype offset);

    bool hasSelection() const { return m_anchor >= 0 && m_anchor != m_cursorOffset; }
    qsizetype selectionStart() const;
    qsizetype selectionLength() const;
    void clearSelection();
    void selectRange(qsizetype start, qsizetype length);

    // Fill [start, start+length) with `pattern`, repeating it as needed.
    // Wrapped in one undo step. Returns the number of bytes actually written.
    qsizetype fillRange(qsizetype start, qsizetype length, const QByteArray &pattern);

    // Copy `length` bytes from `src` into `dst`. Single undoable step.
    // Source is snapshotted up-front, so src/dst ranges may overlap freely.
    // Returns the number of bytes actually written (clamped to buffer).
    qsizetype copyRange(qsizetype src, qsizetype length, qsizetype dst);

    // Search for `needle` starting at `from` (inclusive). Returns -1 if not
    // found. Wraps around to the start of the buffer if not found tailward.
    qsizetype findBytes(const QByteArray &needle, qsizetype from) const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private slots:
    void onModelReset();
    void onModelChanged(qsizetype offset, qsizetype length);

private:
    enum class Column { Hex, Ascii };

    void updateScrollbars();
    int rowHeight() const;
    int addrColumnPx() const;
    int hexByteWidth() const;
    int hexColumnLeft() const;
    int asciiColumnLeft() const;
    int firstVisibleRow() const;
    void ensureCursorVisible();
    void moveCursor(qsizetype newOffset, bool keepNibble, bool extendSelection = false);
    qsizetype offsetAtPos(const QPoint &p, Column *col) const;
    void typeHexNibble(quint8 nibble);
    void typeAsciiChar(char c);
    void applyByteEdit(qsizetype offset, quint8 newValue);

    QPointer<BufferModel> m_model;
    QUndoStack m_undo;
    int m_bytesPerRow = 16;
    qsizetype m_cursorOffset = 0;
    qsizetype m_anchor = -1;  // selection anchor; -1 = no selection in progress
    Column m_cursorColumn = Column::Hex;
    bool m_nibbleHi = true;   // true = next typed hex digit is HIGH nibble
    bool m_mouseDown = false;
};
