#include "partition.h"

SpacePartitioner::SpacePartitioner(const std::vector<ContourPlane>& contourPlanes) 
    : originalPlanes(contourPlanes) {
    BoundingBox bbox = computeTightBoundingBox();
    addBoundingBoxPlanes(bbox);
    classifyPlanes();
}

bool SpacePartitioner::arePlanesParallel(const Plane& p1, const Plane& p2) {
    glm::vec3 n1(p1.a, p1.b, p1.c);
    glm::vec3 n2(p2.a, p2.b, p2.c);
    return glm::all(glm::epsilonEqual(
        glm::abs(glm::normalize(n1)), 
        glm::abs(glm::normalize(n2)), 
        0.0001f
    ));
}

void SpacePartitioner::classifyPlanes() {
    for (const auto& contourPlane : originalPlanes) {
        bool foundGroup = false;
        for (auto& group : parallelGroups) {
            if (arePlanesParallel(contourPlane.plane, group.planes[0].plane)) {
                group.planes.push_back(contourPlane);
                foundGroup = true;
                break;
            }
        }
        
        if (!foundGroup) {
            PlaneGroup newGroup;
            newGroup.normal = glm::vec3(
                contourPlane.plane.a,
                contourPlane.plane.b,
                contourPlane.plane.c
            );
            newGroup.planes.push_back(contourPlane);
            parallelGroups.push_back(newGroup);
        }
    }
}

glm::vec3 SpacePartitioner::computePlaneIntersection(
    const Plane& p1, const Plane& p2, const Plane& p3) {
    glm::mat3 A(
        p1.a, p1.b, p1.c,
        p2.a, p2.b, p2.c,
        p3.a, p3.b, p3.c
    );
    glm::vec3 b(-p1.d, -p2.d, -p3.d);
    return glm::inverse(A) * b;
}

std::vector<ConvexCell> SpacePartitioner::computeCells() {
    computeParallelPlaneCells();
    computeNonParallelPlaneCells();
    return cells;
}

void SpacePartitioner::renderCells() {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    
    for (const auto& cell : cells) {
        glBegin(GL_LINES);
        for (const auto& edge : cell.edges) {
            const auto& v1 = cell.vertices[edge.vertexIndex1];
            const auto& v2 = cell.vertices[edge.vertexIndex2];
            
            glColor3f(0.0f, 1.0f, 0.0f);
            glVertex3f(v1.x, v1.y, v1.z);
            glVertex3f(v2.x, v2.y, v2.z);
        }
        glEnd();
    }
}

// Add to partition.cpp
void SpacePartitioner::computeParallelPlaneCells() {
    for (const auto& group : parallelGroups) {
        if (group.planes.size() < 2) continue;

        // Sort planes by distance along normal
        auto sortedPlanes = group.planes;
        std::sort(sortedPlanes.begin(), sortedPlanes.end(), 
            [](const ContourPlane& a, const ContourPlane& b) {
                return a.plane.d < b.plane.d;
            });

        // Create cells between consecutive parallel planes
        for (size_t i = 0; i < sortedPlanes.size() - 1; ++i) {
            createSlabCell(sortedPlanes[i].plane, sortedPlanes[i + 1].plane);
        }
    }
}

void SpacePartitioner::createSlabCell(const Plane& bottom, const Plane& top) {
    ConvexCell cell;
    cell.boundaryPlanes.push_back(bottom);
    cell.boundaryPlanes.push_back(top);

    // Create vertices for the slab (assuming a bounding box)
    float size = 100.0f; // Adjust based on your scene size
    glm::vec3 normal(bottom.a, bottom.b, bottom.c);
    glm::vec3 tangent = glm::cross(normal, glm::vec3(0, 1, 0));
    glm::vec3 bitangent = glm::cross(normal, tangent);

    // Create 8 vertices for the slab
    std::vector<Vertex> vertices;
    for (int i = 0; i < 8; ++i) {
        glm::vec3 pos = (i & 1 ? tangent : -tangent) * size +
                        (i & 2 ? bitangent : -bitangent) * size +
                        normal * (i & 4 ? bottom.d : top.d);
        vertices.push_back(Vertex(pos.x, pos.y, pos.z));
    }

    // Create edges
    for (int i = 0; i < 4; ++i) {
        Edge edge;
        edge.vertexIndex1 = i;
        edge.vertexIndex2 = (i + 1) % 4;
        cell.edges.push_back(edge);
        
        edge.vertexIndex1 = i + 4;
        edge.vertexIndex2 = ((i + 1) % 4) + 4;
        cell.edges.push_back(edge);
        
        edge.vertexIndex1 = i;
        edge.vertexIndex2 = i + 4;
        cell.edges.push_back(edge);
    }

    cell.vertices = std::move(vertices);
    cells.push_back(cell);
}

