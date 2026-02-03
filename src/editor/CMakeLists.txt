add_library(editor STATIC
        asset-generation/asset_generator.cpp
        asset-generation/asset_generator.h
        asset-generation/asset_generation_types.cpp
        asset-generation/asset_generation_types.h
        asset-generation/stb_impl.cpp
        asset-generation/model_generate_slot.cpp
        asset-generation/model_generate_slot.h
        asset-generation/texture_generate_slot.cpp
        asset-generation/texture_generate_slot.h
)

target_include_directories(editor PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(editor PUBLIC
        core
        render
        fastgltf
        enkiTS
        meshoptimizer
        bc7enc_rdo
)