#include <nodemesh/misc.h>
#include <cmath>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <unordered_set>
#include <unordered_map>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Polygon_mesh_processing/triangulate_faces.h>
#include <nodemesh/cgalmesh.h>

typedef CGAL::Exact_predicates_inexact_constructions_kernel InexactKernel;
typedef CGAL::Surface_mesh<InexactKernel::Point_3> InexactMesh;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace nodemesh;

float nodemesh::radianToDegree(float r)
{
    return r * 180.0 / M_PI;
}

float nodemesh::angleBetween(const QVector3D &v1, const QVector3D &v2)
{
    return atan2(QVector3D::crossProduct(v1, v2).length(), QVector3D::dotProduct(v1, v2));
}

float nodemesh::degreeBetween(const QVector3D &v1, const QVector3D &v2)
{
    return radianToDegree(angleBetween(v1, v2));
}

float nodemesh::degreeBetweenIn360(const QVector3D &a, const QVector3D &b, const QVector3D &direct)
{
    auto angle = radianToDegree(angleBetween(a, b));
    auto c = QVector3D::crossProduct(a, b);
    if (QVector3D::dotProduct(c, direct) < 0) {
        angle += 180;
    }
    return angle;
}

QVector3D nodemesh::polygonNormal(const std::vector<QVector3D> &vertices, const std::vector<size_t> &polygon)
{
    QVector3D normal;
    for (size_t i = 0; i < polygon.size(); ++i) {
        auto j = (i + 1) % polygon.size();
        auto k = (i + 2) % polygon.size();
        const auto &enter = vertices[polygon[i]];
        const auto &cone = vertices[polygon[j]];
        const auto &leave = vertices[polygon[k]];
        normal += QVector3D::normal(enter, cone, leave);
    }
    return normal.normalized();
}

bool nodemesh::pointInTriangle(const QVector3D &a, const QVector3D &b, const QVector3D &c, const QVector3D &p)
{
    auto u = b - a;
    auto v = c - a;
    auto w = p - a;
    auto vXw = QVector3D::crossProduct(v, w);
    auto vXu = QVector3D::crossProduct(v, u);
    if (QVector3D::dotProduct(vXw, vXu) < 0.0) {
        return false;
    }
    auto uXw = QVector3D::crossProduct(u, w);
    auto uXv = QVector3D::crossProduct(u, v);
    if (QVector3D::dotProduct(uXw, uXv) < 0.0) {
        return false;
    }
    auto denom = uXv.length();
    auto r = vXw.length() / denom;
    auto t = uXw.length() / denom;
    return r + t <= 1.0;
}