void SpacePartitioner::computeNonParallelPlaneCells() {
    // Find non-parallel planes
    for (const auto& plane : originalPlanes) {
        bool isNonParallel = true;
        for (const auto& group : parallelGroups) {
            if (arePlanesParallel(plane.plane, group.planes[0].plane)) {
                isNonParallel = false;
                break;
            }
        }
        if (isNonParallel) {
            nonParallelPlanes.push_back(plane);
        }
    }

    if (nonParallelPlanes.size() < 3) return;

    // Find intersection vertices
    findIntersectionVertices(nonParallelPlanes);
}


void SpacePartitioner::findIntersectionVertices(const std::vector<ContourPlane>& planes) {
    std::vector<Vertex> intersectionVertices;
    
    // Find all triple plane intersections
    for (size_t i = 0; i < planes.size() - 2; ++i) {
        for (size_t j = i + 1; j < planes.size() - 1; ++j) {
            for (size_t k = j + 1; k < planes.size(); ++k) {
                // Check if any pair of planes is nearly parallel
                if (arePlanesParallel(planes[i].plane, planes[j].plane) ||
                    arePlanesParallel(planes[j].plane, planes[k].plane) ||
                    arePlanesParallel(planes[i].plane, planes[k].plane)) {
                    continue; // Skip if any planes are nearly parallel
                }

                try {
                    glm::vec3 intersection = computePlaneIntersection(
                        planes[i].plane, planes[j].plane, planes[k].plane);
                    
                    // Check if intersection point is valid
                    bool isValid = true;
                    for (const auto& plane : planes) {
                        if (computeDistance(plane.plane, intersection) > 0.0001f) {
                            isValid = false;
                            break;
                        }
                    }
                    
                    if (isValid) {
                        Vertex v(intersection.x, intersection.y, intersection.z);
                        v.associatedPlanes = {(int)i, (int)j, (int)k};
                        intersectionVertices.push_back(v);
                    }
                }
                catch (...) {
                    // Handle other matrix inversion errors
                    std::cerr << "Error computing intersection for planes " 
                              << i << ", " << j << ", " << k << std::endl;
                    continue;
                }
            }
        }
    }

    // Create cells from intersection vertices
    if (!intersectionVertices.empty()) {
        std::vector<Plane> boundaryPlanes;
        for (const auto& contourPlane : planes) {
            boundaryPlanes.push_back(contourPlane.plane);
        }
        createCellFromIntersection(intersectionVertices, boundaryPlanes);
    }
}

float SpacePartitioner::computeDistance(const Plane& plane, const glm::vec3& point) {
    return plane.a * point.x + plane.b * point.y + plane.c * point.z + plane.d;
}

void SpacePartitioner::createCellFromIntersection(
    const std::vector<Vertex>& cellVertices, 
    const std::vector<Plane>& boundaryPlanes) {
    
    ConvexCell cell;
    cell.vertices = cellVertices;
    
    // Extract only the plane information from ContourPlanes
    std::vector<Plane> planes;
    for (const auto& boundaryPlane : boundaryPlanes) {
        planes.push_back(boundaryPlane);  // Copy just the plane data
    }
    cell.boundaryPlanes = planes;

    // Create edges between vertices that lie on the same plane
    for (size_t i = 0; i < cellVertices.size(); ++i) {
        for (size_t j = i + 1; j < cellVertices.size(); ++j) {
            // Check if vertices share at least two planes
            std::vector<int> sharedPlanes;
            std::set_intersection(
                cellVertices[i].associatedPlanes.begin(), 
                cellVertices[i].associatedPlanes.end(),
                cellVertices[j].associatedPlanes.begin(), 
                cellVertices[j].associatedPlanes.end(),
                std::back_inserter(sharedPlanes)
            );

            if (sharedPlanes.size() >= 2) {
                Edge edge;
                edge.vertexIndex1 = i;
                edge.vertexIndex2 = j;
                cell.edges.push_back(edge);
            }
        }
    }

    cells.push_back(cell);
}

