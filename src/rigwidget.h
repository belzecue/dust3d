#ifndef DUST3D_RIG_WIDGET_H
#define DUST3D_RIG_WIDGET_H
#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include "rigtype.h"
#include "modelwidget.h"
#include "infolabel.h"

class Document;

class RigWidget : public QWidget
{
    Q_OBJECT
signals:
    void setRigType(RigType rigType);
public slots:
    void rigTypeChanged();
    void updateResultInfo();
public:
    RigWidget(const Document *document, QWidget *parent=nullptr);
    ModelWidget *rigWeightRenderWidget();
private:
    const Document *m_document = nullptr;
    QComboBox *m_rigTypeBox = nullptr;
    ModelWidget *m_rigWeightRenderWidget = nullptr;
    InfoLabel *m_infoLabel = nullptr;
};

#endif
