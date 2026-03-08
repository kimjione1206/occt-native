#include "realtime_chart.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>

namespace occt { namespace gui {

RealtimeChart::RealtimeChart(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(200, 100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void RealtimeChart::addPoint(double value)
{
    data_.append(value);
    if (data_.size() > maxPoints_)
        data_.removeFirst();

    if (autoScale_)
        updateAutoScale();

    update();
}

void RealtimeChart::clear()
{
    data_.clear();
    update();
}

void RealtimeChart::updateAutoScale()
{
    if (data_.isEmpty()) return;

    double min = *std::min_element(data_.begin(), data_.end());
    double max = *std::max_element(data_.begin(), data_.end());

    double range = max - min;
    if (range < 1.0) range = 1.0;

    minY_ = min - range * 0.1;
    maxY_ = max + range * 0.1;

    if (minY_ < 0 && min >= 0) minY_ = 0;
}

void RealtimeChart::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Margins
    double leftMargin = 55;
    double rightMargin = 15;
    double topMargin = title_.isEmpty() ? 10 : 30;
    double bottomMargin = 10;

    QRectF plotArea(
        leftMargin, topMargin,
        width() - leftMargin - rightMargin,
        height() - topMargin - bottomMargin
    );

    drawBackground(painter, plotArea);
    if (gridVisible_) drawGrid(painter, plotArea);
    drawYAxis(painter, plotArea);
    if (!data_.isEmpty()) drawLine(painter, plotArea);
    if (!title_.isEmpty()) drawTitle(painter, rect());
}

void RealtimeChart::drawBackground(QPainter& painter, const QRectF& plotArea)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(13, 17, 23));
    painter.drawRoundedRect(plotArea, 4, 4);
}

void RealtimeChart::drawGrid(QPainter& painter, const QRectF& plotArea)
{
    QPen gridPen(QColor(48, 54, 61), 1, Qt::DotLine);
    painter.setPen(gridPen);

    int hLines = 5;
    for (int i = 0; i <= hLines; ++i) {
        double y = plotArea.top() + (plotArea.height() * i / hLines);
        painter.drawLine(QPointF(plotArea.left(), y), QPointF(plotArea.right(), y));
    }

    int vLines = 6;
    for (int i = 0; i <= vLines; ++i) {
        double x = plotArea.left() + (plotArea.width() * i / vLines);
        painter.drawLine(QPointF(x, plotArea.top()), QPointF(x, plotArea.bottom()));
    }
}

void RealtimeChart::drawYAxis(QPainter& painter, const QRectF& plotArea)
{
    QFont axisFont = font();
    axisFont.setPixelSize(10);
    painter.setFont(axisFont);
    painter.setPen(QColor(149, 165, 166));

    int ticks = 5;
    for (int i = 0; i <= ticks; ++i) {
        double y = plotArea.top() + (plotArea.height() * i / ticks);
        double val = maxY_ - (maxY_ - minY_) * i / ticks;

        QString text;
        if (std::abs(val) >= 1000)
            text = QString::number(val, 'f', 0);
        else if (std::abs(val) >= 10)
            text = QString::number(val, 'f', 1);
        else
            text = QString::number(val, 'f', 2);

        if (!unit_.isEmpty())
            text += " " + unit_;

        QRectF labelRect(0, y - 8, plotArea.left() - 5, 16);
        painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, text);
    }
}

void RealtimeChart::drawLine(QPainter& painter, const QRectF& plotArea)
{
    if (data_.size() < 2) return;

    double range = maxY_ - minY_;
    if (range < 0.001) range = 1.0;

    QPainterPath path;
    int count = data_.size();
    double dx = plotArea.width() / (maxPoints_ - 1);
    double startX = plotArea.right() - (count - 1) * dx;

    for (int i = 0; i < count; ++i) {
        double x = startX + i * dx;
        double normalized = (data_[i] - minY_) / range;
        normalized = std::clamp(normalized, 0.0, 1.0);
        double y = plotArea.bottom() - normalized * plotArea.height();

        if (i == 0)
            path.moveTo(x, y);
        else
            path.lineTo(x, y);
    }

    // Fill under curve
    if (fillEnabled_) {
        QPainterPath fillPath = path;
        double lastX = startX + (count - 1) * dx;
        fillPath.lineTo(lastX, plotArea.bottom());
        fillPath.lineTo(startX, plotArea.bottom());
        fillPath.closeSubpath();

        QLinearGradient grad(0, plotArea.top(), 0, plotArea.bottom());
        QColor fillColor = lineColor_;
        fillColor.setAlpha(80);
        grad.setColorAt(0, fillColor);
        fillColor.setAlpha(10);
        grad.setColorAt(1, fillColor);

        painter.setPen(Qt::NoPen);
        painter.setBrush(grad);
        painter.drawPath(fillPath);
    }

    // Draw line
    QPen linePen(lineColor_, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(linePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    // Current value dot
    if (count > 0) {
        double lastNorm = (data_.last() - minY_) / range;
        lastNorm = std::clamp(lastNorm, 0.0, 1.0);
        double lastY = plotArea.bottom() - lastNorm * plotArea.height();
        double lastXp = startX + (count - 1) * dx;

        painter.setPen(Qt::NoPen);
        painter.setBrush(lineColor_);
        painter.drawEllipse(QPointF(lastXp, lastY), 4, 4);

        // Glow effect
        QColor glow = lineColor_;
        glow.setAlpha(60);
        painter.setBrush(glow);
        painter.drawEllipse(QPointF(lastXp, lastY), 8, 8);
    }
}

void RealtimeChart::drawTitle(QPainter& painter, const QRectF& fullArea)
{
    QFont titleFont = font();
    titleFont.setPixelSize(12);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor(201, 209, 217));

    QRectF titleRect(fullArea.left() + 55, fullArea.top(), fullArea.width() - 70, 25);
    painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, title_);
}

}} // namespace occt::gui