bool nodemesh::triangulate(std::vector<QVector3D> &vertices, const std::vector<std::vector<size_t>> &faces, std::vector<std::vector<size_t>> &triangles)
{
    auto cgalMesh = buildCgalMesh<InexactKernel>(vertices, faces);
    bool isSucceed = CGAL::Polygon_mesh_processing::triangulate_faces(*cgalMesh);
    if (isSucceed) {
        vertices.clear();
        fetchFromCgalMesh<InexactKernel>(cgalMesh, vertices, triangles);
        delete cgalMesh;
        return true;
    }
    delete cgalMesh;
    
    // fallback to our own imeplementation

    isSucceed = true;
    std::vector<std::vector<size_t>> rings;
    for (const auto &face: faces) {
        if (face.size() > 3) {
            rings.push_back(face);
        } else {
            triangles.push_back(face);
        }
    }
    for (const auto &ring: rings) {
        std::vector<size_t> fillRing = ring;
        QVector3D direct = polygonNormal(vertices, fillRing);
        while (fillRing.size() > 3) {
            bool newFaceGenerated = false;
            for (decltype(fillRing.size()) i = 0; i < fillRing.size(); ++i) {
                auto j = (i + 1) % fillRing.size();
                auto k = (i + 2) % fillRing.size();
                const auto &enter = vertices[fillRing[i]];
                const auto &cone = vertices[fillRing[j]];
                const auto &leave = vertices[fillRing[k]];
                auto angle = degreeBetweenIn360(cone - enter, leave - cone, direct);
                if (angle >= 1.0 && angle <= 179.0) {
                    bool isEar = true;
                    for (size_t x = 0; x < fillRing.size() - 3; ++x) {
                        auto fourth = vertices[(i + 3 + k) % fillRing.size()];
                        if (pointInTriangle(enter, cone, leave, fourth)) {
                            isEar = false;
                            break;
                        }
                    }
                    if (isEar) {
                        std::vector<size_t> newFace = {
                            fillRing[i],
                            fillRing[j],
                            fillRing[k],
                        };
                        triangles.push_back(newFace);
                        fillRing.erase(fillRing.begin() + j);
                        newFaceGenerated = true;
                        break;
                    }
                }
            }
            if (!newFaceGenerated)
                break;
        }
        if (fillRing.size() == 3) {
            std::vector<size_t> newFace = {
                fillRing[0],
                fillRing[1],
                fillRing[2],
            };
            triangles.push_back(newFace);
        } else {
            qDebug() << "Triangulate failed, ring size:" << fillRing.size();
            isSucceed = false;
        }
    }
    return isSucceed;
}

void nodemesh::exportMeshAsObj(const std::vector<QVector3D> &vertices, const std::vector<std::vector<size_t>> &faces, const QString &filename, const std::set<size_t> *excludeFacesOfVertices)
{
    QFile file(filename);
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream stream(&file);
        stream << "# Generated by nodemesh, a library from Dust3D https://dust3d.org" << endl;
        for (std::vector<QVector3D>::const_iterator it = vertices.begin() ; it != vertices.end(); ++it) {
            stream << "v " << (*it).x() << " " << (*it).y() << " " << (*it).z() << endl;
        }
        for (std::vector<std::vector<size_t>>::const_iterator it = faces.begin() ; it != faces.end(); ++it) {
            bool excluded = false;
            for (std::vector<size_t>::const_iterator subIt = (*it).begin() ; subIt != (*it).end(); ++subIt) {
                if (excludeFacesOfVertices && excludeFacesOfVertices->find(*subIt) != excludeFacesOfVertices->end()) {
                    excluded = true;
                    break;
                }
            }
            if (excluded)
                continue;
            stream << "f";
            for (std::vector<size_t>::const_iterator subIt = (*it).begin() ; subIt != (*it).end(); ++subIt) {
                stream << " " << (1 + *subIt);
            }
            stream << endl;
        }
    }
}

void nodemesh::exportMeshAsObjWithNormals(const std::vector<QVector3D> &vertices, const std::vector<std::vector<size_t>> &faces, const QString &filename,
    const std::vector<QVector3D> &triangleVertexNormals)
{
    QFile file(filename);
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream stream(&file);
        stream << "# Generated by nodemesh, a library from Dust3D https://dust3d.org" << endl;
        for (std::vector<QVector3D>::const_iterator it = vertices.begin() ; it != vertices.end(); ++it) {
            stream << "v " << (*it).x() << " " << (*it).y() << " " << (*it).z() << endl;
        }
        for (std::vector<QVector3D>::const_iterator it = triangleVertexNormals.begin() ; it != triangleVertexNormals.end(); ++it) {
            stream << "vn " << (*it).x() << " " << (*it).y() << " " << (*it).z() << endl;
        }
        size_t normalIndex = 0;
        for (std::vector<std::vector<size_t>>::const_iterator it = faces.begin() ; it != faces.end(); ++it) {
            stream << "f";
            for (std::vector<size_t>::const_iterator subIt = (*it).begin() ; subIt != (*it).end(); ++subIt) {
                ++normalIndex;
                stream << " " << QString::number(1 + *subIt) + "//" + QString::number(normalIndex);
            }
            stream << endl;
        }
    }
}

