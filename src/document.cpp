#include <QFileDialog>
#include <QDebug>
#include <QThread>
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QApplication>
#include <QVector3D>
#include <functional>
#include <QtCore/qbuffer.h>
#include <QElapsedTimer>
#include <queue>
#include "document.h"
#include "util.h"
#include "snapshotxml.h"
#include "materialpreviewsgenerator.h"
#include "motionsgenerator.h"
#include "skeletonside.h"
#include "scriptrunner.h"
#include "imageforever.h"
#include "meshgenerator.h"

unsigned long Document::m_maxSnapshot = 1000;

Document::Document() :
    SkeletonDocument()
{
    connect(&Preferences::instance(), &Preferences::partColorChanged, this, &Document::applyPreferencePartColorChange);
    connect(&Preferences::instance(), &Preferences::flatShadingChanged, this, &Document::applyPreferenceFlatShadingChange);
    connect(&Preferences::instance(), &Preferences::textureSizeChanged, this, &Document::applyPreferenceTextureSizeChange);
    connect(&Preferences::instance(), &Preferences::interpolationEnabledChanged, this, &Document::applyPreferenceInterpolationChange);
}

void Document::applyPreferencePartColorChange()
{
    regenerateMesh();
}

void Document::applyPreferenceFlatShadingChange()
{
    m_smoothNormal = !Preferences::instance().flatShading();
    regenerateMesh();
}

void Document::applyPreferenceTextureSizeChange()
{
    generateTexture();
}

void Document::applyPreferenceInterpolationChange()
{
    regenerateMesh();
}

Document::~Document()
{
    delete m_resultMesh;
    delete m_paintedMesh;
    delete m_resultMeshNodesCutFaces;
    delete m_postProcessedObject;
    delete textureImage;
    delete textureImageByteArray;
    delete textureNormalImage;
    delete textureNormalImageByteArray;
    delete textureMetalnessImage;
    delete textureMetalnessImageByteArray;
    delete textureRoughnessImage;
    delete textureRoughnessImageByteArray;
    delete textureAmbientOcclusionImage;
    delete textureAmbientOcclusionImageByteArray;
    delete m_resultTextureMesh;
    delete m_resultRigWeightMesh;
}

void Document::uiReady()
{
    qDebug() << "uiReady";
    emit editModeChanged();
}

void Document::addMotion(QUuid motionId, QString name, std::map<QString, QString> parameters)
{
    QUuid newMotionId = motionId;
    auto &motion = motionMap[newMotionId];
    motion.id = newMotionId;
    
    motion.name = name;
    motion.parameters = parameters;
    motion.dirty = true;
    
    emit motionsChanged();
    emit motionAdded(newMotionId);
    emit motionListChanged();
    emit optionsChanged();
}

void Document::removeMotion(QUuid motionId)
{
    auto findMotionResult = motionMap.find(motionId);
    if (findMotionResult == motionMap.end()) {
        qDebug() << "Remove a none exist motion:" << motionId;
        return;
    }
    motionMap.erase(findMotionResult);
    emit motionsChanged();
    emit motionListChanged();
    emit motionRemoved(motionId);
    emit optionsChanged();
}

void Document::setMotionParameters(QUuid motionId, std::map<QString, QString> parameters)
{
    auto findMotionResult = motionMap.find(motionId);
    if (findMotionResult == motionMap.end()) {
        qDebug() << "Find motion failed:" << motionId;
        return;
    }
    findMotionResult->second.parameters = parameters;
    findMotionResult->second.dirty = true;
    emit motionsChanged();
    emit motionParametersChanged(motionId);
    emit optionsChanged();
}

void Document::renameMotion(QUuid motionId, QString name)
{
    auto findMotionResult = motionMap.find(motionId);
    if (findMotionResult == motionMap.end()) {
        qDebug() << "Find motion failed:" << motionId;
        return;
    }
    if (findMotionResult->second.name == name)
        return;
    
    findMotionResult->second.name = name;
    emit motionNameChanged(motionId);
    emit motionListChanged();
    emit optionsChanged();
}

bool Document::originSettled() const
{
    return !qFuzzyIsNull(getOriginX()) && !qFuzzyIsNull(getOriginY()) && !qFuzzyIsNull(getOriginZ());
}

const Material *Document::findMaterial(QUuid materialId) const
{
    auto it = materialMap.find(materialId);
    if (it == materialMap.end())
        return nullptr;
    return &it->second;
}

const Motion *Document::findMotion(QUuid motionId) const
{
    auto it = motionMap.find(motionId);
    if (it == motionMap.end())
        return nullptr;
    return &it->second;
}

void Document::setNodeBoneMark(QUuid nodeId, BoneMark mark)
{
    auto it = nodeMap.find(nodeId);
    if (it == nodeMap.end()) {
        qDebug() << "Find node failed:" << nodeId;
        return;
    }
    if (isPartReadonly(it->second.partId))
        return;
    if (it->second.boneMark == mark)
        return;
    it->second.boneMark = mark;
    auto part = partMap.find(it->second.partId);
    if (part != partMap.end())
        part->second.dirty = true;
    emit nodeBoneMarkChanged(nodeId);
    emit skeletonChanged();
}

void Document::setNodeCutRotation(QUuid nodeId, float cutRotation)
{
    auto node = nodeMap.find(nodeId);
    if (node == nodeMap.end()) {
        qDebug() << "Node not found:" << nodeId;
        return;
    }
    if (qFuzzyCompare(cutRotation, node->second.cutRotation))
        return;
    node->second.setCutRotation(cutRotation);
    auto part = partMap.find(node->second.partId);
    if (part != partMap.end())
        part->second.dirty = true;
    emit nodeCutRotationChanged(nodeId);
    emit skeletonChanged();
}

void Document::setNodeCutFace(QUuid nodeId, CutFace cutFace)
{
    auto node = nodeMap.find(nodeId);
    if (node == nodeMap.end()) {
        qDebug() << "Node not found:" << nodeId;
        return;
    }
    if (node->second.cutFace == cutFace)
        return;
    node->second.setCutFace(cutFace);
    auto part = partMap.find(node->second.partId);
    if (part != partMap.end())
        part->second.dirty = true;
    emit nodeCutFaceChanged(nodeId);
    emit skeletonChanged();
}

void Document::setNodeCutFaceLinkedId(QUuid nodeId, QUuid linkedId)
{
    auto node = nodeMap.find(nodeId);
    if (node == nodeMap.end()) {
        qDebug() << "Node not found:" << nodeId;
        return;
    }
    if (node->second.cutFace == CutFace::UserDefined &&
            node->second.cutFaceLinkedId == linkedId)
        return;
    node->second.setCutFaceLinkedId(linkedId);
    auto part = partMap.find(node->second.partId);
    if (part != partMap.end())
        part->second.dirty = true;
    emit nodeCutFaceChanged(nodeId);
    emit skeletonChanged();
}

void Document::clearNodeCutFaceSettings(QUuid nodeId)
{
    auto node = nodeMap.find(nodeId);
    if (node == nodeMap.end()) {
        qDebug() << "Node not found:" << nodeId;
        return;
    }
    if (!node->second.hasCutFaceSettings)
        return;
    node->second.clearCutFaceSettings();
    auto part = partMap.find(node->second.partId);
    if (part != partMap.end())
        part->second.dirty = true;
    emit nodeCutFaceChanged(nodeId);
    emit skeletonChanged();
}

void Document::updateTurnaround(const QImage &image)
{
    turnaround = image;
    turnaroundPngByteArray.clear();
    QBuffer pngBuffer(&turnaroundPngByteArray);
    pngBuffer.open(QIODevice::WriteOnly);
    turnaround.save(&pngBuffer, "PNG");
    emit turnaroundChanged();
}

void Document::clearTurnaround()
{
    turnaround = QImage();
    turnaroundPngByteArray.clear();
    emit turnaroundChanged();
}

void Document::updateTextureImage(QImage *image)
{
    delete textureImageByteArray;
    textureImageByteArray = nullptr;
    
    delete textureImage;
    textureImage = image;
}

void Document::updateTextureNormalImage(QImage *image)
{
    delete textureNormalImageByteArray;
    textureNormalImageByteArray = nullptr;
    
    delete textureNormalImage;
    textureNormalImage = image;
}

void Document::updateTextureMetalnessImage(QImage *image)
{
    delete textureMetalnessImageByteArray;
    textureMetalnessImageByteArray = nullptr;
    
    delete textureMetalnessImage;
    textureMetalnessImage = image;
}

void Document::updateTextureRoughnessImage(QImage *image)
{
    delete textureRoughnessImageByteArray;
    textureRoughnessImageByteArray = nullptr;
    
    delete textureRoughnessImage;
    textureRoughnessImage = image;
}

void Document::updateTextureAmbientOcclusionImage(QImage *image)
{
    delete textureAmbientOcclusionImageByteArray;
    textureAmbientOcclusionImageByteArray = nullptr;
    
    delete textureAmbientOcclusionImage;
    textureAmbientOcclusionImage = image;
}

void Document::setEditMode(SkeletonDocumentEditMode mode)
{
    if (editMode == mode)
        return;
    
    if (SkeletonDocumentEditMode::Paint == mode && !objectLocked)
        return;
    
    editMode = mode;
    if (editMode != SkeletonDocumentEditMode::Paint)
        m_paintMode = PaintMode::None;
    emit editModeChanged();
}

void Document::setMeshLockState(bool locked)
{
    if (objectLocked == locked)
        return;
    
    objectLocked = locked;
    if (locked) {
        if (SkeletonDocumentEditMode::Paint != editMode) {
            editMode = SkeletonDocumentEditMode::Paint;
            emit editModeChanged();
        }
    } else {
        if (SkeletonDocumentEditMode::Paint == editMode) {
            editMode = SkeletonDocumentEditMode::Select;
            emit editModeChanged();
        }
    }
    emit objectLockStateChanged();
    emit textureChanged();
}

void Document::setPaintMode(PaintMode mode)
{
    if (m_paintMode == mode)
        return;
    
    m_paintMode = mode;
    emit paintModeChanged();
    
    paint();
}

