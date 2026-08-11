add_library(editor STATIC
        asset-generation/asset_generator.cpp
        asset-generation/asset_generator.h
        asset-generation/asset_generation_types.h
        asset-generation/asset_source_catalog.cpp
        asset-generation/asset_source_catalog.h
        asset-generation/cgltf_impl.cpp
        asset-generation/stb_impl.cpp
        asset-generation/static_model_generate_slot.cpp
        asset-generation/static_model_generate_slot.h
        asset-generation/texture_generate_slot.cpp
        asset-generation/texture_generate_slot.h
        asset-generation/environment_map_generate_slot.cpp
        asset-generation/environment_map_generate_slot.h
        asset-generation/environment_map_generate_resources.cpp
        asset-generation/environment_map_generate_resources.h
        asset-generation/font_generate_slot.cpp
        asset-generation/font_generate_slot.h
        asset-generation/miscellaneous_asset_generate.cpp
        asset-generation/miscellaneous_asset_generate.h
        renderer/debug_readback_buffer.cpp
        renderer/debug_readback_buffer.h
)

target_include_directories(editor PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(editor PUBLIC
        core
        render
        ktx
        enkiTS
        meshoptimizer
        bc7enc_rdo
        msdf-atlas-gen
        freetype
)