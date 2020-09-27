#include <cmath>
#include <QtMath>
#include "animalposer.h"
#include "util.h"

AnimalPoser::AnimalPoser(const std::vector<RiggerBone> &bones) :
    Poser(bones)
{
}

void AnimalPoser::resolveTransform()
{
    std::map<QString, std::vector<QString>> chains;
    std::vector<QString> boneNames;
    for (const auto &item: parameters()) {
        boneNames.push_back(item.first);
    }
    Poser::fetchChains(boneNames, chains);
    for (auto &chain: chains) {
        resolveChainRotation(chain.second);
    }
    
    float mostBottomYBeforeTransform = std::numeric_limits<float>::max();
    for (const auto &bone: bones()) {
        if (bone.tailPosition.y() < mostBottomYBeforeTransform)
            mostBottomYBeforeTransform = bone.tailPosition.y();
    }
    
    auto transformedJointNodeTree = m_jointNodeTree;
    transformedJointNodeTree.recalculateTransformMatrices();
    float mostBottomYAfterTransform = std::numeric_limits<float>::max();
    for (int i = 0; i < (int)transformedJointNodeTree.nodes().size(); ++i) {
        const auto &bone = bones()[i];
        const auto &jointNode = transformedJointNodeTree.nodes()[i];
        QVector3D newPosition = jointNode.transformMatrix * bone.tailPosition;
        if (newPosition.y() < mostBottomYAfterTransform)
            mostBottomYAfterTransform = newPosition.y();
    }
    float translateY = mostBottomYBeforeTransform - mostBottomYAfterTransform;
    
    if (!qFuzzyIsNull(translateY)) {
        int rootBoneIndex = findBoneIndex(Rigger::rootBoneName);
        if (-1 == rootBoneIndex) {
            qDebug() << "Find root bone failed:" << Rigger::rootBoneName;
            return;
        }
        m_jointNodeTree.addTranslation(rootBoneIndex, QVector3D(0, translateY * m_yTranslationScale, 0));
    }
}

std::pair<bool, QVector3D> AnimalPoser::findQVector3DFromMap(const std::map<QString, QString> &map, const QString &xName, const QString &yName, const QString &zName)
{
    auto findXResult = map.find(xName);
    auto findYResult = map.find(yName);
    auto findZResult = map.find(zName);
    if (findXResult == map.end() &&
            findYResult == map.end() &&
            findZResult == map.end()) {
        return {false, QVector3D()};
    }
    return {true, {
        valueOfKeyInMapOrEmpty(map, xName).toFloat(),
        valueOfKeyInMapOrEmpty(map, yName).toFloat(),
        valueOfKeyInMapOrEmpty(map, zName).toFloat()
    }};
}

std::pair<bool, std::pair<QVector3D, QVector3D>> AnimalPoser::findBonePositionsFromParameters(const std::map<QString, QString> &map)
{
    auto findBoneStartResult = findQVector3DFromMap(map, "fromX", "fromY", "fromZ");
    auto findBoneStopResult = findQVector3DFromMap(map, "toX", "toY", "toZ");
    
    if (!findBoneStartResult.first || !findBoneStopResult.first)
        return {false, {QVector3D(), QVector3D()}};
    
    return {true, {findBoneStartResult.second, findBoneStopResult.second}};
}

