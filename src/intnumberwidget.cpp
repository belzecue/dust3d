#include <QtWidgets>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include "theme.h"
#include "intnumberwidget.h"

IntNumberWidget::IntNumberWidget(QWidget *parent, bool singleLine) :
    QWidget(parent)
{
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 100);
    m_slider->setFixedWidth(240);

    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignLeft);

    connect(m_slider, &QAbstractSlider::valueChanged, [=](int value) {
        updateValueLabel(value);
        emit valueChanged(value);
    });
    
    QBoxLayout *layout = nullptr;
    if (singleLine) {
        layout = new QHBoxLayout(this);
        layout->setMargin(2);
        layout->addWidget(m_slider);
        layout->addWidget(m_label);
    } else {
        layout = new QVBoxLayout(this);
        layout->setMargin(2);
        layout->addWidget(m_label);
        layout->addWidget(m_slider);
    }
}

void IntNumberWidget::updateValueLabel(int value)
{
    QString valueString = QString::number(value);
    if (m_itemName.isEmpty())
        m_label->setText(valueString);
    else
        m_label->setText(m_itemName + ": " + valueString);
}

void IntNumberWidget::setItemName(const QString &name)
{
    m_itemName = name;
    updateValueLabel(value());
}

void IntNumberWidget::setRange(int min, int max)
{
    m_slider->setRange(min, max);
}

void IntNumberWidget::increaseValue()
{
    m_slider->triggerAction(QSlider::SliderPageStepAdd);
}

void IntNumberWidget::descreaseValue()
{
    m_slider->triggerAction(QSlider::SliderPageStepSub);
}

int IntNumberWidget::value() const
{
    return m_slider->value();
}

void IntNumberWidget::setValue(int value)
{
    m_slider->setValue(value);
}
