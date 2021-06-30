#include <QDebug>
#include <QElapsedTimer>
#include <QVector2D>
#include <QGuiApplication>
#include <QMatrix4x4>
#include "strokemeshbuilder.h"
#include "strokemodifier.h"
#include "meshrecombiner.h"
#include "meshgenerator.h"
#include "util.h"
#include "trianglesourcenoderesolve.h"
#include "cutface.h"
#include "parttarget.h"
#include "theme.h"
#include "partbase.h"
#include "imageforever.h"
#include "triangulatefaces.h"
#include "isotropicremesh.h"
#include "document.h"
#include "meshstroketifier.h"
#include "fileforever.h"
#include "snapshotxml.h"
#include "fixholes.h"
#include "modeloffscreenrender.h"

MeshGenerator::MeshGenerator(Snapshot *snapshot) :
    m_snapshot(snapshot)
{
}

MeshGenerator::~MeshGenerator()
{
    for (auto &it: m_partPreviewImages)
        delete it.second;
    for (auto &it: m_partPreviewMeshes)
        delete it.second;
    delete m_resultMesh;
    delete m_snapshot;
    delete m_object;
    delete m_cutFaceTransforms;
    delete m_nodesCutFaces;
}

void MeshGenerator::setId(quint64 id)
{
    m_id = id;
}

quint64 MeshGenerator::id()
{
    return m_id;
}

bool MeshGenerator::isSuccessful()
{
    return m_isSuccessful;
}

Model *MeshGenerator::takeResultMesh()
{
    Model *resultMesh = m_resultMesh;
    m_resultMesh = nullptr;
    return resultMesh;
}

Model *MeshGenerator::takePartPreviewMesh(const QUuid &partId)
{
    Model *resultMesh = m_partPreviewMeshes[partId];
    m_partPreviewMeshes[partId] = nullptr;
    return resultMesh;
}

QImage *MeshGenerator::takePartPreviewImage(const QUuid &partId)
{
    QImage *image = m_partPreviewImages[partId];
    m_partPreviewImages[partId] = nullptr;
    return image;
}

const std::set<QUuid> &MeshGenerator::generatedPreviewPartIds()
{
    return m_generatedPreviewPartIds;
}

const std::set<QUuid> &MeshGenerator::generatedPreviewImagePartIds()
{
    return m_generatedPreviewImagePartIds;
}

Object *MeshGenerator::takeObject()
{
    Object *object = m_object;
    m_object = nullptr;
    return object;
}

std::map<QUuid, StrokeMeshBuilder::CutFaceTransform> *MeshGenerator::takeCutFaceTransforms()
{
    auto cutFaceTransforms = m_cutFaceTransforms;
    m_cutFaceTransforms = nullptr;
    return cutFaceTransforms;
}

std::map<QUuid, std::map<QString, QVector2D>> *MeshGenerator::takeNodesCutFaces()
{
    auto nodesCutFaces = m_nodesCutFaces;
    m_nodesCutFaces = nullptr;
    return nodesCutFaces;
}

void MeshGenerator::collectParts()
{
    for (const auto &node: m_snapshot->nodes) {
        QString partId = valueOfKeyInMapOrEmpty(node.second, "partId");
        if (partId.isEmpty())
            continue;
        m_partNodeIds[partId].insert(node.first);
    }
    for (const auto &edge: m_snapshot->edges) {
        QString partId = valueOfKeyInMapOrEmpty(edge.second, "partId");
        if (partId.isEmpty())
            continue;
        m_partEdgeIds[partId].insert(edge.first);
    }
}

bool MeshGenerator::checkIsPartDirty(const QString &partIdString)
{
    auto findPart = m_snapshot->parts.find(partIdString);
    if (findPart == m_snapshot->parts.end()) {
        qDebug() << "Find part failed:" << partIdString;
        return false;
    }
    return isTrueValueString(valueOfKeyInMapOrEmpty(findPart->second, "__dirty"));
}

bool MeshGenerator::checkIsPartDependencyDirty(const QString &partIdString)
{
    auto findPart = m_snapshot->parts.find(partIdString);
    if (findPart == m_snapshot->parts.end()) {
        qDebug() << "Find part failed:" << partIdString;
        return false;
    }
    QString cutFaceString = valueOfKeyInMapOrEmpty(findPart->second, "cutFace");
    QUuid cutFaceLinkedPartId = QUuid(cutFaceString);
    if (!cutFaceLinkedPartId.isNull()) {
        if (checkIsPartDirty(cutFaceString))
            return true;
    }
    for (const auto &nodeIdString: m_partNodeIds[partIdString]) {
        auto findNode = m_snapshot->nodes.find(nodeIdString);
        if (findNode == m_snapshot->nodes.end()) {
            qDebug() << "Find node failed:" << nodeIdString;
            continue;
        }
        QString cutFaceString = valueOfKeyInMapOrEmpty(findNode->second, "cutFace");
        QUuid cutFaceLinkedPartId = QUuid(cutFaceString);
        if (!cutFaceLinkedPartId.isNull()) {
            if (checkIsPartDirty(cutFaceString))
                return true;
        }
    }
    return false;
}

bool MeshGenerator::checkIsComponentDirty(const QString &componentIdString)
{
    bool isDirty = false;
    
    const std::map<QString, QString> *component = &m_snapshot->rootComponent;
    if (componentIdString != QUuid().toString()) {
        auto findComponent = m_snapshot->components.find(componentIdString);
        if (findComponent == m_snapshot->components.end()) {
            qDebug() << "Component not found:" << componentIdString;
            return isDirty;
        }
        component = &findComponent->second;
    }
    
    if (isTrueValueString(valueOfKeyInMapOrEmpty(*component, "__dirty"))) {
        isDirty = true;
    }
    
    QString linkDataType = valueOfKeyInMapOrEmpty(*component, "linkDataType");
    if ("partId" == linkDataType) {
        QString partId = valueOfKeyInMapOrEmpty(*component, "linkData");
        if (checkIsPartDirty(partId)) {
            m_dirtyPartIds.insert(partId);
            isDirty = true;
        }
        if (!isDirty) {
            if (checkIsPartDependencyDirty(partId)) {
                isDirty = true;
            }
        }
    }
    
    for (const auto &childId: valueOfKeyInMapOrEmpty(*component, "children").split(",")) {
        if (childId.isEmpty())
            continue;
        if (checkIsComponentDirty(childId)) {
            isDirty = true;
        }
    }
    
    if (isDirty)
        m_dirtyComponentIds.insert(componentIdString);
    
    return isDirty;
}

void MeshGenerator::checkDirtyFlags()
{
    checkIsComponentDirty(QUuid().toString());
}