void Document::toSnapshot(Snapshot *snapshot, const std::set<QUuid> &limitNodeIds,
    DocumentToSnapshotFor forWhat,
    const std::set<QUuid> &limitMotionIds,
    const std::set<QUuid> &limitMaterialIds) const
{
    if (DocumentToSnapshotFor::Document == forWhat ||
            DocumentToSnapshotFor::Nodes == forWhat) {
        std::set<QUuid> limitPartIds;
        std::set<QUuid> limitComponentIds;
        for (const auto &nodeId: limitNodeIds) {
            const SkeletonNode *node = findNode(nodeId);
            if (!node)
                continue;
            const SkeletonPart *part = findPart(node->partId);
            if (!part)
                continue;
            limitPartIds.insert(node->partId);
            const SkeletonComponent *component = findComponent(part->componentId);
            while (component) {
                limitComponentIds.insert(component->id);
                if (component->id.isNull())
                    break;
                component = findComponent(component->parentId);
            }
        }
        for (const auto &partIt : partMap) {
            if (!limitPartIds.empty() && limitPartIds.find(partIt.first) == limitPartIds.end())
                continue;
            std::map<QString, QString> part;
            part["id"] = partIt.second.id.toString();
            part["visible"] = partIt.second.visible ? "true" : "false";
            part["locked"] = partIt.second.locked ? "true" : "false";
            part["subdived"] = partIt.second.subdived ? "true" : "false";
            part["disabled"] = partIt.second.disabled ? "true" : "false";
            part["xMirrored"] = partIt.second.xMirrored ? "true" : "false";
            if (partIt.second.zMirrored)
                part["zMirrored"] = partIt.second.zMirrored ? "true" : "false";
            if (partIt.second.base != PartBase::XYZ)
                part["base"] = PartBaseToString(partIt.second.base);
            part["rounded"] = partIt.second.rounded ? "true" : "false";
            part["chamfered"] = partIt.second.chamfered ? "true" : "false";
            if (PartTarget::Model != partIt.second.target)
                part["target"] = PartTargetToString(partIt.second.target);
            if (partIt.second.cutRotationAdjusted())
                part["cutRotation"] = QString::number(partIt.second.cutRotation);
            if (partIt.second.cutFaceAdjusted()) {
                if (CutFace::UserDefined == partIt.second.cutFace) {
                    if (!partIt.second.cutFaceLinkedId.isNull()) {
                        part["cutFace"] = partIt.second.cutFaceLinkedId.toString();
                    }
                } else {
                    part["cutFace"] = CutFaceToString(partIt.second.cutFace);
                }
            }
            if (!partIt.second.fillMeshLinkedId.isNull())
                part["fillMesh"] = partIt.second.fillMeshLinkedId.toString();
            part["__dirty"] = partIt.second.dirty ? "true" : "false";
            if (partIt.second.hasColor)
                part["color"] = partIt.second.color.name(QColor::HexArgb);
            if (partIt.second.colorSolubilityAdjusted())
                part["colorSolubility"] = QString::number(partIt.second.colorSolubility);
            if (partIt.second.metalnessAdjusted())
                part["metallic"] = QString::number(partIt.second.metalness);
            if (partIt.second.roughnessAdjusted())
                part["roughness"] = QString::number(partIt.second.roughness);
            if (partIt.second.deformThicknessAdjusted())
                part["deformThickness"] = QString::number(partIt.second.deformThickness);
            if (partIt.second.deformWidthAdjusted())
                part["deformWidth"] = QString::number(partIt.second.deformWidth);
            if (partIt.second.deformUnified)
                part["deformUnified"] = "true";
            if (!partIt.second.deformMapImageId.isNull())
                part["deformMapImageId"] = partIt.second.deformMapImageId.toString();
            if (partIt.second.deformMapScaleAdjusted())
                part["deformMapScale"] = QString::number(partIt.second.deformMapScale);
            if (partIt.second.hollowThicknessAdjusted())
                part["hollowThickness"] = QString::number(partIt.second.hollowThickness);
            if (!partIt.second.name.isEmpty())
                part["name"] = partIt.second.name;
            if (partIt.second.materialAdjusted())
                part["materialId"] = partIt.second.materialId.toString();
            if (partIt.second.countershaded)
                part["countershaded"] = "true";
            if (partIt.second.smooth)
                part["smooth"] = "true";
            snapshot->parts[part["id"]] = part;
        }
        for (const auto &nodeIt: nodeMap) {
            if (!limitNodeIds.empty() && limitNodeIds.find(nodeIt.first) == limitNodeIds.end())
                continue;
            std::map<QString, QString> node;
            node["id"] = nodeIt.second.id.toString();
            node["radius"] = QString::number(nodeIt.second.radius);
            node["x"] = QString::number(nodeIt.second.getX());
            node["y"] = QString::number(nodeIt.second.getY());
            node["z"] = QString::number(nodeIt.second.getZ());
            node["partId"] = nodeIt.second.partId.toString();
            if (nodeIt.second.boneMark != BoneMark::None)
                node["boneMark"] = BoneMarkToString(nodeIt.second.boneMark);
            if (nodeIt.second.hasCutFaceSettings) {
                node["cutRotation"] = QString::number(nodeIt.second.cutRotation);
                if (CutFace::UserDefined == nodeIt.second.cutFace) {
                    if (!nodeIt.second.cutFaceLinkedId.isNull()) {
                        node["cutFace"] = nodeIt.second.cutFaceLinkedId.toString();
                    }
                } else {
                    node["cutFace"] = CutFaceToString(nodeIt.second.cutFace);
                }
            }
            if (!nodeIt.second.name.isEmpty())
                node["name"] = nodeIt.second.name;
            snapshot->nodes[node["id"]] = node;
        }
        for (const auto &edgeIt: edgeMap) {
            if (edgeIt.second.nodeIds.size() != 2)
                continue;
            if (!limitNodeIds.empty() &&
                    (limitNodeIds.find(edgeIt.second.nodeIds[0]) == limitNodeIds.end() ||
                        limitNodeIds.find(edgeIt.second.nodeIds[1]) == limitNodeIds.end()))
                continue;
            std::map<QString, QString> edge;
            edge["id"] = edgeIt.second.id.toString();
            edge["from"] = edgeIt.second.nodeIds[0].toString();
            edge["to"] = edgeIt.second.nodeIds[1].toString();
            edge["partId"] = edgeIt.second.partId.toString();
            if (!edgeIt.second.name.isEmpty())
                edge["name"] = edgeIt.second.name;
            snapshot->edges[edge["id"]] = edge;
        }
        for (const auto &componentIt: componentMap) {
            if (!limitComponentIds.empty() && limitComponentIds.find(componentIt.first) == limitComponentIds.end())
                continue;
            std::map<QString, QString> component;
            component["id"] = componentIt.second.id.toString();
            if (!componentIt.second.name.isEmpty())
                component["name"] = componentIt.second.name;
            component["expanded"] = componentIt.second.expanded ? "true" : "false";
            component["combineMode"] = CombineModeToString(componentIt.second.combineMode);
            component["__dirty"] = componentIt.second.dirty ? "true" : "false";
            QStringList childIdList;
            for (const auto &childId: componentIt.second.childrenIds) {
                childIdList.append(childId.toString());
            }
            QString children = childIdList.join(",");
            if (!children.isEmpty())
                component["children"] = children;
            QString linkData = componentIt.second.linkData();
            if (!linkData.isEmpty()) {
                component["linkData"] = linkData;
                component["linkDataType"] = componentIt.second.linkDataType();
            }
            if (!componentIt.second.name.isEmpty())
                component["name"] = componentIt.second.name;
            snapshot->components[component["id"]] = component;
        }
        if (limitComponentIds.empty() || limitComponentIds.find(QUuid()) != limitComponentIds.end()) {
            QStringList childIdList;
            for (const auto &childId: rootComponent.childrenIds) {
                childIdList.append(childId.toString());
            }
            QString children = childIdList.join(",");
            if (!children.isEmpty())
                snapshot->rootComponent["children"] = children;
        }
    }
    if (DocumentToSnapshotFor::Document == forWhat ||
            DocumentToSnapshotFor::Materials == forWhat) {
        for (const auto &materialId: materialIdList) {
            if (!limitMaterialIds.empty() && limitMaterialIds.find(materialId) == limitMaterialIds.end())
                continue;
            auto findMaterialResult = materialMap.find(materialId);
            if (findMaterialResult == materialMap.end()) {
                qDebug() << "Find material failed:" << materialId;
                continue;
            }
            auto &materialIt = *findMaterialResult;
            std::map<QString, QString> material;
            material["id"] = materialIt.second.id.toString();
            material["type"] = "MetalRoughness";
            if (!materialIt.second.name.isEmpty())
                material["name"] = materialIt.second.name;
            std::vector<std::pair<std::map<QString, QString>, std::vector<std::map<QString, QString>>>> layers;
            for (const auto &layer: materialIt.second.layers) {
                std::vector<std::map<QString, QString>> maps;
                for (const auto &mapItem: layer.maps) {
                    std::map<QString, QString> textureMap;
                    textureMap["for"] = TextureTypeToString(mapItem.forWhat);
                    textureMap["linkDataType"] = "imageId";
                    textureMap["linkData"] = mapItem.imageId.toString();
                    maps.push_back(textureMap);
                }
                std::map<QString, QString> layerAttributes;
                if (!qFuzzyCompare((float)layer.tileScale, (float)1.0))
                    layerAttributes["tileScale"] = QString::number(layer.tileScale);
                layers.push_back({layerAttributes, maps});
            }
            snapshot->materials.push_back(std::make_pair(material, layers));
        }
    }
    if (DocumentToSnapshotFor::Document == forWhat ||
            DocumentToSnapshotFor::Motions == forWhat) {
        for (const auto &motionIt: motionMap) {
            if (!limitMotionIds.empty() && limitMotionIds.find(motionIt.first) == limitMotionIds.end())
                continue;
            std::map<QString, QString> motion = motionIt.second.parameters;
            motion["id"] = motionIt.second.id.toString();
            if (!motionIt.second.name.isEmpty())
                motion["name"] = motionIt.second.name;
            snapshot->motions[motion["id"]] = motion;
        }
    }
    if (DocumentToSnapshotFor::Document == forWhat) {
        std::map<QString, QString> canvas;
        canvas["originX"] = QString::number(getOriginX());
        canvas["originY"] = QString::number(getOriginY());
        canvas["originZ"] = QString::number(getOriginZ());
        canvas["rigType"] = RigTypeToString(rigType);
        if (this->objectLocked)
            canvas["objectLocked"] = "true";
        snapshot->canvas = canvas;
    }
}

