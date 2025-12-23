add_library(asset-load STATIC
        asset_load_thread.cpp
        asset_load_thread.h
        asset_load_types.cpp
        asset_load_types.h
        asset_load_config.h
        texture_loader.cpp
        texture_loader.h
        asset_load_job.cpp
        asset_load_job.h
        will_model_load_job.cpp
        will_model_load_job.h
)

target_include_directories(asset-load PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(asset-load PUBLIC
        core
        render
        enkiTS
)