void MeshGenerator::cutFaceStringToCutTemplate(const QString &cutFaceString, std::vector<QVector2D> &cutTemplate)
{
    //std::map<QString, QVector2D> cutTemplateMapByName;
    QUuid cutFaceLinkedPartId = QUuid(cutFaceString);
    if (!cutFaceLinkedPartId.isNull()) {
        std::map<QString, std::tuple<float, float, float>> cutFaceNodeMap;
        auto findCutFaceLinkedPart = m_snapshot->parts.find(cutFaceString);
        if (findCutFaceLinkedPart == m_snapshot->parts.end()) {
            qDebug() << "Find cut face linked part failed:" << cutFaceString;
        } else {
            // Build node info map
            for (const auto &nodeIdString: m_partNodeIds[cutFaceString]) {
                auto findNode = m_snapshot->nodes.find(nodeIdString);
                if (findNode == m_snapshot->nodes.end()) {
                    qDebug() << "Find node failed:" << nodeIdString;
                    continue;
                }
                auto &node = findNode->second;
                float radius = valueOfKeyInMapOrEmpty(node, "radius").toFloat();
                float x = (valueOfKeyInMapOrEmpty(node, "x").toFloat() - m_mainProfileMiddleX);
                float y = (m_mainProfileMiddleY - valueOfKeyInMapOrEmpty(node, "y").toFloat());
                cutFaceNodeMap.insert({nodeIdString, std::make_tuple(radius, x, y)});
            }
            // Build edge link
            std::map<QString, std::vector<QString>> cutFaceNodeLinkMap;
            for (const auto &edgeIdString: m_partEdgeIds[cutFaceString]) {
                auto findEdge = m_snapshot->edges.find(edgeIdString);
                if (findEdge == m_snapshot->edges.end()) {
                    qDebug() << "Find edge failed:" << edgeIdString;
                    continue;
                }
                auto &edge = findEdge->second;
                QString fromNodeIdString = valueOfKeyInMapOrEmpty(edge, "from");
                QString toNodeIdString = valueOfKeyInMapOrEmpty(edge, "to");
                cutFaceNodeLinkMap[fromNodeIdString].push_back(toNodeIdString);
                cutFaceNodeLinkMap[toNodeIdString].push_back(fromNodeIdString);
            }
            // Find endpoint
            QString endPointNodeIdString;
            std::vector<std::pair<QString, std::tuple<float, float, float>>> endpointNodes;
            for (const auto &it: cutFaceNodeLinkMap) {
                if (1 == it.second.size()) {
                    const auto &findNode = cutFaceNodeMap.find(it.first);
                    if (findNode != cutFaceNodeMap.end())
                        endpointNodes.push_back({it.first, findNode->second});
                }
            }
            bool isRing = endpointNodes.empty();
            if (endpointNodes.empty()) {
                for (const auto &it: cutFaceNodeMap) {
                    endpointNodes.push_back({it.first, it.second});
                }
            }
            if (!endpointNodes.empty()) {
                // Calculate the center points
                QVector2D sumOfPositions;
                for (const auto &it: endpointNodes) {
                    sumOfPositions += QVector2D(std::get<1>(it.second), std::get<2>(it.second));
                }
                QVector2D center = sumOfPositions / endpointNodes.size();
                
                // Calculate all the directions emit from center to the endpoint,
                // choose the minimal angle, angle: (0, 0 -> -1, -1) to the direction
                const QVector3D referenceDirection = QVector3D(-1, -1, 0).normalized();
                int choosenEndpoint = -1;
                float choosenRadian = 0;
                for (int i = 0; i < (int)endpointNodes.size(); ++i) {
                    const auto &it = endpointNodes[i];
                    QVector2D direction2d = (QVector2D(std::get<1>(it.second), std::get<2>(it.second)) -
                        center);
                    QVector3D direction = QVector3D(direction2d.x(), direction2d.y(), 0).normalized();
                    float radian = radianBetweenVectors(referenceDirection, direction);
                    if (-1 == choosenEndpoint || radian < choosenRadian) {
                        choosenRadian = radian;
                        choosenEndpoint = i;
                    }
                }
                endPointNodeIdString = endpointNodes[choosenEndpoint].first;
            }
            // Loop all linked nodes
            std::vector<std::tuple<float, float, float, QString>> cutFaceNodes;
            std::set<QString> cutFaceVisitedNodeIds;
            std::function<void (const QString &)> loopNodeLink;
            loopNodeLink = [&](const QString &fromNodeIdString) {
                auto findCutFaceNode = cutFaceNodeMap.find(fromNodeIdString);
                if (findCutFaceNode == cutFaceNodeMap.end())
                    return;
                if (cutFaceVisitedNodeIds.find(fromNodeIdString) != cutFaceVisitedNodeIds.end())
                    return;
                cutFaceVisitedNodeIds.insert(fromNodeIdString);
                cutFaceNodes.push_back(std::make_tuple(std::get<0>(findCutFaceNode->second),
                    std::get<1>(findCutFaceNode->second),
                    std::get<2>(findCutFaceNode->second),
                    fromNodeIdString));
                auto findNeighbor = cutFaceNodeLinkMap.find(fromNodeIdString);
                if (findNeighbor == cutFaceNodeLinkMap.end())
                    return;
                for (const auto &it: findNeighbor->second) {
                    if (cutFaceVisitedNodeIds.find(it) == cutFaceVisitedNodeIds.end()) {
                        loopNodeLink(it);
                        break;
                    }
                }
            };
            if (!endPointNodeIdString.isEmpty()) {
                loopNodeLink(endPointNodeIdString);
            }
            // Fetch points from linked nodes
            std::vector<QString> cutTemplateNames;
            cutFacePointsFromNodes(cutTemplate, cutFaceNodes, isRing, &cutTemplateNames);
            //for (size_t i = 0; i < cutTemplateNames.size(); ++i) {
            //    cutTemplateMapByName.insert({cutTemplateNames[i], cutTemplate[i]});
            //}
        }
    }
    if (cutTemplate.size() < 3) {
        CutFace cutFace = CutFaceFromString(cutFaceString.toUtf8().constData());
        cutTemplate = CutFaceToPoints(cutFace);
        //cutTemplateMapByName.clear();
        //for (size_t i = 0; i < cutTemplate.size(); ++i) {
        //    cutTemplateMapByName.insert({cutFaceString + "/" + QString::number(i + 1), cutTemplate[i]});
        //}
    }
}