void Document::updateObject(Object *object)
{
    delete m_postProcessedObject;
    m_postProcessedObject = object;
}

void Document::addFromSnapshot(const Snapshot &snapshot, enum SnapshotSource source)
{
    bool isOriginChanged = false;
    bool isRigTypeChanged = false;
    bool isMeshLockedChanged = false;
    if (SnapshotSource::Paste != source &&
            SnapshotSource::Import != source) {
        const auto &originXit = snapshot.canvas.find("originX");
        const auto &originYit = snapshot.canvas.find("originY");
        const auto &originZit = snapshot.canvas.find("originZ");
        if (originXit != snapshot.canvas.end() &&
                originYit != snapshot.canvas.end() &&
                originZit != snapshot.canvas.end()) {
            setOriginX(originXit->second.toFloat());
            setOriginY(originYit->second.toFloat());
            setOriginZ(originZit->second.toFloat());
            isOriginChanged = true;
        }
        const auto &rigTypeIt = snapshot.canvas.find("rigType");
        if (rigTypeIt != snapshot.canvas.end()) {
            rigType = RigTypeFromString(rigTypeIt->second.toUtf8().constData());
        }
        bool setMeshLocked = isTrueValueString(valueOfKeyInMapOrEmpty(snapshot.canvas, "objectLocked"));
        if (this->objectLocked != setMeshLocked) {
            this->objectLocked = setMeshLocked;
            isMeshLockedChanged = true;
        }
        isRigTypeChanged = true;
    }
    
    std::set<QUuid> newAddedNodeIds;
    std::set<QUuid> newAddedEdgeIds;
    std::set<QUuid> newAddedPartIds;
    std::set<QUuid> newAddedComponentIds;
    
    std::set<QUuid> inversePartIds;
    
    std::map<QUuid, QUuid> oldNewIdMap;
    for (const auto &materialIt: snapshot.materials) {
        const auto &materialAttributes = materialIt.first;
        auto materialType = valueOfKeyInMapOrEmpty(materialAttributes, "type");
        if ("MetalRoughness" != materialType) {
            qDebug() << "Unsupported material type:" << materialType;
            continue;
        }
        QUuid oldMaterialId = QUuid(valueOfKeyInMapOrEmpty(materialAttributes, "id"));
        QUuid newMaterialId = SnapshotSource::Import == source ? oldMaterialId : QUuid::createUuid();
        oldNewIdMap[oldMaterialId] = newMaterialId;
        if (materialMap.end() == materialMap.find(newMaterialId)) {
            auto &newMaterial = materialMap[newMaterialId];
            newMaterial.id = newMaterialId;
            newMaterial.name = valueOfKeyInMapOrEmpty(materialAttributes, "name");
            for (const auto &layerIt: materialIt.second) {
                MaterialLayer layer;
                auto findTileScale = layerIt.first.find("tileScale");
                if (findTileScale != layerIt.first.end())
                    layer.tileScale = findTileScale->second.toFloat();
                for (const auto &mapItem: layerIt.second) {
                    auto textureTypeString = valueOfKeyInMapOrEmpty(mapItem, "for");
                    auto textureType = TextureTypeFromString(textureTypeString.toUtf8().constData());
                    if (TextureType::None == textureType) {
                        qDebug() << "Unsupported texture type:" << textureTypeString;
                        continue;
                    }
                    auto linkTypeString = valueOfKeyInMapOrEmpty(mapItem, "linkDataType");
                    if ("imageId" != linkTypeString) {
                        qDebug() << "Unsupported link data type:" << linkTypeString;
                        continue;
                    }
                    auto imageId = QUuid(valueOfKeyInMapOrEmpty(mapItem, "linkData"));
                    MaterialMap materialMap;
                    materialMap.imageId = imageId;
                    materialMap.forWhat = textureType;
                    layer.maps.push_back(materialMap);
                }
                newMaterial.layers.push_back(layer);
            }
            materialIdList.push_back(newMaterialId);
            emit materialAdded(newMaterialId);
        }
    }
    std::map<QUuid, QUuid> cutFaceLinkedIdModifyMap;
    for (const auto &partKv: snapshot.parts) {
        const auto newUuid = QUuid::createUuid();
        SkeletonPart &part = partMap[newUuid];
        part.id = newUuid;
        oldNewIdMap[QUuid(partKv.first)] = part.id;
        part.name = valueOfKeyInMapOrEmpty(partKv.second, "name");
        const auto &visibleIt = partKv.second.find("visible");
        if (visibleIt != partKv.second.end()) {
            part.visible = isTrueValueString(visibleIt->second);
        } else {
            part.visible = true;
        }
        part.locked = isTrueValueString(valueOfKeyInMapOrEmpty(partKv.second, "locked"));
        part.subdived = isTrueValueString(valueOfKeyInMapOrEmpty(partKv.second, "subdived"));
        part.disabled = isTrueValueString(valueOfKeyInMapOrEmpty(partKv.second, "disabled"));
        part.xMirrored = isTrueValueString(valueOfKeyInMapOrEmpty(partKv.second, "xMirrored"));
        part.zMirrored = isTrueValueString(valueOfKeyInMapOrEmpty(partKv.second, "zMirrored"));
        part.base = PartBaseFromString(valueOfKeyInMapOrEmpty(partKv.second, "base").toUtf8().constData());
        part.rounded = isTrueValueString(valueOfKeyInMapOrEmpty(partKv.second, "rounded"));
        part.chamfered = isTrueValueString(valueOfKeyInMapOrEmpty(partKv.second, "chamfered"));
        part.target = PartTargetFromString(valueOfKeyInMapOrEmpty(partKv.second, "target").toUtf8().constData());
        const auto &cutRotationIt = partKv.second.find("cutRotation");
        if (cutRotationIt != partKv.second.end())
            part.setCutRotation(cutRotationIt->second.toFloat());
        const auto &cutFaceIt = partKv.second.find("cutFace");
        if (cutFaceIt != partKv.second.end()) {
            QUuid cutFaceLinkedId = QUuid(cutFaceIt->second);
            if (cutFaceLinkedId.isNull()) {
                part.setCutFace(CutFaceFromString(cutFaceIt->second.toUtf8().constData()));
            } else {
                part.setCutFaceLinkedId(cutFaceLinkedId);
                cutFaceLinkedIdModifyMap.insert({part.id, cutFaceLinkedId});
            }
        }
        const auto &fillMeshIt = partKv.second.find("fillMesh");
        if (fillMeshIt != partKv.second.end()) {
            QUuid fillMeshLinkedId = QUuid(fillMeshIt->second);
            if (!fillMeshLinkedId.isNull())
                part.fillMeshLinkedId = fillMeshLinkedId;
        }
        if (isTrueValueString(valueOfKeyInMapOrEmpty(partKv.second, "inverse")))
            inversePartIds.insert(part.id);
        const auto &colorIt = partKv.second.find("color");
        if (colorIt != partKv.second.end()) {
            part.color = QColor(colorIt->second);
            part.hasColor = true;
        }
        const auto &colorSolubilityIt = partKv.second.find("colorSolubility");
        if (colorSolubilityIt != partKv.second.end())
            part.colorSolubility = colorSolubilityIt->second.toFloat();
        const auto &metalnessIt = partKv.second.find("metallic");
        if (metalnessIt != partKv.second.end())
            part.metalness = metalnessIt->second.toFloat();
        const auto &roughnessIt = partKv.second.find("roughness");
        if (roughnessIt != partKv.second.end())
            part.roughness = roughnessIt->second.toFloat();
        const auto &deformThicknessIt = partKv.second.find("deformThickness");
        if (deformThicknessIt != partKv.second.end())
            part.setDeformThickness(deformThicknessIt->second.toFloat());
        const auto &deformWidthIt = partKv.second.find("deformWidth");
        if (deformWidthIt != partKv.second.end())
            part.setDeformWidth(deformWidthIt->second.toFloat());
        const auto &deformUnifiedIt = partKv.second.find("deformUnified");
        if (deformUnifiedIt != partKv.second.end())
            part.deformUnified = isTrueValueString(valueOfKeyInMapOrEmpty(partKv.second, "deformUnified"));
        const auto &deformMapImageIdIt = partKv.second.find("deformMapImageId");
        if (deformMapImageIdIt != partKv.second.end())
            part.deformMapImageId = QUuid(deformMapImageIdIt->second);
        const auto &deformMapScaleIt = partKv.second.find("deformMapScale");
        if (deformMapScaleIt != partKv.second.end())
            part.deformMapScale = deformMapScaleIt->second.toFloat();
        const auto &hollowThicknessIt = partKv.second.find("hollowThickness");
        if (hollowThicknessIt != partKv.second.end())
            part.hollowThickness = hollowThicknessIt->second.toFloat();
        const auto &materialIdIt = partKv.second.find("materialId");
        if (materialIdIt != partKv.second.end())
            part.materialId = oldNewIdMap[QUuid(materialIdIt->second)];
        part.countershaded = isTrueValueString(valueOfKeyInMapOrEmpty(partKv.second, "countershaded"));
        part.smooth = isTrueValueString(valueOfKeyInMapOrEmpty(partKv.second, "smooth"));
        newAddedPartIds.insert(part.id);
    }
    for (const auto &it: cutFaceLinkedIdModifyMap) {
        SkeletonPart &part = partMap[it.first];
        auto findNewLinkedId = oldNewIdMap.find(it.second);
        if (findNewLinkedId == oldNewIdMap.end()) {
            if (partMap.find(it.second) == partMap.end()) {
                part.setCutFaceLinkedId(QUuid());
            }
        } else {
            part.setCutFaceLinkedId(findNewLinkedId->second);
        }
    }
    for (const auto &nodeKv: snapshot.nodes) {
        if (nodeKv.second.find("radius") == nodeKv.second.end() ||
                nodeKv.second.find("x") == nodeKv.second.end() ||
                nodeKv.second.find("y") == nodeKv.second.end() ||
                nodeKv.second.find("z") == nodeKv.second.end() ||
                nodeKv.second.find("partId") == nodeKv.second.end())
            continue;
        QUuid oldNodeId = QUuid(nodeKv.first);
        SkeletonNode node(nodeMap.find(oldNodeId) == nodeMap.end() ? oldNodeId : QUuid::createUuid());
        oldNewIdMap[oldNodeId] = node.id;
        node.name = valueOfKeyInMapOrEmpty(nodeKv.second, "name");
        node.radius = valueOfKeyInMapOrEmpty(nodeKv.second, "radius").toFloat();
        node.setX(valueOfKeyInMapOrEmpty(nodeKv.second, "x").toFloat());
        node.setY(valueOfKeyInMapOrEmpty(nodeKv.second, "y").toFloat());
        node.setZ(valueOfKeyInMapOrEmpty(nodeKv.second, "z").toFloat());
        node.partId = oldNewIdMap[QUuid(valueOfKeyInMapOrEmpty(nodeKv.second, "partId"))];
        node.boneMark = BoneMarkFromString(valueOfKeyInMapOrEmpty(nodeKv.second, "boneMark").toUtf8().constData());
        const auto &cutRotationIt = nodeKv.second.find("cutRotation");
        if (cutRotationIt != nodeKv.second.end())
            node.setCutRotation(cutRotationIt->second.toFloat());
        const auto &cutFaceIt = nodeKv.second.find("cutFace");
        if (cutFaceIt != nodeKv.second.end()) {
            QUuid cutFaceLinkedId = QUuid(cutFaceIt->second);
            if (cutFaceLinkedId.isNull()) {
                node.setCutFace(CutFaceFromString(cutFaceIt->second.toUtf8().constData()));
            } else {
                node.setCutFaceLinkedId(cutFaceLinkedId);
                auto findNewLinkedId = oldNewIdMap.find(cutFaceLinkedId);
                if (findNewLinkedId == oldNewIdMap.end()) {
                    if (partMap.find(cutFaceLinkedId) == partMap.end()) {
                        node.setCutFaceLinkedId(QUuid());
                    }
                } else {
                    node.setCutFaceLinkedId(findNewLinkedId->second);
                }
            }
        }
        nodeMap[node.id] = node;
        newAddedNodeIds.insert(node.id);
    }
    for (const auto &edgeKv: snapshot.edges) {
        if (edgeKv.second.find("from") == edgeKv.second.end() ||
                edgeKv.second.find("to") == edgeKv.second.end() ||
                edgeKv.second.find("partId") == edgeKv.second.end())
            continue;
        QUuid oldEdgeId = QUuid(edgeKv.first);
        SkeletonEdge edge(edgeMap.find(oldEdgeId) == edgeMap.end() ? oldEdgeId : QUuid::createUuid());
        oldNewIdMap[oldEdgeId] = edge.id;
        edge.name = valueOfKeyInMapOrEmpty(edgeKv.second, "name");
        edge.partId = oldNewIdMap[QUuid(valueOfKeyInMapOrEmpty(edgeKv.second, "partId"))];
        QString fromNodeId = valueOfKeyInMapOrEmpty(edgeKv.second, "from");
        if (!fromNodeId.isEmpty()) {
            QUuid fromId = oldNewIdMap[QUuid(fromNodeId)];
            edge.nodeIds.push_back(fromId);
            nodeMap[fromId].edgeIds.push_back(edge.id);
        }
        QString toNodeId = valueOfKeyInMapOrEmpty(edgeKv.second, "to");
        if (!toNodeId.isEmpty()) {
            QUuid toId = oldNewIdMap[QUuid(toNodeId)];
            edge.nodeIds.push_back(toId);
            nodeMap[toId].edgeIds.push_back(edge.id);
        }
        edgeMap[edge.id] = edge;
        newAddedEdgeIds.insert(edge.id);
    }
    for (const auto &nodeIt: nodeMap) {
        if (newAddedNodeIds.find(nodeIt.first) == newAddedNodeIds.end())
            continue;
        partMap[nodeIt.second.partId].nodeIds.push_back(nodeIt.first);
    }
    for (const auto &componentKv: snapshot.components) {
        QString linkData = valueOfKeyInMapOrEmpty(componentKv.second, "linkData");
        QString linkDataType = valueOfKeyInMapOrEmpty(componentKv.second, "linkDataType");
        SkeletonComponent component(QUuid(), linkData, linkDataType);
        oldNewIdMap[QUuid(componentKv.first)] = component.id;
        component.name = valueOfKeyInMapOrEmpty(componentKv.second, "name");
        component.expanded = isTrueValueString(valueOfKeyInMapOrEmpty(componentKv.second, "expanded"));
        component.combineMode = CombineModeFromString(valueOfKeyInMapOrEmpty(componentKv.second, "combineMode").toUtf8().constData());
        if (component.combineMode == CombineMode::Normal) {
            if (isTrueValueString(valueOfKeyInMapOrEmpty(componentKv.second, "inverse")))
                component.combineMode = CombineMode::Inversion;
        }
        //qDebug() << "Add component:" << component.id << " old:" << componentKv.first << "name:" << component.name;
        if ("partId" == linkDataType) {
            QUuid partId = oldNewIdMap[QUuid(linkData)];
            component.linkToPartId = partId;
            //qDebug() << "Add part:" << partId << " from component:" << component.id;
            partMap[partId].componentId = component.id;
            if (inversePartIds.find(partId) != inversePartIds.end())
                component.combineMode = CombineMode::Inversion;
        }
        componentMap[component.id] = component;
        newAddedComponentIds.insert(component.id);
    }
    const auto &rootComponentChildren = snapshot.rootComponent.find("children");
    if (rootComponentChildren != snapshot.rootComponent.end()) {
        for (const auto &childId: rootComponentChildren->second.split(",")) {
            if (childId.isEmpty())
                continue;
            QUuid componentId = oldNewIdMap[QUuid(childId)];
            if (componentMap.find(componentId) == componentMap.end())
                continue;
            //qDebug() << "Add root component:" << componentId;
            rootComponent.addChild(componentId);
        }
    }
    for (const auto &componentKv: snapshot.components) {
        QUuid componentId = oldNewIdMap[QUuid(componentKv.first)];
        if (componentMap.find(componentId) == componentMap.end())
            continue;
        for (const auto &childId: valueOfKeyInMapOrEmpty(componentKv.second, "children").split(",")) {
            if (childId.isEmpty())
                continue;
            QUuid childComponentId = oldNewIdMap[QUuid(childId)];
            if (componentMap.find(childComponentId) == componentMap.end())
                continue;
            //qDebug() << "Add child component:" << childComponentId << "to" << componentId;
            componentMap[componentId].addChild(childComponentId);
            componentMap[childComponentId].parentId = componentId;
        }
    }
    for (const auto &motionKv: snapshot.motions) {
        QUuid oldMotionId = QUuid(motionKv.first);
        QUuid newMotionId = QUuid::createUuid();
        auto &motion = motionMap[newMotionId];
        motion.id = newMotionId;
        oldNewIdMap[oldMotionId] = motion.id;
        motion.name = valueOfKeyInMapOrEmpty(motionKv.second, "name");
        motion.parameters = motionKv.second;
        emit motionAdded(newMotionId);
    }
    
    for (const auto &nodeIt: newAddedNodeIds) {
        emit nodeAdded(nodeIt);
    }
    for (const auto &edgeIt: newAddedEdgeIds) {
        emit edgeAdded(edgeIt);
    }
    for (const auto &partIt : newAddedPartIds) {
        emit partAdded(partIt);
    }
    
    emit componentChildrenChanged(QUuid());
    if (isOriginChanged)
        emit originChanged();
    if (isRigTypeChanged)
        emit rigTypeChanged();
    emit skeletonChanged();
    
    for (const auto &partIt : newAddedPartIds) {
        emit partVisibleStateChanged(partIt);
    }
    
    emit uncheckAll();
    for (const auto &nodeIt: newAddedNodeIds) {
        emit checkNode(nodeIt);
    }
    for (const auto &edgeIt: newAddedEdgeIds) {
        emit checkEdge(edgeIt);
    }
    
    if (!snapshot.materials.empty())
        emit materialListChanged();
    if (!snapshot.motions.empty())
        emit motionListChanged();
    
    if (isMeshLockedChanged)
        emit objectLockStateChanged();
}

