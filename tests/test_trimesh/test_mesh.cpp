#include "gtest/gtest.h"

#include <cstdio>
#include <sstream>
#include <vector>
#include <tuple>
#include <algorithm>

#include "tinymesh/tinymesh.h"
using namespace tinymesh;

#include "test_config.h"

class MeshBasicTest : public ::testing::Test {
protected:
    MeshBasicTest() {}
    virtual ~MeshBasicTest() {}
};

class MeshTest : public MeshBasicTest, public ::testing::WithParamInterface<std::string> {
protected:
    MeshTest() {}
    virtual ~MeshTest() {}

    void SetUp() {
        filename = std::string(DATA_DIRECTORY) + "models/" + GetParam();
    }

    std::string filename;
};

TEST_F(MeshBasicTest, MeshInvaidLoad) {
    Mesh mesh;
    ASSERT_DEATH(mesh.load("mesh_not_found.ply"), "");
}

TEST_P(MeshTest, MeshLoad) {
    Mesh mesh;
    mesh.load(filename);
    ASSERT_TRUE(mesh.verify());

    ASSERT_GT(mesh.num_vertices(), 0);
    ASSERT_GT(mesh.num_faces(), 0);
    ASSERT_GT(mesh.num_halfedges(), 0);

    const int nVerts = mesh.num_vertices();
    for (int i = 0; i < nVerts; i++) {
        ASSERT_GT(mesh.vertex(i)->degree(), 0);
    }
}

std::vector<std::string> filenames = {
    "box.obj",
    "sphere.obj",
    "torus.obj",
    "bunny.ply",
};

INSTANTIATE_TEST_CASE_P(, MeshTest, ::testing::ValuesIn(filenames));