add_library(engine STATIC
        will_engine.cpp
        will_engine.h
        engine_api.cpp
        engine_api.h
        game_model.cpp
        game_model.h
        components.cpp
        components.h
        components/camera_component.h
        components/transform_component.h
)

target_include_directories(engine PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(engine PUBLIC
        core
        render
        # physics
        # audio
        asset-load
        utils
        platform
        SDL3::SDL3
        enkiTS
)

if(GAME_STATIC)
    target_link_libraries(engine PUBLIC game)
endif()

target_compile_definitions(engine PRIVATE ENGINE_EXPORTS)

if(WILL_BUILD_EDITOR)
    target_link_libraries(engine PUBLIC editor)
endif()