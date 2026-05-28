// SPDX-License-Identifier: GPL-3.0-or-later
#include "ZifSocketView.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>

ZifSocketView::ZifSocketView(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(280, 700);
    setAutoFillBackground(true);
}

void ZifSocketView::setChip(const ChipEntry *chip)
{
    m_chip = chip;
    update();
}

QSize ZifSocketView::sizeHint() const { return {320, 780}; }
QSize ZifSocketView::minimumSizeHint() const { return {260, 660}; }

void ZifSocketView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect r = rect().adjusted(8, 8, -8, -8);

    // Header line.
    const QString title = m_chip
        ? QString("%1  %2  (%3 pins)").arg(m_chip->name, m_chip->package)
                                       .arg(m_chip->pinCount)
        : QString("(no chip selected)");
    p.setPen(palette().windowText().color());
    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    const int headerH = QFontMetrics(f).height() + 6;
    p.drawText(QRect(r.left(), r.top(), r.width(), headerH),
               Qt::AlignHCenter | Qt::AlignTop, title);
    f.setBold(false);
    p.setFont(f);

    // Socket geometry — reserve ~120 px below for the lever drawing.
    const QRect socketArea = r.adjusted(0, headerH + 4, 0, -120);
    const int gap = std::max(8, socketArea.width() / 8);
    const int pinW = std::max(18, (socketArea.width() - gap) / 8);
    const int pinH = std::max(10, socketArea.height() / (kPerSide + 2));
    const int totalH = pinH * kPerSide;
    const int yStart = socketArea.top() + (socketArea.height() - totalH) / 2;

    const int leftColX = socketArea.left() + socketArea.width() / 2 - gap / 2 - pinW;
    const int rightColX = socketArea.left() + socketArea.width() / 2 + gap / 2;

    QFont pf = p.font();
    pf.setPointSizeF(std::max(7.0, pf.pointSizeF() - 1.0));

    // Outer socket body.
    const int bodyMargin = 12;
    QRect socketBody(leftColX - bodyMargin,
                     yStart - bodyMargin,
                     (rightColX + pinW) - (leftColX) + 2 * bodyMargin,
                     totalH + 2 * bodyMargin);
    QColor socketColor(60, 90, 120);
    p.setBrush(socketColor);
    p.setPen(QPen(socketColor.darker(150), 1.0));
    p.drawRoundedRect(socketBody, 8, 8);

    // "IC↑" indicator on the LEFT side, near the TOP — matches the silkscreen
    // arrow on the physical T48 socket.
    {
        const int ax = socketBody.left() - 18;
        const int ay = socketBody.top() + 8;
        QFont af = p.font();
        af.setBold(true);
        af.setPointSizeF(std::max(8.0, af.pointSizeF()));
        p.setFont(af);
        p.setPen(QColor(220, 220, 230));
        // arrow ↑
        QPainterPath arrow;
        arrow.moveTo(ax, ay + 14);
        arrow.lineTo(ax, ay + 4);
        arrow.moveTo(ax - 4, ay + 8);
        arrow.lineTo(ax, ay + 4);
        arrow.lineTo(ax + 4, ay + 8);
        p.strokePath(arrow, QPen(QColor(220, 220, 230), 1.6));
        p.drawText(QPoint(ax - 8, ay + 28), "IC");
        p.setFont(pf);
    }

    // ZIF lever — thin stem exits to the RIGHT of the pin column and runs
    // straight DOWN into a pill-shaped thumb knob with a tiny ball at the
    // tip (matches the real T48 actuator).
    {
        // Just outside the right pin column.
        const QPoint exitPt(rightColX + pinW + 6,
                            socketBody.bottom() - 2);
        const int stemLen = 60;
        const QPoint stemEnd(exitPt.x(), exitPt.y() + stemLen);

        // Stem (vertical rod).
        p.save();
        p.setPen(QPen(QColor(40, 40, 50), 8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(exitPt, stemEnd);
        p.restore();

        // Round ball at the end of the stem (thumb grip).
        const int ballR = 12;
        QPoint ballC(stemEnd.x(), stemEnd.y() + ballR);
        // Subtle radial highlight for depth.
        QRadialGradient rg(ballC.x() - ballR / 3.0, ballC.y() - ballR / 3.0,
                           ballR * 1.6);
        rg.setColorAt(0.0, QColor(90, 90, 100));
        rg.setColorAt(1.0, QColor(20, 20, 25));
        p.setBrush(rg);
        p.setPen(QPen(QColor(10, 10, 15), 1.0));
        p.drawEllipse(ballC, ballR, ballR);

        p.setPen(palette().placeholderText().color());
        p.drawText(QPoint(socketBody.left(), ballC.y() + ballR + 14),
                   "(ZIF lever — opposite end from pin 1)");
    }

    // Compute placement: TL866/T48 convention is TOP-justified — chip pin 1
    // aligns with ZIF pin 1 (top-left, next to the IC↑ arrow on the case).
    // For an N-pin DIP, chip occupies ZIF positions
    //   left  column: 1 .. N/2
    //   right column: (kZif - N/2 + 1) .. kZif  (top half of right column)
    int chipPins = m_chip ? m_chip->pinCount : 0;
    if (chipPins < 4 || chipPins > kZifPins || chipPins % 2 != 0)
        chipPins = 0;

    const int chipSide = chipPins / 2;
    const int firstUsedLeft = chipPins ? 1 : 0;
    const int lastUsedLeft = chipPins ? chipSide : 0;
    const int firstUsedRight = chipPins ? (kZifPins - chipSide + 1) : 0;

    // Pin numbering helper. Right column is numbered bottom-to-top on the
    // ZIF (pin 25 at the bottom, pin 48 at the top), so we draw them so the
    // higher pin numbers appear higher up.
    auto zifLeftRect = [&](int idx /*1..24*/) {
        return QRect(leftColX, yStart + (idx - 1) * pinH, pinW, pinH - 2);
    };
    auto zifRightRect = [&](int idx /*25..48*/) {
        const int row = (kZifPins - idx + 1);  // 48->1, 25->24
        return QRect(rightColX, yStart + (row - 1) * pinH, pinW, pinH - 2);
    };

    p.setFont(pf);

    QColor pinIdle(180, 180, 180);
    QColor pinActive(60, 200, 120);

    for (int i = 1; i <= kPerSide; ++i) {
        const QRect L = zifLeftRect(i);
        const bool activeL = chipPins && i >= firstUsedLeft && i <= lastUsedLeft;
        p.setBrush(activeL ? pinActive : pinIdle);
        p.setPen(Qt::black);
        p.drawRect(L);
        p.drawText(L, Qt::AlignCenter, QString::number(i));
    }
    for (int i = kPerSide + 1; i <= kZifPins; ++i) {
        const QRect R = zifRightRect(i);
        const bool activeR = chipPins && i >= firstUsedRight && i <= kZifPins;
        p.setBrush(activeR ? pinActive : pinIdle);
        p.setPen(Qt::black);
        p.drawRect(R);
        p.drawText(R, Qt::AlignCenter, QString::number(i));
    }

    // Chip body overlay.
    if (chipPins) {
        const int chipTop = yStart + (firstUsedLeft - 1) * pinH - 2;
        const int chipBot = yStart + lastUsedLeft * pinH;
        QRect chipBody(leftColX + pinW + 2,
                       chipTop,
                       rightColX - (leftColX + pinW) - 4,
                       chipBot - chipTop);
        QColor body(35, 35, 45, 230);
        p.setBrush(body);
        p.setPen(QPen(body.lighter(180), 1.0));
        p.drawRoundedRect(chipBody, 4, 4);

        // TL866/T48 convention: notch faces AWAY from the lever (up here),
        // pin 1 sits at the TOP-LEFT of the chip body next to the notch.
        const int dotR = std::max(3, pinH / 4);
        QPoint pin1Center(chipBody.left() + dotR + 4,
                          chipBody.top() + dotR + 4);
        p.setBrush(QColor(255, 220, 60));
        p.setPen(Qt::NoPen);
        p.drawEllipse(pin1Center, dotR, dotR);

        // Notch at the TOP of the chip (orientation marker).
        QPainterPath notch;
        const int notchW = std::max(10, chipBody.width() / 6);
        const int notchH = std::max(4, dotR);
        notch.moveTo(chipBody.center().x() - notchW / 2.0, chipBody.top());
        notch.arcTo(QRectF(chipBody.center().x() - notchW / 2.0,
                           chipBody.top() - notchH,
                           notchW, notchH * 2.0),
                    180, 180);
        p.setBrush(palette().window());
        p.setPen(Qt::NoPen);
        p.drawPath(notch);

        // Caption: "1" next to dot.
        p.setPen(QColor(255, 220, 60));
        QFont cf = p.font();
        cf.setBold(true);
        p.setFont(cf);
        p.drawText(QPoint(pin1Center.x() + dotR + 4, pin1Center.y() + 4), "1");
        p.setFont(pf);
    }

    // Footer hint.
    p.setPen(palette().windowText().color());
    QString hint;
    if (!m_chip) {
        hint = "Pick a chip to see placement";
    } else if (!chipPins) {
        hint = QString("Package %1 — no DIP-on-ZIF preview").arg(m_chip->package);
    } else if (m_chip->adapter) {
        hint = QString("Requires adapter (code %1)")
                   .arg(m_chip->adapter, 0, 16);
    } else {
        hint = "Top-align in ZIF socket, notch UP (pin 1 at IC↑ arrow)";
    }
    p.drawText(QRect(r.left(), socketBody.bottom() + 4, r.width(), 18),
               Qt::AlignHCenter | Qt::AlignTop, hint);
}
