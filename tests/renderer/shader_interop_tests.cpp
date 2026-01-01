//
// Created by William on 2025-12-25.
//
// Tests for shader interop structures to catch breaking changes in GPU data layout.
// These tests are critical for preventing hard-to-debug GPU bugs caused by
// mismatched structure layouts between CPU and shader code.
//

#include <catch2/catch_test_macros.hpp>

#include "render/shaders/model_interop.h"
#include "render/shaders/common_interop.h"

TEST_CASE("Vertex structure size and alignment", "[renderer][shader-interop]") {
    SECTION("Vertex size is as expected") {
        // Vertex should be tightly packed:
        // float3 position (12) + float texcoordU (4) = 16
        // float3 normal (12) + float texcoordV (4) = 16
        // float4 tangent (16) = 16
        // float4 color (16) = 16
        // Total: 64 bytes
        REQUIRE(sizeof(Vertex) == 64);
    }

    SECTION("Vertex alignment is suitable for GPU") {
        // Should be at least 4-byte aligned (for float)
        REQUIRE(alignof(Vertex) >= 4);
    }

    SECTION("Vertex field offsets") {
        Vertex v{};
        const char* base = reinterpret_cast<const char*>(&v);

        // Position should be at offset 0
        REQUIRE(reinterpret_cast<const char*>(&v.position) - base == 0);

        // TexcoordU should be at offset 12
        REQUIRE(reinterpret_cast<const char*>(&v.texcoordU) - base == 12);

        // Normal should be at offset 16
        REQUIRE(reinterpret_cast<const char*>(&v.normal) - base == 16);

        // TexcoordV should be at offset 28
        REQUIRE(reinterpret_cast<const char*>(&v.texcoordV) - base == 28);

        // Tangent should be at offset 32
        REQUIRE(reinterpret_cast<const char*>(&v.tangent) - base == 32);

        // Color should be at offset 48
        REQUIRE(reinterpret_cast<const char*>(&v.color) - base == 48);
    }
}

TEST_CASE("SkinnedVertex structure size and alignment", "[renderer][shader-interop]") {
    SECTION("SkinnedVertex size is as expected") {
        // SkinnedVertex = Vertex (64) + uint4 joints (16) + float4 weights (16)
        // Total: 96 bytes
        REQUIRE(sizeof(SkinnedVertex) == 96);
    }

    SECTION("SkinnedVertex alignment") {
        REQUIRE(alignof(SkinnedVertex) >= 4);
    }

    SECTION("SkinnedVertex skinning data offsets") {
        SkinnedVertex v{};
        const char* base = reinterpret_cast<const char*>(&v);

        // Joints should be at offset 64
        REQUIRE(reinterpret_cast<const char*>(&v.joints) - base == 64);

        // Weights should be at offset 80
        REQUIRE(reinterpret_cast<const char*>(&v.weights) - base == 80);
    }
}

TEST_CASE("Meshlet structure size and alignment", "[renderer][shader-interop]") {
    SECTION("Meshlet size is as expected") {
        // float4 boundingSphere (16)
        // float3 coneApex (12) + float coneCutoff (4) = 16
        // float3 coneAxis (12) + uint32 vertexOffset (4) = 16
        // uint32 meshletVerticesOffset (4)
        // uint32 meshletTriangleOffset (4)
        // uint32 meshletVerticesCount (4)
        // uint32 meshletTriangleCount (4)
        // Total: 16 + 16 + 16 + 4 + 4 + 4 + 4 = 64 bytes
        REQUIRE(sizeof(Meshlet) == 64);
    }

    SECTION("Meshlet alignment") {
        REQUIRE(alignof(Meshlet) >= 4);
    }
}

TEST_CASE("MeshletPrimitive structure size and alignment", "[renderer][shader-interop]") {
    SECTION("MeshletPrimitive size") {
        // uint32 meshletOffset (4)
        // uint32 meshletCount (4)
        // uint32 padding (4)
        // uint32 bHasTransparent (4)
        // float4 boundingSphere (16)
        // Total: 32 bytes
        REQUIRE(sizeof(MeshletPrimitive) == 32);
    }

    SECTION("MeshletPrimitive alignment") {
        REQUIRE(alignof(MeshletPrimitive) >= 4);
    }
}

TEST_CASE("MaterialProperties structure size and alignment", "[renderer][shader-interop]") {
    SECTION("MaterialProperties size") {
        // This is a complex structure with many fields
        // float4 colorFactor (16)
        // float4 metalRoughFactors (16)
        // int4 textureImageIndices (16)
        // int4 textureSamplerIndices (16)
        // int4 textureImageIndices2 (16)
        // int4 textureSamplerIndices2 (16)
        // float4 colorUvTransform (16)
        // float4 metalRoughUvTransform (16)
        // float4 normalUvTransform (16)
        // float4 emissiveUvTransform (16)
        // float4 occlusionUvTransform (16)
        // float4 emissiveFactor (16)
        // float4 alphaProperties (16)
        // float4 physicalProperties (16)
        // Total: 14 * 16 = 224 bytes
        REQUIRE(sizeof(MaterialProperties) == 224);
    }

    SECTION("MaterialProperties alignment") {
        REQUIRE(alignof(MaterialProperties) >= 4);
    }

    SECTION("MaterialProperties is suitable for std430 layout") {
        // In std430, the alignment should be the largest alignment of any member
        // For float4/int4, that's 16 bytes
        REQUIRE(alignof(MaterialProperties) == 16);
    }
}