void Document::silentReset()
{
    setOriginX(0.0);
    setOriginY(0.0);
    setOriginZ(0.0);
    rigType = RigType::None;
    nodeMap.clear();
    edgeMap.clear();
    partMap.clear();
    componentMap.clear();
    materialMap.clear();
    materialIdList.clear();
    motionMap.clear();
    rootComponent = SkeletonComponent();
    removeRigResults();
}

void Document::silentResetScript()
{
    m_script.clear();
    m_mergedVariables.clear();
    m_cachedVariables.clear();
    m_scriptError.clear();
    m_scriptConsoleLog.clear();
}

void Document::reset()
{
    silentReset();
    emit cleanup();
    emit skeletonChanged();
}

void Document::resetScript()
{
    silentResetScript();
    emit cleanupScript();
    emit scriptChanged();
    emit scriptErrorChanged();
    emit scriptConsoleLogChanged();
    emit mergedVaraiblesChanged();
}

void Document::fromSnapshot(const Snapshot &snapshot)
{
    reset();
    addFromSnapshot(snapshot, SnapshotSource::Unknown);
    emit uncheckAll();
}

Model *Document::takeResultMesh()
{
    if (nullptr == m_resultMesh)
        return nullptr;
    Model *resultMesh = new Model(*m_resultMesh);
    return resultMesh;
}

