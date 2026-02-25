add_library(engine STATIC
        will_engine.cpp
        will_engine.h
        engine_api.cpp
        engine_api.h
        game_model.cpp
        game_model.h
        asset_manager.cpp
        asset_manager.h
        asset_manager_config.h
        asset_manager_types.h
        material_manager.cpp
        material_manager.h
        logging/engine_logger.cpp
        logging/engine_logger.h
        logging/imgui_sink.cpp
        logging/imgui_sink.h
        logging/engine_log.cpp
        logging/engine_log.h
        logging/log_category.cpp
        logging/log_category.h
)

target_include_directories(engine PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(engine PUBLIC
        core
        render
        physics
        audio
        asset-load
        utils
        platform
        SDL3::SDL3
        enkiTS
        tracy_interface
        imgui
        imguizmo
)

if(WILL_ENGINE_GAME_STATIC)
    target_link_libraries(engine PUBLIC game)
endif()

target_compile_definitions(engine PRIVATE ENGINE_EXPORTS)

if(WILL_ENGINE_BUILD_EDITOR)
    target_link_libraries(engine PUBLIC editor)
endif()