//
// WImage blob format test suite. Drafted by Claude.
//
// WImage is the image payload container inside .wtexture/.wenvmap/.wprobe/.wsfont files
// (replaced the KTX2 container 2026-08-12). These tests lock the writer/reader pair:
// generator-side WImageBlobInit/WImageFaceData against loader-side WImageView.

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <memory>

#include "engine/resources/wimage_format.h"

struct Blob
{
    std::unique_ptr<uint8_t[]> data;
    size_t size{0};
};

static Blob MakeBlob(const Engine::WImageDesc& desc)
{
    Blob blob;
    blob.size = Engine::WImageBlobSize(desc);
    REQUIRE(blob.size > 0);
    blob.data = std::make_unique<uint8_t[]>(blob.size);
    memset(blob.data.get(), 0xCD, blob.size);
    REQUIRE(Engine::WImageBlobInit(blob.data.get(), blob.size, desc) == blob.size);
    return blob;
}

static void FillFace(uint8_t* blob, uint32_t level, uint32_t face, uint8_t value)
{
    memset(Engine::WImageFaceData(blob, level, face), value, Engine::WImageFaceSize(blob, level));
}

TEST_CASE("WImage: BC7 mip chain round-trips through init and view parse", "[wimage]")
{
    const Engine::WImageDesc desc{VK_FORMAT_BC7_SRGB_BLOCK, 256, 128, 9, 1};
    Blob blob = MakeBlob(desc);
    for (uint32_t level = 0; level < desc.levelCount; level++) {
        FillFace(blob.data.get(), level, 0, static_cast<uint8_t>(level + 1));
    }

    Engine::WImageView view;
    REQUIRE(view.Parse(blob.data.get(), blob.size));
    CHECK(view.vkFormat == VK_FORMAT_BC7_SRGB_BLOCK);
    CHECK(view.baseWidth == 256);
    CHECK(view.baseHeight == 128);
    CHECK(view.levelCount == 9);
    CHECK(view.faceCount == 1);
    CHECK_FALSE(view.bCubemap);

    // BC7: 4x4 blocks, 16 bytes each; sub-block mips still occupy one block
    CHECK(view.RowPitch(0) == 64 * 16);
    CHECK(view.FaceSize(0) == 64 * 32 * 16);
    CHECK(view.LevelWidth(8) == 1);
    CHECK(view.LevelHeight(8) == 1);
    CHECK(view.RowPitch(8) == 16);
    CHECK(view.FaceSize(8) == 16);

    for (uint32_t level = 0; level < desc.levelCount; level++) {
        const uint8_t* data = view.FaceData(level, 0);
        CHECK(reinterpret_cast<uintptr_t>(data) % Engine::WIMAGE_DATA_ALIGN ==
              reinterpret_cast<uintptr_t>(blob.data.get()) % Engine::WIMAGE_DATA_ALIGN);
        CHECK(data[0] == level + 1);
        CHECK(data[view.FaceSize(level) - 1] == level + 1);
    }
}

TEST_CASE("WImage: RGBA16F cubemap parses with distinct per-face data", "[wimage]")
{
    const Engine::WImageDesc desc{VK_FORMAT_R16G16B16A16_SFLOAT, 64, 64, 6, 6};
    Blob blob = MakeBlob(desc);
    for (uint32_t level = 0; level < desc.levelCount; level++) {
        for (uint32_t face = 0; face < 6; face++) {
            FillFace(blob.data.get(), level, face, static_cast<uint8_t>(level * 6 + face + 1));
        }
    }

    Engine::WImageView view;
    REQUIRE(view.Parse(blob.data.get(), blob.size));
    CHECK(view.bCubemap);
    CHECK(view.faceCount == 6);
    CHECK(view.RowPitch(0) == 64ull * 8);
    CHECK(view.FaceSize(0) == 64ull * 64 * 8);
    CHECK(view.FaceSize(5) == 2ull * 2 * 8);

    for (uint32_t level = 0; level < desc.levelCount; level++) {
        for (uint32_t face = 0; face < 6; face++) {
            CHECK(view.FaceData(level, face)[0] == level * 6 + face + 1);
        }
    }
}