Model *Document::takePaintedMesh()
{
    if (nullptr == m_paintedMesh)
        return nullptr;
    Model *paintedMesh = new Model(*m_paintedMesh);
    return paintedMesh;
}

bool Document::isMeshGenerationSucceed()
{
    return m_isMeshGenerationSucceed;
}

Model *Document::takeResultTextureMesh()
{
    if (nullptr == m_resultTextureMesh)
        return nullptr;
    Model *resultTextureMesh = new Model(*m_resultTextureMesh);
    return resultTextureMesh;
}

Model *Document::takeResultRigWeightMesh()
{
    if (nullptr == m_resultRigWeightMesh)
        return nullptr;
    Model *resultMesh = new Model(*m_resultRigWeightMesh);
    return resultMesh;
}

void Document::meshReady()
{
    Model *resultMesh = m_meshGenerator->takeResultMesh();
    Object *object = m_meshGenerator->takeObject();
    bool isSuccessful = m_meshGenerator->isSuccessful();
    
    for (auto &partId: m_meshGenerator->generatedPreviewImagePartIds()) {
        auto part = partMap.find(partId);
        if (part != partMap.end()) {
            part->second.isPreviewMeshObsolete = false;
            QImage *resultPartPreviewImage = m_meshGenerator->takePartPreviewImage(partId);
            if (nullptr != resultPartPreviewImage)
                part->second.previewPixmap = QPixmap::fromImage(*resultPartPreviewImage);
            delete resultPartPreviewImage;
            emit partPreviewChanged(partId);
        }
    }
    
    bool partPreviewsChanged = false;
    for (auto &partId: m_meshGenerator->generatedPreviewPartIds()) {
        auto part = partMap.find(partId);
        if (part != partMap.end()) {
            Model *resultPartPreviewMesh = m_meshGenerator->takePartPreviewMesh(partId);
            part->second.updatePreviewMesh(resultPartPreviewMesh);
            partPreviewsChanged = true;
        }
    }
    if (partPreviewsChanged)
        emit resultPartPreviewsChanged();
    
    delete m_resultMesh;
    m_resultMesh = resultMesh;
    
    delete m_resultMeshNodesCutFaces;
    m_resultMeshNodesCutFaces = m_meshGenerator->takeNodesCutFaces();
    
    m_isMeshGenerationSucceed = isSuccessful;
    
    delete m_currentObject;
    m_currentObject = object;
    
    if (nullptr == m_resultMesh) {
        qDebug() << "Result mesh is null";
    }
    
    delete m_meshGenerator;
    m_meshGenerator = nullptr;
    
    qDebug() << "Mesh generation done";
    
    m_isPostProcessResultObsolete = true;
    m_isRigObsolete = true;
    emit resultMeshChanged();
    
    if (m_isResultMeshObsolete) {
        generateMesh();
    } else {
        if (objectLocked) {
            emit postProcessedResultChanged();
            
            if (nullptr != m_postProcessedObject) {
                Model *model = new Model(*m_postProcessedObject);
                if (nullptr != textureImage)
                    model->setTextureImage(new QImage(*textureImage));
                if (nullptr != textureNormalImage)
                    model->setNormalMapImage(new QImage(*textureNormalImage));
                if (nullptr != textureMetalnessImage || nullptr != textureRoughnessImage || nullptr != textureAmbientOcclusionImage) {
                    model->setMetalnessRoughnessAmbientOcclusionImage(TextureGenerator::combineMetalnessRoughnessAmbientOcclusionImages(
                        textureMetalnessImage,
                        textureRoughnessImage,
                        textureAmbientOcclusionImage));
                    model->setHasMetalnessInImage(nullptr != textureMetalnessImage);
                    model->setHasRoughnessInImage(nullptr != textureRoughnessImage);
                    model->setHasAmbientOcclusionInImage(nullptr != textureAmbientOcclusionImage);
                }
                model->setMeshId(m_nextMeshGenerationId++);
                delete m_resultTextureMesh;
                m_resultTextureMesh = model;
                emit resultTextureChanged();
            }
            
            checkExportReadyState();
        }
    }
}

bool Document::isPostProcessResultObsolete() const
{
    return m_isPostProcessResultObsolete;
}

void Document::batchChangeBegin()
{
    m_batchChangeRefCount++;
}

void Document::batchChangeEnd()
{
    m_batchChangeRefCount--;
    if (0 == m_batchChangeRefCount) {
        if (m_isResultMeshObsolete) {
            generateMesh();
        }
    }
}

void Document::regenerateMesh()
{
    if (objectLocked)
        return;
    
    markAllDirty();
    generateMesh();
}

void Document::toggleSmoothNormal()
{
    m_smoothNormal = !m_smoothNormal;
    regenerateMesh();
}

void Document::enableWeld(bool enabled)
{
    weldEnabled = enabled;
    regenerateMesh();
}

