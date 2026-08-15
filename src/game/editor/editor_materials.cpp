//
// Created by William on 2026-06-26.
//

#include "editor_materials.h"

#include <algorithm>
#include <cstring>

#include <fmt/format.h>

#include "imgui.h"
#include "core/input/input_frame.h"
#include "core/containers/arena_array.h"
#include "core/containers/arena_fixed_vector.h"
#include "engine/include/engine_context.h"
#include "engine/engine_api.h"
#include "engine/material_manager.h"
#include "engine/asset_manager.h"
#include "engine/resources/texture/texture.h"
#include "game/input/game_actions.h"

namespace Game
{
static bool INCLUDE_MODEL_TEXTURES = false;
static bool INCLUDE_BUILTIN_TEXTURES = false;

static bool IsTextureFiltered(const Engine::AssetManager::EditorTextureInfo& info)
{
    if (!INCLUDE_MODEL_TEXTURES && info.category == Engine::TextureCategory::Model) { return true; }
    if (!INCLUDE_BUILTIN_TEXTURES && info.category == Engine::TextureCategory::Builtin) { return true; }
    return false;
}

void DrawMaterialsWindow(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (ImGui::Begin("Materials")) {
        Engine::MaterialManager* materialManager = ctx->materialManager;

        if (ImGui::BeginTabBar("##MaterialTabs")) {
            if (ImGui::BeginTabItem("Mesh Materials")) {
                static char newMatName[128] = "new_material";
                ImGui::SetNextItemWidth(200.0f);
                ImGui::InputText("##matname", newMatName, sizeof(newMatName));
                ImGui::SameLine();
                const bool nameEmpty = newMatName[0] == '\0';
                const bool nameExists = !nameEmpty && materialManager->FindMutableMaterial(StringID{newMatName, strlen(newMatName)}).IsValid();
                ImGui::BeginDisabled(nameEmpty || nameExists);
                if (ImGui::Button("Create Material")) {
                    materialManager->CreateMaterial(newMatName);
                }
                ImGui::EndDisabled();
                if (nameExists) {
                    ImGui::SameLine();
                    ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "already exists");
                }

                const auto& allMaterials = materialManager->GetMaterials();
                int32_t mutableCount = 0;
                for (const auto& [id, mat] : allMaterials) {
                    if (!mat.immutable) { ++mutableCount; }
                }
                ImGui::SeparatorText(Core::InlineString<48>::Format("Materials (%u)", mutableCount).c_str());

                static Engine::MaterialID matRenameActive = Engine::MaterialID::INVALID;
                static char matRenameBuffer[128] = {};

                Engine::MaterialID materialPendingDelete = Engine::MaterialID::INVALID;
                for (const auto& [id, mat] : allMaterials) {
                    if (mat.immutable) continue;
                    ImGui::PushID(static_cast<int>(id.id));
                    if (ImGui::CollapsingHeader(mat.name.c_str())) {
                        ImGui::BeginDisabled(true);
                        ImGui::Text("ID: %llu", id.id);
                        ImGui::EndDisabled(); {
                            const auto& entryMap = materialManager->GetIdToEntryMap();
                            const bool materialInUse = entryMap.Contains(id) && materialManager->GetActiveMaterials()[entryMap.At(id)].refCounter > 0;
                            ImGui::BeginDisabled(materialInUse);
                            if (ImGui::Button("Delete Material")) {
                                materialPendingDelete = id;
                            }
                            ImGui::EndDisabled();
                            if (materialInUse && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                ImGui::SetTooltip("Material is referenced by scene entities");
                            }
                        }
                        ImGui::SameLine();
                        const bool isRenaming = matRenameActive == id;
                        if (isRenaming) {
                            ImGui::SetNextItemWidth(180.0f);
                            ImGui::InputText("##rename", matRenameBuffer, sizeof(matRenameBuffer));
                            ImGui::SameLine();
                            const bool nameUnchanged = strcmp(matRenameBuffer, mat.name.c_str()) == 0;
                            const bool nameEmpty = matRenameBuffer[0] == '\0';
                            const bool nameExists = !nameEmpty && !nameUnchanged && materialManager->FindMutableMaterial(StringID{matRenameBuffer, strlen(matRenameBuffer)}).IsValid();
                            ImGui::BeginDisabled(nameEmpty || nameUnchanged || nameExists);
                            if (ImGui::Button("Apply")) {
                                materialManager->RenameMutableMaterial(id, matRenameBuffer);
                                matRenameActive = Engine::MaterialID::INVALID;
                            }
                            ImGui::EndDisabled();
                            ImGui::SameLine();
                            if (ImGui::Button("Cancel")) {
                                matRenameActive = Engine::MaterialID::INVALID;
                            }
                            if (nameExists) {
                                ImGui::SameLine();
                                ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "already exists");
                            }
                        }
                        else {
                            if (ImGui::Button("Rename")) {
                                matRenameActive = id;
                                const auto& n = mat.name;
                                const size_t copyLen = std::min(n.Size(), sizeof(matRenameBuffer) - 1);
                                memcpy(matRenameBuffer, n.c_str(), copyLen);
                                matRenameBuffer[copyLen] = '\0';
                            }
                        }

                        Engine::Material editMat = mat;
                        MaterialProperties& props = editMat.props;
                        bool changed = false;

                        ImGui::SeparatorText("Base");
                        changed |= ImGui::ColorEdit4("Color Factor", &props.colorFactor.x);
                        changed |= ImGui::SliderFloat("Metallic", &props.metalRoughFactors.x, 0.0f, 1.0f);
                        changed |= ImGui::SliderFloat("Roughness", &props.metalRoughFactors.y, 0.0f, 1.0f);

                        ImGui::SeparatorText("Emissive");
                        changed |= ImGui::ColorEdit3("Emissive Color", &props.emissiveFactor.x);
                        changed |= ImGui::DragFloat("Emissive Strength", &props.emissiveFactor.w, 0.01f, 0.0f, 100.0f);

                        ImGui::SeparatorText("Alpha");
                        const char* alphaModes[] = {"Opaque", "Mask", "Blend"};
                        int alphaMode = static_cast<int>(props.alphaProperties.y);
                        if (ImGui::Combo("Alpha Mode", &alphaMode, alphaModes, 3)) {
                            props.alphaProperties.y = static_cast<float>(alphaMode);
                            changed = true;
                        }
                        if (alphaMode == 1) {
                            changed |= ImGui::SliderFloat("Alpha Cutoff", &props.alphaProperties.x, 0.0f, 1.0f);
                        }
                        bool doubleSided = props.alphaProperties.z > 0.5f;
                        if (ImGui::Checkbox("Double Sided", &doubleSided)) {
                            props.alphaProperties.z = doubleSided ? 1.0f : 0.0f;
                            changed = true;
                        }
                        bool unlit = props.alphaProperties.w > 0.5f;
                        if (ImGui::Checkbox("Unlit", &unlit)) {
                            props.alphaProperties.w = unlit ? 1.0f : 0.0f;
                            changed = true;
                        }

                        ImGui::SeparatorText("Physical");
                        changed |= ImGui::SliderFloat("IOR", &props.physicalProperties.x, 1.0f, 3.0f);
                        changed |= ImGui::SliderFloat("Normal Intensity", &props.physicalProperties.z, 0.0f, 2.0f);
                        changed |= ImGui::SliderFloat("Occlusion Strength", &props.physicalProperties.w, 0.0f, 1.0f);

                        if (ImGui::TreeNode("UV Transforms")) {
                            ImGui::SeparatorText("Color");
                            changed |= ImGui::DragFloat2("Scale##color_uv", &props.colorUvTransform.x, 0.01f);
                            changed |= ImGui::DragFloat2("Offset##color_uv", &props.colorUvTransform.z, 0.01f);
                            ImGui::SeparatorText("Metal/Rough");
                            changed |= ImGui::DragFloat2("Scale##mr_uv", &props.metalRoughUvTransform.x, 0.01f);
                            changed |= ImGui::DragFloat2("Offset##mr_uv", &props.metalRoughUvTransform.z, 0.01f);
                            ImGui::SeparatorText("Normal");
                            changed |= ImGui::DragFloat2("Scale##normal_uv", &props.normalUvTransform.x, 0.01f);
                            changed |= ImGui::DragFloat2("Offset##normal_uv", &props.normalUvTransform.z, 0.01f);
                            ImGui::SeparatorText("Emissive");
                            changed |= ImGui::DragFloat2("Scale##emissive_uv", &props.emissiveUvTransform.x, 0.01f);
                            changed |= ImGui::DragFloat2("Offset##emissive_uv", &props.emissiveUvTransform.z, 0.01f);
                            ImGui::SeparatorText("Occlusion");
                            changed |= ImGui::DragFloat2("Scale##occlusion_uv", &props.occlusionUvTransform.x, 0.01f);
                            changed |= ImGui::DragFloat2("Offset##occlusion_uv", &props.occlusionUvTransform.z, 0.01f);
                            ImGui::TreePop();
                        }

                        ImGui::SeparatorText("Shader"); {
                            Core::Span<const StringID> shadingPipelines = ctx->pipelineManager->GetShadingPipelines();
                            const int32_t pipelineCount = static_cast<int32_t>(shadingPipelines.Size());
                            Core::Arena& arena = ctx->editorArena.Get();

                            int currentShader = -1;
                            for (int32_t i = 0; i < pipelineCount; ++i) {
                                if (editMat.fragmentShader == shadingPipelines[i]) {
                                    currentShader = i;
                                    break;
                                }
                            }

                            const bool isUnknown = currentShader < 0;
                            const int32_t optionCount = isUnknown ? pipelineCount + 1 : pipelineCount;
                            Core::ArenaArray<Core::InlineString<> > labels(&arena, optionCount);
                            for (int32_t i = 0; i < pipelineCount; ++i) { labels[i] = Core::InlineString(shadingPipelines[i].ToString()); }
                            if (isUnknown) {
                                labels[pipelineCount] = Core::InlineString("(unknown) ");
                                labels[pipelineCount].Append(editMat.fragmentShader.ToString());
                                currentShader = pipelineCount;
                            }
                            auto shadingGetter = [](void* data, int idx) -> const char* { return (*static_cast<Core::ArenaArray<Core::InlineString<> >*>(data))[idx].c_str(); };
                            if (ImGui::Combo("Fragment Shader", &currentShader, shadingGetter, &labels, static_cast<int32_t>(labels.Size()))) {
                                if (currentShader < pipelineCount) {
                                    editMat.fragmentShader = shadingPipelines[currentShader];
                                    changed = true;
                                }
                            }
                        } {
                            Core::Span<const StringID> lightingPipelines = ctx->pipelineManager->GetLightingPipelines();
                            const int32_t pipelineCount = static_cast<int32_t>(lightingPipelines.Size());
                            Core::Arena& arena = ctx->editorArena.Get();

                            int currentShader = -1;
                            for (int32_t i = 0; i < pipelineCount; ++i) {
                                if (editMat.lightingShader == lightingPipelines[i]) {
                                    currentShader = i;
                                    break;
                                }
                            }

                            const bool isUnknown = currentShader < 0;
                            const int32_t optionCount = isUnknown ? pipelineCount + 1 : pipelineCount;
                            Core::ArenaArray<Core::InlineString<> > labels(&arena, optionCount);
                            for (int32_t i = 0; i < pipelineCount; ++i) { labels[i] = Core::InlineString(lightingPipelines[i].ToString()); }
                            if (isUnknown) {
                                labels[pipelineCount] = Core::InlineString("(unknown) ");
                                labels[pipelineCount].Append(editMat.lightingShader.ToString());
                                currentShader = pipelineCount;
                            }
                            auto lightingGetter = [](void* data, int idx) -> const char* { return (*static_cast<Core::ArenaArray<Core::InlineString<> >*>(data))[idx].c_str(); };
                            if (ImGui::Combo("Lighting Shader", &currentShader, lightingGetter, &labels, static_cast<int32_t>(labels.Size()))) {
                                if (currentShader < pipelineCount) {
                                    editMat.lightingShader = lightingPipelines[currentShader];
                                    changed = true;
                                }
                            }
                        }

                        ImGui::SeparatorText("Textures");
                        static const char* slotNames[] = {"Color", "Metal/Rough", "Normal", "Emissive", "Occlusion", "Packed NRM"};
                        static Engine::TextureID texEditPending = Engine::TextureID::INVALID;
                        static Engine::SamplerDesc samplerEditPending{};

                        if (!state->editor.textureInfoCache) {
                            const uint32_t count = ctx->assetManager->GetTextureInfoCount();
                            state->editor.textureInfoCache = ctx->editorArena.Get().Alloc<Core::ArenaFixedMap<Engine::TextureID, Engine::AssetManager::EditorTextureInfo> >(&ctx->editorArena.Get(), count);
                            ctx->assetManager->GetAllTextureInfos(*state->editor.textureInfoCache);
                        }
                        const auto& matTexInfoMap = *state->editor.textureInfoCache;

                        for (int32_t slot = 0; slot < 6; ++slot) {
                            ImGui::PushID(slot);

                            const Engine::TextureID& texId = mat.textureRefs[slot];
                            const char* currentTexName = "None";
                            if (const Engine::AssetManager::EditorTextureInfo* info = texId.IsValid() ? matTexInfoMap.Find(texId) : nullptr) {
                                currentTexName = info->name.c_str();
                            }

                            ImGui::Text("%-13s", slotNames[slot]);
                            ImGui::SameLine();
                            ImGui::TextDisabled("|");
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Tex")) {
                                texEditPending = texId;
                                ImGui::OpenPopup("TextureSelect");
                            }
                            ImGui::SameLine();
                            ImGui::Text("%-32s", currentTexName);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Smp")) {
                                samplerEditPending = mat.samplerDesc[slot];
                                ImGui::OpenPopup("SamplerEdit");
                            }
                            ImGui::SameLine(); {
                                const Engine::SamplerDesc& sd = mat.samplerDesc[slot];
                                const Engine::SamplerDesc def{};
                                const bool filterDiff = sd.magFilter != def.magFilter || sd.minFilter != def.minFilter;
                                const bool mipDiff = sd.mipmapMode != def.mipmapMode;
                                const bool addrDiff = sd.addressModeU != def.addressModeU || sd.addressModeV != def.addressModeV || sd.addressModeW != def.addressModeW;
                                const bool anisoDiff = sd.anisotropyEnable != def.anisotropyEnable || sd.maxAnisotropy != def.maxAnisotropy;
                                const bool lodDiff = sd.mipLodBias != def.mipLodBias || sd.minLod != def.minLod || sd.maxLod != def.maxLod;
                                const int diffCount = filterDiff + mipDiff + addrDiff + anisoDiff + lodDiff;
                                const char* label = "Default";
                                if (diffCount == 1) {
                                    if (filterDiff) { label = "Custom Filter"; }
                                    else if (mipDiff) { label = "Custom Mip Mode"; }
                                    else if (addrDiff) { label = "Custom Address"; }
                                    else if (anisoDiff) { label = "Custom Aniso"; }
                                    else if (lodDiff) { label = "Custom LOD"; }
                                }
                                else if (diffCount > 1) {
                                    label = "Custom Sampler";
                                }
                                ImGui::TextDisabled("%s", label);
                            }

                            if (ImGui::BeginPopupModal("TextureSelect", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                                static Engine::TextureID previewId = Engine::TextureID::INVALID;

                                ImGui::Text("Slot: %s", slotNames[slot]);
                                ImGui::Separator();

                                if (ImGui::BeginChild("##texlist", {400.0f, 300.0f}, ImGuiChildFlags_Borders)) {
                                    ImGui::Checkbox("Include Model Textures", &INCLUDE_MODEL_TEXTURES);
                                    ImGui::Checkbox("Include Builtin Textures", &INCLUDE_BUILTIN_TEXTURES);
                                    bool noneSelected = !texEditPending.IsValid();
                                    if (ImGui::Selectable("(None)", noneSelected)) {
                                        texEditPending = Engine::TextureID::INVALID;
                                    }
                                    if (!state->editor.textureInfoCache) {
                                        const uint32_t count = ctx->assetManager->GetTextureInfoCount();
                                        state->editor.textureInfoCache = ctx->editorArena.Get().Alloc<Core::ArenaFixedMap<Engine::TextureID, Engine::AssetManager::EditorTextureInfo> >(&ctx->editorArena.Get(), count);
                                        ctx->assetManager->GetAllTextureInfos(*state->editor.textureInfoCache);
                                    }
                                    const uint32_t texCount = static_cast<uint32_t>(state->editor.textureInfoCache->Size());
                                    auto sorted = Core::ArenaFixedVector<Engine::AssetManager::EditorTextureInfo>(&ctx->editorArena.Get(), texCount);
                                    for (const auto& [id2, info] : *state->editor.textureInfoCache) {
                                        if (IsTextureFiltered(info)) { continue; }
                                        sorted.EmplaceBack(info);
                                    }
                                    std::ranges::sort(sorted, {}, &Engine::AssetManager::EditorTextureInfo::name);
                                    for (const auto& info : sorted) {
                                        const auto& id2 = info.id;
                                        const auto& name = info.name;
                                        bool selected = texEditPending == id2;
                                        if (ImGui::Selectable(name.c_str(), selected)) {
                                            texEditPending = id2;
                                        }
                                        if (ImGui::IsItemHovered()) {
                                            if (previewId != id2) {
                                                if (previewId.IsValid()) state->editor.texResidency.Release(previewId, ctx);
                                                previewId = id2;
                                                // todo: ideally only load the lowest mip for preview
                                                state->editor.texResidency.Acquire(id2, ctx);
                                            }
                                            ImGui::BeginTooltip();
                                            uint64_t ds = state->editor.texResidency.GetDescSet(id2, ctx);
                                            if (ds) {
                                                ImGui::Image(ds, {128.0f, 128.0f});
                                            }
                                            else {
                                                ImGui::Text("Loading...");
                                            }
                                            ImGui::EndTooltip();
                                        }
                                    }
                                }
                                ImGui::EndChild();

                                if (ImGui::Button("OK")) {
                                    Engine::Material _editMat = mat;
                                    _editMat.textureRefs[slot] = texEditPending;
                                    materialManager->UpdateMutableMaterial(id, _editMat, true);
                                    if (previewId.IsValid()) {
                                        state->editor.texResidency.Release(previewId, ctx);
                                        previewId = Engine::TextureID::INVALID;
                                    }
                                    ImGui::CloseCurrentPopup();
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Cancel") || state->input.GetActionState(Actions::ACTION_ESCAPE).pressed) {
                                    if (previewId.IsValid()) {
                                        state->editor.texResidency.Release(previewId, ctx);
                                        previewId = Engine::TextureID::INVALID;
                                    }
                                    ImGui::CloseCurrentPopup();
                                }
                                ImGui::EndPopup();
                            }

                            if (ImGui::BeginPopupModal("SamplerEdit", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                                ImGui::Text("Slot: %s", slotNames[slot]);
                                ImGui::Separator();

                                static const char* filterNames[] = {"Nearest", "Linear"};
                                int magF = samplerEditPending.magFilter;
                                int minF = samplerEditPending.minFilter;
                                if (ImGui::Combo("Mag Filter", &magF, filterNames, 2)) samplerEditPending.magFilter = static_cast<VkFilter>(magF);
                                if (ImGui::Combo("Min Filter", &minF, filterNames, 2)) samplerEditPending.minFilter = static_cast<VkFilter>(minF);

                                static const char* mipmapModeNames[] = {"Nearest", "Linear"};
                                int mipMode = samplerEditPending.mipmapMode;
                                if (ImGui::Combo("Mip Mode", &mipMode, mipmapModeNames, 2)) samplerEditPending.mipmapMode = static_cast<VkSamplerMipmapMode>(mipMode);

                                static const char* addressModeNames[] = {"Repeat", "Mirrored Repeat", "Clamp To Edge", "Clamp To Border", "Mirror Clamp"};
                                int addrU = samplerEditPending.addressModeU;
                                int addrV = samplerEditPending.addressModeV;
                                int addrW = samplerEditPending.addressModeW;
                                if (ImGui::Combo("Address U", &addrU, addressModeNames, 5)) samplerEditPending.addressModeU = static_cast<VkSamplerAddressMode>(addrU);
                                if (ImGui::Combo("Address V", &addrV, addressModeNames, 5)) samplerEditPending.addressModeV = static_cast<VkSamplerAddressMode>(addrV);
                                if (ImGui::Combo("Address W", &addrW, addressModeNames, 5)) samplerEditPending.addressModeW = static_cast<VkSamplerAddressMode>(addrW);

                                bool aniso = samplerEditPending.anisotropyEnable == VK_TRUE;
                                if (ImGui::Checkbox("Anisotropy", &aniso)) samplerEditPending.anisotropyEnable = aniso ? VK_TRUE : VK_FALSE;
                                if (aniso) {
                                    static const float anisoLevels[] = {1.0f, 2.0f, 4.0f, 8.0f, 16.0f};
                                    static const char* anisoLabels[] = {"1x", "2x", "4x", "8x", "16x"};
                                    int anisoIdx = 0;
                                    for (int k = 4; k >= 0; --k) {
                                        if (samplerEditPending.maxAnisotropy >= anisoLevels[k]) {
                                            anisoIdx = k;
                                            break;
                                        }
                                    }
                                    if (ImGui::Combo("Max Anisotropy", &anisoIdx, anisoLabels, 5)) {
                                        samplerEditPending.maxAnisotropy = anisoLevels[anisoIdx];
                                    }
                                }

                                ImGui::DragFloat("Mip LOD Bias", &samplerEditPending.mipLodBias, 0.1f);
                                ImGui::DragFloat("Min LOD", &samplerEditPending.minLod, 0.1f, 0.0f, 100.0f);
                                ImGui::DragFloat("Max LOD", &samplerEditPending.maxLod, 0.1f, 0.0f, 1000.0f);

                                if (ImGui::Button("OK")) {
                                    Engine::Material _editMat = mat;
                                    _editMat.samplerDesc[slot] = samplerEditPending;
                                    materialManager->UpdateMutableMaterial(id, _editMat, true);
                                    ImGui::CloseCurrentPopup();
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Cancel") || state->input.GetActionState(Actions::ACTION_ESCAPE).pressed) {
                                    ImGui::CloseCurrentPopup();
                                }
                                ImGui::EndPopup();
                            }

                            ImGui::PopID();
                        }

                        if (changed) {
                            materialManager->UpdateMutableMaterial(id, editMat, true);
                        }
                    }
                    ImGui::PopID();
                }
                if (materialPendingDelete.IsValid()) {
                    materialManager->DeleteMutableMaterial(materialPendingDelete);
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Text Materials")) {
                static char newTextMatName[128] = "new_text_material";
                ImGui::SetNextItemWidth(200.0f);
                ImGui::InputText("##textmatname", newTextMatName, sizeof(newTextMatName));
                ImGui::SameLine();
                const bool textNameEmpty = newTextMatName[0] == '\0';
                const bool textNameExists = !textNameEmpty && materialManager->FindTextMaterial(StringID{newTextMatName, strlen(newTextMatName)}).IsValid();
                ImGui::BeginDisabled(textNameEmpty || textNameExists);
                if (ImGui::Button("Create Text Material")) {
                    materialManager->CreateTextMaterial(newTextMatName);
                }
                ImGui::EndDisabled();
                if (textNameExists) {
                    ImGui::SameLine();
                    ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "already exists");
                }

                const auto& allTextMaterials = materialManager->GetTextMaterials();
                ImGui::SeparatorText(Core::InlineString<48>::Format("Text Materials (%u)", static_cast<uint32_t>(allTextMaterials.Size())).c_str());

                static Engine::TextMaterialID textMatRenameActive = Engine::TextMaterialID::INVALID;
                static char textMatRenameBuffer[128] = {};

                Engine::TextMaterialID textMatPendingDelete = Engine::TextMaterialID::INVALID;
                for (const auto& [id, mat] : allTextMaterials) {
                    ImGui::PushID(static_cast<int>(id.id));
                    if (ImGui::CollapsingHeader(mat.name.c_str())) {
                        ImGui::BeginDisabled(true);
                        ImGui::Text("ID: %llu", id.id);
                        ImGui::EndDisabled();
                        if (ImGui::Button("Delete")) {
                            textMatPendingDelete = id;
                        }
                        ImGui::SameLine();
                        const bool isRenaming = textMatRenameActive == id;
                        if (isRenaming) {
                            ImGui::SetNextItemWidth(180.0f);
                            ImGui::InputText("##rename", textMatRenameBuffer, sizeof(textMatRenameBuffer));
                            ImGui::SameLine();
                            const bool nameUnchanged = strcmp(textMatRenameBuffer, mat.name.c_str()) == 0;
                            const bool nameEmpty = textMatRenameBuffer[0] == '\0';
                            const bool nameExists = !nameEmpty && !nameUnchanged && materialManager->FindTextMaterial(StringID{textMatRenameBuffer, strlen(textMatRenameBuffer)}).IsValid();
                            ImGui::BeginDisabled(nameEmpty || nameUnchanged || nameExists);
                            if (ImGui::Button("Apply")) {
                                materialManager->RenameTextMaterial(id, textMatRenameBuffer);
                                textMatRenameActive = Engine::TextMaterialID::INVALID;
                            }
                            ImGui::EndDisabled();
                            ImGui::SameLine();
                            if (ImGui::Button("Cancel")) {
                                textMatRenameActive = Engine::TextMaterialID::INVALID;
                            }
                            if (nameExists) {
                                ImGui::SameLine();
                                ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "already exists");
                            }
                        }
                        else {
                            if (ImGui::Button("Rename")) {
                                textMatRenameActive = id;
                                const auto& n = mat.name;
                                const size_t copyLen = std::min(n.Size(), sizeof(textMatRenameBuffer) - 1);
                                memcpy(textMatRenameBuffer, n.c_str(), copyLen);
                                textMatRenameBuffer[copyLen] = '\0';
                            }
                        }

                        Engine::TextMaterial editMat = mat;
                        bool changed = false;

                        changed |= ImGui::ColorEdit4("Color Tint", &editMat.colorTint.x);

                        ImGui::SeparatorText("Outline");
                        changed |= ImGui::ColorEdit4("Outline Color", &editMat.outlineColor.x);
                        changed |= ImGui::SliderFloat("Outline Width (em)", &editMat.outlineWidth, 0.0f, 0.25f);

                        ImGui::SeparatorText("Shadow");
                        changed |= ImGui::ColorEdit4("Shadow Color", &editMat.shadowColor.x);
                        changed |= ImGui::DragFloat2("Shadow Offset (em)", &editMat.shadowOffset.x, 0.005f);
                        changed |= ImGui::SliderFloat("Shadow Softness (em)", &editMat.shadowSoftness, 0.0f, 0.25f);

                        if (changed) {
                            materialManager->UpdateTextMaterial(id, editMat);
                        }
                    }
                    ImGui::PopID();
                }
                if (textMatPendingDelete.IsValid()) {
                    materialManager->DeleteTextMaterial(textMatPendingDelete);
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void DrawTexturesWindow(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    static Engine::TextureID s_previewId = Engine::TextureID::INVALID;
    Engine::TextureID hoveredId = Engine::TextureID::INVALID;

    if (ImGui::Begin("Textures")) {
        if (!state->editor.textureInfoCache) {
            const uint32_t count = ctx->assetManager->GetTextureInfoCount();
            state->editor.textureInfoCache = ctx->editorArena.Get().Alloc<Core::ArenaFixedMap<Engine::TextureID, Engine::AssetManager::EditorTextureInfo> >(&ctx->editorArena.Get(), count);
            ctx->assetManager->GetAllTextureInfos(*state->editor.textureInfoCache);
        }
        ImGui::Checkbox("Include Model Textures", &INCLUDE_MODEL_TEXTURES);
        ImGui::Checkbox("Include Builtin Textures", &INCLUDE_BUILTIN_TEXTURES);
        const uint32_t texCount = static_cast<uint32_t>(state->editor.textureInfoCache->Size());
        auto sorted = Core::ArenaFixedVector<Engine::AssetManager::EditorTextureInfo>(&ctx->editorArena.Get(), texCount);
        for (const auto& [texId, info] : *state->editor.textureInfoCache) {
            if (IsTextureFiltered(info)) { continue; }
            sorted.EmplaceBack(info);
        }
        std::ranges::sort(sorted, {}, &Engine::AssetManager::EditorTextureInfo::name);

        if (ImGui::BeginTable("##textures", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Width", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Height", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Mips", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableHeadersRow();

            for (const auto& entry : sorted) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(entry.id.id));
                ImGui::Selectable(entry.name.c_str(), s_previewId == entry.id, ImGuiSelectableFlags_SpanAllColumns);
                if (ImGui::IsItemHovered()) { hoveredId = entry.id; }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u", entry.width);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u", entry.height);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%u", entry.mipCount);
            }
            ImGui::EndTable();
        }

        if (hoveredId != s_previewId) {
            if (s_previewId.IsValid()) { state->editor.texResidency.Release(s_previewId, ctx); }
            s_previewId = hoveredId;
            if (s_previewId.IsValid()) { state->editor.texResidency.Acquire(s_previewId, ctx); }
        }
        if (s_previewId.IsValid()) {
            ImGui::BeginTooltip();
            const uint64_t ds = state->editor.texResidency.GetDescSet(s_previewId, ctx);
            if (ds) {
                ImGui::Image(ds, {256.0f, 256.0f});
            }
            else {
                ImGui::TextUnformatted("Loading...");
            }
            ImGui::EndTooltip();
        }
    }
    else if (s_previewId.IsValid()) {
        state->editor.texResidency.Release(s_previewId, ctx);
        s_previewId = Engine::TextureID::INVALID;
    }
    ImGui::End();
}
}