TEST_CASE("WImage: odd-size BC dimensions round up to whole blocks", "[wimage]")
{
    const Engine::WImageDesc desc{VK_FORMAT_BC7_UNORM_BLOCK, 5, 3, 1, 1};
    Blob blob = MakeBlob(desc);

    Engine::WImageView view;
    REQUIRE(view.Parse(blob.data.get(), blob.size));
    CHECK(view.RowPitch(0) == 2 * 16);
    CHECK(view.FaceSize(0) == 2 * 16);
}

TEST_CASE("WImage: uncompressed formats use bytes-per-pixel pitch", "[wimage]")
{
    const Engine::WImageDesc desc{VK_FORMAT_R8G8_UNORM, 128, 128, 1, 1};
    Blob blob = MakeBlob(desc);

    Engine::WImageView view;
    REQUIRE(view.Parse(blob.data.get(), blob.size));
    CHECK(view.RowPitch(0) == 128 * 2);
    CHECK(view.FaceSize(0) == 128ull * 128 * 2);
    CHECK(Engine::WImageFaceSize(blob.data.get(), 0) == 128ull * 128 * 2);
}

TEST_CASE("WImage: invalid descs are rejected at size computation", "[wimage]")
{
    CHECK(Engine::WImageBlobSize({VK_FORMAT_R8G8B8A8_UNORM, 0, 64, 1, 1}) == 0);
    CHECK(Engine::WImageBlobSize({VK_FORMAT_R8G8B8A8_UNORM, 64, 0, 1, 1}) == 0);
    CHECK(Engine::WImageBlobSize({VK_FORMAT_R8G8B8A8_UNORM, 64, 64, 0, 1}) == 0);
    CHECK(Engine::WImageBlobSize({VK_FORMAT_R8G8B8A8_UNORM, 64, 64, Engine::WIMAGE_MAX_LEVELS + 1, 1}) == 0);
    CHECK(Engine::WImageBlobSize({VK_FORMAT_R8G8B8A8_UNORM, 64, 64, 1, 0}) == 0);

    uint8_t tiny[8];
    CHECK(Engine::WImageBlobInit(tiny, sizeof(tiny), {VK_FORMAT_R8G8B8A8_UNORM, 64, 64, 1, 1}) == 0);
}

TEST_CASE("WImage: parse rejects corrupt and truncated blobs", "[wimage]")
{
    const Engine::WImageDesc desc{VK_FORMAT_R8G8B8A8_UNORM, 32, 32, 4, 1};
    Blob blob = MakeBlob(desc);

    Engine::WImageView view;
    SECTION("valid baseline parses") {
        CHECK(view.Parse(blob.data.get(), blob.size));
    }
    SECTION("smaller than header") {
        CHECK_FALSE(view.Parse(blob.data.get(), sizeof(Engine::WImageHeader) - 1));
    }
    SECTION("truncated in level table") {
        CHECK_FALSE(view.Parse(blob.data.get(), sizeof(Engine::WImageHeader) + sizeof(Engine::WImageLevel)));
    }
    SECTION("truncated in level data") {
        CHECK_FALSE(view.Parse(blob.data.get(), blob.size - 1));
    }
    SECTION("bad magic") {
        blob.data[0] ^= 0xFF;
        CHECK_FALSE(view.Parse(blob.data.get(), blob.size));
    }
    SECTION("unknown version") {
        const uint32_t badVersion = Engine::WIMAGE_VERSION + 1;
        memcpy(blob.data.get() + 4, &badVersion, sizeof(badVersion));
        CHECK_FALSE(view.Parse(blob.data.get(), blob.size));
    }
    SECTION("zero level count") {
        const uint32_t zero = 0;
        memcpy(blob.data.get() + 20, &zero, sizeof(zero));
        CHECK_FALSE(view.Parse(blob.data.get(), blob.size));
    }
    SECTION("KTX2 identifier is not a WImage") {
        const uint8_t ktx2[32] = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
        CHECK_FALSE(view.Parse(ktx2, sizeof(ktx2)));
    }
}