void Document::generateMesh()
{
    if (nullptr != m_meshGenerator || m_batchChangeRefCount > 0) {
        m_isResultMeshObsolete = true;
        return;
    }
    
    emit meshGenerating();
    
    qDebug() << "Mesh generating..";
    
    settleOrigin();
    
    m_isResultMeshObsolete = false;
    
    QThread *thread = new QThread;
    
    Snapshot *snapshot = new Snapshot;
    toSnapshot(snapshot);
    resetDirtyFlags();
    m_meshGenerator = new MeshGenerator(snapshot);
    m_meshGenerator->setId(m_nextMeshGenerationId++);
    m_meshGenerator->setDefaultPartColor(Preferences::instance().partColor());
    m_meshGenerator->setInterpolationEnabled(Preferences::instance().interpolationEnabled());
    if (nullptr == m_generatedCacheContext)
        m_generatedCacheContext = new GeneratedCacheContext;
    m_meshGenerator->setGeneratedCacheContext(m_generatedCacheContext);
    if (!m_smoothNormal) {
        m_meshGenerator->setSmoothShadingThresholdAngleDegrees(0);
    }
    m_meshGenerator->moveToThread(thread);
    connect(thread, &QThread::started, m_meshGenerator, &MeshGenerator::process);
    connect(m_meshGenerator, &MeshGenerator::finished, this, &Document::meshReady);
    connect(m_meshGenerator, &MeshGenerator::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void Document::generateTexture()
{
    if (objectLocked)
        return;
    
    if (nullptr != m_textureGenerator) {
        m_isTextureObsolete = true;
        return;
    }
    
    qDebug() << "Texture guide generating..";
    emit textureGenerating();
    
    m_isTextureObsolete = false;
    
    Snapshot *snapshot = new Snapshot;
    toSnapshot(snapshot);
    
    QThread *thread = new QThread;
    m_textureGenerator = new TextureGenerator(*m_postProcessedObject, snapshot);
    m_textureGenerator->moveToThread(thread);
    connect(thread, &QThread::started, m_textureGenerator, &TextureGenerator::process);
    connect(m_textureGenerator, &TextureGenerator::finished, this, &Document::textureReady);
    connect(m_textureGenerator, &TextureGenerator::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void Document::textureReady()
{
    updateTextureImage(m_textureGenerator->takeResultTextureColorImage());
    updateTextureNormalImage(m_textureGenerator->takeResultTextureNormalImage());
    updateTextureMetalnessImage(m_textureGenerator->takeResultTextureMetalnessImage());
    updateTextureRoughnessImage(m_textureGenerator->takeResultTextureRoughnessImage());
    updateTextureAmbientOcclusionImage(m_textureGenerator->takeResultTextureAmbientOcclusionImage());
    
    delete m_resultTextureMesh;
    m_resultTextureMesh = m_textureGenerator->takeResultMesh();
    
    m_postProcessedObject->alphaEnabled = m_textureGenerator->hasTransparencySettings();

    m_textureImageUpdateVersion++;
    
    delete m_textureGenerator;
    m_textureGenerator = nullptr;
    
    qDebug() << "Texture guide generation done";
    
    emit resultTextureChanged();
    
    if (m_isTextureObsolete) {
        generateTexture();
    } else {
        checkExportReadyState();
    }
}

void Document::postProcess()
{
    if (objectLocked) {
        return;
    }
    
    if (nullptr != m_postProcessor) {
        m_isPostProcessResultObsolete = true;
        return;
    }

    m_isPostProcessResultObsolete = false;

    if (!m_currentObject) {
        qDebug() << "Model is null";
        return;
    }

    qDebug() << "Post processing..";
    emit postProcessing();

    QThread *thread = new QThread;
    m_postProcessor = new MeshResultPostProcessor(*m_currentObject);
    m_postProcessor->moveToThread(thread);
    connect(thread, &QThread::started, m_postProcessor, &MeshResultPostProcessor::process);
    connect(m_postProcessor, &MeshResultPostProcessor::finished, this, &Document::postProcessedMeshResultReady);
    connect(m_postProcessor, &MeshResultPostProcessor::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void Document::postProcessedMeshResultReady()
{
    delete m_postProcessedObject;
    m_postProcessedObject = m_postProcessor->takePostProcessedObject();

    delete m_postProcessor;
    m_postProcessor = nullptr;

    qDebug() << "Post process done";

    emit postProcessedResultChanged();

    if (m_isPostProcessResultObsolete) {
        postProcess();
    }
}

void Document::pickMouseTarget(const QVector3D &nearPosition, const QVector3D &farPosition)
{
    m_mouseRayNear = nearPosition;
    m_mouseRayFar = farPosition;
    
    paint();
}

void Document::paint()
{
    if (nullptr != m_texturePainter) {
        m_isMouseTargetResultObsolete = true;
        return;
    }
    
    m_isMouseTargetResultObsolete = false;
    
    if (!m_postProcessedObject) {
        qDebug() << "Model is null";
        return;
    }
    
    if (nullptr == textureImage)
        return;
    
    //qDebug() << "Mouse picking..";

    QThread *thread = new QThread;
    m_texturePainter = new TexturePainter(m_mouseRayNear, m_mouseRayFar);
    if (nullptr == m_texturePainterContext) {
        m_texturePainterContext = new TexturePainterContext;
        m_texturePainterContext->object = new Object(*m_postProcessedObject);
        m_texturePainterContext->colorImage = new QImage(*textureImage);
    } else if (m_texturePainterContext->object->meshId != m_postProcessedObject->meshId) {
        delete m_texturePainterContext->object;
        m_texturePainterContext->object = new Object(*m_postProcessedObject);
        delete m_texturePainterContext->colorImage;
        m_texturePainterContext->colorImage = new QImage(*textureImage);
    }
    m_texturePainter->setContext(m_texturePainterContext);
    m_texturePainter->setBrushColor(brushColor);
    if (SkeletonDocumentEditMode::Paint == editMode) {
        m_texturePainter->setPaintMode(m_paintMode);
        m_texturePainter->setRadius(m_mousePickRadius);
        m_texturePainter->setMaskNodeIds(m_mousePickMaskNodeIds);
    }
    m_texturePainter->moveToThread(thread);
    connect(thread, &QThread::started, m_texturePainter, &TexturePainter::process);
    connect(m_texturePainter, &TexturePainter::finished, this, &Document::paintReady);
    connect(m_texturePainter, &TexturePainter::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void Document::paintReady()
{
    m_mouseTargetPosition = m_texturePainter->targetPosition();
    
    QImage *paintedTextureImage = m_texturePainter->takeColorImage();
    if (nullptr != paintedTextureImage) {
        updateTextureImage(paintedTextureImage);
        emit resultColorTextureChanged();
        emit optionsChanged();
    }
    
    delete m_texturePainter;
    m_texturePainter = nullptr;
    
    emit mouseTargetChanged();

    if (m_isMouseTargetResultObsolete) {
        pickMouseTarget(m_mouseRayNear, m_mouseRayFar);
    }
}

const QVector3D &Document::mouseTargetPosition() const
{
    return m_mouseTargetPosition;
}

float Document::mousePickRadius() const
{
    return m_mousePickRadius;
}

void Document::setMousePickRadius(float radius)
{
    m_mousePickRadius = radius;
    emit mousePickRadiusChanged();
}

const Object &Document::currentPostProcessedObject() const
{
    return *m_postProcessedObject;
}

void Document::setComponentCombineMode(QUuid componentId, CombineMode combineMode)
{
    auto component = componentMap.find(componentId);
    if (component == componentMap.end()) {
        qDebug() << "SkeletonComponent not found:" << componentId;
        return;
    }
    if (component->second.combineMode == combineMode)
        return;
    component->second.combineMode = combineMode;
    component->second.dirty = true;
    emit componentCombineModeChanged(componentId);
    emit skeletonChanged();
}

void Document::setPartSubdivState(QUuid partId, bool subdived)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.subdived == subdived)
        return;
    part->second.subdived = subdived;
    part->second.dirty = true;
    emit partSubdivStateChanged(partId);
    emit skeletonChanged();
}

void Document::settleOrigin()
{
    if (originSettled())
        return;
    Snapshot snapshot;
    toSnapshot(&snapshot);
    QRectF mainProfile;
    QRectF sideProfile;
    snapshot.resolveBoundingBox(&mainProfile, &sideProfile);
    setOriginX(mainProfile.x() + mainProfile.width() / 2);
    setOriginY(mainProfile.y() + mainProfile.height() / 2);
    setOriginZ(sideProfile.x() + sideProfile.width() / 2);
    markAllDirty();
    emit originChanged();
}

void Document::setPartXmirrorState(QUuid partId, bool mirrored)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.xMirrored == mirrored)
        return;
    part->second.xMirrored = mirrored;
    part->second.dirty = true;
    settleOrigin();
    emit partXmirrorStateChanged(partId);
    emit skeletonChanged();
}

//void Document::setPartZmirrorState(QUuid partId, bool mirrored)
//{
//    auto part = partMap.find(partId);
//    if (part == partMap.end()) {
//        qDebug() << "Part not found:" << partId;
//        return;
//    }
//    if (part->second.zMirrored == mirrored)
//        return;
//    part->second.zMirrored = mirrored;
//    part->second.dirty = true;
//    settleOrigin();
//    emit partZmirrorStateChanged(partId);
//    emit skeletonChanged();
//}

void Document::setPartDeformThickness(QUuid partId, float thickness)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    part->second.setDeformThickness(thickness);
    part->second.dirty = true;
    emit partDeformThicknessChanged(partId);
    emit skeletonChanged();
}

void Document::setPartBase(QUuid partId, PartBase base)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.base == base)
        return;
    part->second.base = base;
    part->second.dirty = true;
    emit partBaseChanged(partId);
    emit skeletonChanged();
}

void Document::setPartDeformWidth(QUuid partId, float width)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    part->second.setDeformWidth(width);
    part->second.dirty = true;
    emit partDeformWidthChanged(partId);
    emit skeletonChanged();
}

void Document::setPartDeformUnified(QUuid partId, bool unified)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.deformUnified == unified)
        return;
    part->second.deformUnified = unified;
    part->second.dirty = true;
    emit partDeformUnifyStateChanged(partId);
    emit skeletonChanged();
}

void Document::setPartDeformMapImageId(QUuid partId, QUuid imageId)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.deformMapImageId == imageId)
        return;
    part->second.deformMapImageId = imageId;
    part->second.dirty = true;
    emit partDeformMapImageIdChanged(partId);
    emit skeletonChanged();
}

void Document::setPartDeformMapScale(QUuid partId, float scale)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (qFuzzyCompare(part->second.deformMapScale, scale))
        return;
    part->second.deformMapScale = scale;
    part->second.dirty = true;
    emit partDeformMapScaleChanged(partId);
    emit skeletonChanged();
}

void Document::setPartMaterialId(QUuid partId, QUuid materialId)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.materialId == materialId)
        return;
    part->second.materialId = materialId;
    part->second.dirty = true;
    emit partMaterialIdChanged(partId);
    emit textureChanged();
}

void Document::setPartRoundState(QUuid partId, bool rounded)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.rounded == rounded)
        return;
    part->second.rounded = rounded;
    part->second.dirty = true;
    emit partRoundStateChanged(partId);
    emit skeletonChanged();
}

void Document::setPartChamferState(QUuid partId, bool chamfered)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.chamfered == chamfered)
        return;
    part->second.chamfered = chamfered;
    part->second.dirty = true;
    emit partChamferStateChanged(partId);
    emit skeletonChanged();
}

void Document::setPartTarget(QUuid partId, PartTarget target)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.target == target)
        return;
    part->second.target = target;
    part->second.dirty = true;
    emit partTargetChanged(partId);
    emit skeletonChanged();
}

void Document::setPartColorSolubility(QUuid partId, float solubility)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (qFuzzyCompare(part->second.colorSolubility, solubility))
        return;
    part->second.colorSolubility = solubility;
    part->second.dirty = true;
    emit partColorSolubilityChanged(partId);
    emit skeletonChanged();
}

void Document::setPartMetalness(QUuid partId, float metalness)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (qFuzzyCompare(part->second.metalness, metalness))
        return;
    part->second.metalness = metalness;
    part->second.dirty = true;
    emit partMetalnessChanged(partId);
    emit skeletonChanged();
}

void Document::setPartRoughness(QUuid partId, float roughness)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (qFuzzyCompare(part->second.roughness, roughness))
        return;
    part->second.roughness = roughness;
    part->second.dirty = true;
    emit partRoughnessChanged(partId);
    emit skeletonChanged();
}

void Document::setPartHollowThickness(QUuid partId, float hollowThickness)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (qFuzzyCompare(part->second.hollowThickness, hollowThickness))
        return;
    part->second.hollowThickness = hollowThickness;
    part->second.dirty = true;
    emit partHollowThicknessChanged(partId);
    emit skeletonChanged();
}

void Document::setPartCountershaded(QUuid partId, bool countershaded)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.countershaded == countershaded)
        return;
    part->second.countershaded = countershaded;
    part->second.dirty = true;
    emit partCountershadeStateChanged(partId);
    emit textureChanged();
}

void Document::setPartSmoothState(QUuid partId, bool smooth)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.smooth == smooth)
        return;
    part->second.smooth = smooth;
    part->second.dirty = true;
    emit partSmoothStateChanged(partId);
    emit skeletonChanged();
}

void Document::setPartCutRotation(QUuid partId, float cutRotation)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (qFuzzyCompare(cutRotation, part->second.cutRotation))
        return;
    part->second.setCutRotation(cutRotation);
    part->second.dirty = true;
    emit partCutRotationChanged(partId);
    emit skeletonChanged();
}

void Document::setPartCutFace(QUuid partId, CutFace cutFace)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.cutFace == cutFace)
        return;
    part->second.setCutFace(cutFace);
    part->second.dirty = true;
    emit partCutFaceChanged(partId);
    emit skeletonChanged();
}

void Document::setPartCutFaceLinkedId(QUuid partId, QUuid linkedId)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.cutFace == CutFace::UserDefined &&
            part->second.cutFaceLinkedId == linkedId)
        return;
    part->second.setCutFaceLinkedId(linkedId);
    part->second.dirty = true;
    emit partCutFaceChanged(partId);
    emit skeletonChanged();
}

void Document::setPartColorState(QUuid partId, bool hasColor, QColor color)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.hasColor == hasColor && part->second.color == color)
        return;
    part->second.hasColor = hasColor;
    part->second.color = color;
    part->second.dirty = true;
    emit partColorStateChanged(partId);
    emit skeletonChanged();
}

void Document::saveSnapshot()
{
    HistoryItem item;
    QElapsedTimer elapsedTimer;
    elapsedTimer.start();
    toSnapshot(&item.snapshot);
    if (m_undoItems.size() + 1 > m_maxSnapshot)
        m_undoItems.pop_front();
    m_undoItems.push_back(item);
}