float nodemesh::areaOfTriangle(const QVector3D &a, const QVector3D &b, const QVector3D &c)
{
    auto ab = b - a;
    auto ac = c - a;
    return 0.5 * QVector3D::crossProduct(ab, ac).length();
}

void nodemesh::angleSmooth(const std::vector<QVector3D> &vertices,
    const std::vector<std::vector<size_t>> &triangles,
    const std::vector<QVector3D> &triangleNormals,
    float thresholdAngleDegrees,
    std::vector<QVector3D> &triangleVertexNormals)
{
    std::vector<std::vector<std::pair<size_t, size_t>>> triangleVertexNormalsMapByIndices(vertices.size());
    std::vector<QVector3D> angleAreaWeightedNormals;
    for (size_t triangleIndex = 0; triangleIndex < triangles.size(); ++triangleIndex) {
        const auto &sourceTriangle = triangles[triangleIndex];
        if (sourceTriangle.size() != 3) {
            qDebug() << "Encounter non triangle";
            continue;
        }
        const auto &v1 = vertices[sourceTriangle[0]];
        const auto &v2 = vertices[sourceTriangle[1]];
        const auto &v3 = vertices[sourceTriangle[2]];
        float area = areaOfTriangle(v1, v2, v3);
        float angles[] = {radianToDegree(angleBetween(v2-v1, v3-v1)),
            radianToDegree(angleBetween(v1-v2, v3-v2)),
            radianToDegree(angleBetween(v1-v3, v2-v3))};
        for (int i = 0; i < 3; ++i) {
            if (sourceTriangle[i] >= vertices.size()) {
                qDebug() << "Invalid vertex index" << sourceTriangle[i] << "vertices size" << vertices.size();
                continue;
            }
            triangleVertexNormalsMapByIndices[sourceTriangle[i]].push_back({triangleIndex, angleAreaWeightedNormals.size()});
            angleAreaWeightedNormals.push_back(triangleNormals[triangleIndex] * area * angles[i]);
        }
    }
    triangleVertexNormals = angleAreaWeightedNormals;
    std::map<std::pair<size_t, size_t>, float> degreesBetweenFacesMap;
    for (size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex) {
        const auto &triangleVertices = triangleVertexNormalsMapByIndices[vertexIndex];
        for (const auto &triangleVertex: triangleVertices) {
            for (const auto &otherTriangleVertex: triangleVertices) {
                if (triangleVertex.first == otherTriangleVertex.first)
                    continue;
                float degrees = 0;
                auto findDegreesResult = degreesBetweenFacesMap.find({triangleVertex.first, otherTriangleVertex.first});
                if (findDegreesResult == degreesBetweenFacesMap.end()) {
                    degrees = angleBetween(triangleNormals[triangleVertex.first], triangleNormals[otherTriangleVertex.first]);
                    degreesBetweenFacesMap.insert({{triangleVertex.first, otherTriangleVertex.first}, degrees});
                    degreesBetweenFacesMap.insert({{otherTriangleVertex.first, triangleVertex.first}, degrees});
                } else {
                    degrees = findDegreesResult->second;
                }
                if (degrees > thresholdAngleDegrees) {
                    continue;
                }
                triangleVertexNormals[triangleVertex.second] += angleAreaWeightedNormals[otherTriangleVertex.second];
            }
        }
    }
    for (auto &item: triangleVertexNormals)
        item.normalize();
}

