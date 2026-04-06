add_library(asset-load STATIC
        asset_load_types.cpp
        asset_load_types.h
        asset_load_config.h

        asset-load-jobs/static_model_load_slot.cpp
        asset-load-jobs/static_model_load_slot.h
        asset-load-jobs/texture_load_slot.cpp
        asset-load-jobs/texture_load_slot.h

        asset-load-jobs/pipeline_load_slot.cpp
        asset-load-jobs/pipeline_load_slot.h
        asset-load-jobs/audio_load_slot.cpp
        asset-load-jobs/audio_load_slot.h
        async_asset_load_manager.cpp
        async_asset_load_manager.h
        asset-load-jobs/cubemap_load_slot.cpp
        asset-load-jobs/cubemap_load_slot.h
        asset-load-jobs/procedural_model_load_slot.cpp
        asset-load-jobs/procedural_model_load_slot.h
        asset-load-jobs/par_shapes_impl.cpp
        asset_load_utils.cpp
        asset_load_utils.h
        model_types.cpp
)

target_include_directories(asset-load PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(asset-load PUBLIC
        core
        render
        audio
        platform
        enkiTS
        tracy_interface
        meshoptimizer
)