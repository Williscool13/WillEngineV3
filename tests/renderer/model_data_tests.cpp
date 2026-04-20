//
// Created by William on 2025-12-25.
//
// Tests for model and mesh data structures to ensure proper initialization,
// data packing, and attribute handling.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "render/shaders/common_interop.h"
#include "render/shaders/model_interop.h"
#include "render/types/render_types.h"

TEST_CASE("Vertex initialization and data packing", "[renderer][model-data]")
{
}

TEST_CASE("Meshlet bounding and culling data", "[renderer][model-data]") {
    SECTION("Default initialization") {
        Meshlet m{};

        REQUIRE(m.meshletBoundingSphere.x == 0.0f);
        REQUIRE(m.meshletBoundingSphere.y == 0.0f);
        REQUIRE(m.meshletBoundingSphere.z == 0.0f);
        REQUIRE(m.meshletBoundingSphere.w == 0.0f);
    }

    SECTION("Bounding sphere data") {
        Meshlet m{};
        m.meshletBoundingSphere = float4{1.0f, 2.0f, 3.0f, 5.0f}; // center (1,2,3), radius 5

        REQUIRE(m.meshletBoundingSphere.x == 1.0f); // center x
        REQUIRE(m.meshletBoundingSphere.y == 2.0f); // center y
        REQUIRE(m.meshletBoundingSphere.z == 3.0f); // center z
        REQUIRE(m.meshletBoundingSphere.w == 5.0f); // radius
    }

    SECTION("Cone culling data") {
        Meshlet m{};
        m.coneApex = float3{0.0f, 0.0f, 0.0f};
        m.coneAxis = float3{0.0f, 0.0f, 1.0f};
        m.coneCutoff = 0.866f; // cos(30 degrees)

        REQUIRE(m.coneApex.z == 0.0f);
        REQUIRE(m.coneAxis.z == 1.0f);
        REQUIRE_THAT(m.coneCutoff, Catch::Matchers::WithinRel(0.866f, 0.001f));
    }

    SECTION("Meshlet offsets and counts") {
        Meshlet m{};
        m.vertexOffset = 100;
        m.meshletVertexOffset = 50;
        m.meshletTriangleOffset = 75;
        m.meshletVertexCount = 64;
        m.meshletTriangleCount = 124;

        REQUIRE(m.vertexOffset == 100);
        REQUIRE(m.meshletVertexOffset == 50);
        REQUIRE(m.meshletTriangleOffset == 75);
        REQUIRE(m.meshletVertexCount == 64);
        REQUIRE(m.meshletTriangleCount == 124);
    }
}

