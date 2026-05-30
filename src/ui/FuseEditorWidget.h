// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QWidget>

#include "core/Programmer.h"  // FuseSet, FuseItem

class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

// Generic, minipro-metadata-driven fuse / lock editor. One hex field per
// declared fuse item (mask-aware), defaults shown alongside. Config fuses and
// lock bits live in separate groups with separate write buttons, because lock
// bits can only be loosened by a full chip erase.
class FuseEditorWidget : public QWidget {
    Q_OBJECT
public:
    explicit FuseEditorWidget(QWidget *parent = nullptr);

    // Rebuild the rows for a newly-opened chip's declared layout. Values are
    // seeded to defaults until applyRead() fills them from the chip.
    void bindLayout(const FuseSet &layout);
    // Fill the fields from a chip read (or report a read error).
    void applyRead(const FuseSet &values);
    void setBusy(bool busy);
    bool hasFuses() const { return m_layout.valid; }

signals:
    void readRequested();
    // `locks` distinguishes the two write buttons so the caller can warn
    // appropriately. `subset` carries only the relevant section's items.
    void writeRequested(const FuseSet &subset, bool locks);

private:
    void rebuild();
    FuseSet buildSubset(bool locks) const;
    void resetToDefaults();

    FuseSet m_layout;
    struct Row {
        QLineEdit *edit = nullptr;
        int itemIndex = -1;  // index into m_layout.items
    };
    QList<Row> m_rows;

    QVBoxLayout *m_rootLayout = nullptr;
    QGroupBox *m_fuseGroup = nullptr;
    QGroupBox *m_lockGroup = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_readBtn = nullptr;
    QPushButton *m_writeFusesBtn = nullptr;
    QPushButton *m_writeLocksBtn = nullptr;
    QPushButton *m_defaultsBtn = nullptr;
};
