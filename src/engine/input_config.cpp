//
// Created by William on 2026-07-06.
//

#include "input_config.h"

#include <fstream>

#include <json/nlohmann/json.hpp>

#include "platform/paths.h"
#include "engine/input/input_rebinding.h"

namespace Engine
{
static Core::Path GetInputConfigPath()
{
    return Platform::GetConfigPath() / "input.wconfig";
}

static nlohmann::json BindingSourceToJson(const BindingSource& source)
{
    nlohmann::json j;
    j["type"] = source.type;
    switch (source.type) {
        case BindingSourceType::Key: j["value"] = source.key; break;
        case BindingSourceType::MouseButton: j["value"] = source.mouseButton; break;
        case BindingSourceType::GamepadButton: j["value"] = source.gamepadButton; break;
        case BindingSourceType::GamepadAxis: j["value"] = source.gamepadAxis; break;
    }
    return j;
}

static bool BindingSourceFromJson(const nlohmann::json& j, BindingSource& outSource)
{
    if (!j.contains("type") || !j["type"].is_number_integer()) { return false; }
    if (!j.contains("value") || !j["value"].is_number_integer()) { return false; }

    const auto type = static_cast<BindingSourceType>(j["type"].get<int32_t>());
    const auto value = j["value"].get<uint32_t>();
    switch (type) {
        case BindingSourceType::Key: outSource = BindingSource::FromKey(static_cast<Core::Key>(value)); return true;
        case BindingSourceType::MouseButton: outSource = BindingSource::FromMouse(static_cast<Core::MouseButton>(value)); return true;
        case BindingSourceType::GamepadButton: outSource = BindingSource::FromGamepadButton(static_cast<Core::GamepadButton>(value)); return true;
        case BindingSourceType::GamepadAxis: outSource = BindingSource::FromGamepadAxis(static_cast<Core::GamepadAxis>(value)); return true;
    }
    return false;
}

InputConfig ReadInputConfig()
{
    InputConfig config{};

    const Core::Path path = GetInputConfigPath();
    std::ifstream file(path.c_str());
    if (!file.is_open()) {
        return config;
    }

    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.contains("overrides") || !j["overrides"].is_array()) {
        return config;
    }

    for (const auto& entry : j["overrides"]) {
        if (!entry.is_object()) { continue; }
        if (!entry.contains("action") || !entry["action"].is_number_unsigned()) { continue; }
        if (!entry.contains("row") || !entry["row"].is_number_unsigned()) { continue; }
        if (!entry.contains("source") || !entry["source"].is_object()) { continue; }

        BindingSource source{};
        if (!BindingSourceFromJson(entry["source"], source)) { continue; }

        config.overrides.PushBack({
            ActionHandle(entry["action"].get<uint64_t>()),
            entry["row"].get<size_t>(),
            source
        });
    }

    return config;
}

bool WriteInputConfig(const InputConfig& config)
{
    const Core::Path path = GetInputConfigPath();
    std::ofstream file(path.c_str());
    if (!file.is_open()) {
        return false;
    }

    nlohmann::json overrides = nlohmann::json::array();
    for (size_t i = 0; i < config.overrides.Size(); ++i) {
        const InputBindingOverride& o = config.overrides[i];
        overrides.push_back({
            {"action", o.action.id},
            {"row", o.bindingRowInDefault},
            {"source", BindingSourceToJson(o.source)}
        });
    }

    nlohmann::json j;
    j["overrides"] = overrides;

    file << j.dump(2);
    return file.good();
}

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
} // Engine
