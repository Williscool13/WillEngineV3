add_library(engine STATIC
        will_engine.cpp
        will_engine.h
        engine_api.cpp
        engine_api.h
        asset_manager.cpp
        asset_manager.h
        asset_manager_config.h
        asset_manager_types.h
        project_config.cpp
        project_config.h
        material_manager.cpp
        material_manager.h
        logging/engine_logger.cpp
        logging/engine_logger.h
        logging/imgui_sink.h
        logging/engine_log.h
        logging/log_category.h
        core/environment_map_id.h
        core/material_id.h
        core/texture_id.h
        core/sampler_id.h
        core/model_id.h
        include/frame_sync.h
        include/game_interface.cpp
        include/game_interface.h
        include/engine_context.h
        resources/material/material.cpp
        resources/material/material.h
        resources/material/material_format.cpp
        resources/material/material_format.h
        resources/environment_map/environment_map_format.cpp
        resources/environment_map/environment_map_format.h
        resources/texture/texture.h
        resources/texture/texture_format.cpp
        resources/texture/texture_format.h
        resources/scene/scene_format.cpp
        resources/scene/scene_format.h
        resources/sampler/sampler.h
        resources/scene/scene.h
        resources/prefab/prefab_format.cpp
        resources/prefab/prefab_format.h
        resources/model/model_format.h
        resources/model/model_types.h
        resources/model/static_model.h
        resources/model/model_format.cpp
        serialization/serialization.h
        compression/compression.cpp
        compression/compression.h
        spline/spline.cpp
        core/hash.h
        include/frame_sync.cpp
        logging/engine_assert.h
        resources/font/font.h
        resources/font/font_format.cpp
        resources/font/font_format.h
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