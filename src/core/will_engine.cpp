//
// Created by William on 2025-12-09.
//

#include "will_engine.h"

#include <fmt/format.h>

namespace Engine
{
WillEngine::WillEngine() = default;

WillEngine::~WillEngine() = default;

void WillEngine::Initialize()
{
    fmt::println("Initialize");
}
void WillEngine::Run()
{
    fmt::println("Run");
}
void WillEngine::Cleanup()
{
    fmt::println("Cleanup");
}
}
