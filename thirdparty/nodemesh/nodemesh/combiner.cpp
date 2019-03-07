#include <nodemesh/combiner.h>
#include <nodemesh/misc.h>
#include <nodemesh/positionkey.h>
#include <nodemesh/cgalmesh.h>
#include <CGAL/Polygon_mesh_processing/corefinement.h>
#include <CGAL/Polygon_mesh_processing/repair.h>
#include <CGAL/Polygon_mesh_processing/triangulate_faces.h>
#include <QDebug>
#include <map>

typedef CGAL::Exact_predicates_exact_constructions_kernel ExactKernel;
typedef CGAL::Surface_mesh<ExactKernel::Point_3> ExactMesh;

namespace nodemesh 
{

Combiner::Mesh::Mesh(const std::vector<QVector3D> &vertices, const std::vector<std::vector<size_t>> &faces, bool removeSelfIntersects)
{
    ExactMesh *cgalMesh = nullptr;
    if (!faces.empty()) {
        std::vector<QVector3D> triangleVertices = vertices;
        std::vector<std::vector<size_t>> triangles;
        if (nodemesh::triangulate(triangleVertices, faces, triangles)) {
            cgalMesh = buildCgalMesh<ExactKernel>(triangleVertices, triangles);
            if (!CGAL::is_valid_polygon_mesh(*cgalMesh)) {
                qDebug() << "Mesh is not valid polygon";
                delete cgalMesh;
                cgalMesh = nullptr;
            } else {
                if (CGAL::Polygon_mesh_processing::does_self_intersect(*cgalMesh)) {
                    m_isSelfIntersected = true;
                    if (removeSelfIntersects) {
                        if (!CGAL::Polygon_mesh_processing::remove_self_intersections(*cgalMesh)) {
                            delete cgalMesh;
                            cgalMesh = nullptr;
                        }
                    } else {
                        delete cgalMesh;
                        cgalMesh = nullptr;
                    }
                }
            }
        }
    }
    m_privateData = cgalMesh;
}

Combiner::Mesh::Mesh(const Mesh &other)
{
    if (other.m_privateData) {
        m_privateData = new ExactMesh(*(ExactMesh *)other.m_privateData);
    }
}

Combiner::Mesh::~Mesh()
{
    ExactMesh *cgalMesh = (ExactMesh *)m_privateData;
    delete cgalMesh;
}

void Combiner::Mesh::fetch(std::vector<QVector3D> &vertices, std::vector<std::vector<size_t>> &faces) const
{
    ExactMesh *exactMesh = (ExactMesh *)m_privateData;
    if (nullptr == exactMesh)
        return;
    
    fetchFromCgalMesh<ExactKernel>(exactMesh, vertices, faces);
}

bool Combiner::Mesh::isNull() const
{
    return nullptr == m_privateData;
}

bool Combiner::Mesh::isSelfIntersected() const
{
    return m_isSelfIntersected;
}

Combiner::Mesh *Combiner::combine(const Mesh &firstMesh, const Mesh &secondMesh, Method method,
    std::vector<std::pair<Source, size_t>> *combinedVerticesComeFrom)
{
    ExactMesh *resultCgalMesh = nullptr;
    ExactMesh *firstCgalMesh = (ExactMesh *)firstMesh.m_privateData;
    ExactMesh *secondCgalMesh = (ExactMesh *)secondMesh.m_privateData;
    std::map<PositionKey, std::pair<Source, size_t>> verticesSourceMap;
    
    auto addToSourceMap = [&](ExactMesh *mesh, Source source) {
        size_t vertexIndex = 0;
        for (auto vertexIt = mesh->vertices_begin(); vertexIt != mesh->vertices_end(); vertexIt++) {
            auto point = mesh->point(*vertexIt);
            float x = (float)CGAL::to_double(point.x());
            float y = (float)CGAL::to_double(point.y());
            float z = (float)CGAL::to_double(point.z());
            auto insertResult = verticesSourceMap.insert({{x, y, z}, {source, vertexIndex}});
            if (!insertResult.second) {
                qDebug() << "Position key conflict:" << QVector3D {x, y, z} << "with:" << insertResult.first->first.position();
            }
            ++vertexIndex;
        }
    };
    if (nullptr != combinedVerticesComeFrom) {
        addToSourceMap(firstCgalMesh, Source::First);
        addToSourceMap(secondCgalMesh, Source::Second);
    }
    
    if (Method::Union == method) {
        resultCgalMesh = new ExactMesh;
        try {
            if (!CGAL::Polygon_mesh_processing::corefine_and_compute_union(*firstCgalMesh, *secondCgalMesh, *resultCgalMesh)) {
                delete resultCgalMesh;
                resultCgalMesh = nullptr;
            }
        } catch (...) {
            delete resultCgalMesh;
            resultCgalMesh = nullptr;
        }
    } else if (Method::Diff == method) {
        resultCgalMesh = new ExactMesh;
        try {
            if (!CGAL::Polygon_mesh_processing::corefine_and_compute_difference(*firstCgalMesh, *secondCgalMesh, *resultCgalMesh)) {
                delete resultCgalMesh;
                resultCgalMesh = nullptr;
            }
        } catch (...) {
            delete resultCgalMesh;
            resultCgalMesh = nullptr;
        }
    }

    if (nullptr != combinedVerticesComeFrom) {
        combinedVerticesComeFrom->clear();
        if (nullptr != resultCgalMesh) {
            for (auto vertexIt = resultCgalMesh->vertices_begin(); vertexIt != resultCgalMesh->vertices_end(); vertexIt++) {
                auto point = resultCgalMesh->point(*vertexIt);
                float x = (float)CGAL::to_double(point.x());
                float y = (float)CGAL::to_double(point.y());
                float z = (float)CGAL::to_double(point.z());
                auto findSource = verticesSourceMap.find(PositionKey(x, y, z));
                if (findSource == verticesSourceMap.end()) {
                    combinedVerticesComeFrom->push_back({Source::None, 0});
                } else {
                    combinedVerticesComeFrom->push_back(findSource->second);
                }
            }
        }
    }
    
    if (nullptr == resultCgalMesh)
        return nullptr;
    
    Mesh *mesh = new Mesh;
    mesh->m_privateData = resultCgalMesh;
    return mesh;
}

}
