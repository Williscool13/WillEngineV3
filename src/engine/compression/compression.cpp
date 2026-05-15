//
// Created by William on 2026-03-12.
//

#include "compression.h"

#include "engine/logging/engine_log.h"
#include "miniz/miniz.h"
#include "lz4/lz4hc.h"
#include "zstd/lib/zstd.h"

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

size_t CompressZstdMaxSize(size_t size)
{
    return ZSTD_compressBound(size);
}

size_t CompressZstd(const void* uncompressedData, size_t uncompressedSize, void* compressedData, size_t compressedSize)
{
    size_t result = ZSTD_compress(compressedData, compressedSize, uncompressedData, uncompressedSize, ZSTD_CLEVEL_DEFAULT);
    if (ZSTD_isError(result)) {
        LOG_CRITICAL(Engine, "zstd compression failed: {}", ZSTD_getErrorName(result));
        assert(false);
    }
    return result;
}

void DecompressZstd(const void* compressedData, size_t compressedSize, void* decompressedData, size_t uncompressedSize)
{
    size_t result = ZSTD_decompress(decompressedData, uncompressedSize, compressedData, compressedSize);
    if (ZSTD_isError(result)) {
        LOG_CRITICAL(Engine, "zstd decompression failed: {}", ZSTD_getErrorName(result));
        assert(false);
    }
}

size_t CompressMaxSize(CompressionType type, size_t size)
{
    switch (type) {
        case CompressionType::Zlib: return CompressZlibMaxSize(size);
        case CompressionType::LZ4:  return CompressLZ4MaxSize(size);
        case CompressionType::Zstd: return CompressZstdMaxSize(size);
        default:
            LOG_CRITICAL(Engine, "CompressMaxSize called with unsupported CompressionType {}.", static_cast<uint32_t>(type));
            assert(false);
            return 0;
    }
}

size_t Compress(CompressionType type, const void* uncompressedData, size_t uncompressedSize, void* compressedData, size_t compressedSize)
{
    switch (type) {
        case CompressionType::Zlib: return CompressZlib(uncompressedData, uncompressedSize, compressedData, compressedSize);
        case CompressionType::LZ4:  return CompressLZ4(uncompressedData, uncompressedSize, compressedData, compressedSize);
        case CompressionType::Zstd: return CompressZstd(uncompressedData, uncompressedSize, compressedData, compressedSize);
        default:
            LOG_CRITICAL(Engine, "Compress called with unsupported CompressionType {}.", static_cast<uint32_t>(type));
            assert(false);
            return 0;
    }
}

void Decompress(CompressionType type, const void* compressedData, size_t compressedSize, void* decompressedData, size_t uncompressedSize)
{
    switch (type) {
        case CompressionType::Zlib: DecompressZlib(compressedData, compressedSize, decompressedData, uncompressedSize); break;
        case CompressionType::LZ4:  DecompressLZ4(compressedData, compressedSize, decompressedData, uncompressedSize);  break;
        case CompressionType::Zstd: DecompressZstd(compressedData, compressedSize, decompressedData, uncompressedSize); break;
        default:
            LOG_CRITICAL(Engine, "Decompress called with unsupported CompressionType {}.", static_cast<uint32_t>(type));
            assert(false);
            break;
    }
}
} // Engine
