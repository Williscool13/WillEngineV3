//
// Created by William on 2026-03-12.
//

#ifndef WILL_ENGINE_SERIALIZATION_H
#define WILL_ENGINE_SERIALIZATION_H

#include <cstddef>
#include <string>
#include <vector>

#include "engine/resources/material/material.h"
#include "engine/resources/model/model_types.h"

namespace Engine
{
inline size_t AppendRaw(std::vector<std::byte>& buf, const void* data, size_t size)
{
    const auto* ptr = reinterpret_cast<const std::byte*>(data);
    buf.insert(buf.end(), ptr, ptr + size);
    return size;
}

template<typename T>
size_t WriteVector(std::vector<std::byte>& buf, const std::vector<T>& vec)
{
    if (vec.empty()) return 0;
    return AppendRaw(buf, vec.data(), vec.size() * sizeof(T));
}

inline size_t WriteString(std::vector<std::byte>& buf, const std::string& str)
{
    uint32_t length = static_cast<uint32_t>(str.size());
    size_t written = AppendRaw(buf, &length, sizeof(length));
    if (length > 0) {
        written += AppendRaw(buf, str.data(), length);
    }
    return written;
}

inline void ReadString(const uint8_t*& data, std::string& str)
{
    uint32_t length;
    std::memcpy(&length, data, sizeof(length));
    data += sizeof(length);

    str.resize(length);
    if (length > 0) {
        std::memcpy(str.data(), data, length);
        data += length;
    }
}

template<typename T>
size_t WriteDynamicVector(std::vector<std::byte>& buf, const std::vector<T>& vec)
{
    auto count = static_cast<uint32_t>(vec.size());
    size_t written = AppendRaw(buf, &count, sizeof(count));
    if (count > 0) {
        written += AppendRaw(buf, vec.data(), count * sizeof(T));
    }
    return written;
}

template<typename T>
void ReadDynamicVector(const uint8_t*& data, std::vector<T>& vec)
{
    uint32_t count;
    std::memcpy(&count, data, sizeof(count));
    data += sizeof(count);

    vec.resize(count);
    if (count > 0) {
        std::memcpy(vec.data(), data, count * sizeof(T));
        data += count * sizeof(T);
    }
}

inline size_t WriteMaterial(std::vector<std::byte>& buf, const Material& mat)
{
    size_t written = 0;
    written += AppendRaw(buf, &mat.props, sizeof(mat.props));
    written += AppendRaw(buf, mat.textureRefs, sizeof(mat.textureRefs));
    written += AppendRaw(buf, mat.samplerDesc, sizeof(mat.samplerDesc));
    return written;
}

inline void ReadMaterial(const uint8_t*& data, Material& mat)
{
    std::memcpy(&mat.props, data, sizeof(MaterialProperties));
    data += sizeof(MaterialProperties);
    std::memcpy(mat.textureRefs, data, sizeof(mat.textureRefs));
    data += sizeof(mat.textureRefs);
    std::memcpy(mat.samplerDesc, data, sizeof(mat.samplerDesc));
    data += sizeof(mat.samplerDesc);
}

inline size_t WriteMeshInformation(std::vector<std::byte>& buf, const MeshInformation& mesh)
{
    size_t written = 0;
    written += WriteString(buf, mesh.name);
    written += WriteDynamicVector(buf, mesh.primitiveProperties);
    return written;
}

inline void ReadMeshInformation(const uint8_t*& data, MeshInformation& mesh)
{
    ReadString(data, mesh.name);
    ReadDynamicVector(data, mesh.primitiveProperties);
}

inline size_t WriteNode(std::vector<std::byte>& buf, const Node& node)
{
    size_t written = 0;
    written += WriteString(buf, node.name);
    written += AppendRaw(buf, &node.parent, sizeof(node.parent));
    written += AppendRaw(buf, &node.meshIndex, sizeof(node.meshIndex));
    written += AppendRaw(buf, &node.depth, sizeof(node.depth));
    written += AppendRaw(buf, &node.inverseBindIndex, sizeof(node.inverseBindIndex));
    written += AppendRaw(buf, &node.localTranslation, sizeof(node.localTranslation));
    written += AppendRaw(buf, &node.localRotation, sizeof(node.localRotation));
    written += AppendRaw(buf, &node.localScale, sizeof(node.localScale));
    return written;
}

inline void ReadNode(const uint8_t*& data, Node& node)
{
    ReadString(data, node.name);
    std::memcpy(&node.parent, data, sizeof(node.parent));
    data += sizeof(node.parent);
    std::memcpy(&node.meshIndex, data, sizeof(node.meshIndex));
    data += sizeof(node.meshIndex);
    std::memcpy(&node.depth, data, sizeof(node.depth));
    data += sizeof(node.depth);
    std::memcpy(&node.inverseBindIndex, data, sizeof(node.inverseBindIndex));
    data += sizeof(node.inverseBindIndex);
    std::memcpy(&node.localTranslation, data, sizeof(node.localTranslation));
    data += sizeof(node.localTranslation);
    std::memcpy(&node.localRotation, data, sizeof(node.localRotation));
    data += sizeof(node.localRotation);
    std::memcpy(&node.localScale, data, sizeof(node.localScale));
    data += sizeof(node.localScale);
}

inline size_t WriteAnimationSampler(std::vector<std::byte>& buf, const AnimationSampler& sampler)
{
    size_t written = 0;
    written += WriteDynamicVector(buf, sampler.timestamps);
    written += WriteDynamicVector(buf, sampler.values);
    written += AppendRaw(buf, &sampler.interpolation, sizeof(sampler.interpolation));
    return written;
}

inline void ReadAnimationSampler(const uint8_t*& data, AnimationSampler& sampler)
{
    ReadDynamicVector(data, sampler.timestamps);
    ReadDynamicVector(data, sampler.values);
    std::memcpy(&sampler.interpolation, data, sizeof(sampler.interpolation));
    data += sizeof(sampler.interpolation);
}

inline size_t WriteAnimation(std::vector<std::byte>& buf, const Animation& anim)
{
    size_t written = 0;
    written += WriteString(buf, anim.name);

    auto samplerCount = static_cast<uint32_t>(anim.samplers.size());
    written += AppendRaw(buf, &samplerCount, sizeof(samplerCount));
    for (const auto& sampler : anim.samplers) {
        written += WriteAnimationSampler(buf, sampler);
    }

    written += WriteDynamicVector(buf, anim.channels);
    written += AppendRaw(buf, &anim.duration, sizeof(anim.duration));
    return written;
}

inline void ReadAnimation(const uint8_t*& data, Animation& anim)
{
    ReadString(data, anim.name);

    uint32_t samplerCount;
    std::memcpy(&samplerCount, data, sizeof(samplerCount));
    data += sizeof(samplerCount);

    anim.samplers.resize(samplerCount);
    for (uint32_t i = 0; i < samplerCount; i++) {
        ReadAnimationSampler(data, anim.samplers[i]);
    }

    ReadDynamicVector(data, anim.channels);
    std::memcpy(&anim.duration, data, sizeof(anim.duration));
    data += sizeof(anim.duration);
}

} // Engine

#endif //WILL_ENGINE_SERIALIZATION_H