void nodemesh::recoverQuads(const std::vector<QVector3D> &vertices, const std::vector<std::vector<size_t>> &triangles, const std::set<std::pair<PositionKey, PositionKey>> &sharedQuadEdges, std::vector<std::vector<size_t>> &triangleAndQuads)
{
    std::vector<PositionKey> verticesPositionKeys;
    for (const auto &position: vertices) {
        verticesPositionKeys.push_back(PositionKey(position));
    }
    std::map<std::pair<size_t, size_t>, std::pair<size_t, size_t>> triangleEdgeMap;
    for (size_t i = 0; i < triangles.size(); i++) {
        const auto &faceIndices = triangles[i];
        if (faceIndices.size() == 3) {
            triangleEdgeMap[std::make_pair(faceIndices[0], faceIndices[1])] = std::make_pair(i, faceIndices[2]);
            triangleEdgeMap[std::make_pair(faceIndices[1], faceIndices[2])] = std::make_pair(i, faceIndices[0]);
            triangleEdgeMap[std::make_pair(faceIndices[2], faceIndices[0])] = std::make_pair(i, faceIndices[1]);
        }
    }
    std::unordered_set<size_t> unionedFaces;
    std::vector<std::vector<size_t>> newUnionedFaceIndices;
    for (const auto &edge: triangleEdgeMap) {
        if (unionedFaces.find(edge.second.first) != unionedFaces.end())
            continue;
        auto pair = std::make_pair(verticesPositionKeys[edge.first.first], verticesPositionKeys[edge.first.second]);
        if (sharedQuadEdges.find(pair) != sharedQuadEdges.end()) {
            auto oppositeEdge = triangleEdgeMap.find(std::make_pair(edge.first.second, edge.first.first));
            if (oppositeEdge == triangleEdgeMap.end()) {
                qDebug() << "Find opposite edge failed";
            } else {
                if (unionedFaces.find(oppositeEdge->second.first) == unionedFaces.end()) {
                    unionedFaces.insert(edge.second.first);
                    unionedFaces.insert(oppositeEdge->second.first);
                    std::vector<size_t> indices;
                    indices.push_back(edge.second.second);
                    indices.push_back(edge.first.first);
                    indices.push_back(oppositeEdge->second.second);
                    indices.push_back(edge.first.second);
                    triangleAndQuads.push_back(indices);
                }
            }
        }
    }
    for (size_t i = 0; i < triangles.size(); i++) {
        if (unionedFaces.find(i) == unionedFaces.end()) {
            triangleAndQuads.push_back(triangles[i]);
        }
    }
}

