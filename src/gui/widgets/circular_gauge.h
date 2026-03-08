#pragma once

#include <QWidget>
#include <QPainter>
#include <QPropertyAnimation>

namespace occt { namespace gui {

class CircularGauge : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double value READ value WRITE setValue NOTIFY valueChanged)

public:
    explicit CircularGauge(QWidget* parent = nullptr);

    void setValue(double val);
    double value() const { return value_; }

    void setLabel(const QString& label) { label_ = label; update(); }
    QString label() const { return label_; }

    void setUnit(const QString& unit) { unit_ = unit; update(); }
    QString unit() const { return unit_; }

    void setArcColor(const QColor& color) { arcColor_ = color; update(); }

    QSize sizeHint() const override { return QSize(160, 160); }
    QSize minimumSizeHint() const override { return QSize(100, 100); }

signals:
    void valueChanged(double value);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QColor valueToColor(double val) const;

    double value_ = 0.0;
    QString label_;
    QString unit_ = "%";
    QColor arcColor_;
};

}} // namespace occt::gui
