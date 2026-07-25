//
// Created by William on 2026-06-13.
//

#ifndef WILL_ENGINE_CONFIG_SERIALIZATION_H
#define WILL_ENGINE_CONFIG_SERIALIZATION_H

#include <json/nlohmann/json.hpp>

namespace Core
{
struct ReSTIRParams;
struct DDGIParams;
struct ReflectionConfiguration;
struct ReflectionProbeConfiguration;
struct HeroShadowConfiguration;
struct GTAOConfiguration;
struct SMAAConfiguration;
struct TAAConfiguration;
struct DonutTAAConfiguration;
struct AntiAliasingConfiguration;
struct PostProcessConfiguration;
}

namespace Engine::ConfigSerialization
{
/**
 * Per-config JSON (de)serialization shared by the project config and profile files. FromJson reads field-by-field with the passed-in struct supplying defaults, so missing keys keep their current value; ToJson(ReSTIRParams) nests its relax/atrous/svgf objects.
 */
nlohmann::json ToJson(const Core::ReSTIRParams& p);
void FromJson(const nlohmann::json& j, Core::ReSTIRParams& p);

nlohmann::json ToJson(const Core::DDGIParams& p);
void FromJson(const nlohmann::json& j, Core::DDGIParams& p);

nlohmann::json ToJson(const Core::ReflectionConfiguration& p);
void FromJson(const nlohmann::json& j, Core::ReflectionConfiguration& p);

nlohmann::json ToJson(const Core::ReflectionProbeConfiguration& p);
void FromJson(const nlohmann::json& j, Core::ReflectionProbeConfiguration& p);

nlohmann::json ToJson(const Core::HeroShadowConfiguration& p);
void FromJson(const nlohmann::json& j, Core::HeroShadowConfiguration& p);

nlohmann::json ToJson(const Core::GTAOConfiguration& p);
void FromJson(const nlohmann::json& j, Core::GTAOConfiguration& p);

nlohmann::json ToJson(const Core::SMAAConfiguration& p);
void FromJson(const nlohmann::json& j, Core::SMAAConfiguration& p);

nlohmann::json ToJson(const Core::TAAConfiguration& p);
void FromJson(const nlohmann::json& j, Core::TAAConfiguration& p);

nlohmann::json ToJson(const Core::DonutTAAConfiguration& p);
void FromJson(const nlohmann::json& j, Core::DonutTAAConfiguration& p);

nlohmann::json ToJson(const Core::AntiAliasingConfiguration& p);
void FromJson(const nlohmann::json& j, Core::AntiAliasingConfiguration& p);

nlohmann::json ToJson(const Core::PostProcessConfiguration& p);
void FromJson(const nlohmann::json& j, Core::PostProcessConfiguration& p);
}

#endif //WILL_ENGINE_CONFIG_SERIALIZATION_H
