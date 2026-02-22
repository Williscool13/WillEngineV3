//
// Created by William on 2026-02-22.
//

#include "core/string_id.h"

#if DEBUG
#ifdef WILL_ENGINE_GAME_DLL
void (*gInternStringFn)(uint64_t, const char*) = nullptr;
const char* (*gResolveStringIdFn)(uint64_t) = nullptr;
#endif // WILL_ENGINE_GAME_DLL
#endif // DEBUG