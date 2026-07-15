//
// Created by William on 2026-02-23.
//

#include "engine_logger.h"

namespace Engine
{
EngineLogger::EngineLogger(Core::MemoryManager& memoryManager)
{
    imguiSink = new(memoryManager.PersistentAllocRaw(sizeof(ImGuiSink), Core::AllocTag::EngineLogger)) ImGuiSink(&memoryManager.Persistent(), Core::AllocTag::EngineLogger);
}

void EngineLogger::Init(Utils::Logger* baseLogger)
{
    if (!baseLogger) {
        fmt::println("Engine Logger received nullptr baseLogger");
        return;
    }

    imguiSink->set_pattern("[%n] %v");
    // sink_ptr format but no-op destructor
    baseLogger->AddSink(spdlog::sink_ptr(imguiSink, [](spdlog::sinks::sink*) {}));

    auto& s = baseLogger->GetSinks();

    // Engine logger first, set as default
    // MEM: spdlog allocates internally; not worth custom allocator since logging is disabled in packaged builds
    auto engineLogger = std::make_shared<spdlog::logger>(kCategoryNames[0], s.begin(), s.end());
    engineLogger->set_level(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
    engineLogger->flush_on(spdlog::level::warn);
    spdlog::register_logger(engineLogger);
    categoryLoggers[0] = engineLogger;
    spdlog::set_default_logger(engineLogger);

    for (int i = 1; i < static_cast<int>(LogCategory::Count); i++) {
        auto cat = std::make_shared<spdlog::logger>(kCategoryNames[i], s.begin(), s.end());
        cat->set_level(spdlog::default_logger()->level());
        cat->flush_on(spdlog::level::warn);
        spdlog::register_logger(cat);
        categoryLoggers[i] = cat;
    }
}

void EngineLogger::Flush()
{
    spdlog::apply_all([](const std::shared_ptr<spdlog::logger>& l) { l->flush(); });
}

void EngineLogger::Shutdown() const
{
    Flush();
    spdlog::drop_all();
}

void EngineLogger::RegisterLoggersForDLL(LogCategory defaultCategory) const
{
    for (auto& cat : categoryLoggers) {
        if (cat) spdlog::register_or_replace(cat);
    }
    spdlog::set_default_logger(categoryLoggers[static_cast<int>(defaultCategory)]);
}

spdlog::logger* EngineLogger::RegisterGameSubCategory(const char* name)
{
    if (const auto existing = spdlog::get(name)) {
        return existing.get();
    }

    const auto& engineLoggerSinks = categoryLoggers[0]->sinks();
    auto logger = std::make_shared<spdlog::logger>(name, engineLoggerSinks.begin(), engineLoggerSinks.end());
    logger->set_level(spdlog::default_logger()->level());
    logger->flush_on(spdlog::level::warn);
    spdlog::register_or_replace(logger);

    imguiSink->RegisterGameSubCategory(name);

    return logger.get();
}
}
