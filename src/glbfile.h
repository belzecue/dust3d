#ifndef DUST3D_GLB_FILE_H
#define DUST3D_GLB_FILE_H
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QMatrix4x4>
#include <vector>
#include <QQuaternion>
#include <QImage>
#include "object.h"
#include "json.hpp"
#include "document.h"

class GlbFileWriter : public QObject
{
    Q_OBJECT
public:
    GlbFileWriter(Object &object,
        const std::vector<RigBone> *resultRigBones,
        const std::map<int, RigVertexWeights> *resultRigWeights,
        const QString &filename,
        QImage *textureImage=nullptr,
        QImage *normalImage=nullptr,
        QImage *ormImage=nullptr,
        const std::vector<std::pair<QString, std::vector<std::pair<float, JointNodeTree>>>> *motions=nullptr);
    bool save();
private:
    QString m_filename;
    bool m_outputNormal = true;
    bool m_outputAnimation = true;
    bool m_outputUv = true;
    QByteArray m_binByteArray;
    QByteArray m_jsonByteArray;
private:
    nlohmann::json m_json;
public:
    static bool m_enableComment;
};

#endif
