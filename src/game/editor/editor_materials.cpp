//
// Created by William on 2026-06-26.
//

#include "editor_materials.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstring>

#include <fmt/format.h>

#include "imgui.h"
#include "core/containers/arena_array.h"
#include "core/containers/arena_fixed_vector.h"
#include "engine/include/engine_context.h"
#include "engine/engine_api.h"
#include "engine/editor_state.h"
#include "engine/material_manager.h"
#include "engine/asset_manager.h"
#include "engine/resources/texture/texture.h"
#include "game/components/render/module_mesh_component.h"
#include "game/components/render/procedural_mesh_component.h"
#include "game/components/render/spline_mesh_component.h"
#include "game/components/render/static_mesh_component.h"
#include "game/components/render/static_mesh_primitive_component.h"
#include "game/components/render/text3d_component.h"
#include "platform/paths.h"

namespace Game
{
static bool INCLUDE_MODEL_TEXTURES = false;
static bool INCLUDE_BUILTIN_TEXTURES = false;

static constexpr uint32_t MAX_GROUP_DEPTH = 8;
static constexpr int32_t MATERIAL_TEXTURE_SLOTS = 6;
static constexpr const char* MATERIALS_ROOT = "materials";

static Engine::TextureID s_texPreviewId = Engine::TextureID::INVALID;
static Core::InlineString<128> s_selectedGroup{};
static char s_texturePickerSearch[64] = {};

static bool IsTextureFiltered(const Engine::AssetManager::EditorTextureInfo& info)
{
    if (!INCLUDE_MODEL_TEXTURES && info.category == Engine::TextureCategory::Model) { return true; }
    if (!INCLUDE_BUILTIN_TEXTURES && info.category == Engine::TextureCategory::Builtin) { return true; }
    return false;
}

static int CompareNoCase(const char* a, const char* b)
{
    while (*a && *b) {
        const int ca = std::tolower(static_cast<unsigned char>(*a));
        const int cb = std::tolower(static_cast<unsigned char>(*b));
        if (ca != cb) { return ca - cb; }
        ++a;
        ++b;
    }
    return static_cast<int>(static_cast<unsigned char>(*a)) - static_cast<int>(static_cast<unsigned char>(*b));
}

static bool MatchesSearch(const char* name, const char* search)
{
    if (search[0] == '\0') { return true; }
    for (const char* n = name; *n; ++n) {
        const char* a = n;
        const char* b = search;
        while (*b && *a && std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b))) {
            ++a;
            ++b;
        }
        if (*b == '\0') { return true; }
    }
    return false;
}

static uint32_t SplitGroup(const char* group, Core::InlineString<64>* out, uint32_t maxSegments)
{
    uint32_t count = 0;
    const char* start = group;
    while (*start != '\0' && count < maxSegments) {
        const char* end = strchr(start, '/');
        const size_t len = end ? static_cast<size_t>(end - start) : strlen(start);
        if (len > 0) {
            out[count] = Core::InlineString<64>(std::string_view{start, len});
            ++count;
        }
        if (!end) { break; }
        start = end + 1;
    }
    return count;
}

static Core::InlineString<128> JoinGroup(const Core::InlineString<64>* segments, uint32_t count)
{
    Core::InlineString<128> out;
    for (uint32_t i = 0; i < count; ++i) {
        if (i > 0) { out.Append("/"); }
        out.Append(segments[i].c_str());
    }
    return out;
}

static Core::InlineString<128> MaterialGroupOf(const Engine::Material& mat, Engine::MaterialGroup mode)
{
    if (mode == Engine::MaterialGroup::Flat) { return {}; }

    if (mode == Engine::MaterialGroup::Prefix) {
        const char* name = mat.name.c_str();
        const char* underscore = strchr(name, '_');
        if (!underscore || underscore == name) { return {}; }
        return Core::InlineString<128>(std::string_view{name, static_cast<size_t>(underscore - name)});
    }

    if (mat.sourcePath.IsEmpty()) { return {}; }

    const std::string_view root = Platform::GetAssetPath().View();
    std::string_view rel = mat.sourcePath.Parent().View();
    if (rel.size() > root.size() && rel.compare(0, root.size(), root) == 0) {
        rel = rel.substr(root.size());
    }
    while (!rel.empty() && rel.front() == '/') { rel.remove_prefix(1); }

    const std::string_view materialsRoot = MATERIALS_ROOT;
    if (rel == materialsRoot) { return {}; }
    if (rel.size() > materialsRoot.size() && rel.compare(0, materialsRoot.size(), materialsRoot) == 0 && rel[materialsRoot.size()] == '/') {
        rel = rel.substr(materialsRoot.size() + 1);
    }
    return Core::InlineString<128>(rel);
}

enum SamplerDiffBits : uint32_t
{
    SAMPLER_DIFF_FILTER = 1u << 0,
    SAMPLER_DIFF_MIP = 1u << 1,
    SAMPLER_DIFF_ADDRESS = 1u << 2,
    SAMPLER_DIFF_ANISO = 1u << 3,
    SAMPLER_DIFF_LOD = 1u << 4,
};

