//
// Created by William on 2026-10-27.
//

#ifndef WILL_ENGINE_ACCESS_CHECK_H
#define WILL_ENGINE_ACCESS_CHECK_H

#include <string_view>

#include "core/string_id.h"

#ifdef WDEBUG
#define WILL_ACCESS_CHECK 1
#else
#define WILL_ACCESS_CHECK 0
#endif

namespace Engine
{
struct AccessSet;

#if WILL_ACCESS_CHECK
/**
 * Checks what the running SystemGraph system touches against its declared AccessSet. Exclusive systems and code outside the graph pass.
 * A violation logs an error once per system and type, and asserts.
 */
void SetRunningSystem(const char* name, const AccessSet* access);

void CheckReadAccess(StringID id);

void CheckWriteAccess(StringID id);

/** A type without COMPONENT_NAME cannot be declared, so touching it from a non-exclusive system is a violation. */
void CheckUnnamedAccess(std::string_view typeName);

void CheckStructuralAccess(const char* operation);
#endif
} // Engine

#endif //WILL_ENGINE_ACCESS_CHECK_H
