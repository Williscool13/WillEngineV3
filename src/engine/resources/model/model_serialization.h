//
// Created by William on 2025-12-16.
//

#ifndef WILL_ENGINE_MODEL_SERIALIZATION_H
#define WILL_ENGINE_MODEL_SERIALIZATION_H
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iosfwd>
#include <string>
#include <vector>

#include "model_format.h"
#include "model_types.h"
#include "../material/material.h"

namespace Engine
{
class ModelWriter
{
public:
    explicit ModelWriter(std::filesystem::path path);

    ~ModelWriter();

    bool AddFile(const std::string& filename, const void* data, size_t size, CompressionType compression);

    bool AddFileFromDisk(const std::string& filename, const std::string& sourcePath, CompressionType compression);

    void SetMetadata(ModelMetadata m) { metadata = m; }

    bool Finalize();

private:
    std::filesystem::path outputPath;
    std::vector<FileEntry> fileEntries;
    std::vector<std::vector<uint8_t> > fileData;
    ModelMetadata metadata{};
    bool finalized = false;
};

class ModelReader
{
public:
    ModelReader();

    explicit ModelReader(std::filesystem::path path);

    ~ModelReader();

    uint32_t GetFileCount() const { return header.numFiles; }

    const ModelMetadata& GetMetadata() const { return header.metadata; }

    void ReadNodes(std::vector<Node>& nodes) const;

    std::vector<std::string> ListFiles() const;

    bool HasFile(const std::string& filename) const;

    std::vector<uint8_t> ReadFile(const std::string& filename) const;

    bool ReadFile(const std::string& filename, void* buffer, size_t bufferSize) const;

    const FileEntry* GetFileEntry(const std::string& filename) const;

    bool GetSuccessfullyLoaded() const { return successfullyLoaded; }

private:
    bool ReadHeader();

    void ReadFileTable();

    std::filesystem::path archivePath;
    std::string archiveFileName;
    mutable std::ifstream file;
    StaticModelHeader header{};
    std::vector<FileEntry> fileEntries;

    bool successfullyLoaded{};
};

std::vector<uint8_t> CompressZlib(const void* data, size_t size);
std::vector<uint8_t> DecompressZlib(const void* data, size_t compressedSize, size_t uncompressedSize);

std::vector<uint8_t> CompressLZ4(const void* data, size_t size);
std::vector<uint8_t> DecompressLZ4(const void* data, size_t compressedSize, size_t uncompressedSize);
} // Render

#endif //WILL_ENGINE_MODEL_SERIALIZATION_H
