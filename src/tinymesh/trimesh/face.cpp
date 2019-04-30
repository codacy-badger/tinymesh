#define TINYMESH_API_EXPORT
#include "face.h"

#include "halfedge.h"

namespace tinymesh {

Face::Face() {   
}


Face::VertexIterator Face::v_begin() {
    return Face::VertexIterator(halfedge_);
}

Face::VertexIterator Face::v_end() {
    return Face::VertexIterator(nullptr);
}

// ----------
// VertexIterator
// ----------

Face::VertexIterator::VertexIterator(Halfedge *he)
    : halfedge_{ he }
    , iter_{ he } {
}

bool Face::VertexIterator::operator!=(const Face::VertexIterator &it) const {
    return iter_ != it.iter_;
}

Vertex &Face::VertexIterator::operator*() {
    return *iter_->dst();
}

Vertex *Face::VertexIterator::ptr() const {
    return iter_->dst();
}

Vertex *Face::VertexIterator::operator->() const {
    return iter_->dst();
}

Face::VertexIterator &Face::VertexIterator::operator++() {
    iter_ = iter_->rev()->next();
    if (iter_ == halfedge_) {
        iter_ = nullptr;
    }
    return *this;
}

Face::VertexIterator Face::VertexIterator::operator++(int) {
    Halfedge *tmp = iter_;
    iter_ = iter_->rev()->next();
    if (iter_ == halfedge_) {
        iter_ = nullptr;
    }
    return Face::VertexIterator(tmp);
}

}  // namespace tinymesh
