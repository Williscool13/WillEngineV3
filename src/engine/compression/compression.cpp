//
// Created by William on 2026-03-12.
//

#include "compression.h"

#include "engine/logging/engine_log.h"
#include "miniz/miniz.h"
#include "lz4/lz4hc.h"

namespace Engine
{
size_t CompressZlibMaxSize(size_t size)
{
    return mz_compressBound(static_cast<mz_ulong>(size));
}

size_t CompressZlib(const void* uncompressedData, size_t uncompressedSize, void* compressedData, size_t compressedSize)
{
    size_t maxCompressedSize = CompressZlibMaxSize(uncompressedSize);
    assert(maxCompressedSize <= compressedSize && "CompressZlib failed due to compressed output buffer too small");

    mz_ulong actualCompressedSize = static_cast<mz_ulong>(compressedSize);
    int result = mz_compress(static_cast<unsigned char*>(compressedData), &actualCompressedSize,
                             static_cast<const unsigned char*>(uncompressedData), static_cast<mz_ulong>(uncompressedSize));
    if (result != MZ_OK) {
        LOG_CRITICAL(Engine, "zlib compression failed.");
        assert(false);
    }

    return actualCompressedSize;
}

void DecompressZlib(const void* compressedData, size_t compressedSize, void* decompressedData, size_t uncompressedSize)
{
    mz_ulong destLen = static_cast<mz_ulong>(uncompressedSize);
    int result = mz_uncompress(static_cast<unsigned char*>(decompressedData), &destLen,
                               static_cast<const unsigned char*>(compressedData), static_cast<mz_ulong>(compressedSize));
    if (result != MZ_OK) {
        LOG_CRITICAL(Engine, "zlib decompression failed.");
        assert(false);
    }
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
