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
 * Uploads every level and face of a WImage into layers 0..faceCount-1, ending in SHADER_READ_ONLY_OPTIMAL.
 * @param submitAndWait called when staging is full; must submit, wait and restart the command buffer
 */
void UploadWImageLayers(VkCommandBuffer cmd, const Engine::WImageView& blobView, VkImage image, UploadStaging* uploadStaging, const Core::InlineFunction<void(bool)>& submitAndWait);
} // AssetLoad

#endif //WILL_ENGINE_WIMAGE_LAYER_UPLOAD_H