MeshCombiner::Mesh *MeshGenerator::combinePartMesh(const QString &partIdString, bool *hasError, bool *retryable, bool addIntermediateNodes)
{
    auto findPart = m_snapshot->parts.find(partIdString);
    if (findPart == m_snapshot->parts.end()) {
        qDebug() << "Find part failed:" << partIdString;
        return nullptr;
    }
    
    QUuid partId = QUuid(partIdString);
    auto &part = findPart->second;
    
    *retryable = true;
    
    bool isDisabled = isTrueValueString(valueOfKeyInMapOrEmpty(part, "disabled"));
    //bool xMirrored = isTrueValueString(valueOfKeyInMapOrEmpty(part, "xMirrored"));
    //bool _xFlip = isTrueValueString(valueOfKeyInMapOrEmpty(part, "_xFlip"));
    QString __mirroredByPartId = valueOfKeyInMapOrEmpty(part, "__mirroredByPartId");
    QString __mirrorFromPartId = valueOfKeyInMapOrEmpty(part, "__mirrorFromPartId");
    bool subdived = isTrueValueString(valueOfKeyInMapOrEmpty(part, "subdived"));
    bool rounded = isTrueValueString(valueOfKeyInMapOrEmpty(part, "rounded"));
    bool chamfered = isTrueValueString(valueOfKeyInMapOrEmpty(part, "chamfered"));
    bool countershaded = isTrueValueString(valueOfKeyInMapOrEmpty(part, "countershaded"));
    bool smooth = isTrueValueString(valueOfKeyInMapOrEmpty(part, "smooth"));
    QString colorString = valueOfKeyInMapOrEmpty(part, "color");
    QColor partColor = colorString.isEmpty() ? m_defaultPartColor : QColor(colorString);
    float deformThickness = 1.0;
    float deformWidth = 1.0;
    float cutRotation = 0.0;
    float hollowThickness = 0.0;
    auto target = PartTargetFromString(valueOfKeyInMapOrEmpty(part, "target").toUtf8().constData());
    auto base = PartBaseFromString(valueOfKeyInMapOrEmpty(part, "base").toUtf8().constData());
    
    QString searchPartIdString = __mirrorFromPartId.isEmpty() ? partIdString : __mirrorFromPartId;

    QString cutFaceString = valueOfKeyInMapOrEmpty(part, "cutFace");
    std::vector<QVector2D> cutTemplate;
    cutFaceStringToCutTemplate(cutFaceString, cutTemplate);
    if (chamfered)
        chamferFace2D(&cutTemplate);
    
    QString cutRotationString = valueOfKeyInMapOrEmpty(part, "cutRotation");
    if (!cutRotationString.isEmpty()) {
        cutRotation = cutRotationString.toFloat();
    }
    
    QString hollowThicknessString = valueOfKeyInMapOrEmpty(part, "hollowThickness");
    if (!hollowThicknessString.isEmpty()) {
        hollowThickness = hollowThicknessString.toFloat();
    }
    
    QString thicknessString = valueOfKeyInMapOrEmpty(part, "deformThickness");
    if (!thicknessString.isEmpty()) {
        deformThickness = thicknessString.toFloat();
    }
    
    QString widthString = valueOfKeyInMapOrEmpty(part, "deformWidth");
    if (!widthString.isEmpty()) {
        deformWidth = widthString.toFloat();
    }
    
    bool deformUnified = isTrueValueString(valueOfKeyInMapOrEmpty(part, "deformUnified"));
    
    QImage deformImageStruct;
    const QImage *deformImage = nullptr;
    QString deformMapImageIdString = valueOfKeyInMapOrEmpty(part, "deformMapImageId");
    if (!deformMapImageIdString.isEmpty()) {
        ImageForever::copy(QUuid(deformMapImageIdString), deformImageStruct);
        if (!deformImageStruct.isNull())
            deformImage = &deformImageStruct;
        if (nullptr == deformImage) {
            qDebug() << "Deform image id not found:" << deformMapImageIdString;
        }
    }
    
    float deformMapScale = 1.0;
    QString deformMapScaleString = valueOfKeyInMapOrEmpty(part, "deformMapScale");
    if (!deformMapScaleString.isEmpty())
        deformMapScale = deformMapScaleString.toFloat();
    
    QUuid materialId;
    QString materialIdString = valueOfKeyInMapOrEmpty(part, "materialId");
    if (!materialIdString.isEmpty())
        materialId = QUuid(materialIdString);
    
    float colorSolubility = 0;
    QString colorSolubilityString = valueOfKeyInMapOrEmpty(part, "colorSolubility");
    if (!colorSolubilityString.isEmpty())
        colorSolubility = colorSolubilityString.toFloat();
    
    float metalness = 0;
    QString metalnessString = valueOfKeyInMapOrEmpty(part, "metallic");
    if (!metalnessString.isEmpty())
        metalness = metalnessString.toFloat();
    
    float roughness = 1.0;
    QString roughnessString = valueOfKeyInMapOrEmpty(part, "roughness");
    if (!roughnessString.isEmpty())
        roughness = roughnessString.toFloat();
    
    QUuid fillMeshFileId;
    QString fillMeshString = valueOfKeyInMapOrEmpty(part, "fillMesh");
    if (!fillMeshString.isEmpty()) {
        fillMeshFileId = QUuid(fillMeshString);
        if (!fillMeshFileId.isNull()) {
            *retryable = false;
            //xMirrored = false;
        }
    }
    
    auto &partCache = m_cacheContext->parts[partIdString];
    partCache.objectNodes.clear();
    partCache.objectEdges.clear();
    partCache.objectNodeVertices.clear();
    partCache.vertices.clear();
    partCache.faces.clear();
    partCache.previewTriangles.clear();
    partCache.previewVertices.clear();
    partCache.isSuccessful = false;
    partCache.joined = (target == PartTarget::Model && !isDisabled);
    partCache.releaseMeshes();
    
    struct NodeInfo
    {
        float radius = 0;
        QVector3D position;
        BoneMark boneMark = BoneMark::None;
        bool hasCutFaceSettings = false;
        float cutRotation = 0.0;
        QString cutFace;
    };
    std::map<QString, NodeInfo> nodeInfos;
    for (const auto &nodeIdString: m_partNodeIds[searchPartIdString]) {
        auto findNode = m_snapshot->nodes.find(nodeIdString);
        if (findNode == m_snapshot->nodes.end()) {
            qDebug() << "Find node failed:" << nodeIdString;
            continue;
        }
        auto &node = findNode->second;
        
        float radius = valueOfKeyInMapOrEmpty(node, "radius").toFloat();
        float x = (valueOfKeyInMapOrEmpty(node, "x").toFloat() - m_mainProfileMiddleX);
        float y = (m_mainProfileMiddleY - valueOfKeyInMapOrEmpty(node, "y").toFloat());
        float z = (m_sideProfileMiddleX - valueOfKeyInMapOrEmpty(node, "z").toFloat());

        BoneMark boneMark = BoneMarkFromString(valueOfKeyInMapOrEmpty(node, "boneMark").toUtf8().constData());
        
        bool hasCutFaceSettings = false;
        float cutRotation = 0.0;
        QString cutFace;
        
        const auto &cutFaceIt = node.find("cutFace");
        if (cutFaceIt != node.end()) {
            cutFace = cutFaceIt->second;
            hasCutFaceSettings = true;
            const auto &cutRotationIt = node.find("cutRotation");
            if (cutRotationIt != node.end()) {
                cutRotation = cutRotationIt->second.toFloat();
            }
        }
        
        auto &nodeInfo = nodeInfos[nodeIdString];
        nodeInfo.position = QVector3D(x, y, z);
        nodeInfo.radius = radius;
        nodeInfo.boneMark = boneMark;
        nodeInfo.hasCutFaceSettings = hasCutFaceSettings;
        nodeInfo.cutRotation = cutRotation;
        nodeInfo.cutFace = cutFace;
    }
    
    std::set<std::pair<QString, QString>> edges;
    for (const auto &edgeIdString: m_partEdgeIds[searchPartIdString]) {
        auto findEdge = m_snapshot->edges.find(edgeIdString);
        if (findEdge == m_snapshot->edges.end()) {
            qDebug() << "Find edge failed:" << edgeIdString;
            continue;
        }
        auto &edge = findEdge->second;
        
        QString fromNodeIdString = valueOfKeyInMapOrEmpty(edge, "from");
        QString toNodeIdString = valueOfKeyInMapOrEmpty(edge, "to");
        
        const auto &findFromNodeInfo = nodeInfos.find(fromNodeIdString);
        if (findFromNodeInfo == nodeInfos.end()) {
            qDebug() << "Find from-node info failed:" << fromNodeIdString;
            continue;
        }
        
        const auto &findToNodeInfo = nodeInfos.find(toNodeIdString);
        if (findToNodeInfo == nodeInfos.end()) {
            qDebug() << "Find to-node info failed:" << toNodeIdString;
            continue;
        }
        
        edges.insert({fromNodeIdString, toNodeIdString});
    }
    
    bool buildSucceed = false;
    std::map<QString, int> nodeIdStringToIndexMap;
    std::map<int, QString> nodeIndexToIdStringMap;
    StrokeModifier *strokeModifier = nullptr;
    
    //QString mirroredPartIdString;
    //QUuid mirroredPartId;
    //if (xMirrored) {
    //    mirroredPartId = QUuid().createUuid();
    //    mirroredPartIdString = mirroredPartId.toString();
    //    m_cacheContext->partMirrorIdMap[mirroredPartIdString] = partIdString;
    //}
    
    auto addNodeToPartCache = [&](const QString &nodeIdString, const NodeInfo &nodeInfo) {
        ObjectNode objectNode;
        objectNode.partId = QUuid(partIdString);
        objectNode.nodeId = QUuid(nodeIdString);
        objectNode.origin = nodeInfo.position;
        objectNode.radius = nodeInfo.radius;
        objectNode.color = partColor;
        objectNode.materialId = materialId;
        objectNode.countershaded = countershaded;
        objectNode.colorSolubility = colorSolubility;
        objectNode.metalness = metalness;
        objectNode.roughness = roughness;
        objectNode.boneMark = nodeInfo.boneMark;
        if (!__mirroredByPartId.isEmpty())
            objectNode.mirroredByPartId = QUuid(__mirroredByPartId);
        if (!__mirrorFromPartId.isEmpty()) {
            objectNode.mirrorFromPartId = QUuid(__mirrorFromPartId);
            objectNode.origin.setX(-nodeInfo.position.x());
        }
        objectNode.joined = partCache.joined;
        partCache.objectNodes.push_back(objectNode);
        //if (xMirrored) {
        //    objectNode.partId = mirroredPartId;
        //    objectNode.mirrorFromPartId = QUuid(partId);
        //    objectNode.mirroredByPartId = QUuid();
        //    objectNode.origin.setX(-nodeInfo.position.x());
        //    partCache.objectNodes.push_back(objectNode);
        //}
    };
    auto addEdgeToPartCache = [&](const QString &firstNodeIdString, const QString &secondNodeIdString) {
        partCache.objectEdges.push_back({
            {QUuid(partIdString), QUuid(firstNodeIdString)},
            {QUuid(partIdString), QUuid(secondNodeIdString)}
        });
        //if (xMirrored) {
        //    partCache.objectEdges.push_back({
        //        {mirroredPartId, QUuid(firstNodeIdString)},
        //        {mirroredPartId, QUuid(secondNodeIdString)}
        //    });
        //}
    };
    
    strokeModifier = new StrokeModifier;
    
    if (smooth)
        strokeModifier->enableSmooth();
    if (addIntermediateNodes)
        strokeModifier->enableIntermediateAddition();
    
    for (const auto &nodeIt: nodeInfos) {
        const auto &nodeIdString = nodeIt.first;
        const auto &nodeInfo = nodeIt.second;
        size_t nodeIndex = 0;
        if (nodeInfo.hasCutFaceSettings) {
            std::vector<QVector2D> nodeCutTemplate;
            cutFaceStringToCutTemplate(nodeInfo.cutFace, nodeCutTemplate);
            if (chamfered)
                chamferFace2D(&nodeCutTemplate);
            nodeIndex = strokeModifier->addNode(nodeInfo.position, nodeInfo.radius, nodeCutTemplate, nodeInfo.cutRotation);
        } else {
            nodeIndex = strokeModifier->addNode(nodeInfo.position, nodeInfo.radius, cutTemplate, cutRotation);
        }
        nodeIdStringToIndexMap[nodeIdString] = nodeIndex;
        nodeIndexToIdStringMap[nodeIndex] = nodeIdString;
    }
    
    for (const auto &edgeIt: edges) {
        const QString &fromNodeIdString = edgeIt.first;
        const QString &toNodeIdString = edgeIt.second;
        
        auto findFromNodeIndex = nodeIdStringToIndexMap.find(fromNodeIdString);
        if (findFromNodeIndex == nodeIdStringToIndexMap.end()) {
            qDebug() << "Find from-node failed:" << fromNodeIdString;
            continue;
        }
        
        auto findToNodeIndex = nodeIdStringToIndexMap.find(toNodeIdString);
        if (findToNodeIndex == nodeIdStringToIndexMap.end()) {
            qDebug() << "Find to-node failed:" << toNodeIdString;
            continue;
        }
        
        strokeModifier->addEdge(findFromNodeIndex->second, findToNodeIndex->second);
    }
    
    if (subdived)
        strokeModifier->subdivide();
    
    if (rounded)
        strokeModifier->roundEnd();
    
    strokeModifier->finalize();
    
    std::vector<size_t> sourceNodeIndices;
    
    StrokeMeshBuilder *strokeMeshBuilder = new StrokeMeshBuilder;
        
    strokeMeshBuilder->setDeformThickness(deformThickness);
    strokeMeshBuilder->setDeformWidth(deformWidth);
    strokeMeshBuilder->setDeformMapScale(deformMapScale);
    strokeMeshBuilder->setDeformUnified(deformUnified);
    strokeMeshBuilder->setHollowThickness(hollowThickness);
    if (nullptr != deformImage)
        strokeMeshBuilder->setDeformMapImage(deformImage);
    if (PartBase::YZ == base) {
        strokeMeshBuilder->enableBaseNormalOnX(false);
    } else if (PartBase::Average == base) {
        strokeMeshBuilder->enableBaseNormalAverage(true);
    } else if (PartBase::XY == base) {
        strokeMeshBuilder->enableBaseNormalOnZ(false);
    } else if (PartBase::ZX == base) {
        strokeMeshBuilder->enableBaseNormalOnY(false);
    }
    
    for (const auto &node: strokeModifier->nodes()) {
        auto nodeIndex = strokeMeshBuilder->addNode(node.position, node.radius, node.cutTemplate, node.cutRotation);
        strokeMeshBuilder->setNodeOriginInfo(nodeIndex, node.nearOriginNodeIndex, node.farOriginNodeIndex);
    }
    for (const auto &edge: strokeModifier->edges())
        strokeMeshBuilder->addEdge(edge.firstNodeIndex, edge.secondNodeIndex);
    
    if (fillMeshFileId.isNull()) {
        for (const auto &nodeIt: nodeInfos) {
            const auto &nodeIdString = nodeIt.first;
            const auto &nodeInfo = nodeIt.second;
            addNodeToPartCache(nodeIdString, nodeInfo);
        }
        
        for (const auto &edgeIt: edges) {
            const QString &fromNodeIdString = edgeIt.first;
            const QString &toNodeIdString = edgeIt.second;
            addEdgeToPartCache(fromNodeIdString, toNodeIdString);
        }

        buildSucceed = strokeMeshBuilder->build();
        
        partCache.vertices = strokeMeshBuilder->generatedVertices();
        partCache.faces = strokeMeshBuilder->generatedFaces();
        if (!__mirrorFromPartId.isEmpty()) {
            for (auto &it: partCache.vertices)
                it.setX(-it.x());
            for (auto &it: partCache.faces)
                std::reverse(it.begin(), it.end());
        }
        sourceNodeIndices = strokeMeshBuilder->generatedVerticesSourceNodeIndices();
        for (size_t i = 0; i < partCache.vertices.size(); ++i) {
            const auto &position = partCache.vertices[i];
            const auto &source = strokeMeshBuilder->generatedVerticesSourceNodeIndices()[i];
            size_t nodeIndex = strokeModifier->nodes()[source].originNodeIndex;
            const auto &nodeIdString = nodeIndexToIdStringMap[nodeIndex];
            partCache.objectNodeVertices.push_back({position, {partIdString, nodeIdString}});
        }
    } else {
        if (strokeMeshBuilder->buildBaseNormalsOnly()) {
            buildSucceed = fillPartWithMesh(partCache, fillMeshFileId, 
                deformThickness, deformWidth, cutRotation, strokeMeshBuilder);
            if (!__mirrorFromPartId.isEmpty()) {
                for (auto &it: partCache.vertices)
                    it.setX(-it.x());
                for (auto &it: partCache.faces)
                    std::reverse(it.begin(), it.end());
            }
        }
    }
    
    delete strokeMeshBuilder;
    strokeMeshBuilder = nullptr;
    
    bool hasMeshError = false;
    MeshCombiner::Mesh *mesh = nullptr;
    
    if (buildSucceed) {
        mesh = new MeshCombiner::Mesh(partCache.vertices, partCache.faces, false);
        if (mesh->isNull()) {
            hasMeshError = true;
            qDebug() << "Mesh built is uncombinable";
        }
    } else {
        hasMeshError = true;
        qDebug() << "Mesh build failed";
    }
    
    std::vector<QVector3D> partPreviewVertices;
    QColor partPreviewColor = partColor;
    if (nullptr != mesh) {
        partCache.mesh = new MeshCombiner::Mesh(*mesh);
        mesh->fetch(partPreviewVertices, partCache.previewTriangles);
        partCache.previewVertices = partPreviewVertices;
        partCache.isSuccessful = true;
    }
    if (partCache.previewTriangles.empty()) {
        partPreviewVertices = partCache.vertices;
        triangulateFacesWithoutKeepVertices(partPreviewVertices, partCache.faces, partCache.previewTriangles);
        partCache.previewVertices = partPreviewVertices;
        partPreviewColor = Qt::red;
        partCache.isSuccessful = false;
    }
    
    trim(&partPreviewVertices, true);
    for (auto &it: partPreviewVertices) {
        it *= 2.0;
    }
    std::vector<QVector3D> partPreviewTriangleNormals;
    for (const auto &face: partCache.previewTriangles) {
        partPreviewTriangleNormals.push_back(QVector3D::normal(
            partPreviewVertices[face[0]],
            partPreviewVertices[face[1]],
            partPreviewVertices[face[2]]
        ));
    }
    std::vector<std::vector<QVector3D>> partPreviewTriangleVertexNormals;
    generateSmoothTriangleVertexNormals(partPreviewVertices,
        partCache.previewTriangles,
        partPreviewTriangleNormals,
        &partPreviewTriangleVertexNormals);
    if (!partCache.previewTriangles.empty()) {
        if (PartTarget::CutFace == target) {
            std::vector<QVector2D> cutTemplate;
            cutFaceStringToCutTemplate(partIdString, cutTemplate);
            m_partPreviewImages[partId] = buildCutFaceTemplatePreviewImage(cutTemplate);
            m_generatedPreviewImagePartIds.insert(partId);
        } else {
            m_partPreviewMeshes[partId] = new Model(partPreviewVertices,
                partCache.previewTriangles,
                partPreviewTriangleVertexNormals,
                partPreviewColor,
                metalness,
                roughness);
            m_generatedPreviewPartIds.insert(partId);
        }
        /*
        if (target == PartTarget::CutFace)
            partPreviewColor = Theme::red;
        m_partPreviewMeshes[partId] = new Model(partPreviewVertices,
            partCache.previewTriangles,
            partPreviewTriangleVertexNormals,
            partPreviewColor,
            metalness,
            roughness);
        */
    }
    
    delete strokeModifier;
    
    if (mesh && mesh->isNull()) {
        delete mesh;
        mesh = nullptr;
    }
    
    if (isDisabled) {
        delete mesh;
        mesh = nullptr;
    }
    
    if (target != PartTarget::Model) {
        delete mesh;
        mesh = nullptr;
    }
    
    if (hasMeshError && target == PartTarget::Model) {
        *hasError = true;
    }
    
    return mesh;
}