size_t nodemesh::weldSeam(const std::vector<QVector3D> &sourceVertices, const std::vector<std::vector<size_t>> &sourceTriangles,
    float allowedSmallestDistance, const std::set<PositionKey> &excludePositions,
    std::vector<QVector3D> &destVertices, std::vector<std::vector<size_t>> &destTriangles)
{
    std::unordered_set<int> excludeVertices;
    for (size_t i = 0; i < sourceVertices.size(); ++i) {
        if (excludePositions.find(sourceVertices[i]) != excludePositions.end())
            excludeVertices.insert(i);
    }
    float squareOfAllowedSmallestDistance = allowedSmallestDistance * allowedSmallestDistance;
    std::map<int, int> weldVertexToMap;
    std::unordered_set<int> weldTargetVertices;
    std::unordered_set<int> processedFaces;
    std::map<std::pair<int, int>, std::pair<int, int>> triangleEdgeMap;
    std::unordered_map<int, int> vertexAdjFaceCountMap;
    for (int i = 0; i < (int)sourceTriangles.size(); i++) {
        const auto &faceIndices = sourceTriangles[i];
        if (faceIndices.size() == 3) {
            vertexAdjFaceCountMap[faceIndices[0]]++;
            vertexAdjFaceCountMap[faceIndices[1]]++;
            vertexAdjFaceCountMap[faceIndices[2]]++;
            triangleEdgeMap[std::make_pair(faceIndices[0], faceIndices[1])] = std::make_pair(i, faceIndices[2]);
            triangleEdgeMap[std::make_pair(faceIndices[1], faceIndices[2])] = std::make_pair(i, faceIndices[0]);
            triangleEdgeMap[std::make_pair(faceIndices[2], faceIndices[0])] = std::make_pair(i, faceIndices[1]);
        }
    }
    for (int i = 0; i < (int)sourceTriangles.size(); i++) {
        if (processedFaces.find(i) != processedFaces.end())
            continue;
        const auto &faceIndices = sourceTriangles[i];
        if (faceIndices.size() == 3) {
            bool indicesSeamCheck[3] = {
                excludeVertices.find(faceIndices[0]) == excludeVertices.end(),
                excludeVertices.find(faceIndices[1]) == excludeVertices.end(),
                excludeVertices.find(faceIndices[2]) == excludeVertices.end()
            };
            for (int j = 0; j < 3; j++) {
                int next = (j + 1) % 3;
                int nextNext = (j + 2) % 3;
                if (indicesSeamCheck[j] && indicesSeamCheck[next]) {
                    std::pair<int, int> edge = std::make_pair(faceIndices[j], faceIndices[next]);
                    int thirdVertexIndex = faceIndices[nextNext];
                    if ((sourceVertices[edge.first] - sourceVertices[edge.second]).lengthSquared() < squareOfAllowedSmallestDistance) {
                        auto oppositeEdge = std::make_pair(edge.second, edge.first);
                        auto findOppositeFace = triangleEdgeMap.find(oppositeEdge);
                        if (findOppositeFace == triangleEdgeMap.end()) {
                            qDebug() << "Find opposite edge failed";
                            continue;
                        }
                        int oppositeFaceIndex = findOppositeFace->second.first;
                        if (((sourceVertices[edge.first] - sourceVertices[thirdVertexIndex]).lengthSquared() <
                                    (sourceVertices[edge.second] - sourceVertices[thirdVertexIndex]).lengthSquared()) &&
                                vertexAdjFaceCountMap[edge.second] <= 4 &&
                                weldVertexToMap.find(edge.second) == weldVertexToMap.end()) {
                            weldVertexToMap[edge.second] = edge.first;
                            weldTargetVertices.insert(edge.first);
                            processedFaces.insert(i);
                            processedFaces.insert(oppositeFaceIndex);
                            break;
                        } else if (vertexAdjFaceCountMap[edge.first] <= 4 &&
                                weldVertexToMap.find(edge.first) == weldVertexToMap.end()) {
                            weldVertexToMap[edge.first] = edge.second;
                            weldTargetVertices.insert(edge.second);
                            processedFaces.insert(i);
                            processedFaces.insert(oppositeFaceIndex);
                            break;
                        }
                    }
                }
            }
        }
    }
    int weldedCount = 0;
    int faceCountAfterWeld = 0;
    std::map<int, int> oldToNewVerticesMap;
    for (int i = 0; i < (int)sourceTriangles.size(); i++) {
        const auto &faceIndices = sourceTriangles[i];
        std::vector<int> mappedFaceIndices;
        bool errored = false;
        for (const auto &index: faceIndices) {
            int finalIndex = index;
            int mapTimes = 0;
            while (mapTimes < 500) {
                auto findMapResult = weldVertexToMap.find(finalIndex);
                if (findMapResult == weldVertexToMap.end())
                    break;
                finalIndex = findMapResult->second;
                mapTimes++;
            }
            if (mapTimes >= 500) {
                qDebug() << "Map too much times";
                errored = true;
                break;
            }
            mappedFaceIndices.push_back(finalIndex);
        }
        if (errored || mappedFaceIndices.size() < 3)
            continue;
        bool welded = false;
        for (decltype(mappedFaceIndices.size()) j = 0; j < mappedFaceIndices.size(); j++) {
            int next = (j + 1) % 3;
            if (mappedFaceIndices[j] == mappedFaceIndices[next]) {
                welded = true;
                break;
            }
        }
        if (welded) {
            weldedCount++;
            continue;
        }
        faceCountAfterWeld++;
        std::vector<size_t> newFace;
        for (const auto &index: mappedFaceIndices) {
            auto findMap = oldToNewVerticesMap.find(index);
            if (findMap == oldToNewVerticesMap.end()) {
                size_t newIndex = destVertices.size();
                newFace.push_back(newIndex);
                destVertices.push_back(sourceVertices[index]);
                oldToNewVerticesMap.insert({index, newIndex});
            } else {
                newFace.push_back(findMap->second);
            }
        }
        destTriangles.push_back(newFace);
    }
    return weldedCount;
}

