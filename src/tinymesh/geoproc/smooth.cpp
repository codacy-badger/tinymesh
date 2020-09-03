#define TINYMESH_API_EXPORT
#include "smooth.h"

#include <algorithm>

#include "core/openmp.h"
#include "polymesh/face.h"
#include "polymesh/vertex.h"
#include "polymesh/halfedge.h"

namespace tinymesh {

void smooth(Mesh &mesh, double strength) {
    // Volonoi tessellation
    const int nv = mesh.num_vertices();
    std::vector<Vec3> centroids(nv);
    std::vector<Vec3> normals(nv);

    // Compute centroids and tangent planes
    omp_parallel_for (int i = 0; i < mesh.num_vertices(); i++) {
        Vertex *v = mesh.vertex(i);

        // Collect surrounding vertices
        Vec3 org = v->pos();
        std::vector<Vec3> pts;
        for (auto vit = v->v_begin(); vit != v->v_end(); ++vit) {
            pts.push_back(vit->pos());
        }

        // Compute centroids, tangents, and binormals
        Vec3 cent(0.0);
        Vec3 norm(0.0);
        for (int i = 0; i < pts.size(); i++) {
            const int j = (i + 1) % pts.size();
            Vec3 e1 = pts[i] - org;
            Vec3 e2 = pts[j] - org;
            Vec3 g = (org + pts[i] + pts[j]) / 3.0;

            cent += g;
            norm += cross(e1, e2);
        }

        cent /= pts.size();
        const double l = length(norm);

        if (l != 0.0) {
            centroids[i] = cent;
            normals[i] = norm / l;
        }
    }

    // Update vertex positions
    omp_parallel_for (int i = 0; i < mesh.num_vertices(); i++) {
        Vertex *v = mesh.vertex(i);
        if (v->isBoundary()) {
            continue;
        }

        if (length(normals[i]) != 0.0) {
            const Vec3 pt = v->pos();
            Vec3 e = centroids[i] - pt;
            e -= normals[i] * dot(e, normals[i]);
            Vec3 newPos = (1.0 - strength) * v->pos() + strength * (pt + e);
            v->setPos(newPos);
        }
    }
}

}  // namespace tinymesh