static uint32_t SamplerDiff(const Engine::SamplerDesc& sd)
{
    const Engine::SamplerDesc def{};
    uint32_t bits = 0;
    if (sd.magFilter != def.magFilter || sd.minFilter != def.minFilter) { bits |= SAMPLER_DIFF_FILTER; }
    if (sd.mipmapMode != def.mipmapMode) { bits |= SAMPLER_DIFF_MIP; }
    if (sd.addressModeU != def.addressModeU || sd.addressModeV != def.addressModeV || sd.addressModeW != def.addressModeW) { bits |= SAMPLER_DIFF_ADDRESS; }
    if (sd.anisotropyEnable != def.anisotropyEnable || sd.maxAnisotropy != def.maxAnisotropy) { bits |= SAMPLER_DIFF_ANISO; }
    if (sd.mipLodBias != def.mipLodBias || sd.minLod != def.minLod || sd.maxLod != def.maxLod) { bits |= SAMPLER_DIFF_LOD; }
    return bits;
}

static const char* SamplerLabel(uint32_t diff)
{
    switch (diff) {
        case 0: return "Default";
        case SAMPLER_DIFF_FILTER: return "Custom Filter";
        case SAMPLER_DIFF_MIP: return "Custom Mip Mode";
        case SAMPLER_DIFF_ADDRESS: return "Custom Address";
        case SAMPLER_DIFF_ANISO: return "Custom Aniso";
        case SAMPLER_DIFF_LOD: return "Custom LOD";
        default: return "Custom Sampler";
    }
}

static bool HasCustomSampler(const Engine::Material& mat)
{
    for (int32_t slot = 0; slot < MATERIAL_TEXTURE_SLOTS; ++slot) {
        if (SamplerDiff(mat.samplerDesc[slot]) != 0) { return true; }
    }
    return false;
}

static bool PassesFilters(const Engine::Material& mat, int32_t refCount, uint32_t flags)
{
    if ((flags & Engine::MATERIAL_FILTER_IN_USE) != 0 && refCount <= 0) { return false; }
    if ((flags & Engine::MATERIAL_FILTER_UNUSED) != 0 && refCount > 0) { return false; }
    if ((flags & Engine::MATERIAL_FILTER_EMISSIVE) != 0 && mat.props.emissiveFactor.w <= 0.0f) { return false; }
    if ((flags & Engine::MATERIAL_FILTER_MASKED) != 0 && static_cast<int32_t>(mat.props.alphaProperties.y) != 1) { return false; }
    if ((flags & Engine::MATERIAL_FILTER_BLEND) != 0 && static_cast<int32_t>(mat.props.alphaProperties.y) != 2) { return false; }
    if ((flags & Engine::MATERIAL_FILTER_UNLIT) != 0 && mat.props.alphaProperties.w <= 0.5f) { return false; }
    if ((flags & Engine::MATERIAL_FILTER_CUSTOM_SAMPLER) != 0 && !HasCustomSampler(mat)) { return false; }
    return true;
}

struct MaterialListEntry
{
    Engine::MaterialID id{Engine::MaterialID::INVALID};
    const char* name{""};
    Core::InlineString<128> group{};
    int32_t refCount{0};
};

struct MaterialListResult
{
    Engine::MaterialID clicked{Engine::MaterialID::INVALID};
    Engine::MaterialID contextMenu{Engine::MaterialID::INVALID};
    Engine::MaterialID dropped{Engine::MaterialID::INVALID};
    Engine::MaterialID renameCommitted{Engine::MaterialID::INVALID};
    Core::InlineString<128> dropGroup{};
    Core::InlineString<128> clickedGroup{};
    bool bGroupClicked{false};
};

static int32_t MaterialRefCount(const Engine::MaterialManager* materialManager, Engine::MaterialID id)
{
    const auto& entryMap = materialManager->GetIdToEntryMap();
    if (!entryMap.Contains(id)) { return 0; }
    return materialManager->GetActiveMaterials()[entryMap.At(id)].refCounter;
}

static void BuildMaterialEntries(Engine::EngineContext* ctx, const Engine::MaterialBrowserState& view, Core::ArenaFixedVector<MaterialListEntry>& out)
{
    const Engine::MaterialManager* materialManager = ctx->materialManager;

    for (const auto& [id, mat] : materialManager->GetMaterials()) {
        if (mat.bSynthesized) { continue; }
        const int32_t refCount = MaterialRefCount(materialManager, id);
        if (!PassesFilters(mat, refCount, view.filterFlags)) { continue; }
        if (!MatchesSearch(mat.name.c_str(), view.search)) { continue; }
        out.EmplaceBack(MaterialListEntry{id, mat.name.c_str(), MaterialGroupOf(mat, view.group), refCount});
    }

    std::ranges::sort(out, [&view](const MaterialListEntry& a, const MaterialListEntry& b) {
        const int groupCmp = CompareNoCase(a.group.c_str(), b.group.c_str());
        if (groupCmp != 0) { return groupCmp < 0; }
        switch (view.sort) {
            case Engine::MaterialSort::Usage:
                if (a.refCount != b.refCount) { return a.refCount > b.refCount; }
                break;
            case Engine::MaterialSort::Id:
                return a.id.id < b.id.id;
            case Engine::MaterialSort::Name:
                break;
        }
        return CompareNoCase(a.name, b.name) < 0;
    });
}

