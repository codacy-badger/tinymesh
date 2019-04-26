#define TINYMESH_API_EXPORT
#include "mesh.h"

#include <cstring>
#include <iostream>
#include <fstream>
#include <set>
#include <functional>
#include <unordered_map>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

#include "core/vec.h"
#include "trimesh/vertex.h"
#include "trimesh/halfedge.h"
#include "trimesh/face.h"
#include "tiny_obj_loader.h"
#include "tinyply.h"

namespace tinymesh {

// ----------
// Mesh
// ----------

Mesh::Mesh() {}

Mesh::Mesh(const std::string &filename) {
    load(filename);
}

void Mesh::load(const std::string &filename) {
    const std::string ext = fs::path(filename.c_str()).extension();
    if (ext == ".obj") {
        loadOBJ(filename);
    } else if (ext == ".ply") {
        loadPLY(filename);
    } else {
        FatalError("Unsupported file extension: %s", ext.c_str());
    }

    // Clear
    m_hes.clear();
    m_faces.clear();

    // Setup halfedge structure
    std::unordered_map<Vertex*, std::vector<Halfedge*>> perVertexHEs;
    for (int i = 0; i < m_indices.size(); i += 3) {
        auto he0 = std::make_shared<Halfedge>();
        auto he1 = std::make_shared<Halfedge>();
        auto he2 = std::make_shared<Halfedge>();

        he0->m_src = m_verts[m_indices[i + 0]].get();
        he1->m_src = m_verts[m_indices[i + 1]].get();
        he2->m_src = m_verts[m_indices[i + 2]].get();

        he0->m_next = he1.get();
        he1->m_next = he2.get();
        he2->m_next = he0.get();

        m_verts[m_indices[i + 0]]->m_he = he0.get();
        m_verts[m_indices[i + 1]]->m_he = he1.get();
        m_verts[m_indices[i + 2]]->m_he = he2.get();

        auto face = std::make_shared<Face>();
        face->m_he = he0.get();

        he0->m_face = face.get();
        he1->m_face = face.get();
        he2->m_face = face.get();

        perVertexHEs[m_verts[m_indices[i + 0]].get()].push_back(he0.get());
        perVertexHEs[m_verts[m_indices[i + 1]].get()].push_back(he1.get());
        perVertexHEs[m_verts[m_indices[i + 2]].get()].push_back(he2.get());

        m_hes.push_back(he0);
        m_hes.push_back(he1);
        m_hes.push_back(he2);
        m_faces.push_back(face);
    }

    // Set opposite half edges
    for (const auto &he0 : m_hes) {
        for (Halfedge *he1 : perVertexHEs[he0->dst()]) {
            if (he0->src() == he1->dst() && he0->dst() == he1->src()) {
                he0->m_rev = he1;
                break;
            }
        }
    }

    // Put indices
    for (int i = 0; i < m_verts.size(); i++) {
        m_verts[i]->m_index = i;
    }

    for (int i = 0; i < m_hes.size(); i++) {
        m_hes[i]->m_index = i;
    }

    for (int i = 0; i < m_faces.size(); i++) {
        m_faces[i]->m_index = i;
    }
}

void Mesh::save(const std::string &filename) {
    std::ofstream writer(filename.c_str(), std::ios::out);
    if (writer.fail()) {
        FatalError("Failed to open file: %s", filename.c_str());
    }

    for (const auto &v : m_verts) {
        writer << "v " << v->pt().x << " " << v->pt().y << " " << v->pt().z << std::endl;
    }

    for (const auto &f : m_faces) {
        const int i0 = f->m_he->src()->index();
        const int i1 = f->m_he->next()->src()->index();
        const int i2 = f->m_he->prev()->src()->index();
        if (i0 < m_verts.size() && i1 <= m_verts.size() && i2 <= m_verts.size()) {
            writer << "f " << (i0 + 1) << " " << (i1 + 1) << " " << (i2 + 1) << std::endl;
        }
    }

    writer.close();
}

void Mesh::loadOBJ(const std::string &filename) {
    // Open
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> mats;
    std::string warnMsg;
    std::string errMsg;
    bool success = tinyobj::LoadObj(&attrib, &shapes, &mats, &warnMsg, &errMsg, filename.c_str());
    if (!errMsg.empty()) {
        Warn("%s", errMsg.c_str());
    }

    if (!success) {
        FatalError("Failed to load *.obj file: %s", filename.c_str());
    }

    // Traverse triangles
    m_verts.clear();
    m_indices.clear();
    std::unordered_map<Vec, uint32_t> uniqueVertices;
    for (const auto &shape : shapes) {
        for (const auto &index : shape.mesh.indices) {
            Vec v;
            if (index.vertex_index >= 0) {
                v = Vec(attrib.vertices[index.vertex_index * 3 + 0],
                        attrib.vertices[index.vertex_index * 3 + 1],
                        attrib.vertices[index.vertex_index * 3 + 2]);
            }

            if (uniqueVertices.count(v) == 0) {
                uniqueVertices[v] = static_cast<uint32_t>(m_verts.size());
                m_verts.push_back(std::make_shared<Vertex>(v));
            }

            m_indices.push_back(uniqueVertices[v]);
        }
    }
}

void Mesh::loadPLY(const std::string &filename) {
    using tinyply::PlyFile;
    using tinyply::PlyData;

    try {
        // Open
        std::ifstream reader(filename.c_str(), std::ios::binary);
        if (reader.fail()) {
            FatalError("Failed to open file: %s", filename.c_str());
        }

        // Read header
        PlyFile file;
        file.parse_header(reader);

        // Request vertex data
        std::shared_ptr<PlyData> vert_data, norm_data, uv_data, face_data;
        try {
            vert_data = file.request_properties_from_element("vertex", { "x", "y", "z" });
        } catch (std::exception &e) {
            std::cerr << "tinyply exception: " << e.what() << std::endl;
        }

        try {
            norm_data = file.request_properties_from_element("vertex", { "nx", "ny", "nz" });
        } catch (std::exception &e) {
            std::cerr << "tinyply exception: " << e.what() << std::endl;
        }

        try {
            uv_data = file.request_properties_from_element("vertex", { "u", "v" });
        } catch (std::exception &e) {
            std::cerr << "tinyply exception: " << e.what() << std::endl;
        }

        try {
            face_data = file.request_properties_from_element("face", { "vertex_indices" }, 3);
        } catch (std::exception &e) {
            std::cerr << "tinyply exception: " << e.what() << std::endl;
        }

        // Read vertex data
        file.read(reader);

        // Copy vertex data
        const size_t numVerts = vert_data->count;
        std::vector<float> raw_vertices, raw_normals, raw_uv;
        if (vert_data) {
            raw_vertices.resize(numVerts * 3);
            std::memcpy(raw_vertices.data(), vert_data->buffer.get(), sizeof(float) * numVerts * 3);
        }

        const size_t numFaces = face_data->count;
        std::vector<uint32_t> raw_indices(numFaces * 3);
        std::memcpy(raw_indices.data(), face_data->buffer.get(), sizeof(uint32_t) * numFaces * 3);

        std::unordered_map<Vec, uint32_t> uniqueVertices;
        m_verts.clear();
        for (uint32_t i : raw_indices) {
            Vec pos;

            if (vert_data) {
                pos = Vec(raw_vertices[i * 3 + 0],
                          raw_vertices[i * 3 + 1],
                          raw_vertices[i * 3 + 2]);
            }

            if (uniqueVertices.count(pos) == 0) {
                uniqueVertices[pos] = static_cast<uint32_t>(m_verts.size());
                m_verts.push_back(std::make_shared<Vertex>(pos));
            }

            m_indices.push_back(uniqueVertices[pos]);
        }
    } catch (const std::exception &e) {
        std::cerr << "Caught tinyply exception: " << e.what() << std::endl;
    }
}

bool Mesh::splitHE(Halfedge *he) {
    Halfedge *rev = he->rev();
    Vertex *v0 = he->src();
    Vertex *v1 = rev->src();
    if (v0->degree() < v1->degree()) {
        std::swap(he, rev);
        std::swap(v0, v1);
    }

    if (v0->degree() < 6) {
        return false;
    }

    // Prepare halfedge / face
    auto he_00 = new Halfedge();
    auto he_01 = new Halfedge();
    auto he_02 = new Halfedge();
    auto he_10 = new Halfedge();
    auto he_11 = new Halfedge();
    auto he_12 = new Halfedge();
    
    auto f0 = new Face();
    auto f1 = new Face();

    // Prepare new vertex
    const Vec newPt = 0.5 * (he->src()->pt() + he->dst()->pt());
    auto v_new = new Vertex(newPt);

    // Setup connection between new components
    he_00->m_next = he_01;
    he_01->m_next = he_02;
    he_02->m_next = he_00;
    he_10->m_next = he_11;
    he_11->m_next = he_12;
    he_12->m_next = he_10;

    he_00->m_face = f0;
    he_01->m_face = f0;
    he_02->m_face = f0;
    f0->m_he = he_00;

    he_10->m_face = f1;
    he_11->m_face = f1;
    he_12->m_face = f1;
    f1->m_he = he_10;

    // Update opposite halfedges
    Halfedge *whe0 = he->prev()->rev()->prev();
    Halfedge *wre0 = whe0->rev();
    Halfedge *whe1 = rev->next()->rev()->next();
    Halfedge *wre1 = whe1->rev();

    whe0->m_rev = he_01;
    wre0->m_rev = he_02;
    whe1->m_rev = he_12;
    wre1->m_rev = he_11;

    he_01->m_rev = whe0;
    he_02->m_rev = wre0;
    he_12->m_rev = whe1;
    he_11->m_rev = wre1;

    he_00->m_rev = he_10;
    he_10->m_rev = he_00;

    // Update halfedge origins
    he_00->m_src = v0;
    he_01->m_src = v_new;
    he_02->m_src = whe0->m_src;
    he_10->m_src = v_new;
    he_11->m_src = v0;
    he_12->m_src = wre1->m_src;

    he->m_src = v_new;
    he->prev()->rev()->m_src = v_new;
    rev->next()->m_src = v_new;
    whe1->m_src = v_new;

    // Update halfedges for vertices
    v0->m_he = he_00;
    v_new->m_he = he_10;

    // Add new components to the list
    addFace(f0);
    addFace(f1);
    addVertex(v_new);
    addHalfedge(he_00);
    addHalfedge(he_01);
    addHalfedge(he_02);
    addHalfedge(he_10);
    addHalfedge(he_11);
    addHalfedge(he_12);

    return true;
}

bool Mesh::collapseHE(Halfedge* he) {
    Halfedge *rev = he->m_rev;
    const int d0 = he->src()->degree();
    const int d1 = rev->src()->degree();
    // Merge vertex to the one with smaller degree
    if (d0 > d1) {
        std::swap(he, rev);
    }

    Vertex *v0 = he->src();
    Vertex *v1 = he->prev()->src();
    Vertex *v2 = rev->src();
    Vertex *v3 = rev->prev()->src();
    if (v0->degree() <= 3 || v1->degree() <= 4 || v2->degree() <= 3 || v3->degree() <= 4) {
        return false;
    }

    // Check unsafe collapse
    std::set<Vertex*> neighbors;
    for (auto it = v0->ohe_begin(); it != v0->ohe_end(); ++it) {
        neighbors.insert(it->dst());
    }

    int numUnion = 0;
    for (auto it = v2->ohe_begin(); it != v2->ohe_end(); ++it) {
        if (neighbors.find(it->dst()) != neighbors.end()) {
            numUnion += 1;            
        }
    }

    if (numUnion != 2) {
        return false;
    }

    // Update halfedge of origin
    he->m_src->m_he = he->prev()->rev();

    // Update origins of all outward halfedges
    Vertex *v_remove = rev->m_src;
    Vertex *v_remain = he->m_src;
    for (auto it = he->m_src->ohe_begin(); it != he->m_src->ohe_end(); ++it) {
        it->m_src = v_remain;
    }

    for (auto it = rev->m_src->ohe_begin(); it != rev->m_src->ohe_end(); ++it) {
        it->m_src = v_remain;
    }

    // Update opposite halfedges
    Halfedge *he0 = he->m_next->m_rev;
    Halfedge *he1 = he->m_next->m_next->m_rev;
    he0->m_rev = he1;
    he1->m_rev = he0;

    Halfedge *he2 = rev->m_next->m_rev;
    Halfedge *he3 = rev->m_next->m_next->m_rev;
    he2->m_rev = he3;
    he3->m_rev = he2;

    // Update halfedge for wing-vertices
    he->prev()->src()->m_he = he->m_next->m_rev;
    rev->prev()->src()->m_he = rev->m_next->m_rev;

    // Remove faces
    Face *f0 = he->m_face;
    Face *f1 = rev->m_face;
    removeFace(f0);
    removeFace(f1);

    // Remove vertices
    removeVertex(v_remove);

    // Remove halfedges
    removeHalfedge(he->m_next->m_next);
    removeHalfedge(he->m_next);
    removeHalfedge(he);
    removeHalfedge(rev->m_next->m_next);
    removeHalfedge(rev->m_next);
    removeHalfedge(rev);

    return true;
}

bool Mesh::flipHE(Halfedge* he) {    
    Halfedge *rev = he->m_rev;
    if (!rev) {
        FatalError("Flip is called boundary halfedge!");
    }

    Vertex *u0 = he->src();
    Vertex *u1 = he->prev()->src();
    Vertex *u2 = rev->src();
    Vertex *u3 = rev->prev()->src();
    if (u0->degree() <= 4 || u2->degree() <= 4) {
        return false;
    }

    // Get surrounding vertices, halfedges and faces
    Halfedge *he0 = he->m_next;
    Halfedge *he1 = he->m_next->m_next;
    Halfedge *he2 = rev->m_next;
    Halfedge *he3 = rev->m_next->m_next;

    Vertex *v0 = he0->m_src;
    Vertex *v1 = he1->m_src;
    Vertex *v2 = he2->m_src;
    Vertex *v3 = he3->m_src;

    Face *f0 = he->m_face;
    Face *f1 = rev->m_face;

    // Update halfedges of start/end vertices
    v0->m_he = he0;
    v2->m_he = he2;

    // Update halfedge's start and end
    he->m_src = v3;
    rev->m_src = v1;

    // Update face circulation
    he->m_next = he1;
    he1->m_next = he2;
    he2->m_next = he;
    rev->m_next = he3;
    he3->m_next = he0;
    he0->m_next = rev;

    // Update faces
    f0->m_he = he;
    he->m_face = f0;
    he1->m_face = f0;
    he2->m_face = f0;

    f1->m_he = rev;
    rev->m_face = f1;
    he0->m_face = f1;
    he3->m_face = f1;

    return true;
}

void Mesh::verify() const {
    //for (int i = 0; i < m_verts.size(); i++) {
    //    Vertex *v = m_verts[i].get();
    //    if (v->index() != i) {
    //        fprintf(stderr, "Vertex index does not match array index: v[%d].index = %d\n", i, v->index());
    //    }
    //    verifyVertex(v);
    //}

    //for (int i = 0; i < m_hes.size(); i++) {
    //    auto he = m_hes[i];
    //    if (he->index() != i) {
    //        fprintf(stderr, "Halfedge index does not match array index: he[%d].index = %d\n", i, he->index());
    //    }

    //    verifyVertex(he->src());
    //    verifyVertex(he->dst());
    //}

    //for (int i = 0; i < m_faces.size(); i++) {
    //    auto f = m_faces[i];
    //    if (f->m_index != i) {
    //        printf("v: %d %d\n", f->m_index, i);
    //    }

    //    if (f->m_he->m_index >= m_hes.size()) {
    //        printf("v: %d %d\n", f->m_he->m_index, m_hes.size());
    //    }

    //    const int i0 = f->m_he->src()->index();
    //    const int i1 = f->m_he->next()->src()->index();
    //    const int i2 = f->m_he->prev()->src()->index();
    //    if (i0 >= m_verts.size() || i1 >= m_verts.size() || i2 >= m_verts.size()) {
    //        printf("%d %d %d, %d\n", i0, i1, i2, m_verts.size());
    //    }
    //}
}

void Mesh::verifyVertex(Vertex* v) const {
    if (v->index() >= m_verts.size()) {
        fprintf(stderr, "Vertex index out of range: %d > %d\n", v->index(), (int)m_verts.size());
    }

    Vec p = v->pt();
    if (std::isinf(p.x) || std::isnan(p.x) || std::isinf(p.y) || std::isnan(p.y) || std::isinf(p.z) || std::isnan(p.z)) {
        fprintf(stderr, "Inf of NaN found at v[%d]: (%f, %f, %f)\n", v->index(), p.x, p.y, p.z);
    }    
}

Mesh::VertexIterator Mesh::v_begin() {
    return Mesh::VertexIterator(m_verts);
}

Mesh::VertexIterator Mesh::v_end() {
    return Mesh::VertexIterator(m_verts, m_verts.size());
}

Mesh::HalfedgeIterator Mesh::he_begin() {
    return Mesh::HalfedgeIterator(m_hes);
}

Mesh::HalfedgeIterator Mesh::he_end() {
    return Mesh::HalfedgeIterator(m_hes, m_hes.size());
}

Mesh::FaceIterator Mesh::f_begin() {
    return Mesh::FaceIterator(m_faces);
}

Mesh::FaceIterator Mesh::f_end() {
    return Mesh::FaceIterator(m_faces, m_faces.size());
}

void Mesh::addVertex(Vertex *v) {
    v->m_index = m_verts.size();
    m_verts.emplace_back(v);
}

void Mesh::addHalfedge(Halfedge *he) {
    he->m_index = m_hes.size();
    m_hes.emplace_back(he);
}

void Mesh::addFace(Face *f) {
    f->m_index = m_faces.size();
    m_faces.emplace_back(f);
}

void Mesh::removeVertex(Vertex* v) {
    if (v->m_index < m_verts.size() - 1) {
        std::swap(m_verts[v->m_index], m_verts[m_verts.size() - 1]);
        std::swap(m_verts[v->m_index]->m_index , m_verts[m_verts.size() - 1]->m_index);
    } 
    m_verts.resize(m_verts.size() - 1);
}

void Mesh::removeHalfedge(Halfedge *he) {
    if (he->m_index < m_hes.size() - 1) {
        std::swap(m_hes[he->m_index], m_hes[m_hes.size() - 1]);
        std::swap(m_hes[he->m_index]->m_index, m_hes[m_hes.size() - 1]->m_index);
    }
    m_hes.resize(m_hes.size() - 1);
}

void Mesh::removeFace(Face *f) {
    if (f->m_index < m_faces.size() - 1) {
        std::swap(m_faces[f->m_index], m_faces[m_faces.size() - 1]);
        std::swap(m_faces[f->m_index]->m_index, m_faces[m_faces.size() - 1]->m_index);
    }
    m_faces.resize(m_faces.size() - 1);
}


// ----------
// VertexIterator
// ----------

Mesh::VertexIterator::VertexIterator(std::vector<std::shared_ptr<Vertex>> &vertices, int index)
    : m_verts{ vertices }
    , m_index{ index } {
}

bool Mesh::VertexIterator::operator!=(const Mesh::VertexIterator &it) const {
    return m_index != it.m_index;
}

Vertex &Mesh::VertexIterator::operator*() {
    return *m_verts[m_index];
}

Vertex *Mesh::VertexIterator::operator->() const {
    return m_index < m_verts.size() ? m_verts[m_index].get() : nullptr;
}

Vertex* Mesh::VertexIterator::ptr() const {
    return m_index < m_verts.size() ? m_verts[m_index].get() : nullptr;
}

Mesh::VertexIterator &Mesh::VertexIterator::operator++() {
    ++m_index;
    return *this;
}

Mesh::VertexIterator Mesh::VertexIterator::operator++(int) {
    int prev = m_index;
    ++m_index;
    return Mesh::VertexIterator(m_verts, prev);
}

Mesh::VertexIterator &Mesh::VertexIterator::operator--() {
    --m_index;
    return *this;
}

Mesh::VertexIterator Mesh::VertexIterator::operator--(int) {
    int prev = m_index;
    --m_index;
    return Mesh::VertexIterator(m_verts, prev);
}

// ----------
// HalfedgeIterator
// ----------

Mesh::HalfedgeIterator::HalfedgeIterator(std::vector<std::shared_ptr<Halfedge>>& hes, int index)
    : m_hes{ hes }
    , m_index{ index } {    
}

bool Mesh::HalfedgeIterator::operator!=(const HalfedgeIterator& it) const {
    return m_index != it.m_index;
}

Halfedge &Mesh::HalfedgeIterator::operator*() {
    return *m_hes[m_index];
}

Halfedge* Mesh::HalfedgeIterator::operator->() const {
    return m_index < m_hes.size() ? m_hes[m_index].get() : nullptr;
}

Halfedge* Mesh::HalfedgeIterator::ptr() const {
    return m_index < m_hes.size() ? m_hes[m_index].get() : nullptr;
}

Mesh::HalfedgeIterator &Mesh::HalfedgeIterator::operator++() {
    ++m_index;
    return *this;
}

Mesh::HalfedgeIterator Mesh::HalfedgeIterator::operator++(int) {
    int prev = m_index;
    ++m_index;
    return Mesh::HalfedgeIterator(m_hes, prev);
}

Mesh::HalfedgeIterator &Mesh::HalfedgeIterator::operator--() {
    --m_index;
    return *this;
}

Mesh::HalfedgeIterator Mesh::HalfedgeIterator::operator--(int) {
    int prev = m_index;
    --m_index;
    return Mesh::HalfedgeIterator(m_hes, prev);
}

// ----------
// FaceIterator
// ----------

Mesh::FaceIterator::FaceIterator(std::vector<std::shared_ptr<Face>> &faces, int index)
    : m_faces{ faces }
    , m_index{ index } {
}

bool Mesh::FaceIterator::operator!=(const FaceIterator &it) const {
    return m_index != it.m_index;
}

Face &Mesh::FaceIterator::operator*() {
    return *m_faces[m_index];
}

Face *Mesh::FaceIterator::operator->() const {
    return m_index < m_faces.size() ? m_faces[m_index].get() : nullptr;
}

Face *Mesh::FaceIterator::ptr() const {
    return m_index < m_faces.size() ? m_faces[m_index].get() : nullptr;
}

Mesh::FaceIterator &Mesh::FaceIterator::operator++() {
    ++m_index;
    return *this;
}

Mesh::FaceIterator Mesh::FaceIterator::operator++(int) {
    int prev = m_index;
    ++m_index;
    return Mesh::FaceIterator(m_faces, prev);
}

Mesh::FaceIterator &Mesh::FaceIterator::operator--() {
    --m_index;
    return *this;
}

Mesh::FaceIterator Mesh::FaceIterator::operator--(int) {
    int prev = m_index;
    --m_index;
    return Mesh::FaceIterator(m_faces, prev);
}

}  // namespace tinymesh
