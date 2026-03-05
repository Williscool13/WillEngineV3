//
// Created by William on 2025-12-16.
//

#include "model_serialization.h"

#include <utility>

#include "miniz/miniz.h"
#include "lz4/lz4hc.h"
#include "spdlog/spdlog.h"

namespace Render
{
ModelWriter::ModelWriter(std::filesystem::path  path)
    : outputPath(std::move(path))
{}

ModelWriter::~ModelWriter()
{
    if (!finalized) {
        Finalize();
    }
}

bool ModelWriter::AddFile(const std::string& filename, const void* data, size_t size, CompressionType compression)
{
    if (finalized) {
        SPDLOG_WARN("Cannot add files after finalization");
        return false;
    }

    if (filename.length() >= MAX_FILENAME_LENGTH) {
        SPDLOG_WARN("Filename too long: " + filename);
        return false;
    }

    FileEntry entry{};
    std::copy_n(filename.begin(), filename.size(), entry.filename);
    entry.filename[filename.size()] = '\0';
    entry.uncompressedSize = size;
    entry.offset = 0;
    entry.compressionType = compression;

    std::vector<uint8_t> buffer;

    switch (compression) {
        case CompressionType::LZ4:
            buffer = CompressLZ4(data, size);
            break;
        case CompressionType::Zlib:
            buffer = CompressZlib(data, size);
            break;
        case CompressionType::None:
        default:
            buffer.resize(size);
            std::memcpy(buffer.data(), data, size);
            break;
    }

    entry.compressedSize = buffer.size();
    fileEntries.push_back(entry);
    fileData.push_back(std::move(buffer));
    return true;
}

bool ModelWriter::AddFileFromDisk(const std::string& filename, const std::string& sourcePath, CompressionType compression)
{
    std::ifstream file(sourcePath, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(fileSize);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

    return AddFile(filename, buffer.data(), buffer.size(), compression);
}

bool ModelWriter::Finalize()
{
    if (finalized) {
        SPDLOG_WARN("Already finalized willmodel was finalized again");
        return false;
    }

    std::ofstream file(outputPath, std::ios::binary);
    if (!file) {
        SPDLOG_WARN("Failed to create output file: " + outputPath.string());
        return false;
    }

    uint64_t currentOffset = sizeof(WillModelHeader);

    for (FileEntry& fileEntry : fileEntries) {
        fileEntry.offset = currentOffset;
        currentOffset += fileEntry.compressedSize;
    }

    uint64_t fileTableOffset = currentOffset;

    WillModelHeader header{};
    std::memcpy(header.magic, WILL_MODEL_MAGIC, 8);
    header.majorVersion = MODEL_MAJOR_VERSION;
    header.minorVersion = MODEL_MINOR_VERSION;
    header.patchVersion = MODEL_PATCH_VERSION;
    header.numFiles = static_cast<uint32_t>(fileEntries.size());
    header.fileTableOffset = fileTableOffset;
    header.metadata = metadata;
    file.write(reinterpret_cast<const char*>(&header), sizeof(WillModelHeader));

    for (const auto& data : fileData) {
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
    }

    for (const auto& entry : fileEntries) {
        file.write(reinterpret_cast<const char*>(&entry), sizeof(FileEntry));
    }

    finalized = true;
    return true;
}

std::vector<uint8_t> CompressZlib(const void* data, size_t size)
{
    mz_ulong compressedSize = mz_compressBound(size);
    std::vector<uint8_t> compressed(compressedSize);

    int result = mz_compress(compressed.data(), &compressedSize, static_cast<const unsigned char*>(data), size);

    if (result != MZ_OK) {
        throw std::runtime_error("Compression failed");
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
        throw std::runtime_error("Decompression failed");
    }

    return decompressed;
}

std::vector<uint8_t> CompressLZ4(const void* data, size_t size)
{
    const int maxCompressedSize = LZ4_compressBound(static_cast<int>(size));
    std::vector<uint8_t> compressed(maxCompressedSize);
    const int compressedSize = LZ4_compress_HC(
        static_cast<const char*>(data), reinterpret_cast<char*>(compressed.data()),
        static_cast<int>(size), maxCompressedSize, LZ4HC_CLEVEL_DEFAULT);
    if (compressedSize <= 0) {
        throw std::runtime_error("LZ4 compression failed");
    }
    compressed.resize(compressedSize);
    return compressed;
}

std::vector<uint8_t> DecompressLZ4(const void* data, size_t compressedSize, size_t uncompressedSize)
{
    std::vector<uint8_t> decompressed(uncompressedSize);
    const int result = LZ4_decompress_safe(
        static_cast<const char*>(data), reinterpret_cast<char*>(decompressed.data()),
        static_cast<int>(compressedSize), static_cast<int>(uncompressedSize));
    if (result < 0) {
        throw std::runtime_error("LZ4 decompression failed");
    }
    return decompressed;
}

ModelReader::ModelReader() = default;

ModelReader::ModelReader(std::filesystem::path  path)
    : archivePath(std::move(path))
{
    archiveFileName = archivePath.filename().string();
    file.open(archivePath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open archive: " + archivePath.string());
    }

    bool headerCheck = ReadHeader();
    if (!headerCheck) {
        successfullyLoaded = false;
        return;
    }

    ReadFileTable();
    successfullyLoaded = true;
}

ModelReader::~ModelReader()
{
    if (file.is_open()) {
        file.close();
    }
}

bool ModelReader::ReadHeader()
{
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(&header), sizeof(WillModelHeader));

    if (std::strncmp(header.magic, WILL_MODEL_MAGIC, 8) != 0) {
        SPDLOG_ERROR("Invalid file format - magic number mismatch");
        return false;
    }

    if (header.majorVersion != MODEL_MAJOR_VERSION) {
        SPDLOG_ERROR("Loading {}: major version mismatch ({}.{}.{} vs expected {}.{}.{})",
            archiveFileName,
            header.majorVersion, header.minorVersion, header.patchVersion,
            MODEL_MAJOR_VERSION, MODEL_MINOR_VERSION, MODEL_PATCH_VERSION);
        return false;
    }
    if (header.minorVersion != MODEL_MINOR_VERSION) {
        SPDLOG_WARN("Loading {}: minor version mismatch ({}.{}.{} vs expected {}.{}.{})",
            archiveFileName,
            header.majorVersion, header.minorVersion, header.patchVersion,
            MODEL_MAJOR_VERSION, MODEL_MINOR_VERSION, MODEL_PATCH_VERSION);
        return false;
    }
    if (header.patchVersion != MODEL_PATCH_VERSION) {
        SPDLOG_TRACE("Loading {}: patch version mismatch ({}.{}.{} vs expected {}.{}.{})",
            archiveFileName,
            header.majorVersion, header.minorVersion, header.patchVersion,
            MODEL_MAJOR_VERSION, MODEL_MINOR_VERSION, MODEL_PATCH_VERSION);
    }

    return true;
}

void ModelReader::ReadFileTable()
{
    file.seekg(header.fileTableOffset, std::ios::beg);

    fileEntries.resize(header.numFiles);
    file.read(reinterpret_cast<char*>(fileEntries.data()), sizeof(FileEntry) * header.numFiles);
}

std::vector<std::string> ModelReader::ListFiles() const
{
    std::vector<std::string> files;
    files.reserve(fileEntries.size());

    for (const auto& entry : fileEntries) {
        files.emplace_back(entry.filename);
    }

    return files;
}

bool ModelReader::HasFile(const std::string& filename) const
{
    return GetFileEntry(filename) != nullptr;
}

const FileEntry* ModelReader::GetFileEntry(const std::string& filename) const
{
    for (const auto& entry : fileEntries) {
        if (filename == entry.filename) {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<uint8_t> ModelReader::ReadFile(const std::string& filename) const
{
    const FileEntry* entry = GetFileEntry(filename);
    if (!entry) {
        throw std::runtime_error("File not found: " + filename);
    }

    std::vector<uint8_t> compressedData(entry->compressedSize);
    file.seekg(entry->offset, std::ios::beg);
    file.read(reinterpret_cast<char*>(compressedData.data()), entry->compressedSize);

    switch (entry->compressionType) {
        case CompressionType::LZ4:
            return DecompressLZ4(compressedData.data(), entry->compressedSize, entry->uncompressedSize);
        case CompressionType::Zlib:
            return DecompressZlib(compressedData.data(), entry->compressedSize, entry->uncompressedSize);
        case CompressionType::None:
        default:
            return compressedData;
    }
}

void ModelReader::ReadNodes(std::vector<Node>& nodes) const
{
    std::vector<uint8_t> data = ReadFile("nodes.bin");
    const uint8_t* ptr = data.data();
    uint32_t count;
    std::memcpy(&count, ptr, sizeof(count));
    ptr += sizeof(count);
    nodes.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        ReadNode(ptr, nodes[i]);
    }
}

bool ModelReader::ReadFile(const std::string& filename, void* buffer, size_t bufferSize) const
{
    const FileEntry* entry = GetFileEntry(filename);
    if (!entry) {
        return false;
    }

    if (bufferSize < entry->uncompressedSize) {
        return false;
    }

    std::vector<uint8_t> compressedData(entry->compressedSize);
    file.seekg(entry->offset, std::ios::beg);
    file.read(reinterpret_cast<char*>(compressedData.data()), entry->compressedSize);

    switch (entry->compressionType) {
        case CompressionType::LZ4: {
            std::vector<uint8_t> decompressed = DecompressLZ4(compressedData.data(), entry->compressedSize, entry->uncompressedSize);
            std::memcpy(buffer, decompressed.data(), entry->uncompressedSize);
            break;
        }
        case CompressionType::Zlib: {
            std::vector<uint8_t> decompressed = DecompressZlib(compressedData.data(), entry->compressedSize, entry->uncompressedSize);
            std::memcpy(buffer, decompressed.data(), entry->uncompressedSize);
            break;
        }
        case CompressionType::None:
        default: {
            std::memcpy(buffer, compressedData.data(), entry->compressedSize);
            break;
        }
    }

    return true;
}
} // Render