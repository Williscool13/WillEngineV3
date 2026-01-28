add_library(asset-load STATIC
        asset_load_types.cpp
        asset_load_types.h
        asset_load_config.h

        gpu_asset_load_thread.cpp
        gpu_asset_load_thread.h
        asset-load-jobs/asset_load_job.cpp
        asset-load-jobs/asset_load_job.h
        asset-load-jobs/will_model_load_job.cpp
        asset-load-jobs/will_model_load_job.h
        asset-load-jobs/texture_load_job.cpp
        asset-load-jobs/texture_load_job.h

        asset-load-jobs/pipeline_load_slot.cpp
        asset-load-jobs/pipeline_load_slot.h
        asset-load-jobs/audio_load_slot.cpp
        asset-load-jobs/audio_load_slot.h
        async_asset_load_manager.cpp
        async_asset_load_manager.h


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
        tracy
)

target_compile_definitions(asset-load PRIVATE TRACY_IMPORTS)