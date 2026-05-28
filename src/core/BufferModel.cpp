// SPDX-License-Identifier: GPL-3.0-or-later
#include "BufferModel.h"

BufferModel::BufferModel(QObject *parent) : QObject(parent) {}

void BufferModel::setData(const QByteArray &data)
{
    m_data = data;
    const bool had = !m_originals.isEmpty();
    m_originals.clear();
    emit reset();
    if (had) emit dirtyChanged(false);
}

void BufferModel::resize(qsizetype size, char fill)
{
    if (size == m_data.size())
        return;
    if (size < m_data.size()) {
        m_data.truncate(size);
    } else {
        m_data.append(QByteArray(size - m_data.size(), fill));
    }
    const bool had = !m_originals.isEmpty();
    m_originals.clear();
    emit reset();
    if (had) emit dirtyChanged(false);
}

void BufferModel::clear()
{
    m_data.clear();
    const bool had = !m_originals.isEmpty();
    m_originals.clear();
    emit reset();
    if (had) emit dirtyChanged(false);
}

void BufferModel::markClean()
{
    if (m_originals.isEmpty()) return;
    QList<qsizetype> offsets = m_originals.keys();
    m_originals.clear();
    for (qsizetype off : offsets) emit dataChanged(off, 1);
    emit dirtyChanged(false);
}

quint8 BufferModel::byteAt(qsizetype offset) const
{
    if (offset < 0 || offset >= m_data.size())
        return 0xFF;
    return static_cast<quint8>(m_data[offset]);
}

void BufferModel::setByteAt(qsizetype offset, quint8 value)
{
    if (offset < 0 || offset >= m_data.size())
        return;
    const quint8 cur = static_cast<quint8>(m_data[offset]);
    if (cur == value) return;

    const bool wasDirtyBefore = !m_originals.isEmpty();

    // Record original on first edit at this offset.
    if (!m_originals.contains(offset))
        m_originals.insert(offset, cur);
    m_data[offset] = static_cast<char>(value);
    // If we've returned to the original value, this offset is no longer dirty.
    if (m_originals.value(offset) == value)
        m_originals.remove(offset);

    emit dataChanged(offset, 1);
    const bool isDirtyNow = !m_originals.isEmpty();
    if (wasDirtyBefore != isDirtyNow)
        emit dirtyChanged(isDirtyNow);
}