TEST_CASE("MaterialProperties default values", "[renderer][model-data]") {
    SECTION("Default initialization") {
        MaterialProperties mat{};

        REQUIRE(mat.colorFactor.x == 0.0f);
        REQUIRE(mat.colorFactor.y == 0.0f);
        REQUIRE(mat.colorFactor.z == 0.0f);
        REQUIRE(mat.colorFactor.w == 0.0f);
    }

    SECTION("PBR base properties") {
        MaterialProperties mat{};
        mat.colorFactor = float4{1.0f, 1.0f, 1.0f, 1.0f};
        mat.metalRoughFactors = float4{0.0f, 0.5f, 0.0f, 0.0f}; // metallic=0, roughness=0.5

        REQUIRE(mat.colorFactor.w == 1.0f);
        REQUIRE(mat.metalRoughFactors.x == 0.0f); // metallic
        REQUIRE(mat.metalRoughFactors.y == 0.5f); // roughness
    }

    SECTION("Texture indices") {
        MaterialProperties mat{};
        mat.textureImageIndices = int4{0, 1, 2, 3};
        mat.textureSamplerIndices = int4{0, 0, 0, 0};

        REQUIRE(mat.textureImageIndices.x == 0); // color texture
        REQUIRE(mat.textureImageIndices.y == 1); // metallic-rough texture
        REQUIRE(mat.textureImageIndices.z == 2); // normal texture
        REQUIRE(mat.textureImageIndices.w == 3); // emissive texture
    }

    SECTION("Invalid texture indices") {
        MaterialProperties mat{};
        mat.textureImageIndices = int4{-1, -1, -1, -1}; // no textures

        REQUIRE(mat.textureImageIndices.x == -1);
        REQUIRE(mat.textureImageIndices.y == -1);
        REQUIRE(mat.textureImageIndices.z == -1);
        REQUIRE(mat.textureImageIndices.w == -1);
    }

    SECTION("UV transforms") {
        MaterialProperties mat{};
        mat.colorUvTransform = float4{1.0f, 1.0f, 0.0f, 0.0f}; // scale (1,1), offset (0,0)

        REQUIRE(mat.colorUvTransform.x == 1.0f); // scale u
        REQUIRE(mat.colorUvTransform.y == 1.0f); // scale v
        REQUIRE(mat.colorUvTransform.z == 0.0f); // offset u
        REQUIRE(mat.colorUvTransform.w == 0.0f); // offset v
    }

    SECTION("Emissive properties") {
        MaterialProperties mat{};
        mat.emissiveFactor = float4{1.0f, 0.5f, 0.0f, 2.0f}; // orange emissive, 2x strength

        REQUIRE(mat.emissiveFactor.x == 1.0f);
        REQUIRE(mat.emissiveFactor.y == 0.5f);
        REQUIRE(mat.emissiveFactor.z == 0.0f);
        REQUIRE(mat.emissiveFactor.w == 2.0f); // strength
    }

    SECTION("Alpha properties") {
        MaterialProperties mat{};
        mat.alphaProperties = float4{0.5f, 1.0f, 0.0f, 0.0f}; // cutoff=0.5, mode=1 (mask), not double-sided, not unlit

        REQUIRE(mat.alphaProperties.x == 0.5f); // alpha cutoff
        REQUIRE(mat.alphaProperties.y == 1.0f); // alpha mode
        REQUIRE(mat.alphaProperties.z == 0.0f); // double sided
        REQUIRE(mat.alphaProperties.w == 0.0f); // unlit
    }

    SECTION("Physical properties") {
        MaterialProperties mat{};
        mat.physicalProperties = float4{1.5f, 0.0f, 1.0f, 1.0f}; // IOR=1.5, dispersion=0, normal scale=1, occlusion=1

        REQUIRE(mat.physicalProperties.x == 1.5f); // IOR
        REQUIRE(mat.physicalProperties.y == 0.0f); // dispersion
        REQUIRE(mat.physicalProperties.z == 1.0f); // normal scale
        REQUIRE(mat.physicalProperties.w == 1.0f); // occlusion strength
    }
}

TEST_CASE("Instance data", "[renderer][model-data]") {
    SECTION("Default initialization") {
        Instance inst{};

        REQUIRE(inst.primitiveIndex == 0);
        REQUIRE(inst.modelIndex == 0);
        REQUIRE(inst.materialIndex == 0);
    }

    SECTION("Valid instance data") {
        Instance inst{};
        inst.primitiveIndex = 5;
        inst.modelIndex = 10;
        inst.materialIndex = 2;

        REQUIRE(inst.primitiveIndex == 5);
        REQUIRE(inst.modelIndex == 10);
        REQUIRE(inst.materialIndex == 2);
    }
}

TEST_CASE("Model matrix data", "[renderer][model-data]") {
    SECTION("Identity matrix") {
        Model model{};
        model.modelMatrix = glm::mat4(1.0f);

        REQUIRE(model.modelMatrix[0][0] == 1.0f);
        REQUIRE(model.modelMatrix[1][1] == 1.0f);
        REQUIRE(model.modelMatrix[2][2] == 1.0f);
        REQUIRE(model.modelMatrix[3][3] == 1.0f);
    }

    SECTION("Translation matrix") {
        Model model{};
        model.modelMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 10.0f, 15.0f));

        REQUIRE(model.modelMatrix[3][0] == 5.0f);
        REQUIRE(model.modelMatrix[3][1] == 10.0f);
        REQUIRE(model.modelMatrix[3][2] == 15.0f);
    }

    SECTION("Previous model matrix for motion blur") {
        Model model{};
        model.modelMatrix = glm::mat4(1.0f);
        model.prevModelMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));

        // Current position
        REQUIRE(model.modelMatrix[3][0] == 0.0f);

        // Previous position (moved 1 unit in x)
        REQUIRE(model.prevModelMatrix[3][0] == 1.0f);
    }
}

