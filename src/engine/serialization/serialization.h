//
// Created by William on 2026-03-12.
//

#ifndef WILL_ENGINE_SERIALIZATION_H
#define WILL_ENGINE_SERIALIZATION_H

#include <cstddef>

#include "core/containers/inline_string.h"
#include "core/containers/inline_vector.h"
#include "core/containers/vector.h"
#include "engine/resources/material/material.h"
#include "engine/resources/model/model_types.h"

namespace Engine
{
inline size_t AppendRaw(Core::Vector<std::byte>& buf, const void* data, size_t size)
{
    const auto* ptr = reinterpret_cast<const std::byte*>(data);
    buf.Append(ptr, ptr + size);
    return size;
}

template<typename T>
size_t WriteArray(Core::Vector<std::byte>& buf, const Core::HeapArray<T>& arr)
{
    if (arr.Size() == 0) { return 0; }
    return AppendRaw(buf, arr.Data(), arr.Size() * sizeof(T));
}

template<typename T>
size_t WriteVector(Core::Vector<std::byte>& buf, const Core::Vector<T>& vec)
{
    if (vec.Size() == 0) return 0;
    return AppendRaw(buf, vec.Data(), vec.Size() * sizeof(T));
}

template<size_t N>
size_t WriteString(Core::Vector<std::byte>& buf, const Core::InlineString<N>& str)
{
    auto length = static_cast<uint32_t>(str.Size());
    size_t written = AppendRaw(buf, &length, sizeof(length));
    if (length > 0) {
        written += AppendRaw(buf, str.c_str(), length);
    }
    return written;
}

template<typename T, size_t N>
size_t WriteVector(Core::Vector<std::byte>& buf, const Core::InlineVector<T, N>& vec)
{
    auto count = static_cast<uint32_t>(vec.Size());
    size_t written = AppendRaw(buf, &count, sizeof(count));
    if (count > 0) {
        written += AppendRaw(buf, vec.Data(), count * sizeof(T));
    }
    return written;
}

inline size_t WriteMaterial(Core::Vector<std::byte>& buf, const Material& mat)
{
    size_t written = 0;
    written += AppendRaw(buf, &mat.props, sizeof(mat.props));
    written += AppendRaw(buf, mat.textureRefs, sizeof(mat.textureRefs));
    written += AppendRaw(buf, mat.samplerDesc, sizeof(mat.samplerDesc));
    written += AppendRaw(buf, &mat.fragmentShader.id, sizeof(mat.fragmentShader.id));
    written += AppendRaw(buf, &mat.lightingShader.id, sizeof(mat.lightingShader.id));
    return written;
}

inline size_t WriteMeshInformation(Core::Vector<std::byte>& buf, const MeshInformation& mesh)
{
    size_t written = 0;
    written += WriteString(buf, mesh.name);
    written += WriteVector(buf, mesh.primitiveProperties);
    return written;
}

inline size_t WriteNode(Core::Vector<std::byte>& buf, const Node& node)
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


template<size_t N>
void ReadString(const uint8_t*& data, Core::InlineString<N>& str)
{
    uint32_t length;
    std::memcpy(&length, data, sizeof(length));
    data += sizeof(length);

    if (length > 0) {
        str = Core::InlineString<N>(std::string_view(reinterpret_cast<const char*>(data), length));
        data += length;
    }
}

template<typename T, size_t N>
void ReadDynamicVector(const uint8_t*& data, Core::InlineVector<T, N>& vec)
{
    uint32_t count;
    std::memcpy(&count, data, sizeof(count));
    data += sizeof(count);

    assert(count <= N && "Serialized count exceeds InlineVector capacity");
    if (count > 0) {
        for (uint32_t i = 0; i < count; ++i) {
            T item;
            std::memcpy(&item, data, sizeof(T));
            data += sizeof(T);
            vec.PushBack(item);
        }
    }
}


inline void ReadMaterial(const uint8_t*& data, Material& mat)
{
    std::memcpy(&mat.props, data, sizeof(MaterialProperties));
    data += sizeof(MaterialProperties);
    std::memcpy(mat.textureRefs, data, sizeof(mat.textureRefs));
    data += sizeof(mat.textureRefs);
    std::memcpy(mat.samplerDesc, data, sizeof(mat.samplerDesc));
    data += sizeof(mat.samplerDesc);
    std::memcpy(&mat.fragmentShader.id, data, sizeof(mat.fragmentShader.id));
    data += sizeof(mat.fragmentShader.id);
    std::memcpy(&mat.lightingShader.id, data, sizeof(mat.lightingShader.id));
    data += sizeof(mat.lightingShader.id);
}

inline void ReadMeshInformation(const uint8_t*& data, MeshInformation& mesh)
{
    ReadString(data, mesh.name);
    ReadDynamicVector(data, mesh.primitiveProperties);
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

/*inline size_t WriteAnimationSampler(std::vector<std::byte>& buf, const AnimationSampler& sampler)
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
}*/
} // Engine

#endif //WILL_ENGINE_SERIALIZATION_H
