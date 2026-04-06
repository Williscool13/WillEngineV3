//
// Created by William on 2026-03-12.
//

#ifndef WILL_ENGINE_COMPRESSION_H
#define WILL_ENGINE_COMPRESSION_H
#include <cstdint>
#include <vector>

namespace Engine
{
enum class CompressionType : uint32_t
{
    None = 0,
    Zlib = 1,
    LZ4  = 2,
};

std::vector<uint8_t> CompressZlib(const void* data, size_t size);
std::vector<uint8_t> DecompressZlib(const void* data, size_t compressedSize, size_t uncompressedSize);

std::vector<uint8_t> CompressLZ4(const void* data, size_t size);
std::vector<uint8_t> DecompressLZ4(const void* compressedData, size_t compressedSize, size_t uncompressedSize);
void DecompressLZ4(const void* compressedData, size_t compressedSize, void* decompressedData, size_t uncompressedSize);
} // Engine

#endif //WILL_ENGINE_COMPRESSION_H