void AnimalPoser::resolveChainRotation(const std::vector<QString> &limbBoneNames)
{
    std::vector<QQuaternion> rotationsForEndEffector;
    size_t endEffectorStart = 0;
    
    // We match the poses by the distance and rotation plane
    if (limbBoneNames.size() >= 2) {
        endEffectorStart = 2;
    
        const auto &beginBoneName = limbBoneNames[0];
        const auto &middleBoneName = limbBoneNames[1];
        
        const auto &beginBoneParameters = parameters().find(beginBoneName);
        if (beginBoneParameters == parameters().end()) {
            qDebug() << beginBoneName << "'s parameters not found";
            return;
        }
        
        auto matchBeginBonePositions = findBonePositionsFromParameters(beginBoneParameters->second);
        if (!matchBeginBonePositions.first) {
            qDebug() << beginBoneName << "'s positions not found";
            return;
        }
        
        const auto &middleBoneParameters = parameters().find(middleBoneName);
        if (middleBoneParameters == parameters().end()) {
            qDebug() << middleBoneName << "'s parameters not found";
            return;
        }
        
        auto matchMiddleBonePositions = findBonePositionsFromParameters(middleBoneParameters->second);
        if (!matchMiddleBonePositions.first) {
            qDebug() << middleBoneName << "'s positions not found";
            return;
        }
        
        float matchBeginBoneLength = (matchBeginBonePositions.second.first - matchBeginBonePositions.second.second).length();
        float matchMiddleBoneLength = (matchMiddleBonePositions.second.first - matchMiddleBonePositions.second.second).length();
        float matchLimbLength = matchBeginBoneLength + matchMiddleBoneLength;
        
        auto matchDistanceBetweenBeginAndEndBones = (matchBeginBonePositions.second.first - matchMiddleBonePositions.second.second).length();
        auto matchRotatePlaneNormal = QVector3D::crossProduct((matchBeginBonePositions.second.second - matchBeginBonePositions.second.first).normalized(), (matchMiddleBonePositions.second.second - matchBeginBonePositions.second.second).normalized());
        
        auto matchDirectionBetweenBeginAndEndPones = (matchMiddleBonePositions.second.second - matchBeginBonePositions.second.first).normalized();
        
        int beginBoneIndex = findBoneIndex(beginBoneName);
        if (-1 == beginBoneIndex) {
            qDebug() << beginBoneName << "not found in rigged bones";
            return;
        }
        const auto &beginBone = bones()[beginBoneIndex];
        
        int middleBoneIndex = findBoneIndex(middleBoneName);
        if (-1 == middleBoneIndex) {
            qDebug() << middleBoneName << "not found in rigged bones";
            return;
        }
        const auto &middleBone = bones()[middleBoneIndex];
        
        float targetBeginBoneLength = (beginBone.headPosition - beginBone.tailPosition).length();
        float targetMiddleBoneLength = (middleBone.headPosition - middleBone.tailPosition).length();
        float targetLimbLength = targetBeginBoneLength + targetMiddleBoneLength;
        
        float targetDistanceBetweenBeginAndEndBones = matchDistanceBetweenBeginAndEndBones * (targetLimbLength / matchLimbLength);
        QVector3D targetEndBoneStartPosition = beginBone.headPosition + matchDirectionBetweenBeginAndEndPones * targetDistanceBetweenBeginAndEndBones;
        
        float angleBetweenDistanceAndMiddleBones = 0;
        {
            const float &a = targetMiddleBoneLength;
            const float &b = targetDistanceBetweenBeginAndEndBones;
            const float &c = targetBeginBoneLength;
            double cosC = (a*a + b*b - c*c) / (2.0*a*b);
            angleBetweenDistanceAndMiddleBones = qRadiansToDegrees(acos(cosC));
            if (std::isnan(angleBetweenDistanceAndMiddleBones) || std::isinf(angleBetweenDistanceAndMiddleBones))
                angleBetweenDistanceAndMiddleBones = 0;
        }
        
        QVector3D targetMiddleBoneStartPosition;
        {
            //qDebug() << beginBoneName << "Angle:" << angleBetweenDistanceAndMiddleBones << "Distance:" << targetDistanceBetweenBeginAndEndBones;
            auto rotation = QQuaternion::fromAxisAndAngle(matchRotatePlaneNormal, angleBetweenDistanceAndMiddleBones);
            targetMiddleBoneStartPosition = targetEndBoneStartPosition + rotation.rotatedVector(-matchDirectionBetweenBeginAndEndPones).normalized() * targetMiddleBoneLength;
        }
        
        // Now the bones' positions have been resolved, we calculate the rotation
        
        auto oldBeginBoneDirection = (beginBone.tailPosition - beginBone.headPosition).normalized();
        auto newBeginBoneDirection = (targetMiddleBoneStartPosition - beginBone.headPosition).normalized();
        //qDebug() << beginBoneName << "oldBeginBoneDirection:" << oldBeginBoneDirection << "newBeginBoneDirection:" << newBeginBoneDirection;
        auto beginBoneRotation = QQuaternion::rotationTo(oldBeginBoneDirection, newBeginBoneDirection);
        m_jointNodeTree.updateRotation(beginBoneIndex, beginBoneRotation);
        
        auto oldMiddleBoneDirection = (middleBone.tailPosition - middleBone.headPosition).normalized();
        auto newMiddleBoneDirection = (targetEndBoneStartPosition - targetMiddleBoneStartPosition).normalized();
        //qDebug() << beginBoneName << "oldMiddleBoneDirection:" << oldMiddleBoneDirection << "newMiddleBoneDirection:" << newMiddleBoneDirection;
        oldMiddleBoneDirection = beginBoneRotation.rotatedVector(oldMiddleBoneDirection);
        //qDebug() << beginBoneName << "oldMiddleBoneDirection:" << oldMiddleBoneDirection << "after rotation";
        auto middleBoneRotation = QQuaternion::rotationTo(oldMiddleBoneDirection, newMiddleBoneDirection);
        m_jointNodeTree.updateRotation(middleBoneIndex, middleBoneRotation);
        
        rotationsForEndEffector.push_back(beginBoneRotation);
        rotationsForEndEffector.push_back(middleBoneRotation);
    }
    
    // Calculate the end effectors' rotation
    if (limbBoneNames.size() > endEffectorStart) {
        for (size_t i = endEffectorStart; i < limbBoneNames.size(); ++i) {
            const auto &boneName = limbBoneNames[i];
            int boneIndex = findBoneIndex(boneName);
            if (-1 == boneIndex) {
                qDebug() << "Find bone failed:" << boneName;
                continue;
            }
            const auto &bone = bones()[boneIndex];
            const auto &boneParameters = parameters().find(boneName);
            if (boneParameters == parameters().end()) {
                qDebug() << "Find bone parameters:" << boneName;
                continue;
            }
            auto matchBonePositions = findBonePositionsFromParameters(boneParameters->second);
            if (!matchBonePositions.first) {
                qDebug() << "Find bone positions failed:" << boneName;
                continue;
            }
            auto matchBoneDirection = (matchBonePositions.second.second - matchBonePositions.second.first).normalized();
            auto oldBoneDirection = (bone.tailPosition - bone.headPosition).normalized();
            auto newBoneDirection = matchBoneDirection;
            for (const auto &rotation: rotationsForEndEffector) {
                oldBoneDirection = rotation.rotatedVector(oldBoneDirection);
            }
            auto boneRotation = QQuaternion::rotationTo(oldBoneDirection, newBoneDirection);
            m_jointNodeTree.updateRotation(boneIndex, boneRotation);
            rotationsForEndEffector.push_back(boneRotation);
        }
    }
}

void AnimalPoser::commit()
{
    resolveTransform();
    
    Poser::commit();
}

