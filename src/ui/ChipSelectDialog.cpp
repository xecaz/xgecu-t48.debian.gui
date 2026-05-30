// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChipSelectDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

#include "core/ChipDatabase.h"
#include "ThemeManager.h"

namespace {
constexpr int kRoleEntry = Qt::UserRole + 1;

class ChipFilterProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setShowUnsupported(bool v) { m_showUnsupported = v; invalidateFilter(); }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &parent) const override
    {
        const QModelIndex idx = sourceModel()->index(sourceRow, 0, parent);
        // Manufacturer rows: accept if any child accepts.
        if (sourceModel()->hasChildren(idx)) {
            for (int i = 0; i < sourceModel()->rowCount(idx); ++i)
                if (filterAcceptsRow(i, idx))
                    return true;
            return false;
        }
        // Leaf rows: check name match + supported flag.
        const auto *entry = idx.data(kRoleEntry).value<const ChipEntry *>();
        if (!entry)
            return false;
        if (!m_showUnsupported && !entry->supported)
            return false;
        const QRegularExpression &re = filterRegularExpression();
        if (re.pattern().isEmpty())
            return true;
        return re.match(entry->name).hasMatch()
            || re.match(entry->package).hasMatch()
            || re.match(entry->manufacturer).hasMatch();
    }

private:
    bool m_showUnsupported = true;
};
}  // namespace

Q_DECLARE_METATYPE(const ChipEntry *)

ChipSelectDialog::ChipSelectDialog(ChipDatabase *db, QWidget *parent)
    : QDialog(parent), m_db(db)
{
    setWindowTitle("Select chip");
    resize(720, 600);

    auto *layout = new QVBoxLayout(this);

    auto *searchRow = new QHBoxLayout;
    searchRow->addWidget(new QLabel("Search:", this));
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("name, manufacturer, or package…");
    searchRow->addWidget(m_search, 1);
    m_showUnsupported = new QCheckBox("Show Windows-only (unsupported here)", this);
    m_showUnsupported->setChecked(true);
    searchRow->addWidget(m_showUnsupported);
    layout->addLayout(searchRow);

    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({"Chip", "Package", "Pins", "Supported"});

    m_proxy = new ChipFilterProxy(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setRecursiveFilteringEnabled(true);

    m_tree = new QTreeView(this);
    m_tree->setModel(m_proxy);
    m_tree->setUniformRowHeights(true);
    m_tree->setSortingEnabled(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(m_tree, 1);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    layout->addWidget(m_info);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(m_search, &QLineEdit::textChanged, m_proxy,
            [this](const QString &t) {
                m_proxy->setFilterRegularExpression(
                    QRegularExpression::escape(t));
            });
    connect(m_showUnsupported, &QCheckBox::toggled, this,
            [this](bool v) {
                static_cast<ChipFilterProxy *>(m_proxy)->setShowUnsupported(v);
            });
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &ChipSelectDialog::onCurrentChanged);
    connect(buttons, &QDialogButtonBox::accepted, this, &ChipSelectDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    populate();
}

void ChipSelectDialog::populate()
{
    const QStringList manufs = m_db->manufacturers();
    QColor unsupportedFg = ThemeManager::instance().theme().unsupportedText;
    for (const QString &m : manufs) {
        auto *manufItem = new QStandardItem(m);
        manufItem->setEditable(false);
        manufItem->setSelectable(false);
        const auto chips = m_db->chipsFor(m);
        int supportedHere = 0;
        for (const ChipEntry *c : chips) {
            auto *name = new QStandardItem(c->name);
            auto *pkg = new QStandardItem(c->package);
            auto *pins = new QStandardItem(
                c->pinCount > 0 ? QString::number(c->pinCount) : QString());
            auto *sup = new QStandardItem(c->supported ? "yes" : "Windows only");
            for (auto *it : {name, pkg, pins, sup}) {
                it->setEditable(false);
                if (!c->supported)
                    it->setForeground(unsupportedFg);
            }
            name->setData(QVariant::fromValue(c), kRoleEntry);
            manufItem->appendRow({name, pkg, pins, sup});
            if (c->supported)
                ++supportedHere;
        }
        manufItem->setText(QString("%1  (%2 / %3)")
                               .arg(m).arg(supportedHere).arg(chips.size()));
        m_model->appendRow(manufItem);
    }
}

void ChipSelectDialog::onCurrentChanged()
{
    const QModelIndex i = m_proxy->mapToSource(m_tree->currentIndex());
    const auto *entry = i.data(kRoleEntry).value<const ChipEntry *>();
    m_selected = entry;
    if (!entry) {
        m_info->clear();
        return;
    }
    const QString tag = entry->supported ? "Supported" : "Not supported (Windows-only)";
    m_info->setText(QString("%1 / %2  @%3  —  %4 pins, %5")
                        .arg(entry->manufacturer, entry->name, entry->package)
                        .arg(entry->pinCount).arg(tag));
}

void ChipSelectDialog::onAccept()
{
    if (m_selected)
        accept();
}