bool MeshGenerator::fillPartWithMesh(GeneratedPart &partCache, 
    const QUuid &fillMeshFileId,
    float deformThickness,
    float deformWidth,
    float cutRotation,
    const StrokeMeshBuilder *strokeMeshBuilder)
{
    bool fillIsSucessful = false;
    const QByteArray *fillMeshByteArray = FileForever::getContent(fillMeshFileId);
    if (nullptr == fillMeshByteArray)
        return false;
    
    QXmlStreamReader fillMeshStream(*fillMeshByteArray);  
    Snapshot *fillMeshSnapshot = new Snapshot;
    loadSkeletonFromXmlStream(fillMeshSnapshot, fillMeshStream);
    
    GeneratedCacheContext *fillMeshCacheContext = new GeneratedCacheContext();
    MeshGenerator *meshGenerator = new MeshGenerator(fillMeshSnapshot);
    meshGenerator->setWeldEnabled(false);
    meshGenerator->setGeneratedCacheContext(fillMeshCacheContext);
    meshGenerator->generate();
    fillIsSucessful = meshGenerator->isSuccessful();
    Object *object = meshGenerator->takeObject();
    if (nullptr != object) {
        MeshStroketifier stroketifier;
        std::vector<MeshStroketifier::Node> strokeNodes;
        for (const auto &nodeIndex: strokeMeshBuilder->nodeIndices()) {
            const auto &node = strokeMeshBuilder->nodes()[nodeIndex];
            MeshStroketifier::Node strokeNode;
            strokeNode.position = node.position;
            strokeNode.radius = node.radius;
            strokeNodes.push_back(strokeNode);
        }
        stroketifier.setCutRotation(cutRotation);
        stroketifier.setDeformWidth(deformWidth);
        stroketifier.setDeformThickness(deformThickness);
        if (stroketifier.prepare(strokeNodes, object->vertices)) {
            stroketifier.stroketify(&object->vertices);
            std::vector<MeshStroketifier::Node> agentNodes(object->nodes.size());
            for (size_t i = 0; i < object->nodes.size(); ++i) {
                auto &dest = agentNodes[i];
                const auto &src = object->nodes[i];
                dest.position = src.origin;
                dest.radius = src.radius;
            }
            stroketifier.stroketify(&agentNodes);
            for (size_t i = 0; i < object->nodes.size(); ++i) {
                const auto &src = agentNodes[i];
                auto &dest = object->nodes[i];
                dest.origin = src.position;
                dest.radius = src.radius;
            }
        }
        partCache.objectNodes.insert(partCache.objectNodes.end(), object->nodes.begin(), object->nodes.end());
        partCache.objectEdges.insert(partCache.objectEdges.end(), object->edges.begin(), object->edges.end());
        partCache.vertices.insert(partCache.vertices.end(), object->vertices.begin(), object->vertices.end());
        if (!strokeNodes.empty()) {
            for (auto &it: partCache.vertices)
                it += strokeNodes.front().position;
        }
        for (size_t i = 0; i < object->vertexSourceNodes.size(); ++i)
            partCache.objectNodeVertices.push_back({partCache.vertices[i], object->vertexSourceNodes[i]});
        partCache.faces.insert(partCache.faces.end(), object->triangleAndQuads.begin(), object->triangleAndQuads.end());
        fillIsSucessful = true;
    }
    delete object;
    delete meshGenerator;
    delete fillMeshCacheContext;

    return fillIsSucessful;
}