TEST_CASE("Instance structure size and alignment", "[renderer][shader-interop]") {
    SECTION("Instance size") {
        // uint32 primitiveIndex (4)
        // uint32 modelIndex (4)
        // uint32 materialIndex (4)
        // uint32 jointMatrixOffset (4)
        // Total: 16 bytes
        REQUIRE(sizeof(Instance) == 16);
    }

    SECTION("Instance alignment") {
        REQUIRE(alignof(Instance) >= 4);
    }
}

TEST_CASE("Model structure size and alignment", "[renderer][shader-interop]") {
    SECTION("Model size") {
        // float4x4 modelMatrix (64)
        // float4x4 prevModelMatrix (64)
        // Total: 128 bytes
        REQUIRE(sizeof(Model) == 128);
    }

    SECTION("Model alignment") {
        // GLM matrices are aligned to 16 bytes
        REQUIRE(alignof(Model) == 16);
    }

    SECTION("Model field offsets") {
        Model m{};
        const char* base = reinterpret_cast<const char*>(&m);

        // modelMatrix at offset 0
        REQUIRE(reinterpret_cast<const char*>(&m.modelMatrix) - base == 0);

        // prevModelMatrix at offset 64
        REQUIRE(reinterpret_cast<const char*>(&m.prevModelMatrix) - base == 64);
    }
}

TEST_CASE("Frustum structure size and alignment", "[renderer][shader-interop]") {
    SECTION("Frustum size") {
        // float4 planes[6] = 6 * 16 = 96 bytes
        REQUIRE(sizeof(Frustum) == 96);
    }

    SECTION("Frustum alignment") {
        REQUIRE(alignof(Frustum) >= 4);
    }
}

TEST_CASE("SceneData structure size and alignment", "[renderer][shader-interop]") {
    SECTION("SceneData size") {
        size_t size = sizeof(SceneData);
        REQUIRE(size ==608);
    }

    SECTION("SceneData alignment") {
        // Should be aligned to 16 bytes for float4x4
        REQUIRE(alignof(SceneData) == 16);
    }

    SECTION("SceneData field offsets") {
        SceneData sd{};
        const char* base = reinterpret_cast<const char*>(&sd);
        REQUIRE(reinterpret_cast<const char*>(&sd.view) - base == 0);
        REQUIRE(reinterpret_cast<const char*>(&sd.proj) - base == 64);
        REQUIRE(reinterpret_cast<const char*>(&sd.viewProj) - base == 128);
        REQUIRE(reinterpret_cast<const char*>(&sd.cameraWorldPos) - base == 448);
        REQUIRE(reinterpret_cast<const char*>(&sd.frustum) - base == 480);
        REQUIRE(reinterpret_cast<const char*>(&sd.deltaTime) - base == 592);
    }
}

TEST_CASE("GLM type aliases match expected sizes", "[renderer][shader-interop]") {
    SECTION("float2 is vec2") {
        REQUIRE(sizeof(float2) == sizeof(glm::vec2));
        REQUIRE(sizeof(float2) == 8);
    }

    SECTION("float3 is vec3") {
        REQUIRE(sizeof(float3) == sizeof(glm::vec3));
        REQUIRE(sizeof(float3) == 12);
    }

    SECTION("float4 is vec4") {
        REQUIRE(sizeof(float4) == sizeof(glm::vec4));
        REQUIRE(sizeof(float4) == 16);
    }

    SECTION("int2 is ivec2") {
        REQUIRE(sizeof(int2) == sizeof(glm::ivec2));
        REQUIRE(sizeof(int2) == 8);
    }

    SECTION("uint4 is uvec4") {
        REQUIRE(sizeof(uint4) == sizeof(glm::uvec4));
        REQUIRE(sizeof(uint4) == 16);
    }

    SECTION("float4x4 is mat4") {
        REQUIRE(sizeof(float4x4) == sizeof(glm::mat4));
        REQUIRE(sizeof(float4x4) == 64);
    }
}

TEST_CASE("Structure padding ensures GPU compatibility", "[renderer][shader-interop]") {
    SECTION("No structure has unexpected padding") {
        // This test documents expected sizes
        // If these fail after a change, it indicates a breaking change to GPU layout

        REQUIRE(sizeof(Vertex) == 64);
        REQUIRE(sizeof(SkinnedVertex) == 96);
        REQUIRE(sizeof(Meshlet) == 64);
        REQUIRE(sizeof(MeshletPrimitive) == 32);
        REQUIRE(sizeof(MaterialProperties) == 224);
        REQUIRE(sizeof(Instance) == 16);
        REQUIRE(sizeof(Model) == 128);
        REQUIRE(sizeof(Frustum) == 96);
    }

    SECTION("All structures have proper alignment for std430") {
        // In std430 layout, structs are aligned to the largest member alignment
        // For our structures with float4, this should be 16 bytes

        REQUIRE(alignof(MaterialProperties) == 16);
        REQUIRE(alignof(Model) == 16);
        REQUIRE(alignof(SceneData) == 16);
    }
}
