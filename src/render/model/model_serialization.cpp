//
// Created by William on 2025-12-16.
//

#include "model_serialization.h"

#include "miniz/miniz.h"
#include "spdlog/spdlog.h"

namespace Render
{
ModelWriter::ModelWriter(const std::filesystem::path& path)
    : outputPath(path)
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

void ModelWriter::AddFileFromDisk(const std::string& filename, const std::string& sourcePath, bool compress)
{
    std::ifstream file(sourcePath, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + sourcePath);
    }

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(fileSize);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

    AddFile(filename, buffer.data(), buffer.size(), compress);
}

void ModelWriter::Finalize()
{
    if (finalized) return;

    std::ofstream file(outputPath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to create output file: " + outputPath.string());
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

WillModel LoadModel()
{
    // todo: this will be moved to async load thread.
    // if (!vulkanDeviceInfo) {
    //     const VkCommandPoolCreateInfo poolInfo = Renderer::VkHelpers::CommandPoolCreateInfo(vulkanContext->graphicsQueueFamily);
    //     VK_CHECK(vkCreateCommandPool(vulkanContext->device, &poolInfo, nullptr, &ktxTextureCommandPool));
    //
    //     ktxVulkanFunctions vkFuncs{};
    //     vkFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    //     vkFuncs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    //     vulkanDeviceInfo = ktxVulkanDeviceInfo_CreateEx(vulkanContext->instance, vulkanContext->physicalDevice, vulkanContext->device, vulkanContext->graphicsQueue, ktxTextureCommandPool, nullptr,
    //                                                     &vkFuncs);
    // }
    //
    // Renderer::ExtractedMeshletModel extractedModel;
    //
    // ModelReader reader("sponza2.willmodel");
    // std::vector<uint8_t> modelBinData = reader.ReadFile("model.bin");
    //
    // size_t offset = 0;
    // const auto* header = reinterpret_cast<ModelBinaryHeader*>(modelBinData.data());
    // offset += sizeof(ModelBinaryHeader);
    //
    // if (std::strncmp(header->magic, MODEL_MAGIC, 8) != 0) {
    //     LOG_ERROR("Invalid model.bin format");
    //     return {};
    // }
    //
    // auto readArray = [&]<typename T>(std::vector<T>& vec, uint32_t count) {
    //     vec.resize(count);
    //     if (count > 0) {
    //         std::memcpy(vec.data(), modelBinData.data() + offset, count * sizeof(T));
    //         offset += count * sizeof(T);
    //     }
    // };
    //
    //
    // const uint8_t* dataPtr = modelBinData.data() + offset;
    //
    // readArray(extractedModel.vertices, header->vertexCount);
    // readArray(extractedModel.meshletVertices, header->meshletVertexCount);
    // readArray(extractedModel.meshletTriangles, header->meshletTriangleCount);
    // readArray(extractedModel.meshlets, header->meshletCount);
    // readArray(extractedModel.primitives, header->primitiveCount);
    // readArray(extractedModel.materials, header->materialCount);
    //
    // dataPtr = modelBinData.data() + offset;
    // extractedModel.allMeshes.resize(header->meshCount);
    // for (uint32_t i = 0; i < header->meshCount; i++) {
    //     ReadMeshInformation(dataPtr, extractedModel.allMeshes[i]);
    // }
    //
    // extractedModel.nodes.resize(header->nodeCount);
    // for (uint32_t i = 0; i < header->nodeCount; i++) {
    //     ReadNode(dataPtr, extractedModel.nodes[i]);
    // }
    //
    // offset = dataPtr - modelBinData.data();
    // readArray(extractedModel.nodeRemap, header->nodeRemapCount);
    //
    // dataPtr = modelBinData.data() + offset;
    // extractedModel.animations.resize(header->animationCount);
    // for (uint32_t i = 0; i < header->animationCount; i++) {
    //     ReadAnimation(dataPtr, extractedModel.animations[i]);
    // }
    //
    // offset = dataPtr - modelBinData.data();
    // readArray(extractedModel.inverseBindMatrices, header->inverseBindMatrixCount);
    // readArray(extractedModel.samplerInfos, header->samplerCount);
    //
    //
    // uint32_t textureIndex = 0;
    // while (true) {
    //     std::string textureName = fmt::format("textures/texture_{}.ktx2", textureIndex);
    //     if (!reader.HasFile(textureName)) {
    //         break;
    //     }
    //
    //     std::vector<uint8_t> ktxData = reader.ReadFile(textureName);
    //
    //     std::filesystem::create_directories("temp");
    //     std::string tempKtxPath = fmt::format("temp/loaded_texture_{}.ktx2", textureIndex);
    //     std::ofstream tempFile(tempKtxPath, std::ios::binary);
    //     tempFile.write(reinterpret_cast<const char*>(ktxData.data()), ktxData.size());
    //     tempFile.close();
    //
    //     ktxTexture* loadedTexture = nullptr;
    //     KTX_error_code result = ktxTexture_CreateFromNamedFile(tempKtxPath.c_str(), KTX_TEXTURE_CREATE_NO_FLAGS, &loadedTexture);
    //
    //     auto texture2 = reinterpret_cast<ktxTexture2*>(loadedTexture);
    //     if (ktxTexture2_NeedsTranscoding(texture2)) {
    //         ktx_transcode_fmt_e targetFormat = KTX_TTF_BC7_RGBA;
    //         result = ktxTexture2_TranscodeBasis(texture2, targetFormat, 0);
    //         if (result != KTX_SUCCESS) {
    //             LOG_ERROR("Failed to transcode texture {}", textureIndex);
    //         }
    //     }
    //
    //     // Upload to GPU
    //     // todo: use engine uploader in production
    //     ktxVulkanTexture vkTexture;
    //     result = ktxTexture_VkUploadEx(loadedTexture, vulkanDeviceInfo, &vkTexture, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    //
    //     VkImageViewCreateInfo viewInfo = Renderer::VkHelpers::ImageViewCreateInfo(vkTexture.image, vkTexture.imageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    //     viewInfo.viewType = vkTexture.viewType;
    //     viewInfo.subresourceRange.layerCount = vkTexture.layerCount;
    //     viewInfo.subresourceRange.levelCount = vkTexture.levelCount;
    //     Renderer::ImageView imageView = Renderer::VkResources::CreateImageView(vulkanContext.get(), viewInfo);
    //
    //     Renderer::AllocatedImage allocatedImage;
    //     allocatedImage.handle = vkTexture.image;
    //     // allocatedImage.allocation = vkTexture.deviceMemory;
    //     allocatedImage.format = vkTexture.imageFormat;
    //     allocatedImage.extent = {vkTexture.width, vkTexture.height, vkTexture.depth};
    //
    //     extractedModel.images.push_back(std::move(allocatedImage));
    //     extractedModel.imageViews.push_back(std::move(imageView));
    //
    //     ktxTexture_Destroy(loadedTexture);
    //     textureIndex++;
    // }
    //
    // for (VkSamplerCreateInfo& sampler : extractedModel.samplerInfos) {
    //     extractedModel.samplers.push_back(Renderer::VkResources::CreateSampler(vulkanContext.get(), sampler));
    // }
    //
    // extractedModel.bSuccessfullyLoaded = true;
    // extractedModel.name = "Loaded Model";
    //
    // return extractedModel;
    return {};
}

ModelReader::ModelReader(const std::string& path)
    : archivePath(path)
{
    file.open(archivePath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open archive: " + archivePath);
    }

    ReadHeader();
    ReadFileTable();
}

ModelReader::~ModelReader()
{
    if (file.is_open()) {
        file.close();
    }
}

void ModelReader::ReadHeader()
{
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(&header), sizeof(WillModelHeader));

    if (std::strncmp(header.magic, WILL_MODEL_MAGIC, 8) != 0) {
        throw std::runtime_error("Invalid file format - magic number mismatch");
    }

    // if (header.majorVersion != VERSION) {
    //     throw std::runtime_error("Unsupported version: " + std::to_string(header.version));
    // }
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