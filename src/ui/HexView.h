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

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
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
    void moveCursor(qsizetype newOffset, bool keepNibble);
    void typeHexNibble(quint8 nibble);
    void typeAsciiChar(char c);
    void applyByteEdit(qsizetype offset, quint8 newValue);

    QPointer<BufferModel> m_model;
    QUndoStack m_undo;
    int m_bytesPerRow = 16;
    qsizetype m_cursorOffset = 0;
    Column m_cursorColumn = Column::Hex;
    bool m_nibbleHi = true;   // true = next typed hex digit is HIGH nibble
};