// Add to partition.cpp
BoundingBox SpacePartitioner::computeTightBoundingBox() const {
    BoundingBox bbox;
    bbox.min = glm::vec3(FLT_MAX);
    bbox.max = glm::vec3(-FLT_MAX);

    // Find bounds from all vertices in contour planes
    for (const auto& contourPlane : originalPlanes) {
        for (const auto& vertex : contourPlane.vertices) {
            bbox.min.x = std::min(bbox.min.x, vertex.x);
            bbox.min.y = std::min(bbox.min.y, vertex.y);
            bbox.min.z = std::min(bbox.min.z, vertex.z);
            
            bbox.max.x = std::max(bbox.max.x, vertex.x);
            bbox.max.y = std::max(bbox.max.y, vertex.y);
            bbox.max.z = std::max(bbox.max.z, vertex.z);
        }
    }

    // Expand bbox by factor of 2
    glm::vec3 center = (bbox.max + bbox.min) * 0.5f;
    glm::vec3 extent = bbox.max - center;
    bbox.min = center - extent * 2.0f;
    bbox.max = center + extent * 2.0f;

    return bbox;
}

void SpacePartitioner::addBoundingBoxPlanes(const BoundingBox& bbox) {
    // Add six bounding box planes
    ContourPlane boxPlanes[6];
    
    // Left plane (x = min.x)
    boxPlanes[0].plane = Plane(1.0f, 0.0f, 0.0f, -bbox.min.x);
    // Right plane (x = max.x)
    boxPlanes[1].plane = Plane(-1.0f, 0.0f, 0.0f, bbox.max.x);
    // Bottom plane (y = min.y)
    boxPlanes[2].plane = Plane(0.0f, 1.0f, 0.0f, -bbox.min.y);
    // Top plane (y = max.y)
    boxPlanes[3].plane = Plane(0.0f, -1.0f, 0.0f, bbox.max.y);
    // Front plane (z = min.z)
    boxPlanes[4].plane = Plane(0.0f, 0.0f, 1.0f, -bbox.min.z);
    // Back plane (z = max.z)
    boxPlanes[5].plane = Plane(0.0f, 0.0f, -1.0f, bbox.max.z);

    // Add vertices for each plane (for visualization)
    for (int i = 0; i < 6; ++i) {
        ContourPlane& boxPlane = boxPlanes[i];
        boxPlane.numVertices = 4;
        boxPlane.vertices.resize(4);
        
        // Calculate vertices based on plane orientation
        if (i < 2) { // X planes
            float x = (i == 0) ? bbox.min.x : bbox.max.x;
            boxPlane.vertices = {
                Vertex(x, bbox.min.y, bbox.min.z),
                Vertex(x, bbox.max.y, bbox.min.z),
                Vertex(x, bbox.max.y, bbox.max.z),
                Vertex(x, bbox.min.y, bbox.max.z)
            };
        } else if (i < 4) { // Y planes
            float y = (i == 2) ? bbox.min.y : bbox.max.y;
            boxPlane.vertices = {
                Vertex(bbox.min.x, y, bbox.min.z),
                Vertex(bbox.max.x, y, bbox.min.z),
                Vertex(bbox.max.x, y, bbox.max.z),
                Vertex(bbox.min.x, y, bbox.max.z)
            };
        } else { // Z planes
            float z = (i == 4) ? bbox.min.z : bbox.max.z;
            boxPlane.vertices = {
                Vertex(bbox.min.x, bbox.min.y, z),
                Vertex(bbox.max.x, bbox.min.y, z),
                Vertex(bbox.max.x, bbox.max.y, z),
                Vertex(bbox.min.x, bbox.max.y, z)
            };
        }

        originalPlanes.push_back(boxPlane);
    }
}