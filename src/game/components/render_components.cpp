//
// Created by William on 2026-01-30.
//

#include "render_components.h"

#include <json/nlohmann/json.hpp>

namespace Game::Component
{
void StaticMeshComponent::Serialize(const StaticMeshComponent& comp, nlohmann::json& json)
{
    json["meshIndex"] = comp.meshIndex;
    json["modelId"] = comp.modelId.id;
}

void StaticMeshComponent::Deserialize(StaticMeshComponent& comp, const nlohmann::json& json)
{
    const auto& mi = json["meshIndex"];
    comp.meshIndex = mi.get<int32_t>();

    const auto& mdi = json["modelId"];
    comp.modelId = StringID(mdi.get<uint64_t>());
}
}
