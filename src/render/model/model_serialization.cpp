//
// Created by William on 2025-12-16.
//

#include "model_serialization.h"

#include <utility>

#include "miniz/miniz.h"
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

bool ModelWriter::AddFile(const std::string& filename, const void* data, size_t size, bool compress)
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

    std::vector<uint8_t> buffer;

    if (compress) {
        buffer = CompressZlib(data, size);
        entry.compressedSize = buffer.size();
        entry.compressionType = 1; // zlib
    }
    else {
        buffer.resize(size);
        std::memcpy(buffer.data(), data, size);
        entry.compressedSize = size;
        entry.compressionType = 0; // none
    }

    fileEntries.push_back(entry);
    fileData.push_back(std::move(buffer));
    return true;
}

bool ModelWriter::AddFileFromDisk(const std::string& filename, const std::string& sourcePath, bool compress)
{
    std::ifstream file(sourcePath, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(fileSize);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

    return AddFile(filename, buffer.data(), buffer.size(), compress);
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
        SPDLOG_ERROR("Loading {}: Major version difference ({} vs current {})", archiveFileName, header.majorVersion, MODEL_MAJOR_VERSION);
        return false;
    }
    if (header.minorVersion != MODEL_MINOR_VERSION) {
        SPDLOG_WARN("Loading {}: Minor file version difference ({} vs current {})", archiveFileName, header.majorVersion, MODEL_MAJOR_VERSION);
        return false;
    }
    if (header.patchVersion != MODEL_PATCH_VERSION) {
        SPDLOG_TRACE("Loading {}: patch version difference ({} vs current {})", archiveFileName, header.majorVersion, MODEL_MAJOR_VERSION);
        return false;
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

    if (entry->compressionType == 1) {
        return DecompressZlib(compressedData.data(), entry->compressedSize, entry->uncompressedSize);
    }

    return compressedData;
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

    if (entry->compressionType == 1) {
        // zlib
        std::vector<uint8_t> decompressed = DecompressZlib(compressedData.data(), entry->compressedSize, entry->uncompressedSize);
        std::memcpy(buffer, decompressed.data(), entry->uncompressedSize);
    }
    else {
        std::memcpy(buffer, compressedData.data(), entry->compressedSize);
    }

    return true;
}
} // Render