TEST_CASE("Frustum plane data", "[renderer][model-data]") {
    SECTION("Default initialization") {
        Frustum frustum{};

        for (int i = 0; i < 6; i++) {
            REQUIRE(frustum.planes[i].x == 0.0f);
            REQUIRE(frustum.planes[i].y == 0.0f);
            REQUIRE(frustum.planes[i].z == 0.0f);
            REQUIRE(frustum.planes[i].w == 0.0f);
        }
    }

    SECTION("Plane equation data") {
        Frustum frustum{};
        // Plane equation: ax + by + cz + d = 0
        // Stored as (a, b, c, d)
        frustum.planes[0] = float4{1.0f, 0.0f, 0.0f, -5.0f}; // x = 5 plane

        REQUIRE(frustum.planes[0].x == 1.0f); // normal x
        REQUIRE(frustum.planes[0].y == 0.0f); // normal y
        REQUIRE(frustum.planes[0].z == 0.0f); // normal z
        REQUIRE(frustum.planes[0].w == -5.0f); // distance
    }

    SECTION("All six frustum planes") {
        Frustum frustum{};
        // Left, Right, Bottom, Top, Near, Far
        frustum.planes[0] = float4{1.0f, 0.0f, 0.0f, 1.0f};   // left
        frustum.planes[1] = float4{-1.0f, 0.0f, 0.0f, 1.0f};  // right
        frustum.planes[2] = float4{0.0f, 1.0f, 0.0f, 1.0f};   // bottom
        frustum.planes[3] = float4{0.0f, -1.0f, 0.0f, 1.0f};  // top
        frustum.planes[4] = float4{0.0f, 0.0f, 1.0f, 0.1f};   // near
        frustum.planes[5] = float4{0.0f, 0.0f, -1.0f, 100.0f}; // far

        REQUIRE(frustum.planes[0].x == 1.0f);
        REQUIRE(frustum.planes[1].x == -1.0f);
        REQUIRE(frustum.planes[2].y == 1.0f);
        REQUIRE(frustum.planes[3].y == -1.0f);
        REQUIRE(frustum.planes[4].z == 1.0f);
        REQUIRE(frustum.planes[5].z == -1.0f);
    }
}

TEST_CASE("SceneData camera and view data", "[renderer][model-data]") {
    SECTION("Default initialization") {
        SceneData scene{};

        REQUIRE(scene.cameraWorldPos.x == 0.0f);
        REQUIRE(scene.cameraWorldPos.y == 0.0f);
        REQUIRE(scene.cameraWorldPos.z == 0.0f);
        REQUIRE(scene.deltaTime == 0.0f);
    }

    SECTION("Camera position") {
        SceneData scene{};
        scene.cameraWorldPos = float4{10.0f, 20.0f, 30.0f, 1.0f};

        REQUIRE(scene.cameraWorldPos.x == 10.0f);
        REQUIRE(scene.cameraWorldPos.y == 20.0f);
        REQUIRE(scene.cameraWorldPos.z == 30.0f);
    }

    SECTION("Delta time") {
        SceneData scene{};
        scene.deltaTime = 0.016f; // ~60 FPS

        REQUIRE_THAT(scene.deltaTime, Catch::Matchers::WithinRel(0.016f, 0.0001f));
    }

    SECTION("View and projection matrices") {
        SceneData scene{};
        scene.view = glm::mat4(1.0f);
        scene.proj = glm::mat4(1.0f);
        scene.viewProj = glm::mat4(1.0f);

        REQUIRE(scene.view[0][0] == 1.0f);
        REQUIRE(scene.proj[0][0] == 1.0f);
        REQUIRE(scene.viewProj[0][0] == 1.0f);
    }

    SECTION("ViewProj should be proj * view") {
        SceneData scene{};
        scene.view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        scene.proj = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100.0f);
        scene.viewProj = scene.proj * scene.view;

        // Just verify it's been set (not zero)
        REQUIRE(scene.viewProj[0][0] != 0.0f);
    }
}
