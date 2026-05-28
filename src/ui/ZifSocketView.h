// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

#include "core/ChipDatabase.h"

class ZifSocketView : public QWidget {
    Q_OBJECT
public:
    explicit ZifSocketView(QWidget *parent = nullptr);

    void setChip(const ChipEntry *chip);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    const ChipEntry *m_chip = nullptr;

    static constexpr int kZifPins = 48;
    static constexpr int kPerSide = kZifPins / 2;
};
