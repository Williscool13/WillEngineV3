//
// Created by William on 2026-10-08.
//

#ifndef WILL_ENGINE_WIMAGE_LAYER_UPLOAD_H
#define WILL_ENGINE_WIMAGE_LAYER_UPLOAD_H

#include <volk.h>

#include "core/containers/inline_function.h"

namespace Engine
{
class WImageView;
}

namespace AssetLoad
{
class UploadStaging;

/**
 * Copies every level and face of a parsed WImage into image layers 0..faceCount-1 and leaves it SHADER_READ_ONLY_OPTIMAL.
 * Faces larger than the staging buffer stream in block-row chunks.
 * @param submitAndWait called with true when staging is full; must submit, wait and restart the command buffer
 */
void UploadWImageLayers(VkCommandBuffer cmd, const Engine::WImageView& blobView, VkImage image, UploadStaging* uploadStaging, const Core::InlineFunction<void(bool)>& submitAndWait);
} // AssetLoad

#endif //WILL_ENGINE_WIMAGE_LAYER_UPLOAD_H
