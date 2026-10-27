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
 * Checks what a non-exclusive system touches against its declared AccessSet; callers skip exclusive access.
 * A violation logs an error once per system and type, and asserts.
 */
void CheckReadAccess(const char* system, const AccessSet& access, StringID id);

void CheckWriteAccess(const char* system, const AccessSet& access, StringID id);

/** A type without COMPONENT_NAME cannot be declared, so touching it from a non-exclusive system is a violation. */
void CheckUnnamedAccess(const char* system, std::string_view typeName);

void CheckStructuralAccess(const char* system, const char* operation);
#endif
} // Engine

#endif //WILL_ENGINE_ACCESS_CHECK_H