const std::map<QString, QString> *MeshGenerator::findComponent(const QString &componentIdString)
{
    const std::map<QString, QString> *component = &m_snapshot->rootComponent;
    if (componentIdString != QUuid().toString()) {
        auto findComponent = m_snapshot->components.find(componentIdString);
        if (findComponent == m_snapshot->components.end()) {
            qDebug() << "Component not found:" << componentIdString;
            return nullptr;
        }
        return &findComponent->second;
    }
    return component;
}

CombineMode MeshGenerator::componentCombineMode(const std::map<QString, QString> *component)
{
    if (nullptr == component)
        return CombineMode::Normal;
    CombineMode combineMode = CombineModeFromString(valueOfKeyInMapOrEmpty(*component, "combineMode").toUtf8().constData());
    if (combineMode == CombineMode::Normal) {
        if (isTrueValueString(valueOfKeyInMapOrEmpty(*component, "inverse")))
            combineMode = CombineMode::Inversion;
    }
    return combineMode;
}

QString MeshGenerator::componentColorName(const std::map<QString, QString> *component)
{
    if (nullptr == component)
        return QString();
    QString linkDataType = valueOfKeyInMapOrEmpty(*component, "linkDataType");
    if ("partId" == linkDataType) {
        QString partIdString = valueOfKeyInMapOrEmpty(*component, "linkData");
        auto findPart = m_snapshot->parts.find(partIdString);
        if (findPart == m_snapshot->parts.end()) {
            qDebug() << "Find part failed:" << partIdString;
            return QString();
        }
        auto &part = findPart->second;
        QString colorSolubility = valueOfKeyInMapOrEmpty(part, "colorSolubility");
        if (!colorSolubility.isEmpty()) {
            return QString("+");
        }
        QString colorName = valueOfKeyInMapOrEmpty(part, "color");
        if (colorName.isEmpty())
            return QString("-");
        return colorName;
    }
    return QString();
}

MeshCombiner::Mesh *MeshGenerator::combineComponentMesh(const QString &componentIdString, CombineMode *combineMode)
{
    MeshCombiner::Mesh *mesh = nullptr;
    
    QUuid componentId;
    const std::map<QString, QString> *component = &m_snapshot->rootComponent;
    if (componentIdString != QUuid().toString()) {
        componentId = QUuid(componentIdString);
        auto findComponent = m_snapshot->components.find(componentIdString);
        if (findComponent == m_snapshot->components.end()) {
            qDebug() << "Component not found:" << componentIdString;
            return nullptr;
        }
        component = &findComponent->second;
    }

    *combineMode = componentCombineMode(component);
    
    auto &componentCache = m_cacheContext->components[componentIdString];
    
    if (m_cacheEnabled) {
        if (m_dirtyComponentIds.find(componentIdString) == m_dirtyComponentIds.end()) {
            if (nullptr != componentCache.mesh)
                return new MeshCombiner::Mesh(*componentCache.mesh);
        }
    }
    
    componentCache.sharedQuadEdges.clear();
    componentCache.noneSeamVertices.clear();
    componentCache.objectNodes.clear();
    componentCache.objectEdges.clear();
    componentCache.objectNodeVertices.clear();
    componentCache.releaseMeshes();
    
    QString linkDataType = valueOfKeyInMapOrEmpty(*component, "linkDataType");
    if ("partId" == linkDataType) {
        QString partIdString = valueOfKeyInMapOrEmpty(*component, "linkData");
        bool hasError = false;
        bool retryable = true;
        mesh = combinePartMesh(partIdString, &hasError, &retryable, m_interpolationEnabled);
        if (hasError) {
            delete mesh;
            mesh = nullptr;
            if (retryable && m_interpolationEnabled) {
                hasError = false;
                qDebug() << "Try combine part again without adding intermediate nodes";
                mesh = combinePartMesh(partIdString, &hasError, &retryable, false);
            }
            if (hasError) {
                m_isSuccessful = false;
            }
        }
        
        const auto &partCache = m_cacheContext->parts[partIdString];
        for (const auto &vertex: partCache.vertices)
            componentCache.noneSeamVertices.insert(vertex);
        collectSharedQuadEdges(partCache.vertices, partCache.faces, &componentCache.sharedQuadEdges);
        for (const auto &it: partCache.objectNodes)
            componentCache.objectNodes.push_back(it);
        for (const auto &it: partCache.objectEdges)
            componentCache.objectEdges.push_back(it);
        for (const auto &it: partCache.objectNodeVertices)
            componentCache.objectNodeVertices.push_back(it);
    } else {
        std::vector<std::pair<CombineMode, std::vector<std::pair<QString, QString>>>> combineGroups;
        // Firstly, group by combine mode
        int currentGroupIndex = -1;
        auto lastCombineMode = CombineMode::Count;
        bool foundColorSolubilitySetting = false;
        for (const auto &childIdString: valueOfKeyInMapOrEmpty(*component, "children").split(",")) {
            if (childIdString.isEmpty())
                continue;
            const auto &child = findComponent(childIdString);
            QString colorName = componentColorName(child);
            if (colorName == "+") {
                foundColorSolubilitySetting = true;
            }
            auto combineMode = componentCombineMode(child);
            if (lastCombineMode != combineMode || lastCombineMode == CombineMode::Inversion) {
                combineGroups.push_back({combineMode, {}});
                ++currentGroupIndex;
                lastCombineMode = combineMode;
                qDebug() << "New group[" << currentGroupIndex << "] for combine mode[" << CombineModeToString(combineMode) << "]";
            }
            if (-1 == currentGroupIndex) {
                qDebug() << "Should not happen: -1 == currentGroupIndex";
                continue;
            }
            combineGroups[currentGroupIndex].second.push_back({childIdString, colorName});
        }
        // Secondly, sub group by color
        std::vector<std::tuple<MeshCombiner::Mesh *, CombineMode, QString>> groupMeshes;
        for (const auto &group: combineGroups) {
            std::set<size_t> used;
            std::vector<std::vector<QString>> componentIdStrings;
            int currentSubGroupIndex = -1;
            auto lastColorName = QString();
            for (size_t i = 0; i < group.second.size(); ++i) {
                if (used.find(i) != used.end())
                    continue;
                //const auto &colorName = group.second[i].second;
                const QString colorName = "white"; // Force to use the same color = deactivate combine by color
                if (lastColorName != colorName || lastColorName.isEmpty()) {
                    //qDebug() << "New sub group[" << currentSubGroupIndex << "] for color[" << colorName << "]";
                    componentIdStrings.push_back({});
                    ++currentSubGroupIndex;
                    lastColorName = colorName;
                }
                if (-1 == currentSubGroupIndex) {
                    qDebug() << "Should not happen: -1 == currentSubGroupIndex";
                    continue;
                }
                used.insert(i);
                componentIdStrings[currentSubGroupIndex].push_back(group.second[i].first);
                if (colorName.isEmpty())
                    continue;
                for (size_t j = i + 1; j < group.second.size(); ++j) {
                    if (used.find(j) != used.end())
                        continue;
                    const auto &otherColorName = group.second[j].second;
                    if (otherColorName.isEmpty())
                        continue;
                    if (otherColorName != colorName)
                        continue;
                    used.insert(j);
                    componentIdStrings[currentSubGroupIndex].push_back(group.second[j].first);
                }
            }
            std::vector<std::tuple<MeshCombiner::Mesh *, CombineMode, QString>> multipleMeshes;
            QStringList subGroupMeshIdStringList;
            for (const auto &it: componentIdStrings) {
                QStringList componentChildGroupIdStringList;
                for (const auto &componentChildGroupIdString: it)
                    componentChildGroupIdStringList += componentChildGroupIdString;
                MeshCombiner::Mesh *childMesh = combineComponentChildGroupMesh(it, componentCache);
                if (nullptr == childMesh)
                    continue;
                if (childMesh->isNull()) {
                    delete childMesh;
                    continue;
                }
                QString componentChildGroupIdStringListString = componentChildGroupIdStringList.join("|");
                subGroupMeshIdStringList += componentChildGroupIdStringListString;
                multipleMeshes.push_back(std::make_tuple(childMesh, CombineMode::Normal, componentChildGroupIdStringListString));
            }
            MeshCombiner::Mesh *subGroupMesh = combineMultipleMeshes(multipleMeshes, true/*foundColorSolubilitySetting*/);
            if (nullptr == subGroupMesh)
                continue;
            groupMeshes.push_back(std::make_tuple(subGroupMesh, group.first, subGroupMeshIdStringList.join("&")));
        }
        mesh = combineMultipleMeshes(groupMeshes, true);
    }
    
    if (nullptr != mesh)
        componentCache.mesh = new MeshCombiner::Mesh(*mesh);
    
    if (nullptr != mesh && mesh->isNull()) {
        delete mesh;
        mesh = nullptr;
    }
    
    if (componentId.isNull()) {
        // Prepare cloth collision shap
        if (nullptr != mesh && !mesh->isNull()) {
            m_clothCollisionVertices.clear();
            m_clothCollisionTriangles.clear();
            mesh->fetch(m_clothCollisionVertices, m_clothCollisionTriangles);
        } else {
            // TODO: when no body is valid, may add ground plane as collision shape
            // ... ...
        }
    }
    
    return mesh;
}

