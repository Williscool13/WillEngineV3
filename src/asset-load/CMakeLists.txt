add_library(asset-load STATIC

)

target_include_directories(asset-load PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(asset-load PUBLIC
        core
        render
        fastgltf
        imgui
        enkiTS
        meshoptimizer
)