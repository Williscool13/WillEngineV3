add_library(audio STATIC
        audio_manager.cpp
        audio_manager.h
        audio_asset.cpp
        audio_asset.h
)

target_include_directories(audio PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(audio PUBLIC
        spdlog::spdlog
        SDL3::SDL3
        SDL3_mixer::SDL3_mixer
)