void Document::undo()
{
    if (!undoable())
        return;
    m_redoItems.push_back(m_undoItems.back());
    m_undoItems.pop_back();
    const auto &item = m_undoItems.back();
    fromSnapshot(item.snapshot);
    qDebug() << "Undo/Redo items:" << m_undoItems.size() << m_redoItems.size();
}

void Document::redo()
{
    if (m_redoItems.empty())
        return;
    m_undoItems.push_back(m_redoItems.back());
    const auto &item = m_redoItems.back();
    fromSnapshot(item.snapshot);
    m_redoItems.pop_back();
    qDebug() << "Undo/Redo items:" << m_undoItems.size() << m_redoItems.size();
}

void Document::clearHistories()
{
    m_undoItems.clear();
    m_redoItems.clear();
}

void Document::paste()
{
    const QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();
    if (mimeData->hasText()) {
        QXmlStreamReader xmlStreamReader(mimeData->text());
        Snapshot snapshot;
        loadSkeletonFromXmlStream(&snapshot, xmlStreamReader);
        addFromSnapshot(snapshot, SnapshotSource::Paste);
        saveSnapshot();
    }
}

bool Document::hasPastableNodesInClipboard() const
{
    const QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();
    if (mimeData->hasText()) {
        if (-1 != mimeData->text().indexOf("<node "))
            return true;
    }
    return false;
}

bool Document::hasPastableMaterialsInClipboard() const
{
    const QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();
    if (mimeData->hasText()) {
        if (-1 != mimeData->text().indexOf("<material "))
            return true;
    }
    return false;
}

bool Document::hasPastableMotionsInClipboard() const
{
    const QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();
    if (mimeData->hasText()) {
        if (-1 != mimeData->text().indexOf("<motion "))
            return true;
    }
    return false;
}

bool Document::undoable() const
{
    return m_undoItems.size() >= 2;
}

bool Document::redoable() const
{
    return !m_redoItems.empty();
}

bool Document::isNodeEditable(QUuid nodeId) const
{
    const SkeletonNode *node = findNode(nodeId);
    if (!node) {
        qDebug() << "Node id not found:" << nodeId;
        return false;
    }
    return !isPartReadonly(node->partId);
}

bool Document::isEdgeEditable(QUuid edgeId) const
{
    const SkeletonEdge *edge = findEdge(edgeId);
    if (!edge) {
        qDebug() << "Edge id not found:" << edgeId;
        return false;
    }
    return !isPartReadonly(edge->partId);
}

bool Document::isExportReady() const
{
    if (m_meshGenerator ||
            m_textureGenerator ||
            m_postProcessor ||
            m_rigGenerator ||
            m_motionsGenerator)
        return false;
    
    if (objectLocked)
        return true;
    
    if (m_isResultMeshObsolete ||
            m_isTextureObsolete ||
            m_isPostProcessResultObsolete ||
            m_isRigObsolete)
        return false;
        
    return true;
}

void Document::checkExportReadyState()
{
    if (isExportReady())
        emit exportReady();
}

