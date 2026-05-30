// SPDX-License-Identifier: GPL-3.0-or-later
#include "FuseEditorWidget.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

namespace {
// Display width follows the fuse's mask, not word_size: a byte-wide fuse
// (mask <= 0xFF) shows as two hex digits (e.g. 0x62) even on a word_size==2
// part, matching how avrdude and fuse calculators present AVR fuses.
int maskDigits(quint16 mask) { return (mask > 0xFF) ? 4 : 2; }

QString fmtValue(quint16 v, quint16 mask)
{
    return QStringLiteral("0x") +
           QString::number(v, 16).rightJustified(maskDigits(mask), '0').toUpper();
}

// Parse "0x62" / "62" / "0X62" → value; returns false if unparseable.
bool parseValue(const QString &text, quint16 *out)
{
    QString s = text.trimmed();
    if (s.startsWith("0x", Qt::CaseInsensitive))
        s = s.mid(2);
    if (s.isEmpty())
        return false;
    bool ok = false;
    const uint v = s.toUInt(&ok, 16);
    if (!ok || v > 0xFFFF)
        return false;
    *out = static_cast<quint16>(v);
    return true;
}
}  // namespace

FuseEditorWidget::FuseEditorWidget(QWidget *parent) : QWidget(parent)
{
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(12, 12, 12, 12);
    m_rootLayout->setSpacing(8);

    auto *intro = new QLabel(
        "Fuses are reprogrammable configuration bytes — not one-time burns. "
        "Lock bits restrict access and can only be cleared by a full chip "
        "erase. Bits outside a fuse's mask always read as 1.", this);
    intro->setWordWrap(true);
    m_rootLayout->addWidget(intro);

    m_fuseGroup = new QGroupBox("Configuration fuses", this);
    new QFormLayout(m_fuseGroup);
    m_rootLayout->addWidget(m_fuseGroup);

    m_lockGroup = new QGroupBox("Lock bits", this);
    new QFormLayout(m_lockGroup);
    m_rootLayout->addWidget(m_lockGroup);

    auto *btnRow = new QHBoxLayout;
    m_readBtn = new QPushButton("Read from chip", this);
    m_defaultsBtn = new QPushButton("Reset to defaults", this);
    m_writeFusesBtn = new QPushButton("Write fuses", this);
    m_writeLocksBtn = new QPushButton("Write lock bits", this);
    btnRow->addWidget(m_readBtn);
    btnRow->addWidget(m_defaultsBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(m_writeFusesBtn);
    btnRow->addWidget(m_writeLocksBtn);
    m_rootLayout->addLayout(btnRow);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_rootLayout->addWidget(m_status);
    m_rootLayout->addStretch(1);

    connect(m_readBtn, &QPushButton::clicked, this,
            [this] { emit readRequested(); });
    connect(m_defaultsBtn, &QPushButton::clicked, this,
            &FuseEditorWidget::resetToDefaults);
    connect(m_writeFusesBtn, &QPushButton::clicked, this,
            [this] { emit writeRequested(buildSubset(false), false); });
    connect(m_writeLocksBtn, &QPushButton::clicked, this,
            [this] { emit writeRequested(buildSubset(true), true); });

    bindLayout(FuseSet{});
}

void FuseEditorWidget::bindLayout(const FuseSet &layout)
{
    m_layout = layout;
    rebuild();
}

void FuseEditorWidget::rebuild()
{
    // Tear down old rows from both form layouts.
    auto clearForm = [](QGroupBox *box) {
        auto *form = qobject_cast<QFormLayout *>(box->layout());
        while (form && form->rowCount() > 0)
            form->removeRow(0);
    };
    clearForm(m_fuseGroup);
    clearForm(m_lockGroup);
    m_rows.clear();

    auto *fuseForm = qobject_cast<QFormLayout *>(m_fuseGroup->layout());
    auto *lockForm = qobject_cast<QFormLayout *>(m_lockGroup->layout());
    // Allow an optional 0x prefix plus up to four hex chars (16-bit max).
    auto *validator = new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("(0[xX])?[0-9a-fA-F]{0,4}")), this);

    int numLocks = 0;
    for (int i = 0; i < m_layout.items.size(); ++i) {
        const FuseItem &it = m_layout.items.at(i);
        auto *edit = new QLineEdit(this);
        edit->setValidator(validator);
        edit->setText(fmtValue(it.value, it.mask));
        edit->setFixedWidth(90);
        const QString label =
            QString("%1  (default %2, mask %3)")
                .arg(it.name, fmtValue(it.def, it.mask),
                     fmtValue(it.mask, it.mask));
        if (it.isLock) {
            lockForm->addRow(label, edit);
            ++numLocks;
        } else {
            fuseForm->addRow(label, edit);
        }
        m_rows.append({edit, i});
    }

    const bool haveFuses = m_layout.valid;
    m_fuseGroup->setVisible(haveFuses);
    m_lockGroup->setVisible(haveFuses && numLocks > 0);
    m_readBtn->setEnabled(haveFuses);
    m_defaultsBtn->setEnabled(haveFuses);
    m_writeFusesBtn->setEnabled(haveFuses);
    m_writeLocksBtn->setEnabled(haveFuses && numLocks > 0);
    if (haveFuses && !m_layout.lockReadable && numLocks > 0)
        m_status->setText("Note: this chip's lock bits are write-only — "
                          "they can be set but not read back.");
    else
        m_status->clear();
}

void FuseEditorWidget::applyRead(const FuseSet &values)
{
    if (!values.errorMessage.isEmpty()) {
        m_status->setText("Read failed: " + values.errorMessage);
        return;
    }
    // Match by item index/order — the layout and the read share fuseLayout().
    for (const Row &row : m_rows) {
        if (row.itemIndex < 0 || row.itemIndex >= values.items.size())
            continue;
        const FuseItem &v = values.items.at(row.itemIndex);
        row.edit->setText(fmtValue(v.value, v.mask));
    }
    m_layout = values;
    m_status->setText("Fuses read from chip.");
}

void FuseEditorWidget::resetToDefaults()
{
    for (const Row &row : m_rows) {
        if (row.itemIndex < 0 || row.itemIndex >= m_layout.items.size())
            continue;
        const FuseItem &it = m_layout.items.at(row.itemIndex);
        row.edit->setText(fmtValue(it.def, it.mask));
    }
    m_status->setText("Fields reset to manufacturer defaults (not yet written).");
}

FuseSet FuseEditorWidget::buildSubset(bool locks) const
{
    FuseSet set;
    set.valid = true;
    set.wordSize = m_layout.wordSize;
    set.lockReadable = m_layout.lockReadable;
    for (const Row &row : m_rows) {
        if (row.itemIndex < 0 || row.itemIndex >= m_layout.items.size())
            continue;
        FuseItem it = m_layout.items.at(row.itemIndex);
        if (it.isLock != locks)
            continue;
        quint16 parsed = 0;
        if (parseValue(row.edit->text(), &parsed))
            it.value = parsed;
        set.items.append(it);
    }
    if (set.items.isEmpty())
        set.valid = false;
    return set;
}

void FuseEditorWidget::setBusy(bool busy)
{
    const bool en = !busy && m_layout.valid;
    m_readBtn->setEnabled(en);
    m_defaultsBtn->setEnabled(en);
    m_writeFusesBtn->setEnabled(en);
    bool haveLocks = false;
    for (const FuseItem &it : m_layout.items)
        if (it.isLock) haveLocks = true;
    m_writeLocksBtn->setEnabled(en && haveLocks);
}
