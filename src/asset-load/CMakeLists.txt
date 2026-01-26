add_library(asset-load STATIC
        asset_load_thread.cpp
        asset_load_thread.h
        asset_load_types.cpp
        asset_load_types.h
        asset_load_config.h
        asset-load-jobs/asset_load_job.cpp
        asset-load-jobs/asset_load_job.h
        asset-load-jobs/will_model_load_job.cpp
        asset-load-jobs/will_model_load_job.h
        asset-load-jobs/texture_load_job.cpp
        asset-load-jobs/texture_load_job.h
        asset-load-jobs/pipeline_load_job.cpp
        asset-load-jobs/pipeline_load_job.h
)

target_include_directories(asset-load PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(asset-load PUBLIC
        core
        render
        audio
        enkiTS
        tracy
)

target_compile_definitions(asset-load PRIVATE TRACY_IMPORTS)