static void DrawMaterialListHeader(Engine::MaterialBrowserState& view)
{
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##matsearch", "Search...", view.search, sizeof(view.search));

    static const char* groupNames[] = {"Folders", "Prefix", "Flat"};
    static const char* sortNames[] = {"Name", "Usage", "ID"};

    const float third = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
    int groupMode = static_cast<int>(view.group);
    ImGui::SetNextItemWidth(third);
    if (ImGui::Combo("##matgroup", &groupMode, groupNames, 3)) { view.group = static_cast<Engine::MaterialGroup>(groupMode); }
    ImGui::SameLine();
    int sortMode = static_cast<int>(view.sort);
    ImGui::SetNextItemWidth(third);
    if (ImGui::Combo("##matsort", &sortMode, sortNames, 3)) { view.sort = static_cast<Engine::MaterialSort>(sortMode); }
    ImGui::SameLine();

    const int activeFilters = std::popcount(view.filterFlags);
    const Core::InlineString<32> filterLabel = activeFilters > 0
                                                   ? Core::InlineString<32>::Format("Filter (%d)", activeFilters)
                                                   : Core::InlineString<32>("Filter");
    if (ImGui::Button(filterLabel.c_str(), {third, 0.0f})) { ImGui::OpenPopup("##matfilter"); }

    if (ImGui::BeginPopup("##matfilter")) {
        struct FilterOption
        {
            const char* label;
            uint32_t bit;
            uint32_t clears;
        };
        static const FilterOption options[] = {
            {"In use", Engine::MATERIAL_FILTER_IN_USE, Engine::MATERIAL_FILTER_UNUSED},
            {"Unused", Engine::MATERIAL_FILTER_UNUSED, Engine::MATERIAL_FILTER_IN_USE},
            {"Emissive", Engine::MATERIAL_FILTER_EMISSIVE, 0},
            {"Alpha masked", Engine::MATERIAL_FILTER_MASKED, Engine::MATERIAL_FILTER_BLEND},
            {"Alpha blended", Engine::MATERIAL_FILTER_BLEND, Engine::MATERIAL_FILTER_MASKED},
            {"Unlit", Engine::MATERIAL_FILTER_UNLIT, 0},
            {"Custom sampler", Engine::MATERIAL_FILTER_CUSTOM_SAMPLER, 0},
        };
        for (const FilterOption& option : options) {
            bool set = (view.filterFlags & option.bit) != 0;
            if (ImGui::Checkbox(option.label, &set)) {
                if (set) {
                    view.filterFlags |= option.bit;
                    view.filterFlags &= ~option.clears;
                }
                else {
                    view.filterFlags &= ~option.bit;
                }
            }
        }
        ImGui::Separator();
        if (ImGui::Button("Clear All")) { view.filterFlags = Engine::MATERIAL_FILTER_NONE; }
        ImGui::EndPopup();
    }
}

