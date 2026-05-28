// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChipDatabase.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

QString ChipEntry::displayName() const
{
    return package.isEmpty() ? name : QString("%1 @%2").arg(name, package);
}

namespace {
QString makeKey(const QString &m, const QString &n, const QString &p)
{
    return m.toUpper() + '|' + n.toUpper() + '|' + p.toUpper();
}

quint32 hexToU32(const QJsonValue &v)
{
    const QString s = v.toString();
    if (s.isEmpty())
        return 0;
    bool ok = false;
    const quint32 n = s.startsWith("0x", Qt::CaseInsensitive)
                          ? s.mid(2).toUInt(&ok, 16)
                          : s.toUInt(&ok, 16);
    return ok ? n : 0;
}
}  // namespace

ChipDatabase::ChipDatabase(QObject *parent) : QObject(parent) {}

bool ChipDatabase::load(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QString("cannot open %1: %2").arg(path, f.errorString());
        return false;
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull()) {
        if (error)
            *error = QString("invalid JSON: %1").arg(pe.errorString());
        return false;
    }

    const QJsonArray arr = doc.object().value("chips").toArray();
    m_chips.clear();
    m_chips.reserve(arr.size());
    m_byManufacturer.clear();
    m_byKey.clear();
    m_supportedCount = 0;

    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        ChipEntry e;
        e.manufacturer = o.value("manufacturer").toString();
        e.name = o.value("name").toString();
        e.package = o.value("package").toString();
        e.supported = o.value("supported").toBool();
        if (e.supported) {
            e.miniproName = o.value("minipro_name").toString();
            e.miniproDb = o.value("minipro_db").toString();
            e.protocolId = o.value("protocol_id").toString();
            e.type = o.value("type").toInt();
            e.pinCount = o.value("pin_count").toInt();
            e.pinMap = hexToU32(o.value("pin_map"));
            e.packageDetails = hexToU32(o.value("package_details"));
            e.adapter = o.value("adapter").toInt();
            e.icsp = o.value("icsp").toInt();
            ++m_supportedCount;
        }
        const int idx = m_chips.size();
        m_chips.push_back(std::move(e));
        const auto &ref = m_chips.back();
        m_byManufacturer[ref.manufacturer.toUpper()].append(idx);
        m_byKey.insert(makeKey(ref.manufacturer, ref.name, ref.package), idx);
    }
    return true;
}

QStringList ChipDatabase::manufacturers() const
{
    QSet<QString> seen;
    QStringList out;
    out.reserve(m_byManufacturer.size());
    for (const ChipEntry &e : m_chips) {
        if (!seen.contains(e.manufacturer)) {
            seen.insert(e.manufacturer);
            out.append(e.manufacturer);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const QString &a, const QString &b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });
    return out;
}

QVector<const ChipEntry *> ChipDatabase::chipsFor(const QString &manufacturer) const
{
    QVector<const ChipEntry *> out;
    const auto it = m_byManufacturer.constFind(manufacturer.toUpper());
    if (it == m_byManufacturer.constEnd())
        return out;
    out.reserve(it->size());
    for (int idx : *it)
        out.append(&m_chips.at(idx));
    return out;
}

const ChipEntry *ChipDatabase::find(const QString &manuf, const QString &name,
                                    const QString &package) const
{
    const auto it = m_byKey.constFind(makeKey(manuf, name, package));
    if (it == m_byKey.constEnd())
        return nullptr;
    return &m_chips.at(*it);
}
