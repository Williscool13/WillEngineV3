//
// Created by William on 2026-04-09.
//

#ifndef WILL_ENGINE_ENGINE_CORE_HASH_H
#define WILL_ENGINE_ENGINE_CORE_HASH_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include "core/containers/hash.h"

namespace Core
{
template<> struct Hash<JPH::BodyID> { uint64_t operator()(JPH::BodyID k) const { return static_cast<uint64_t>(k.GetIndexAndSequenceNumber()); } };
} // Core


#endif //WILL_ENGINE_ENGINE_CORE_HASH_H