static MaterialListResult DrawMaterialEntries(Engine::EngineState* state, Engine::MaterialBrowserState& view, Core::Span<const MaterialListEntry> entries,
                                              Engine::MaterialID current, bool bEditable)
{
    MaterialListResult result{};

    const bool filterActive = view.search[0] != '\0' || view.filterFlags != Engine::MATERIAL_FILTER_NONE;
    const bool expandForFilter = filterActive && !view.bFilterWasActive;
    view.bFilterWasActive = filterActive;

    Core::InlineVector<Core::InlineString<64>, MAX_GROUP_DEPTH> openStack{};
    Core::InlineString<64> segments[MAX_GROUP_DEPTH];
    int32_t closedAt = -1;

    auto unwindTo = [&openStack, &closedAt](size_t depth) {
        while (openStack.Size() > depth) {
            const int32_t top = static_cast<int32_t>(openStack.Size()) - 1;
            if (closedAt < 0) { ImGui::TreePop(); }
            else if (closedAt == top) { closedAt = -1; }
            openStack.PopBack();
        }
    };

    for (const MaterialListEntry& entry : entries) {
        const uint32_t segmentCount = SplitGroup(entry.group.c_str(), segments, MAX_GROUP_DEPTH);

        size_t common = 0;
        while (common < segmentCount && common < openStack.Size() && strcmp(segments[common].c_str(), openStack[common].c_str()) == 0) { ++common; }
        unwindTo(common);

        for (uint32_t depth = static_cast<uint32_t>(common); depth < segmentCount; ++depth) {
            if (closedAt < 0) {
                if (expandForFilter) { ImGui::SetNextItemOpen(true, ImGuiCond_Always); }
                constexpr ImGuiTreeNodeFlags folderFlags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
                const bool open = ImGui::TreeNodeEx(segments[depth].c_str(), folderFlags);
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                    result.bGroupClicked = true;
                    result.clickedGroup = JoinGroup(segments, depth + 1);
                }
                if (bEditable && ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MATERIAL_ID")) {
                        result.dropped = *static_cast<const Engine::MaterialID*>(payload->Data);
                        result.dropGroup = JoinGroup(segments, depth + 1);
                    }
                    ImGui::EndDragDropTarget();
                }
                if (!open) { closedAt = static_cast<int32_t>(openStack.Size()); }
            }
            openStack.PushBack(segments[depth]);
        }

        if (closedAt >= 0) { continue; }

        ImGui::PushID(static_cast<int>(entry.id.id));
        if (bEditable && state->editor.renamingMaterial == entry.id) {
            if (state->editor.bMaterialRenameRequestFocus) {
                ImGui::SetKeyboardFocusHere();
                state->editor.bMaterialRenameRequestFocus = false;
            }
            ImGui::SetNextItemWidth(-1.0f);
            const bool committed = ImGui::InputText("##matrename", state->editor.materialRenameBuffer, sizeof(state->editor.materialRenameBuffer),
                                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (committed || ImGui::IsItemDeactivated()) {
                result.renameCommitted = entry.id;
                state->editor.renamingMaterial = Engine::MaterialID::INVALID;
            }
        }
        else {
            if (ImGui::Selectable(entry.name, entry.id == current)) { result.clicked = entry.id; }
            if (bEditable) {
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("MATERIAL_ID", &entry.id, sizeof(entry.id));
                    ImGui::TextUnformatted(entry.name);
                    ImGui::EndDragDropSource();
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { result.contextMenu = entry.id; }
            }
            if (entry.refCount > 0) {
                const Core::InlineString<16> badge = Core::InlineString<16>::Format("%d", entry.refCount);
                const float badgeWidth = ImGui::CalcTextSize(badge.c_str()).x;
                ImGui::SameLine(ImGui::GetContentRegionMax().x - badgeWidth - ImGui::GetStyle().ItemSpacing.x);
                ImGui::TextDisabled("%s", badge.c_str());
            }
        }
        ImGui::PopID();
    }

    unwindTo(0);
    return result;
}

Engine::MaterialID DrawMaterialSelector(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::MaterialBrowserState& view, Engine::MaterialID current)
{
    DrawMaterialListHeader(view);

    Core::Arena& arena = ctx->editorArena.Get();
    const uint32_t materialCount = static_cast<uint32_t>(ctx->materialManager->GetMaterials().Size());
    Core::ArenaFixedVector<MaterialListEntry> entries(&arena, materialCount + 1);
    BuildMaterialEntries(ctx, view, entries);

    if (entries.IsEmpty()) {
        ImGui::TextDisabled("No matching materials");
        return Engine::MaterialID::INVALID;
    }

    return DrawMaterialEntries(state, view, entries, current, false).clicked;
}

