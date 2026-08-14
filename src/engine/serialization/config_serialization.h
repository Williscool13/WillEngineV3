//
// Created by William on 2026-06-13.
//

#ifndef WILL_ENGINE_CONFIG_SERIALIZATION_H
#define WILL_ENGINE_CONFIG_SERIALIZATION_H

namespace Core
{
struct ReSTIRParams;
struct DDGIParams;
struct ReflectionConfiguration;
struct ReflectionProbeConfiguration;
struct GTAOConfiguration;
struct SMAAConfiguration;
struct TAAConfiguration;
struct DonutTAAConfiguration;
struct AntiAliasingConfiguration;
struct PostProcessConfiguration;
}

namespace Engine
{
class TextWriter;
class TextReader;
}

namespace Engine::ConfigSerialization
{
/**
 * Per-config text (de)serialization shared by the project config and profile files. Deserialize reads field-by-field with the passed-in struct supplying defaults, so missing keys keep their current value; ReSTIRParams nests its atrous/svgf/relax/reblur blocks.
 */
void Serialize(const Core::ReSTIRParams& p, TextWriter& w);
void Deserialize(const TextReader& r, Core::ReSTIRParams& p);

void Serialize(const Core::DDGIParams& p, TextWriter& w);
void Deserialize(const TextReader& r, Core::DDGIParams& p);

void Serialize(const Core::ReflectionConfiguration& p, TextWriter& w);
void Deserialize(const TextReader& r, Core::ReflectionConfiguration& p);

void Serialize(const Core::ReflectionProbeConfiguration& p, TextWriter& w);
void Deserialize(const TextReader& r, Core::ReflectionProbeConfiguration& p);

void Serialize(const Core::GTAOConfiguration& p, TextWriter& w);
void Deserialize(const TextReader& r, Core::GTAOConfiguration& p);

void Serialize(const Core::SMAAConfiguration& p, TextWriter& w);
void Deserialize(const TextReader& r, Core::SMAAConfiguration& p);

void Serialize(const Core::TAAConfiguration& p, TextWriter& w);
void Deserialize(const TextReader& r, Core::TAAConfiguration& p);

void Serialize(const Core::DonutTAAConfiguration& p, TextWriter& w);
void Deserialize(const TextReader& r, Core::DonutTAAConfiguration& p);

void Serialize(const Core::AntiAliasingConfiguration& p, TextWriter& w);
void Deserialize(const TextReader& r, Core::AntiAliasingConfiguration& p);

void Serialize(const Core::PostProcessConfiguration& p, TextWriter& w);
void Deserialize(const TextReader& r, Core::PostProcessConfiguration& p);
}

#endif //WILL_ENGINE_CONFIG_SERIALIZATION_H
