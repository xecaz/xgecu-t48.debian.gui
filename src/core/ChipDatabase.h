// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

struct ChipEntry {
    QString manufacturer;
    QString name;
    QString package;
    bool supported = false;

    // Populated only when supported.
    QString miniproName;      // raw "name" attribute from infoic.xml
    QString miniproDb;        // "INFOIC2PLUS", etc.
    QString protocolId;
    int type = 0;             // 1=EEPROM, 2=MCU, 3=PLD, etc.
    int pinCount = 0;
    quint32 pinMap = 0;
    quint32 packageDetails = 0;
    int adapter = 0;          // 0 = ZIF socket directly; non-zero = adapter required
    int icsp = 0;             // ICSP flags

    QString displayName() const;
};

class ChipDatabase : public QObject {
    Q_OBJECT
public:
    explicit ChipDatabase(QObject *parent = nullptr);

    // Load from a Qt resource path (e.g. ":/chips_merged.json") or filesystem path.
    bool load(const QString &path, QString *error = nullptr);

    const QVector<ChipEntry> &chips() const { return m_chips; }
    int supportedCount() const { return m_supportedCount; }

    // Manufacturer list, alphabetically sorted, unique.
    QStringList manufacturers() const;

    // All chips for a manufacturer.
    QVector<const ChipEntry *> chipsFor(const QString &manufacturer) const;

    // O(1) lookup by (manufacturer, name, package), case-insensitive.
    const ChipEntry *find(const QString &manuf, const QString &name,
                          const QString &package) const;

private:
    QVector<ChipEntry> m_chips;
    QHash<QString, QVector<int>> m_byManufacturer;  // upper(manuf) -> indices
    QHash<QString, int> m_byKey;                    // upper(manuf|name|pkg) -> index
    int m_supportedCount = 0;
};
