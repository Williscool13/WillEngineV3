//
// Created by William on 2026-03-12.
//

#ifndef WILL_ENGINE_COMPRESSION_H
#define WILL_ENGINE_COMPRESSION_H
#include <cstdint>

namespace Engine
{
enum class CompressionType : uint32_t
{
    None = 0,
    Zlib = 1,
    LZ4  = 2,
};

size_t CompressZlibMaxSize(size_t size);
size_t CompressZlib(const void* uncompressedData, size_t uncompressedSize, void* compressedData, size_t compressedSize);
void DecompressZlib(const void* compressedData, size_t compressedSize, void* decompressedData, size_t uncompressedSize);

size_t CompressLZ4MaxSize(size_t size);
/**
 * Compress some data w/ LZ4. Internally checks if output size is <= the size of the destination container
 * @param uncompressedData ptr to the 0th element of the raw data container
 * @param uncompressedSize size of the raw data container. Used to estimate compression size.
 * @param compressedData ptr to the 0th element of the container to write to
 * @param compressedSize the size of the compressedData container
 * @return The actual size of the compressed data
 */
size_t CompressLZ4(const void* uncompressedData, size_t uncompressedSize, void* compressedData, size_t compressedSize);
void DecompressLZ4(const void* compressedData, size_t compressedSize, void* decompressedData, size_t uncompressedSize);
} // Engine

#endif //WILL_ENGINE_COMPRESSION_H