static void DrawMaterialBrowserPane(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    Engine::MaterialManager* materialManager = ctx->materialManager;
    Engine::MaterialBrowserState& view = state->editor.materialBrowser;
    const bool bFolderMode = view.group == Engine::MaterialGroup::Folder;

    if (ImGui::Button("New Material")) { ImGui::OpenPopup("##newmat"); }

    if (ImGui::BeginPopup("##newmat")) {
        static char newMatName[128] = "new_material";
        static char newMatFolder[128] = {};
        if (ImGui::IsWindowAppearing()) { strncpy_s(newMatFolder, s_selectedGroup.c_str(), sizeof(newMatFolder) - 1); }
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputText("Name", newMatName, sizeof(newMatName));
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("Folder", MATERIALS_ROOT, newMatFolder, sizeof(newMatFolder));
        const bool nameEmpty = newMatName[0] == '\0';
        const bool nameExists = !nameEmpty && materialManager->FindMutableMaterial(StringID{newMatName, strlen(newMatName)}).IsValid();
        ImGui::BeginDisabled(nameEmpty || nameExists);
        if (ImGui::Button("Create")) {
            materialManager->CreateMaterial(newMatName, newMatFolder);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        if (nameExists) {
            ImGui::SameLine();
            ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "already exists");
        }
        ImGui::EndPopup();
    }

    DrawMaterialListHeader(view);

    Core::Arena& arena = ctx->editorArena.Get();
    const uint32_t materialCount = static_cast<uint32_t>(materialManager->GetMaterials().Size());
    Core::ArenaFixedVector<MaterialListEntry> entries(&arena, materialCount + 1);
    BuildMaterialEntries(ctx, view, entries);

    static Engine::MaterialID s_deleteCandidate = Engine::MaterialID::INVALID;
    static bool s_deleteRequested = false;

    if (ImGui::BeginChild("##matlist", {0.0f, 0.0f}, ImGuiChildFlags_Borders)) {
        const MaterialListResult result = DrawMaterialEntries(state, view, entries, state->editor.selectedMaterial, true);
        if (result.clicked.IsValid()) { state->editor.selectedMaterial = result.clicked; }
        if (result.bGroupClicked) { s_selectedGroup = result.clickedGroup; }
        if (result.renameCommitted.IsValid()) {
            const char* newName = state->editor.materialRenameBuffer;
            const Engine::Material* renamed = materialManager->GetMaterial(result.renameCommitted);
            if (renamed && newName[0] != '\0' && strcmp(newName, renamed->name.c_str()) != 0) {
                materialManager->RenameMutableMaterial(result.renameCommitted, newName);
            }
        }
        if (result.dropped.IsValid()) { materialManager->MoveMutableMaterial(result.dropped, result.dropGroup.c_str()); }
        if (result.contextMenu.IsValid()) {
            state->editor.selectedMaterial = result.contextMenu;
            ImGui::OpenPopup("##matcontext");
        }

        if (ImGui::BeginPopup("##matcontext")) {
            const Engine::MaterialID id = state->editor.selectedMaterial;
            const Engine::Material* mat = materialManager->GetMaterial(id);
            if (mat) {
                if (ImGui::MenuItem("Rename")) {
                    state->editor.renamingMaterial = id;
                    state->editor.bMaterialRenameRequestFocus = true;
                    strncpy_s(state->editor.materialRenameBuffer, mat->name.c_str(), sizeof(state->editor.materialRenameBuffer) - 1);
                }
                if (bFolderMode && ImGui::MenuItem("Move To Root", nullptr, false, !MaterialGroupOf(*mat, view.group).IsEmpty())) {
                    materialManager->MoveMutableMaterial(id, "");
                }
                const bool materialInUse = MaterialRefCount(materialManager, id) > 0;
                ImGui::BeginDisabled(materialInUse);
                if (ImGui::MenuItem("Delete")) {
                    s_deleteCandidate = id;
                    s_deleteRequested = true;
                }
                ImGui::EndDisabled();
                if (materialInUse && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Material is referenced by scene entities");
                }
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();

    if (s_deleteRequested) {
        s_deleteRequested = false;
        ImGui::OpenPopup("##matdelete");
    }
    if (ImGui::BeginPopupModal("##matdelete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const Engine::Material* candidate = materialManager->GetMaterial(s_deleteCandidate);
        if (!candidate) {
            s_deleteCandidate = Engine::MaterialID::INVALID;
            ImGui::CloseCurrentPopup();
        }
        else {
            ImGui::Text("Delete '%s'?", candidate->name.c_str());
            ImGui::TextDisabled("The .wmaterial file is removed from disk.");
            ImGui::Separator();
            if (ImGui::Button("Delete")) {
                materialManager->DeleteMutableMaterial(s_deleteCandidate);
                if (state->editor.selectedMaterial == s_deleteCandidate) { state->editor.selectedMaterial = Engine::MaterialID::INVALID; }
                s_deleteCandidate = Engine::MaterialID::INVALID;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                s_deleteCandidate = Engine::MaterialID::INVALID;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

static void DrawMaterialTextureSlots(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::MaterialID id, const Engine::Material& mat)
{
    Engine::MaterialManager* materialManager = ctx->materialManager;

    ImGui::SeparatorText("Textures");
    static const char* slotNames[] = {"Color", "Metal/Rough", "Normal", "Emissive", "Occlusion", "Packed NRM"};
    static Engine::TextureID texEditPending = Engine::TextureID::INVALID;
    static Engine::SamplerDesc samplerEditPending{};

    if (!state->editor.textureInfoCache) {
        const uint32_t count = ctx->assetManager->GetTextureInfoCount();
        state->editor.textureInfoCache = ctx->editorArena.Get().Alloc<Core::ArenaFixedMap<Engine::TextureID, Engine::AssetManager::EditorTextureInfo> >(
            &ctx->editorArena.Get(), count);
        ctx->assetManager->GetAllTextureInfos(*state->editor.textureInfoCache);
    }
    const auto& matTexInfoMap = *state->editor.textureInfoCache;
    bool bPickerDrawn = false;

    if (ImGui::BeginTable("##texslots", 4, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("##slot", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("##tex", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##smp", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("##smpstate", ImGuiTableColumnFlags_WidthFixed, 110.0f);

        for (int32_t slot = 0; slot < MATERIAL_TEXTURE_SLOTS; ++slot) {
            ImGui::PushID(slot);
            ImGui::TableNextRow();

            const Engine::TextureID& texId = mat.textureRefs[slot];
            const char* currentTexName = "None";
            if (const Engine::AssetManager::EditorTextureInfo* info = texId.IsValid() ? matTexInfoMap.Find(texId) : nullptr) {
                currentTexName = info->name.c_str();
            }

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(slotNames[slot]);
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Button(currentTexName, {-1.0f, 0.0f})) {
                texEditPending = texId;
                s_texturePickerSearch[0] = '\0';
                ImGui::OpenPopup("TextureSelect");
            }
            ImGui::TableSetColumnIndex(2);
            if (ImGui::SmallButton("Smp")) {
                samplerEditPending = mat.samplerDesc[slot];
                ImGui::OpenPopup("SamplerEdit");
            }
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("%s", SamplerLabel(SamplerDiff(mat.samplerDesc[slot])));

            if (ImGui::BeginPopupModal("TextureSelect", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                bPickerDrawn = true;
                ImGui::Text("Slot: %s", slotNames[slot]);
                ImGui::Separator();

                ImGui::SetNextItemWidth(400.0f);
                ImGui::InputTextWithHint("##texsearch", "Search...", s_texturePickerSearch, sizeof(s_texturePickerSearch));
                ImGui::Checkbox("Include Model Textures", &INCLUDE_MODEL_TEXTURES);
                ImGui::SameLine();
                ImGui::Checkbox("Include Builtin Textures", &INCLUDE_BUILTIN_TEXTURES);

                const uint32_t texCount = static_cast<uint32_t>(state->editor.textureInfoCache->Size());
                auto sorted = Core::ArenaFixedVector<Engine::AssetManager::EditorTextureInfo>(&ctx->editorArena.Get(), texCount + 1);
                for (const auto& [texId2, info] : *state->editor.textureInfoCache) {
                    if (IsTextureFiltered(info)) { continue; }
                    if (!MatchesSearch(info.name.c_str(), s_texturePickerSearch)) { continue; }
                    sorted.EmplaceBack(info);
                }
                std::ranges::sort(sorted, {}, &Engine::AssetManager::EditorTextureInfo::name);

                if (ImGui::BeginChild("##texlist", {400.0f, 300.0f}, ImGuiChildFlags_Borders)) {
                    if (ImGui::Selectable("(None)", !texEditPending.IsValid())) {
                        texEditPending = Engine::TextureID::INVALID;
                    }
                    for (const auto& info : sorted) {
                        if (ImGui::Selectable(info.name.c_str(), texEditPending == info.id)) {
                            texEditPending = info.id;
                        }
                        if (ImGui::IsItemHovered()) {
                            if (s_texPreviewId != info.id) {
                                if (s_texPreviewId.IsValid()) { state->editor.texResidency.Release(s_texPreviewId, ctx); }
                                s_texPreviewId = info.id;
                                // todo: ideally only load the lowest mip for preview
                                state->editor.texResidency.Acquire(info.id, ctx);
                            }
                            ImGui::BeginTooltip();
                            const uint64_t ds = state->editor.texResidency.GetDescSet(info.id, ctx);
                            if (ds) {
                                ImGui::Image(ds, {128.0f, 128.0f});
                            }
                            else {
                                ImGui::TextUnformatted("Loading...");
                            }
                            ImGui::EndTooltip();
                        }
                    }
                }
                ImGui::EndChild();

                if (ImGui::Button("OK")) {
                    Engine::Material editMat = mat;
                    editMat.textureRefs[slot] = texEditPending;
                    materialManager->UpdateMutableMaterial(id, editMat, true);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("SamplerEdit", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Slot: %s", slotNames[slot]);
                ImGui::Separator();

                static const char* filterNames[] = {"Nearest", "Linear"};
                int magF = samplerEditPending.magFilter;
                int minF = samplerEditPending.minFilter;
                if (ImGui::Combo("Mag Filter", &magF, filterNames, 2)) { samplerEditPending.magFilter = static_cast<VkFilter>(magF); }
                if (ImGui::Combo("Min Filter", &minF, filterNames, 2)) { samplerEditPending.minFilter = static_cast<VkFilter>(minF); }

                static const char* mipmapModeNames[] = {"Nearest", "Linear"};
                int mipMode = samplerEditPending.mipmapMode;
                if (ImGui::Combo("Mip Mode", &mipMode, mipmapModeNames, 2)) { samplerEditPending.mipmapMode = static_cast<VkSamplerMipmapMode>(mipMode); }

                static const char* addressModeNames[] = {"Repeat", "Mirrored Repeat", "Clamp To Edge", "Clamp To Border", "Mirror Clamp"};
                int addrU = samplerEditPending.addressModeU;
                int addrV = samplerEditPending.addressModeV;
                int addrW = samplerEditPending.addressModeW;
                if (ImGui::Combo("Address U", &addrU, addressModeNames, 5)) { samplerEditPending.addressModeU = static_cast<VkSamplerAddressMode>(addrU); }
                if (ImGui::Combo("Address V", &addrV, addressModeNames, 5)) { samplerEditPending.addressModeV = static_cast<VkSamplerAddressMode>(addrV); }
                if (ImGui::Combo("Address W", &addrW, addressModeNames, 5)) { samplerEditPending.addressModeW = static_cast<VkSamplerAddressMode>(addrW); }

                bool aniso = samplerEditPending.anisotropyEnable == VK_TRUE;
                if (ImGui::Checkbox("Anisotropy", &aniso)) { samplerEditPending.anisotropyEnable = aniso ? VK_TRUE : VK_FALSE; }
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
                    Engine::Material editMat = mat;
                    editMat.samplerDesc[slot] = samplerEditPending;
                    materialManager->UpdateMutableMaterial(id, editMat, true);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // The picker can close itself on Escape, so release on "stopped drawing" rather than on Cancel.
    if (!bPickerDrawn && s_texPreviewId.IsValid()) {
        state->editor.texResidency.Release(s_texPreviewId, ctx);
        s_texPreviewId = Engine::TextureID::INVALID;
    }
}

template<typename C, typename Tag>
static void RetagIfUsingMaterial(Engine::EngineState* state, Engine::MaterialID id)
{
    const Engine::InstanceStore& store = state->instanceStore;
    for (const auto& [entity, component, runtime] : state->registry.view<C, Component::MeshRuntime>().each()) {
        if (!runtime.range.IsValid()) { continue; }
        for (uint32_t i = 0; i < runtime.range.count; ++i) {
            if (store[runtime.range.offset + i].materialID != id) { continue; }
            state->registry.emplace_or_replace<Tag>(entity);
            break;
        }
    }
}

/** The tri-light range is decided at fill time from the material's immutability, so unlocking one has to re-resolve every mesh using it. */
static void RetagMeshesUsingMaterial(Engine::EngineState* state, Engine::MaterialID id)
{
    RetagIfUsingMaterial<Component::StaticMeshComponent, Component::StaticMeshLoadingTag>(state, id);
    RetagIfUsingMaterial<Component::StaticMeshPrimitiveComponent, Component::StaticMeshPrimitiveLoadingTag>(state, id);
    RetagIfUsingMaterial<Component::ProceduralMeshComponent, Component::ProceduralMeshLoadingTag>(state, id);
    RetagIfUsingMaterial<Component::SplineMeshComponent, Component::SplineMeshLoadingTag>(state, id);
    RetagIfUsingMaterial<Component::Text3DComponent, Component::Text3DLoadingTag>(state, id);
    RetagIfUsingMaterial<Component::ModuleMeshComponent, Component::ModuleMeshLoadingTag>(state, id);
    state->assetLoad.bPendingModelResolve = true;
}

static void DrawMaterialDetailPane(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    Engine::MaterialManager* materialManager = ctx->materialManager;
    const Engine::Material* selected = materialManager->GetMaterial(state->editor.selectedMaterial);
    if (!selected || selected->bSynthesized) {
        ImGui::TextDisabled("Select a material");
        return;
    }

    const Engine::MaterialID id = state->editor.selectedMaterial;
    const Engine::Material& mat = *selected;

    ImGui::TextUnformatted(mat.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%d refs)", MaterialRefCount(materialManager, id));
    ImGui::TextDisabled("ID %llu", id.id);
    if (!mat.sourcePath.IsEmpty()) {
        ImGui::TextDisabled("%s", mat.sourcePath.c_str());
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", mat.sourcePath.c_str()); }
    }

    Engine::Material editMat = mat;
    MaterialProperties& props = editMat.props;
    bool changed = false;

    bool immutable = editMat.immutable;
    if (ImGui::Checkbox("Immutable", &immutable)) {
        editMat.immutable = immutable;
        changed = true;
        if (!immutable) {
            RetagMeshesUsingMaterial(state, id);
        }
    }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Contents never change at runtime. Locks the fields below."); }

    ImGui::BeginDisabled(immutable);

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

    ImGui::SeparatorText("Shader");
    {
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
    }
    {
        Core::Arena& arena = ctx->editorArena.Get();
        Core::ArenaFixedVector<StringID> lightingPipelines = ctx->pipelineManager->GetLightingPipelinesForMode(state->lighting.lightingMode, arena);
        const int32_t pipelineCount = static_cast<int32_t>(lightingPipelines.Size());

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

    DrawMaterialTextureSlots(ctx, state, id, mat);

    ImGui::EndDisabled();

    if (changed) {
        materialManager->UpdateMutableMaterial(id, editMat, true);
    }
}

/** Deliberately not shared with the mesh tab: different delete gating, no folders, no filters. */
static void DrawTextMaterialsTab(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    Engine::MaterialManager* materialManager = ctx->materialManager;
    const auto& allTextMaterials = materialManager->GetTextMaterials();

    if (!ImGui::BeginTable("##textmatsplit", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) { return; }
    ImGui::TableSetupColumn("##list", ImGuiTableColumnFlags_WidthFixed, 240.0f);
    ImGui::TableSetupColumn("##detail", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    static char textSearch[64] = {};
    if (ImGui::Button("New Text Material")) { ImGui::OpenPopup("##newtextmat"); }
    if (ImGui::BeginPopup("##newtextmat")) {
        static char newTextMatName[128] = "new_text_material";
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("##textmatname", newTextMatName, sizeof(newTextMatName));
        const bool nameEmpty = newTextMatName[0] == '\0';
        const bool nameExists = !nameEmpty && materialManager->FindTextMaterial(StringID{newTextMatName, strlen(newTextMatName)}).IsValid();
        ImGui::BeginDisabled(nameEmpty || nameExists);
        if (ImGui::Button("Create")) {
            materialManager->CreateTextMaterial(newTextMatName);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        if (nameExists) {
            ImGui::SameLine();
            ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "already exists");
        }
        ImGui::EndPopup();
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##textmatsearch", "Search...", textSearch, sizeof(textSearch));

    Engine::TextMaterialID textMatPendingDelete = Engine::TextMaterialID::INVALID;
    if (ImGui::BeginChild("##textmatlist", {0.0f, 0.0f}, ImGuiChildFlags_Borders)) {
        for (const auto& [id, mat] : allTextMaterials) {
            if (!MatchesSearch(mat.name.c_str(), textSearch)) { continue; }
            ImGui::PushID(static_cast<int>(id.id));
            if (state->editor.renamingTextMaterial == id) {
                if (state->editor.bMaterialRenameRequestFocus) {
                    ImGui::SetKeyboardFocusHere();
                    state->editor.bMaterialRenameRequestFocus = false;
                }
                ImGui::SetNextItemWidth(-1.0f);
                const bool committed = ImGui::InputText("##textrename", state->editor.materialRenameBuffer, sizeof(state->editor.materialRenameBuffer),
                                                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                if (committed || ImGui::IsItemDeactivated()) {
                    if (strcmp(state->editor.materialRenameBuffer, mat.name.c_str()) != 0 && state->editor.materialRenameBuffer[0] != '\0') {
                        materialManager->RenameTextMaterial(id, state->editor.materialRenameBuffer);
                    }
                    state->editor.renamingTextMaterial = Engine::TextMaterialID::INVALID;
                }
            }
            else {
                if (ImGui::Selectable(mat.name.c_str(), state->editor.selectedTextMaterial == id)) { state->editor.selectedTextMaterial = id; }
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    state->editor.selectedTextMaterial = id;
                    ImGui::OpenPopup("##textmatcontext");
                }
                if (ImGui::BeginPopup("##textmatcontext")) {
                    if (ImGui::MenuItem("Rename")) {
                        state->editor.renamingTextMaterial = id;
                        state->editor.bMaterialRenameRequestFocus = true;
                        strncpy_s(state->editor.materialRenameBuffer, mat.name.c_str(), sizeof(state->editor.materialRenameBuffer) - 1);
                    }
                    if (ImGui::MenuItem("Delete")) { textMatPendingDelete = id; }
                    ImGui::EndPopup();
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    if (textMatPendingDelete.IsValid()) {
        materialManager->DeleteTextMaterial(textMatPendingDelete);
        state->editor.selectedTextMaterial = Engine::TextMaterialID::INVALID;
    }

    ImGui::TableSetColumnIndex(1);
    if (const Engine::TextMaterial* selected = materialManager->GetTextMaterial(state->editor.selectedTextMaterial)) {
        const Engine::TextMaterialID id = state->editor.selectedTextMaterial;
        Engine::TextMaterial editMat = *selected;
        bool changed = false;

        ImGui::TextUnformatted(selected->name.c_str());
        ImGui::TextDisabled("ID %llu", id.id);

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
    else {
        ImGui::TextDisabled("Select a text material");
    }

    ImGui::EndTable();
}

void DrawMaterialsWindow(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (ImGui::Begin("Materials")) {
        state->editor.bMaterialListFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        if (ImGui::BeginTabBar("##MaterialTabs")) {
            if (ImGui::BeginTabItem("Mesh Materials")) {
                if (ImGui::BeginTable("##matsplit", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
                    ImGui::TableSetupColumn("##list", ImGuiTableColumnFlags_WidthFixed, 260.0f);
                    ImGui::TableSetupColumn("##detail", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    DrawMaterialBrowserPane(ctx, state);

                    ImGui::TableSetColumnIndex(1);
                    DrawMaterialDetailPane(ctx, state);

                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Text Materials")) {
                DrawTextMaterialsTab(ctx, state);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    else {
        state->editor.bMaterialListFocused = false;
    }
    ImGui::End();
}

void DrawTexturesWindow(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    static Engine::TextureID s_previewId = Engine::TextureID::INVALID;
    static char s_search[64] = {};
    Engine::TextureID hoveredId = Engine::TextureID::INVALID;

    if (ImGui::Begin("Textures")) {
        if (!state->editor.textureInfoCache) {
            const uint32_t count = ctx->assetManager->GetTextureInfoCount();
            state->editor.textureInfoCache = ctx->editorArena.Get().Alloc<Core::ArenaFixedMap<Engine::TextureID, Engine::AssetManager::EditorTextureInfo> >(
                &ctx->editorArena.Get(), count);
            ctx->assetManager->GetAllTextureInfos(*state->editor.textureInfoCache);
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##texwindowsearch", "Search...", s_search, sizeof(s_search));
        ImGui::Checkbox("Include Model Textures", &INCLUDE_MODEL_TEXTURES);
        ImGui::SameLine();
        ImGui::Checkbox("Include Builtin Textures", &INCLUDE_BUILTIN_TEXTURES);
        const uint32_t texCount = static_cast<uint32_t>(state->editor.textureInfoCache->Size());
        auto sorted = Core::ArenaFixedVector<Engine::AssetManager::EditorTextureInfo>(&ctx->editorArena.Get(), texCount + 1);
        for (const auto& [texId, info] : *state->editor.textureInfoCache) {
            if (IsTextureFiltered(info)) { continue; }
            if (!MatchesSearch(info.name.c_str(), s_search)) { continue; }
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
