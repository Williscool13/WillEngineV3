//
// Created by William on 2026-07-06.
//

#include "input_config.h"

#include "core/containers/vector.h"
#include "platform/paths.h"
#include "platform/file_utils.h"
#include "engine/input/input_rebinding.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"

namespace Engine
{
static Core::Path GetInputConfigPath()
{
    return Platform::GetConfigPath() / "input.wconfig";
}

static void InputConfigToText(const InputConfig& config, TextWriter& w)
{
    w.Count("overrides", static_cast<uint32_t>(config.overrides.Size()));
    for (size_t i = 0; i < config.overrides.Size(); ++i) {
        const InputBindingOverride& o = config.overrides[i];
        w.BeginBlock("o");
        w.Key("action", o.action.id);
        w.Key("row", static_cast<uint64_t>(o.bindingRowInDefault));
        w.Key("type", static_cast<uint32_t>(o.source.type));
        switch (o.source.type) {
            case BindingSourceType::Key: w.Key("value", static_cast<uint32_t>(o.source.key)); break;
            case BindingSourceType::MouseButton: w.Key("value", static_cast<uint32_t>(o.source.mouseButton)); break;
            case BindingSourceType::GamepadButton: w.Key("value", static_cast<uint32_t>(o.source.gamepadButton)); break;
            case BindingSourceType::GamepadAxis: w.Key("value", static_cast<uint32_t>(o.source.gamepadAxis)); break;
            default: return;
        }
        w.EndBlock();
    }
}

static InputConfig InputConfigFromText(const TextReader& r)
{
    InputConfig config{};
    r.ForEachRecord("overrides", [&](const TextReader& e) {
        if (config.overrides.IsFull()) { return; }
        const auto type = static_cast<BindingSourceType>(e.UInt("type"));
        const uint32_t value = e.UInt("value");
        BindingSource source{};
        switch (type) {
            case BindingSourceType::Key: source = BindingSource::FromKey(static_cast<Core::Key>(value)); break;
            case BindingSourceType::MouseButton: source = BindingSource::FromMouse(static_cast<Core::MouseButton>(value)); break;
            case BindingSourceType::GamepadButton: source = BindingSource::FromGamepadButton(static_cast<Core::GamepadButton>(value)); break;
            case BindingSourceType::GamepadAxis: source = BindingSource::FromGamepadAxis(static_cast<Core::GamepadAxis>(value)); break;
            default: return;
        }
        config.overrides.PushBack({
            ActionHandle(e.U64("action")),
            static_cast<size_t>(e.U64("row")),
            source
        });
    });
    return config;
}

static InputConfig ReadInputConfig()
{
    const Core::Path path = GetInputConfigPath();
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return InputConfig{}; }

    return InputConfigFromText(TextReader(map.data, map.size));
}

static bool WriteInputConfig(const InputConfig& config, Core::TlsfAllocator* alloc)
{
    Core::Vector<std::byte> body(alloc, Core::AllocTag::EngineState);
    TextWriter w(body);
    InputConfigToText(config, w);
    return Platform::WriteFile(GetInputConfigPath(), body.Data(), body.Size());
}

static Core::Path InputProfilesDir()
{
    return Platform::GetConfigPath() / "profiles" / "input";
}

static Core::Path InputProfilePath(const char* name)
{
    const auto fileName = Core::InlineString<128>::Format("%s.wprofile", name);
    return InputProfilesDir() / fileName.c_str();
}

namespace Profiles
{
uint32_t ListInputProfiles(ProfileName* outNames, uint32_t maxNames)
{
    Core::Path paths[MAX_PROFILES];
    const uint32_t found = Platform::FindFilesByExtension(InputProfilesDir(), ".wprofile", paths, MAX_PROFILES);
    const uint32_t count = found < maxNames ? found : maxNames;
    for (uint32_t i = 0; i < count; ++i) {
        outNames[i] = ProfileName(paths[i].Stem());
    }
    return count;
}

bool LoadInputProfile(const char* name, InputConfig& out)
{
    Platform::ScopedFileMapping map(InputProfilePath(name));
    if (!map.data) { return false; }

    out = InputConfigFromText(TextReader(map.data, map.size));
    return true;
}

bool SaveInputProfile(const char* name, const InputConfig& config, Core::TlsfAllocator* alloc)
{
    Core::Vector<std::byte> body(alloc, Core::AllocTag::EngineState);
    TextWriter w(body);
    InputConfigToText(config, w);
    return Platform::WriteFile(InputProfilePath(name), body.Data(), body.Size());
}

bool DeleteInputProfile(const char* name)
{
    return Platform::DeleteSingleFile(InputProfilePath(name));
}
} // Profiles

void ApplyInputOverrides(InputState& input, const InputConfig& config)
{
    ApplyDefaultBindings(input);

    for (size_t i = 0; i < config.overrides.Size(); ++i) {
        const InputBindingOverride& o = config.overrides[i];
        if (o.bindingRowInDefault >= input.bindings.Size()) { continue; }

        ActionBinding& binding = input.bindings[o.bindingRowInDefault];
        if (binding.action != o.action || binding.shape != BindingShape::Discrete) { continue; }

        binding.source = o.source;
    }
}

InputConfig BuildInputConfigFromState(const InputState& input)
{
    InputConfig config{};

    for (size_t i = 0; i < input.bindings.Size() && i < input.defaultBindings.Size(); ++i) {
        const ActionBinding& live = input.bindings[i];
        const ActionBinding& def = input.defaultBindings[i];
        if (live.shape != BindingShape::Discrete || live.source == def.source) { continue; }

        config.overrides.PushBack({live.action, i, live.source});
    }

    return config;
}

void LoadAndApplyInputConfig(InputState& input, const ProjectConfig& projectConfig)
{
    InputConfig config{};
    if (!projectConfig.activeInputProfile.IsEmpty()) {
        Profiles::LoadInputProfile(projectConfig.activeInputProfile.c_str(), config);
    }
    else {
        config = ReadInputConfig();
    }
    ApplyInputOverrides(input, config);
}

void SaveInputConfig(const InputState& input, const ProjectConfig& projectConfig, Core::TlsfAllocator* alloc)
{
    const InputConfig config = BuildInputConfigFromState(input);
    if (!projectConfig.activeInputProfile.IsEmpty()) {
        Profiles::SaveInputProfile(projectConfig.activeInputProfile.c_str(), config, alloc);
    }
    else {
        WriteInputConfig(config, alloc);
    }
}
} // Engine