MeshCombiner::Mesh *MeshGenerator::combineMultipleMeshes(const std::vector<std::tuple<MeshCombiner::Mesh *, CombineMode, QString>> &multipleMeshes, bool recombine)
{
    MeshCombiner::Mesh *mesh = nullptr;
    QString meshIdStrings;
    for (const auto &it: multipleMeshes) {
        const auto &childCombineMode = std::get<1>(it);
        MeshCombiner::Mesh *subMesh = std::get<0>(it);
        const QString &subMeshIdString = std::get<2>(it);
        //qDebug() << "Combine mode:" << CombineModeToString(childCombineMode);
        if (nullptr == subMesh || subMesh->isNull()) {
            delete subMesh;
            qDebug() << "Child mesh is null";
            continue;
        }
        if (!subMesh->isCombinable()) {
            qDebug() << "Child mesh is uncombinable";
            // TODO: Collect vertices
            delete subMesh;
            continue;
        }
        if (nullptr == mesh) {
            mesh = subMesh;
            meshIdStrings = subMeshIdString;
        } else {
            auto combinerMethod = childCombineMode == CombineMode::Inversion ?
                    MeshCombiner::Method::Diff : MeshCombiner::Method::Union;
            auto combinerMethodString = combinerMethod == MeshCombiner::Method::Union ?
                "+" : "-";
            meshIdStrings += combinerMethodString + subMeshIdString;
            if (recombine)
                meshIdStrings += "!";
            MeshCombiner::Mesh *newMesh = nullptr;
            auto findCached = m_cacheContext->cachedCombination.find(meshIdStrings);
            if (findCached != m_cacheContext->cachedCombination.end()) {
                if (nullptr != findCached->second) {
                    //qDebug() << "Use cached combination:" << meshIdStrings;
                    newMesh = new MeshCombiner::Mesh(*findCached->second);
                }
            } else {
                newMesh = combineTwoMeshes(*mesh,
                    *subMesh,
                    combinerMethod,
                    recombine);
                delete subMesh;
                if (nullptr != newMesh)
                    m_cacheContext->cachedCombination.insert({meshIdStrings, new MeshCombiner::Mesh(*newMesh)});
                else
                    m_cacheContext->cachedCombination.insert({meshIdStrings, nullptr});
                //qDebug() << "Add cached combination:" << meshIdStrings;
            }
            if (newMesh && !newMesh->isNull()) {
                delete mesh;
                mesh = newMesh;
            } else {
                m_isSuccessful = false;
                qDebug() << "Mesh combine failed";
                delete newMesh;
            }
        }
    }
    if (nullptr != mesh && mesh->isNull()) {
        delete mesh;
        mesh = nullptr;
    }
    return mesh;
}

MeshCombiner::Mesh *MeshGenerator::combineComponentChildGroupMesh(const std::vector<QString> &componentIdStrings, GeneratedComponent &componentCache)
{
    std::vector<std::tuple<MeshCombiner::Mesh *, CombineMode, QString>> multipleMeshes;
    for (const auto &childIdString: componentIdStrings) {
        CombineMode childCombineMode = CombineMode::Normal;
        MeshCombiner::Mesh *subMesh = combineComponentMesh(childIdString, &childCombineMode);
        
        if (CombineMode::Uncombined == childCombineMode) {
            delete subMesh;
            continue;
        }
        
        const auto &childComponentCache = m_cacheContext->components[childIdString];
        for (const auto &vertex: childComponentCache.noneSeamVertices)
            componentCache.noneSeamVertices.insert(vertex);
        for (const auto &it: childComponentCache.sharedQuadEdges)
            componentCache.sharedQuadEdges.insert(it);
        for (const auto &it: childComponentCache.objectNodes)
            componentCache.objectNodes.push_back(it);
        for (const auto &it: childComponentCache.objectEdges)
            componentCache.objectEdges.push_back(it);
        for (const auto &it: childComponentCache.objectNodeVertices)
            componentCache.objectNodeVertices.push_back(it);
        
        if (nullptr == subMesh || subMesh->isNull()) {
            delete subMesh;
            continue;
        }
        
        if (!subMesh->isCombinable()) {
            componentCache.incombinableMeshes.push_back(subMesh);
            continue;
        }
    
        multipleMeshes.push_back(std::make_tuple(subMesh, childCombineMode, childIdString));
    }
    return combineMultipleMeshes(multipleMeshes);
}

MeshCombiner::Mesh *MeshGenerator::combineTwoMeshes(const MeshCombiner::Mesh &first, const MeshCombiner::Mesh &second,
    MeshCombiner::Method method,
    bool recombine)
{
    if (first.isNull() || second.isNull())
        return nullptr;
    std::vector<std::pair<MeshCombiner::Source, size_t>> combinedVerticesSources;
    MeshCombiner::Mesh *newMesh = MeshCombiner::combine(first,
        second,
        method,
        &combinedVerticesSources);
    if (nullptr == newMesh)
        return nullptr;
    if (!newMesh->isNull() && recombine) {
        MeshRecombiner recombiner;
        std::vector<QVector3D> combinedVertices;
        std::vector<std::vector<size_t>> combinedFaces;
        newMesh->fetch(combinedVertices, combinedFaces);
        recombiner.setVertices(&combinedVertices, &combinedVerticesSources);
        recombiner.setFaces(&combinedFaces);
        if (recombiner.recombine()) {
            if (isManifold(recombiner.regeneratedFaces())) {
                MeshCombiner::Mesh *reMesh = new MeshCombiner::Mesh(recombiner.regeneratedVertices(), recombiner.regeneratedFaces(), false);
                if (!reMesh->isNull() && reMesh->isCombinable()) {
                    delete newMesh;
                    newMesh = reMesh;
                } else {
                    delete reMesh;
                }
            }
        }
    }
    if (newMesh->isNull()) {
        delete newMesh;
        return nullptr;
    }
    return newMesh;
}

void MeshGenerator::makeXmirror(const std::vector<QVector3D> &sourceVertices, const std::vector<std::vector<size_t>> &sourceFaces,
        std::vector<QVector3D> *destVertices, std::vector<std::vector<size_t>> *destFaces)
{
    for (const auto &mirrorFrom: sourceVertices) {
        destVertices->push_back(QVector3D(-mirrorFrom.x(), mirrorFrom.y(), mirrorFrom.z()));
    }
    std::vector<std::vector<size_t>> newFaces;
    for (const auto &mirrorFrom: sourceFaces) {
        auto newFace = mirrorFrom;
        std::reverse(newFace.begin(), newFace.end());
        destFaces->push_back(newFace);
    }
}

void MeshGenerator::collectSharedQuadEdges(const std::vector<QVector3D> &vertices, const std::vector<std::vector<size_t>> &faces,
        std::set<std::pair<PositionKey, PositionKey>> *sharedQuadEdges)
{
    for (const auto &face: faces) {
        if (face.size() != 4)
            continue;
        sharedQuadEdges->insert({
            PositionKey(vertices[face[0]]),
            PositionKey(vertices[face[2]])
        });
        sharedQuadEdges->insert({
            PositionKey(vertices[face[1]]),
            PositionKey(vertices[face[3]])
        });
    }
}

