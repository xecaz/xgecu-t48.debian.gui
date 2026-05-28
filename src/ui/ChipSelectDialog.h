// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class ChipDatabase;
struct ChipEntry;
class QLineEdit;
class QTreeView;
class QStandardItemModel;
class QSortFilterProxyModel;
class QCheckBox;
class QLabel;

class ChipSelectDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChipSelectDialog(ChipDatabase *db, QWidget *parent = nullptr);

    const ChipEntry *selectedChip() const { return m_selected; }

private slots:
    void onCurrentChanged();
    void onAccept();

private:
    void populate();

    ChipDatabase *m_db;
    QStandardItemModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QTreeView *m_tree;
    QLineEdit *m_search;
    QCheckBox *m_showUnsupported;
    QLabel *m_info;
    const ChipEntry *m_selected = nullptr;
};
