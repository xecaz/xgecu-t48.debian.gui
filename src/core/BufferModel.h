// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>

class BufferModel : public QObject {
    Q_OBJECT
public:
    explicit BufferModel(QObject *parent = nullptr);

    qsizetype size() const { return m_data.size(); }
    const QByteArray &data() const { return m_data; }

    void setData(const QByteArray &data);
    void resize(qsizetype size, char fill = char(0xFF));
    void clear();

    quint8 byteAt(qsizetype offset) const;
    void setByteAt(qsizetype offset, quint8 value);

    // Dirty tracking: a byte is dirty if it differs from its value at the
    // last setData()/markClean() call. Undoing back to the original value
    // automatically clears the dirty flag.
    bool isDirty(qsizetype offset) const { return m_originals.contains(offset); }
    bool hasDirtyBytes() const { return !m_originals.isEmpty(); }
    void markClean();

signals:
    void dataChanged(qsizetype offset, qsizetype length);
    void reset();
    void dirtyChanged(bool hasDirty);

private:
    QByteArray m_data;
    QHash<qsizetype, quint8> m_originals;  // dirty offset -> original byte
};