void MeshGenerator::setGeneratedCacheContext(GeneratedCacheContext *cacheContext)
{
    m_cacheContext = cacheContext;
}

void MeshGenerator::setSmoothShadingThresholdAngleDegrees(float degrees)
{
    m_smoothShadingThresholdAngleDegrees = degrees;
}

void MeshGenerator::setInterpolationEnabled(bool interpolationEnabled)
{
    m_interpolationEnabled = interpolationEnabled;
}

void MeshGenerator::process()
{
    generate();
    
    emit finished();
}

void MeshGenerator::setWeldEnabled(bool enabled)
{
    m_weldEnabled = enabled;
}

void MeshGenerator::collectErroredParts()
{
    for (const auto &it: m_cacheContext->parts) {
        if (!it.second.isSuccessful) {
            if (!it.second.joined)
                continue;
            
            auto updateVertexIndices = [=](std::vector<std::vector<size_t>> &faces, size_t vertexStartIndex) {
                for (auto &it: faces) {
                    for (auto &subIt: it)
                        subIt += vertexStartIndex;
                }
            };
            
            auto errorTriangleAndQuads = it.second.faces;
            updateVertexIndices(errorTriangleAndQuads, m_object->vertices.size());
            m_object->vertices.insert(m_object->vertices.end(), it.second.vertices.begin(), it.second.vertices.end());
            m_object->triangleAndQuads.insert(m_object->triangleAndQuads.end(), errorTriangleAndQuads.begin(), errorTriangleAndQuads.end());
            
            auto errorTriangles = it.second.previewTriangles;
            updateVertexIndices(errorTriangles, m_object->vertices.size());
            m_object->vertices.insert(m_object->vertices.end(), it.second.previewVertices.begin(), it.second.previewVertices.end());
            m_object->triangles.insert(m_object->triangles.end(), errorTriangles.begin(), errorTriangles.end());
        }
    }
}

void MeshGenerator::postprocessObject(Object *object) 
{
    std::vector<QVector3D> combinedFacesNormals;
    for (const auto &face: object->triangles) {
        combinedFacesNormals.push_back(QVector3D::normal(
            object->vertices[face[0]],
            object->vertices[face[1]],
            object->vertices[face[2]]
        ));
    }
    
    object->triangleNormals = combinedFacesNormals;
    
    std::vector<std::pair<QUuid, QUuid>> sourceNodes;
    triangleSourceNodeResolve(*object, m_nodeVertices, sourceNodes, &object->vertexSourceNodes);
    object->setTriangleSourceNodes(sourceNodes);
    
    std::map<std::pair<QUuid, QUuid>, QColor> sourceNodeToColorMap;
    for (const auto &node: object->nodes)
        sourceNodeToColorMap.insert({{node.partId, node.nodeId}, node.color});
    
    object->triangleColors.resize(object->triangles.size(), Qt::white);
    const std::vector<std::pair<QUuid, QUuid>> *triangleSourceNodes = object->triangleSourceNodes();
    if (nullptr != triangleSourceNodes) {
        for (size_t triangleIndex = 0; triangleIndex < object->triangles.size(); triangleIndex++) {
            const auto &source = (*triangleSourceNodes)[triangleIndex];
            object->triangleColors[triangleIndex] = sourceNodeToColorMap[source];
        }
    }
    
    std::vector<std::vector<QVector3D>> triangleVertexNormals;
    generateSmoothTriangleVertexNormals(object->vertices,
        object->triangles,
        object->triangleNormals,
        &triangleVertexNormals);
    object->setTriangleVertexNormals(triangleVertexNormals);
}

void MeshGenerator::collectIncombinableComponentMeshes(const QString &componentIdString)
{
    const auto &component = findComponent(componentIdString);
    if (CombineMode::Uncombined == componentCombineMode(component))
        return;
    const auto &componentCache = m_cacheContext->components[componentIdString];
    for (const auto &mesh: componentCache.incombinableMeshes) {
        m_isSuccessful = false;
        collectIncombinableMesh(mesh, componentCache);
    }
    for (const auto &childIdString: valueOfKeyInMapOrEmpty(*component, "children").split(",")) {
        if (childIdString.isEmpty())
            continue;
        collectIncombinableComponentMeshes(childIdString);
    }
}

void MeshGenerator::collectIncombinableMesh(const MeshCombiner::Mesh *mesh, const GeneratedComponent &componentCache)
{
    if (nullptr == mesh)
        return;

    std::vector<QVector3D> uncombinedVertices;
    std::vector<std::vector<size_t>> uncombinedFaces;
    mesh->fetch(uncombinedVertices, uncombinedFaces);
    std::vector<std::vector<size_t>> uncombinedTriangleAndQuads;
    
    recoverQuads(uncombinedVertices, uncombinedFaces, componentCache.sharedQuadEdges, uncombinedTriangleAndQuads);
    
    auto vertexStartIndex = m_object->vertices.size();
    auto updateVertexIndices = [=](std::vector<std::vector<size_t>> &faces) {
        for (auto &it: faces) {
            for (auto &subIt: it)
                subIt += vertexStartIndex;
        }
    };
    updateVertexIndices(uncombinedFaces);
    updateVertexIndices(uncombinedTriangleAndQuads);
    
    m_object->vertices.insert(m_object->vertices.end(), uncombinedVertices.begin(), uncombinedVertices.end());
    m_object->triangles.insert(m_object->triangles.end(), uncombinedFaces.begin(), uncombinedFaces.end());
    m_object->triangleAndQuads.insert(m_object->triangleAndQuads.end(), uncombinedTriangleAndQuads.begin(), uncombinedTriangleAndQuads.end());
}

void MeshGenerator::collectUncombinedComponent(const QString &componentIdString)
{
    const auto &component = findComponent(componentIdString);
    if (CombineMode::Uncombined == componentCombineMode(component)) {
        const auto &componentCache = m_cacheContext->components[componentIdString];
        if (nullptr == componentCache.mesh || componentCache.mesh->isNull()) {
            qDebug() << "Uncombined mesh is null";
            return;
        }
        
        m_object->nodes.insert(m_object->nodes.end(), componentCache.objectNodes.begin(), componentCache.objectNodes.end());
        m_object->edges.insert(m_object->edges.end(), componentCache.objectEdges.begin(), componentCache.objectEdges.end());
        m_nodeVertices.insert(m_nodeVertices.end(), componentCache.objectNodeVertices.begin(), componentCache.objectNodeVertices.end());
        
        collectIncombinableMesh(componentCache.mesh, componentCache);
        return;
    }
    for (const auto &childIdString: valueOfKeyInMapOrEmpty(*component, "children").split(",")) {
        if (childIdString.isEmpty())
            continue;
        collectUncombinedComponent(childIdString);
    }
}

void MeshGenerator::generateSmoothTriangleVertexNormals(const std::vector<QVector3D> &vertices, const std::vector<std::vector<size_t>> &triangles,
    const std::vector<QVector3D> &triangleNormals,
    std::vector<std::vector<QVector3D>> *triangleVertexNormals)
{
    std::vector<QVector3D> smoothNormals;
    angleSmooth(vertices,
        triangles,
        triangleNormals,
        m_smoothShadingThresholdAngleDegrees,
        smoothNormals);
    triangleVertexNormals->resize(triangles.size(), {
        QVector3D(), QVector3D(), QVector3D()
    });
    size_t index = 0;
    for (size_t i = 0; i < triangles.size(); ++i) {
        auto &normals = (*triangleVertexNormals)[i];
        for (size_t j = 0; j < 3; ++j) {
            if (index < smoothNormals.size())
                normals[j] = smoothNormals[index];
            ++index;
        }
    }
}

void MeshGenerator::setDefaultPartColor(const QColor &color)
{
    m_defaultPartColor = color;
}

QString MeshGenerator::reverseUuid(const QString &uuidString)
{
    QUuid uuid(uuidString);
    QString newIdString = uuid.toString();
    QString newRawId = newIdString.mid(1, 8) +
        newIdString.mid(10, 4) +
        newIdString.mid(15, 4) +
        newIdString.mid(20, 4) +
        newIdString.mid(25, 12);
    std::reverse(newRawId.begin(), newRawId.end());
    return "{" + newRawId.mid(0, 8) + "-" +
        newRawId.mid(8, 4) + "-" +
        newRawId.mid(12, 4) + "-" +
        newRawId.mid(16, 4) + "-" +
        newRawId.mid(20, 12) + "}";
}

