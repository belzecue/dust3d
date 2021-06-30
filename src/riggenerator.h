#ifndef DUST3D_RIG_GENERATOR_H
#define DUST3D_RIG_GENERATOR_H
#include <QObject>
#include <QThread>
#include <QDebug>
#include <unordered_set>
#include "object.h"
#include "model.h"
#include "rig.h"
#include "rigtype.h"

class RigGenerator : public QObject
{
    Q_OBJECT
public:
    RigGenerator(RigType rigType, const Object &object);
    ~RigGenerator();
    Model *takeResultMesh();
    std::vector<RigBone> *takeResultBones();
    std::map<int, RigVertexWeights> *takeResultWeights();
    const std::vector<std::pair<QtMsgType, QString>> &messages();
    Object *takeObject();
    bool isSuccessful();
    void generate();
signals:
    void finished();
public slots:
    void process();
private:
    struct BoneNodeChain
    {
        size_t fromNodeIndex;
        std::vector<std::vector<size_t>> nodeIndices;
        bool isSpine;
        size_t attachNodeIndex;
    };
    
    RigType m_rigType = RigType::None;
    Object *m_object = nullptr;
    Model *m_resultMesh = nullptr;
    std::vector<RigBone> *m_resultBones = nullptr;
    std::map<int, RigVertexWeights> *m_resultWeights = nullptr;
    std::vector<std::pair<QtMsgType, QString>> m_messages;
    std::map<size_t, std::unordered_set<size_t>> m_neighborMap;
    std::vector<BoneNodeChain> m_boneNodeChain;
    std::vector<size_t> m_neckChains;
    std::vector<size_t> m_leftLimbChains;
    std::vector<size_t> m_rightLimbChains;
    std::vector<size_t> m_tailChains;
    std::vector<size_t> m_spineChains;
    std::unordered_set<size_t> m_virtualJoints;
    std::vector<size_t> m_attachLimbsToSpineNodeIndices;
    std::vector<size_t> m_attachLimbsToSpineJointIndices;
    std::map<int, std::vector<size_t>> m_branchNodesMapByMark;
    std::vector<size_t> m_neckJoints;
    std::vector<std::vector<size_t>> m_leftLimbJoints;
    std::vector<std::vector<size_t>> m_rightLimbJoints;
    std::vector<size_t> m_tailJoints;
    std::vector<size_t> m_spineJoints;
    std::map<QString, int> m_boneNameToIndexMap;
    ShaderVertex *m_debugEdgeVertices = nullptr;
    int m_debugEdgeVerticesNum = 0;
    bool m_isSpineVertical = false;
    bool m_isSuccessful = false;
    size_t m_rootSpineJointIndex = 0;
    size_t m_lastSpineJointIndex = 0;
    
    void buildNeighborMap();
    void buildBoneNodeChain();
    void buildSkeleton();
    void computeSkinWeights();
    void buildDemoMesh();
    void calculateSpineDirection(bool *isVertical);
    void attachLimbsToSpine();
    void extractSpineJoints();
    void extractBranchJoints();
    void extractJointsFromBoneNodeChain(const BoneNodeChain &boneNodeChain,
        std::vector<size_t> *joints);
    void extractJoints(const size_t &fromNodeIndex,
        const std::vector<std::vector<size_t>> &nodeIndices,
        std::vector<size_t> *joints,
        bool checkLastNoneMarkedNode=true);
    void groupNodeIndices(const std::map<size_t, std::unordered_set<size_t>> &neighborMap,
        std::vector<std::unordered_set<size_t>> *groups);
    void computeBranchSkinWeights(size_t fromBoneIndex,
        const QString &boneNamePrefix,
        const std::vector<size_t> &vertexIndices,
        std::vector<size_t> *discardedVertexIndices=nullptr);
    void splitByNodeIndex(size_t nodeIndex,
        std::unordered_set<size_t> *left,
        std::unordered_set<size_t> *right);
    void collectNodes(size_t fromNodeIndex,
        std::unordered_set<size_t> *container,
        std::unordered_set<size_t> *visited);
    void collectNodesForBoneRecursively(size_t fromNodeIndex,
        const std::unordered_set<size_t> *limitedNodeIndices,
        std::vector<std::vector<size_t>> *boneNodeIndices,
        size_t depth,
        std::unordered_set<size_t> *visited);
    void removeBranchsFromNodes(const std::vector<std::vector<size_t>> *boneNodeIndices,
        std::vector<size_t> *resultNodes);
    void fixVirtualBoneSkinWeights();
    int attachedBoneIndex(size_t spineJointIndex);
};

#endif
