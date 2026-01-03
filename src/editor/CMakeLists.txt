add_library(editor STATIC
        asset-generation/asset_generator.cpp
        asset-generation/asset_generator.h
        asset-generation/asset_generation_types.cpp
        asset-generation/asset_generation_types.h
        asset-generation/stb_impl.cpp
        model-generator/model_generator.cpp
        model-generator/model_generator.h
)

target_include_directories(editor PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(editor PUBLIC
        core
        render
        fastgltf
        imgui
        enkiTS
        meshoptimizer
        tinygltf
)