void MeshGenerator::preprocessMirror()
{
    std::vector<std::map<QString, QString>> newParts;
    std::map<QString, QString> partOldToNewMap;
    for (auto &partIt: m_snapshot->parts) {
        bool xMirrored = isTrueValueString(valueOfKeyInMapOrEmpty(partIt.second, "xMirrored"));
        if (!xMirrored)
            continue;
        std::map<QString, QString> mirroredPart = partIt.second;
        
        QString newPartIdString = reverseUuid(mirroredPart["id"]);
        partOldToNewMap.insert({mirroredPart["id"], newPartIdString});
        
        //qDebug() << "Added part:" << newPartIdString << "by mirror from:" << mirroredPart["id"];
        
        mirroredPart["__mirrorFromPartId"] = mirroredPart["id"];
        mirroredPart["id"] = newPartIdString;
        mirroredPart["__dirty"] = "true";
        newParts.push_back(mirroredPart);
    }
    
    for (const auto &it: partOldToNewMap)
        m_snapshot->parts[it.second]["__mirroredByPartId"] = it.first;
    
    std::map<QString, QString> parentMap;
    for (auto &componentIt: m_snapshot->components) {
        for (const auto &childId: valueOfKeyInMapOrEmpty(componentIt.second, "children").split(",")) {
            if (childId.isEmpty())
                continue;
            parentMap[childId] = componentIt.first;
            //qDebug() << "Update component:" << childId << "parent to:" << componentIt.first;
        }
    }
    for (const auto &childId: valueOfKeyInMapOrEmpty(m_snapshot->rootComponent, "children").split(",")) {
        if (childId.isEmpty())
            continue;
        parentMap[childId] = QString();
        //qDebug() << "Update component:" << childId << "parent to root";
    }
    
    std::vector<std::map<QString, QString>> newComponents;
    for (auto &componentIt: m_snapshot->components) {
        QString linkDataType = valueOfKeyInMapOrEmpty(componentIt.second, "linkDataType");
        if ("partId" != linkDataType)
            continue;
        QString partIdString = valueOfKeyInMapOrEmpty(componentIt.second, "linkData");
        auto findPart = partOldToNewMap.find(partIdString);
        if (findPart == partOldToNewMap.end())
            continue;
        std::map<QString, QString> mirroredComponent = componentIt.second;
        QString newComponentIdString = reverseUuid(mirroredComponent["id"]);
        //qDebug() << "Added component:" << newComponentIdString << "by mirror from:" << valueOfKeyInMapOrEmpty(componentIt.second, "id");
        mirroredComponent["linkData"] = findPart->second;
        mirroredComponent["id"] = newComponentIdString;
        mirroredComponent["__dirty"] = "true";
        parentMap[newComponentIdString] = parentMap[valueOfKeyInMapOrEmpty(componentIt.second, "id")];
        //qDebug() << "Update component:" << newComponentIdString << "parent to:" << parentMap[valueOfKeyInMapOrEmpty(componentIt.second, "id")];
        newComponents.push_back(mirroredComponent);
    }

    for (const auto &it: newParts) {
        m_snapshot->parts[valueOfKeyInMapOrEmpty(it, "id")] = it;
    }
    for (const auto &it: newComponents) {
        QString idString = valueOfKeyInMapOrEmpty(it, "id");
        QString parentIdString = parentMap[idString];
        m_snapshot->components[idString] = it;
        if (parentIdString.isEmpty()) {
            m_snapshot->rootComponent["children"] += "," + idString;
            //qDebug() << "Update rootComponent children:" << m_snapshot->rootComponent["children"];
        } else {
            m_snapshot->components[parentIdString]["children"] += "," + idString;
            //qDebug() << "Update component:" << parentIdString << "children:" << m_snapshot->components[parentIdString]["children"];
        }
    }
}

void MeshGenerator::generate()
{
    if (nullptr == m_snapshot)
        return;

    m_isSuccessful = true;
    
    QElapsedTimer countTimeConsumed;
    countTimeConsumed.start();
    
    m_mainProfileMiddleX = valueOfKeyInMapOrEmpty(m_snapshot->canvas, "originX").toFloat();
    m_mainProfileMiddleY = valueOfKeyInMapOrEmpty(m_snapshot->canvas, "originY").toFloat();
    m_sideProfileMiddleX = valueOfKeyInMapOrEmpty(m_snapshot->canvas, "originZ").toFloat();
    
    preprocessMirror();
    
    m_object = new Object;
    m_object->meshId = m_id;
    //m_cutFaceTransforms = new std::map<QUuid, nodemesh::Builder::CutFaceTransform>;
    //m_nodesCutFaces = new std::map<QUuid, std::map<QString, QVector2D>>;
    
    bool needDeleteCacheContext = false;
    if (nullptr == m_cacheContext) {
        m_cacheContext = new GeneratedCacheContext;
        needDeleteCacheContext = true;
    } else {
        //qDebug() << "m_cacheContext->parts.size:" << m_cacheContext->parts.size();
        //qDebug() << "m_cacheContext->components.size:" << m_cacheContext->components.size();
        //qDebug() << "m_cacheContext->cachedCombination.size:" << m_cacheContext->cachedCombination.size();
        
        m_cacheEnabled = true;
        for (auto it = m_cacheContext->parts.begin(); it != m_cacheContext->parts.end(); ) {
            if (m_snapshot->parts.find(it->first) == m_snapshot->parts.end()) {
                auto mirrorFrom = m_cacheContext->partMirrorIdMap.find(it->first);
                if (mirrorFrom != m_cacheContext->partMirrorIdMap.end()) {
                    if (m_snapshot->parts.find(mirrorFrom->second) != m_snapshot->parts.end()) {
                        it++;
                        continue;
                    }
                    m_cacheContext->partMirrorIdMap.erase(mirrorFrom);
                }
                it->second.releaseMeshes();
                it = m_cacheContext->parts.erase(it);
                continue;
            }
            it++;
        }
        for (auto it = m_cacheContext->components.begin(); it != m_cacheContext->components.end(); ) {
            if (m_snapshot->components.find(it->first) == m_snapshot->components.end()) {
                for (auto combinationIt = m_cacheContext->cachedCombination.begin(); combinationIt != m_cacheContext->cachedCombination.end(); ) {
                    if (-1 != combinationIt->first.indexOf(it->first)) {
                        //qDebug() << "Removed cached combination:" << combinationIt->first;
                        delete combinationIt->second;
                        combinationIt = m_cacheContext->cachedCombination.erase(combinationIt);
                        continue;
                    }
                    combinationIt++;
                }
                it->second.releaseMeshes();
                it = m_cacheContext->components.erase(it);
                continue;
            }
            it++;
        }
    }
    
    collectParts();
    checkDirtyFlags();
    
    for (const auto &dirtyComponentId: m_dirtyComponentIds) {
        for (auto combinationIt = m_cacheContext->cachedCombination.begin(); combinationIt != m_cacheContext->cachedCombination.end(); ) {
            if (-1 != combinationIt->first.indexOf(dirtyComponentId)) {
                //qDebug() << "Removed dirty cached combination:" << combinationIt->first;
                delete combinationIt->second;
                combinationIt = m_cacheContext->cachedCombination.erase(combinationIt);
                continue;
            }
            combinationIt++;
        }
    }
    
    m_dirtyComponentIds.insert(QUuid().toString());
    
    CombineMode combineMode;
    auto combinedMesh = combineComponentMesh(QUuid().toString(), &combineMode);
    
    const auto &componentCache = m_cacheContext->components[QUuid().toString()];
    
    m_object->nodes = componentCache.objectNodes;
    m_object->edges = componentCache.objectEdges;
    m_nodeVertices = componentCache.objectNodeVertices;
        
    std::vector<QVector3D> combinedVertices;
    std::vector<std::vector<size_t>> combinedFaces;
    if (nullptr != combinedMesh) {
        combinedMesh->fetch(combinedVertices, combinedFaces);
        if (m_weldEnabled) {
            size_t totalAffectedNum = 0;
            size_t affectedNum = 0;
            do {
                std::vector<QVector3D> weldedVertices;
                std::vector<std::vector<size_t>> weldedFaces;
                affectedNum = weldSeam(combinedVertices, combinedFaces,
                    0.025, componentCache.noneSeamVertices,
                    weldedVertices, weldedFaces);
                combinedVertices = weldedVertices;
                combinedFaces = weldedFaces;
                totalAffectedNum += affectedNum;
            } while (affectedNum > 0);
        }
        recoverQuads(combinedVertices, combinedFaces, componentCache.sharedQuadEdges, m_object->triangleAndQuads);
        m_object->vertices = combinedVertices;
        m_object->triangles = combinedFaces;
    }
    
    // Recursively check uncombined components
    collectUncombinedComponent(QUuid().toString());
    collectIncombinableComponentMeshes(QUuid().toString());
    
    collectErroredParts();
    postprocessObject(m_object);
    
    m_resultMesh = new Model(*m_object);
    
    delete combinedMesh;

    if (needDeleteCacheContext) {
        delete m_cacheContext;
        m_cacheContext = nullptr;
    }
    
    qDebug() << "The mesh generation took" << countTimeConsumed.elapsed() << "milliseconds";
}