void Document::generateRig()
{
    if (nullptr != m_rigGenerator) {
        m_isRigObsolete = true;
        return;
    }
    
    m_isRigObsolete = false;
    
    if (RigType::None == rigType || nullptr == m_currentObject) {
        removeRigResults();
        return;
    }
    
    qDebug() << "Rig generating..";
    
    QThread *thread = new QThread;
    m_rigGenerator = new RigGenerator(rigType, *m_postProcessedObject);
    m_rigGenerator->moveToThread(thread);
    connect(thread, &QThread::started, m_rigGenerator, &RigGenerator::process);
    connect(m_rigGenerator, &RigGenerator::finished, this, &Document::rigReady);
    connect(m_rigGenerator, &RigGenerator::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void Document::rigReady()
{
    m_currentRigSucceed = m_rigGenerator->isSuccessful();

    delete m_resultRigWeightMesh;
    m_resultRigWeightMesh = m_rigGenerator->takeResultMesh();
    
    delete m_resultRigBones;
    m_resultRigBones = m_rigGenerator->takeResultBones();
    
    delete m_resultRigWeights;
    m_resultRigWeights = m_rigGenerator->takeResultWeights();
    
    m_resultRigMessages = m_rigGenerator->messages();
    
    delete m_riggedObject;
    m_riggedObject = m_rigGenerator->takeObject();
    if (nullptr == m_riggedObject)
        m_riggedObject = new Object;
    
    delete m_rigGenerator;
    m_rigGenerator = nullptr;
    
    qDebug() << "Rig generation done";
    
    emit resultRigChanged();
    
    if (m_isRigObsolete) {
        generateRig();
    } else {
        checkExportReadyState();
    }
}

const std::vector<RigBone> *Document::resultRigBones() const
{
    return m_resultRigBones;
}

const std::map<int, RigVertexWeights> *Document::resultRigWeights() const
{
    return m_resultRigWeights;
}

void Document::removeRigResults()
{
    delete m_resultRigBones;
    m_resultRigBones = nullptr;
    
    delete m_resultRigWeights;
    m_resultRigWeights = nullptr;
    
    delete m_resultRigWeightMesh;
    m_resultRigWeightMesh = nullptr;
    
    m_resultRigMessages.clear();
    
    m_currentRigSucceed = false;
    
    emit resultRigChanged();
}

void Document::setRigType(RigType toRigType)
{
    if (rigType == toRigType)
        return;
    
    rigType = toRigType;
    
    m_isRigObsolete = true;
    
    removeRigResults();
    
    emit rigTypeChanged();
    emit rigChanged();
}

const std::vector<std::pair<QtMsgType, QString>> &Document::resultRigMessages() const
{
    return m_resultRigMessages;
}

const Object &Document::currentRiggedObject() const
{
    return *m_riggedObject;
}

bool Document::currentRigSucceed() const
{
    return m_currentRigSucceed;
}

void Document::generateMotions()
{
    if (nullptr != m_motionsGenerator) {
        return;
    }
    
    const std::vector<RigBone> *rigBones = resultRigBones();
    const std::map<int, RigVertexWeights> *rigWeights = resultRigWeights();
    
    if (nullptr == rigBones || nullptr == rigWeights) {
        return;
    }
    
    m_motionsGenerator = new MotionsGenerator(rigType, *rigBones, *rigWeights, currentRiggedObject());
    m_motionsGenerator->enableSnapshotMeshes();
    bool hasDirtyMotion = false;
    for (auto &motion: motionMap) {
        if (motion.second.dirty) {
            hasDirtyMotion = true;
            motion.second.dirty = false;
            m_motionsGenerator->addMotion(motion.first, motion.second.parameters);
        }
    }
    if (!hasDirtyMotion) {
        delete m_motionsGenerator;
        m_motionsGenerator = nullptr;
        checkExportReadyState();
        return;
    }
    
    qDebug() << "Motions generating..";
    
    QThread *thread = new QThread;
    m_motionsGenerator->moveToThread(thread);
    connect(thread, &QThread::started, m_motionsGenerator, &MotionsGenerator::process);
    connect(m_motionsGenerator, &MotionsGenerator::finished, this, &Document::motionsReady);
    connect(m_motionsGenerator, &MotionsGenerator::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void Document::motionsReady()
{
    for (auto &motionId: m_motionsGenerator->generatedMotionIds()) {
        auto motion = motionMap.find(motionId);
        if (motion != motionMap.end()) {
            auto resultPreviewMesh = m_motionsGenerator->takeResultSnapshotMesh(motionId);
            motion->second.updatePreviewMesh(resultPreviewMesh);
            motion->second.jointNodeTrees = m_motionsGenerator->takeResultJointNodeTrees(motionId);
            emit motionPreviewChanged(motionId);
            emit motionResultChanged(motionId);
        }
    }
    
    delete m_motionsGenerator;
    m_motionsGenerator = nullptr;
    
    qDebug() << "Motions generation done";
    
    generateMotions();
}

void Document::addMaterial(QUuid materialId, QString name, std::vector<MaterialLayer> layers)
{
    auto findMaterialResult = materialMap.find(materialId);
    if (findMaterialResult != materialMap.end()) {
        qDebug() << "Material already exist:" << materialId;
        return;
    }
    
    QUuid newMaterialId = materialId;
    auto &material = materialMap[newMaterialId];
    material.id = newMaterialId;
    
    material.name = name;
    material.layers = layers;
    material.dirty = true;
    
    materialIdList.push_back(newMaterialId);
    
    emit materialAdded(newMaterialId);
    emit materialListChanged();
    emit optionsChanged();
}

void Document::removeMaterial(QUuid materialId)
{
    auto findMaterialResult = materialMap.find(materialId);
    if (findMaterialResult == materialMap.end()) {
        qDebug() << "Remove a none exist material:" << materialId;
        return;
    }
    materialIdList.erase(std::remove(materialIdList.begin(), materialIdList.end(), materialId), materialIdList.end());
    materialMap.erase(findMaterialResult);
    
    emit materialListChanged();
    emit materialRemoved(materialId);
    emit optionsChanged();
}

void Document::setMaterialLayers(QUuid materialId, std::vector<MaterialLayer> layers)
{
    auto findMaterialResult = materialMap.find(materialId);
    if (findMaterialResult == materialMap.end()) {
        qDebug() << "Find material failed:" << materialId;
        return;
    }
    findMaterialResult->second.layers = layers;
    findMaterialResult->second.dirty = true;
    emit materialLayersChanged(materialId);
    emit textureChanged();
    emit optionsChanged();
}

void Document::renameMaterial(QUuid materialId, QString name)
{
    auto findMaterialResult = materialMap.find(materialId);
    if (findMaterialResult == materialMap.end()) {
        qDebug() << "Find material failed:" << materialId;
        return;
    }
    if (findMaterialResult->second.name == name)
        return;
    
    findMaterialResult->second.name = name;
    emit materialNameChanged(materialId);
    emit materialListChanged();
    emit optionsChanged();
}

void Document::generateMaterialPreviews()
{
    if (nullptr != m_materialPreviewsGenerator) {
        return;
    }

    QThread *thread = new QThread;
    m_materialPreviewsGenerator = new MaterialPreviewsGenerator();
    bool hasDirtyMaterial = false;
    for (auto &materialIt: materialMap) {
        if (!materialIt.second.dirty)
            continue;
        m_materialPreviewsGenerator->addMaterial(materialIt.first, materialIt.second.layers);
        materialIt.second.dirty = false;
        hasDirtyMaterial = true;
    }
    if (!hasDirtyMaterial) {
        delete m_materialPreviewsGenerator;
        m_materialPreviewsGenerator = nullptr;
        delete thread;
        return;
    }
    
    qDebug() << "Material previews generating..";
    
    m_materialPreviewsGenerator->moveToThread(thread);
    connect(thread, &QThread::started, m_materialPreviewsGenerator, &MaterialPreviewsGenerator::process);
    connect(m_materialPreviewsGenerator, &MaterialPreviewsGenerator::finished, this, &Document::materialPreviewsReady);
    connect(m_materialPreviewsGenerator, &MaterialPreviewsGenerator::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void Document::materialPreviewsReady()
{
    for (const auto &materialId: m_materialPreviewsGenerator->generatedPreviewMaterialIds()) {
        auto material = materialMap.find(materialId);
        if (material != materialMap.end()) {
            Model *resultPartPreviewMesh = m_materialPreviewsGenerator->takePreview(materialId);
            material->second.updatePreviewMesh(resultPartPreviewMesh);
            emit materialPreviewChanged(materialId);
        }
    }

    delete m_materialPreviewsGenerator;
    m_materialPreviewsGenerator = nullptr;
    
    qDebug() << "Material previews generation done";
    
    generateMaterialPreviews();
}

bool Document::isMeshGenerating() const
{
    return nullptr != m_meshGenerator;
}

bool Document::isPostProcessing() const
{
    return nullptr != m_postProcessor;
}

bool Document::isTextureGenerating() const
{
    return nullptr != m_textureGenerator;
}

void Document::copyNodes(std::set<QUuid> nodeIdSet) const
{
    Snapshot snapshot;
    toSnapshot(&snapshot, nodeIdSet, DocumentToSnapshotFor::Nodes);
    QString snapshotXml;
    QXmlStreamWriter xmlStreamWriter(&snapshotXml);
    saveSkeletonToXmlStream(&snapshot, &xmlStreamWriter);
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(snapshotXml);
}

void Document::initScript(const QString &script)
{
    m_script = script;
    emit scriptModifiedFromExternal();
}

void Document::updateScript(const QString &script)
{
    if (m_script == script)
        return;
    
    m_script = script;
    emit scriptChanged();
}

void Document::updateVariable(const QString &name, const std::map<QString, QString> &value)
{
    bool needRunScript = false;
    auto variable = m_cachedVariables.find(name);
    if (variable == m_cachedVariables.end()) {
        m_cachedVariables[name] = value;
        needRunScript = true;
    } else if (variable->second != value) {
        variable->second = value;
    } else {
        return;
    }
    m_mergedVariables[name] = value;
    emit mergedVaraiblesChanged();
    if (needRunScript)
        runScript();
}

void Document::updateVariableValue(const QString &name, const QString &value)
{
    auto variable = m_cachedVariables.find(name);
    if (variable == m_cachedVariables.end()) {
        qDebug() << "Update a nonexist variable:" << name << "value:" << value;
        return;
    }
    auto &variableValue = variable->second["value"];
    if (variableValue == value)
        return;
    variableValue = value;
    m_mergedVariables[name] = variable->second;
    runScript();
}

bool Document::updateDefaultVariables(const std::map<QString, std::map<QString, QString>> &defaultVariables)
{
    bool updated = false;
    for (const auto &it: defaultVariables) {
        auto findMergedVariable = m_mergedVariables.find(it.first);
        if (findMergedVariable != m_mergedVariables.end()) {
            bool hasChangedAttribute = false;
            for (const auto &attribute: it.second) {
                if (attribute.first == "value")
                    continue;
                const auto &findMatch = findMergedVariable->second.find(attribute.first);
                if (findMatch != findMergedVariable->second.end()) {
                    if (findMatch->second == attribute.second)
                        continue;
                }
                hasChangedAttribute = true;
            }
            if (!hasChangedAttribute)
                continue;
        }
        updated = true;
        auto findCached = m_cachedVariables.find(it.first);
        if (findCached != m_cachedVariables.end()) {
            m_mergedVariables[it.first] = it.second;
            m_mergedVariables[it.first]["value"] = valueOfKeyInMapOrEmpty(findCached->second, "value");
        } else {
            m_mergedVariables[it.first] = it.second;
            m_cachedVariables[it.first] = it.second;
        }
    }
    std::vector<QString> eraseList;
    for (const auto &it: m_mergedVariables) {
        if (defaultVariables.end() == defaultVariables.find(it.first)) {
            eraseList.push_back(it.first);
            updated = true;
        }
    }
    for (const auto &it: eraseList) {
        m_mergedVariables.erase(it);
    }
    if (updated) {
        emit mergedVaraiblesChanged();
    }
    return updated;
}

void Document::runScript()
{
    if (nullptr != m_scriptRunner) {
        m_isScriptResultObsolete = true;
        return;
    }
    
    m_isScriptResultObsolete = false;
    
    qDebug() << "Script running..";
    
    QThread *thread = new QThread;

    m_scriptRunner = new ScriptRunner();
    m_scriptRunner->moveToThread(thread);
    m_scriptRunner->setScript(new QString(m_script));
    m_scriptRunner->setVariables(new std::map<QString, std::map<QString, QString>>(
        m_mergedVariables.empty() ? m_cachedVariables : m_mergedVariables
        ));
    connect(thread, &QThread::started, m_scriptRunner, &ScriptRunner::process);
    connect(m_scriptRunner, &ScriptRunner::finished, this, &Document::scriptResultReady);
    connect(m_scriptRunner, &ScriptRunner::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    emit scriptRunning();
    thread->start();
}

void Document::scriptResultReady()
{
    Snapshot *snapshot = m_scriptRunner->takeResultSnapshot();
    std::map<QString, std::map<QString, QString>> *defaultVariables = m_scriptRunner->takeDefaultVariables();
    bool errorChanged = false;
    bool consoleLogChanged = false;
    bool mergedVariablesChanged = false;
    
    const QString &scriptError = m_scriptRunner->scriptError();
    if (m_scriptError != scriptError) {
        m_scriptError = scriptError;
        errorChanged = true;
    }
    
    const QString &consoleLog = m_scriptRunner->consoleLog();
    if (m_scriptConsoleLog != consoleLog) {
        m_scriptConsoleLog = consoleLog;
        consoleLogChanged = true;
    }
    
    if (nullptr != snapshot) {
        fromSnapshot(*snapshot);
        delete snapshot;
        saveSnapshot();
    }
    
    if (nullptr != defaultVariables) {
        mergedVariablesChanged = updateDefaultVariables(*defaultVariables);
        delete defaultVariables;
    }

    delete m_scriptRunner;
    m_scriptRunner = nullptr;
    
    if (errorChanged)
        emit scriptErrorChanged();
    
    if (consoleLogChanged)
        emit scriptConsoleLogChanged();
    
    qDebug() << "Script run done";

    if (m_isScriptResultObsolete || mergedVariablesChanged) {
        runScript();
    }
}

const QString &Document::script() const
{
    return m_script;
}

const std::map<QString, std::map<QString, QString>> &Document::variables() const
{
    return m_mergedVariables;
}

const QString &Document::scriptError() const
{
    return m_scriptError;
}

const QString &Document::scriptConsoleLog() const
{
    return m_scriptConsoleLog;
}

void Document::startPaint()
{
}

void Document::stopPaint()
{
}

void Document::setMousePickMaskNodeIds(const std::set<QUuid> &nodeIds)
{
    m_mousePickMaskNodeIds = nodeIds;
}

void Document::collectCutFaceList(std::vector<QString> &cutFaces) const
{
    cutFaces.clear();
    
    std::vector<QUuid> cutFacePartIdList;
    
    std::set<QUuid> cutFacePartIds;
    for (const auto &it: partMap) {
        if (PartTarget::CutFace == it.second.target) {
            if (cutFacePartIds.find(it.first) != cutFacePartIds.end())
                continue;
            cutFacePartIds.insert(it.first);
            cutFacePartIdList.push_back(it.first);
        }
        if (!it.second.cutFaceLinkedId.isNull()) {
            if (cutFacePartIds.find(it.second.cutFaceLinkedId) != cutFacePartIds.end())
                continue;
            cutFacePartIds.insert(it.second.cutFaceLinkedId);
            cutFacePartIdList.push_back(it.second.cutFaceLinkedId);
        }
    }
    
    // Sort cut face by center.x of front view
    std::map<QUuid, float> centerOffsetMap;
    for (const auto &partId: cutFacePartIdList) {
        const SkeletonPart *part = findPart(partId);
        if (nullptr == part)
            continue;
        float offsetSum = 0;
        for (const auto &nodeId: part->nodeIds) {
            const SkeletonNode *node = findNode(nodeId);
            if (nullptr == node)
                continue;
            offsetSum += node->getX();
        }
        if (qFuzzyIsNull(offsetSum))
            continue;
        centerOffsetMap[partId] = offsetSum / part->nodeIds.size();
    }
    std::sort(cutFacePartIdList.begin(), cutFacePartIdList.end(),
            [&](const QUuid &firstPartId, const QUuid &secondPartId) {
        return centerOffsetMap[firstPartId] < centerOffsetMap[secondPartId];
    });
    
    size_t cutFaceTypeCount = (size_t)CutFace::UserDefined;
    for (size_t i = 0; i < (size_t)cutFaceTypeCount; ++i) {
        CutFace cutFace = (CutFace)i;
        cutFaces.push_back(CutFaceToString(cutFace));
    }
    
    for (const auto &it: cutFacePartIdList)
        cutFaces.push_back(it.toString());
}
