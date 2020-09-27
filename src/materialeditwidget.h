#ifndef DUST3D_MATERIAL_EDIT_WIDGET_H
#define DUST3D_MATERIAL_EDIT_WIDGET_H
#include <QDialog>
#include <map>
#include <QCloseEvent>
#include <QLineEdit>
#include "document.h"
#include "modelwidget.h"
#include "materialpreviewsgenerator.h"
#include "imagepreviewwidget.h"
#include "floatnumberwidget.h"

enum class PopupWidgetType
{
    PitchYawRoll,
    Intersection
};

class MaterialEditWidget : public QDialog
{
    Q_OBJECT
signals:
    void addMaterial(QUuid materialId, QString name, std::vector<MaterialLayer> layers);
    void removeMaterial(QUuid materialId);
    void setMaterialLayers(QUuid materialId, std::vector<MaterialLayer> layers);
    void renameMaterial(QUuid materialId, QString name);
    void layersAdjusted();
public:
    MaterialEditWidget(const Document *document, QWidget *parent=nullptr);
    ~MaterialEditWidget();
public slots:
    void updatePreview();
    void setEditMaterialId(QUuid materialId);
    void setEditMaterialName(QString name);
    void setEditMaterialLayers(std::vector<MaterialLayer> layers);
    void updateTitle();
    void save();
    void clearUnsaveState();
    void previewReady();
protected:
    QSize sizeHint() const override;
    void closeEvent(QCloseEvent *event) override;
    void reject() override;
private:
    void updateMapButtonBackground(ImagePreviewWidget *button, const QImage *image);
    ImagePreviewWidget *createMapButton();
    QImage *pickImage();
    const Document *m_document = nullptr;
    MaterialPreviewsGenerator *m_materialPreviewsGenerator = nullptr;
    ModelWidget *m_previewWidget = nullptr;
    FloatNumberWidget *m_tileScaleSlider = nullptr;
    bool m_isPreviewDirty = false;
    bool m_closed = false;
    QUuid m_materialId;
    bool m_unsaved = false;
    QLineEdit *m_nameEdit = nullptr;
    std::vector<MaterialLayer> m_layers;
    ImagePreviewWidget *m_textureMapButtons[(int)TextureType::Count - 1] = {nullptr};
};

#endif
