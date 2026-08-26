//
// Created by William on 2026-08-26.
//

#ifndef WILL_ENGINE_FRAME_STAMP_H
#define WILL_ENGINE_FRAME_STAMP_H

#include <atomic>
#include <cstdint>

namespace Core
{
/**
 * Do NOT access from Game DLL.
 */
inline std::atomic<uint64_t> gGameFrame{0};
inline std::atomic<uint64_t> gRenderFrame{0};
}

#endif //WILL_ENGINE_FRAME_STAMP_H
