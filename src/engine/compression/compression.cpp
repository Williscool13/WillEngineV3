//
// Created by William on 2026-03-12.
//

#include "compression.h"

#include "engine/logging/engine_log.h"
#include "miniz/miniz.h"
#include "lz4/lz4hc.h"

namespace Engine
{
std::vector<uint8_t> CompressZlib(const void* data, size_t size)
{
    mz_ulong compressedSize = mz_compressBound(size);
    std::vector<uint8_t> compressed(compressedSize);

    int result = mz_compress(compressed.data(), &compressedSize, static_cast<const unsigned char*>(data), size);

    if (result != MZ_OK) {
        LOG_CRITICAL(Engine, "zlib compression failed.");
        assert(false);
    }

    compressed.resize(compressedSize);
    return compressed;
}

std::vector<uint8_t> DecompressZlib(const void* data, size_t compressedSize, size_t uncompressedSize)
{
    std::vector<uint8_t> decompressed(uncompressedSize);
    mz_ulong destLen = uncompressedSize;

    int result = mz_uncompress(decompressed.data(), &destLen, static_cast<const unsigned char*>(data), compressedSize);

    if (result != MZ_OK) {
        LOG_CRITICAL(Engine, "zlib decompression failed.");
        assert(false);
    }

    return decompressed;
}

size_t CompressLZ4MaxSize(size_t size)
{
    return LZ4_compressBound(static_cast<int>(size));
}

size_t CompressLZ4(const void* uncompressedData, size_t uncompressedSize, void* compressedData, size_t compressedSize)
{
    size_t maxCompressedSize = CompressLZ4MaxSize(uncompressedSize);
    assert(maxCompressedSize <= compressedSize && "Compress LZ4 failed due to compressed output too small");

    const int actualCompressedSize = LZ4_compress_HC(
        static_cast<const char*>(uncompressedData), static_cast<char*>(compressedData),
        static_cast<int>(uncompressedSize), compressedSize, LZ4HC_CLEVEL_DEFAULT);
    if (actualCompressedSize <= 0) {
        LOG_CRITICAL(Engine, "LZ4 compression failed.");
        assert(false);
    }

    return actualCompressedSize;
}

void DecompressLZ4(const void* compressedData, size_t compressedSize, void* decompressedData, size_t uncompressedSize)
{
    const int result = LZ4_decompress_safe(
        static_cast<const char*>(compressedData), static_cast<char*>(decompressedData),
        static_cast<int>(compressedSize), static_cast<int>(uncompressedSize));
    if (result < 0) {
        LOG_CRITICAL(Engine, "LZ4 decompression failed.");
        assert(false);
    }
}
} // Engine