bool nodemesh::isManifold(const std::vector<std::vector<size_t>> &faces)
{
    std::set<std::pair<size_t, size_t>> halfEdges;
    for (const auto &face: faces) {
        for (size_t i = 0; i < face.size(); ++i) {
            size_t j = (i + 1) % face.size();
            auto insertResult = halfEdges.insert({face[i], face[j]});
            if (!insertResult.second)
                return false;
        }
    }
    for (const auto &it: halfEdges) {
        if (halfEdges.find({it.second, it.first}) == halfEdges.end())
            return false;
    }
    return true;
}

void nodemesh::trim(std::vector<QVector3D> *vertices, bool normalize)
{
    float xLow = std::numeric_limits<float>::max();
    float xHigh = std::numeric_limits<float>::lowest();
    float yLow = std::numeric_limits<float>::max();
    float yHigh = std::numeric_limits<float>::lowest();
    float zLow = std::numeric_limits<float>::max();
    float zHigh = std::numeric_limits<float>::lowest();
    for (const auto &position: *vertices) {
        if (position.x() < xLow)
            xLow = position.x();
        else if (position.x() > xHigh)
            xHigh = position.x();
        if (position.y() < yLow)
            yLow = position.y();
        else if (position.y() > yHigh)
            yHigh = position.y();
        if (position.z() < zLow)
            zLow = position.z();
        else if (position.z() > zHigh)
            zHigh = position.z();
    }
    float xMiddle = (xHigh + xLow) * 0.5;
    float yMiddle = (yHigh + yLow) * 0.5;
    float zMiddle = (zHigh + zLow) * 0.5;
    if (normalize) {
        float xSize = xHigh - xLow;
        float ySize = yHigh - yLow;
        float zSize = zHigh - zLow;
        float longSize = ySize;
        if (xSize > longSize)
            longSize = xSize;
        if (zSize > longSize)
            longSize = zSize;
        if (qFuzzyIsNull(longSize))
            longSize = 0.000001;
        for (auto &position: *vertices) {
            position.setX((position.x() - xMiddle) / longSize);
            position.setY((position.y() - yMiddle) / longSize);
            position.setZ((position.z() - zMiddle) / longSize);
        }
    } else {
        for (auto &position: *vertices) {
            position.setX((position.x() - xMiddle));
            position.setY((position.y() - yMiddle));
            position.setZ((position.z() - zMiddle));
        }
    }
}

void nodemesh::subdivideFace2D(std::vector<QVector2D> *face)
{
    auto oldFace = *face;
    face->clear();
    for (size_t i = 0; i < oldFace.size(); ++i) {
        size_t j = (i + 1) % oldFace.size();
        QVector2D direct = (oldFace[i] + oldFace[j]).normalized();
        float length = (oldFace[i].length() + oldFace[j].length()) * 0.4; // 0.4 = 0.5 * 0.8
        face->push_back(oldFace[i] * 0.8);
        face->push_back(direct * length);
    }
}

void nodemesh::chamferFace2D(std::vector<QVector2D> *face)
{
    auto oldFace = *face;
    face->clear();
    for (size_t i = 0; i < oldFace.size(); ++i) {
        size_t j = (i + 1) % oldFace.size();
        face->push_back(oldFace[i] * 0.8 + oldFace[j] * 0.2);
        face->push_back(oldFace[i] * 0.2 + oldFace[j] * 0.8);
    }
}
