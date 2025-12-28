add_library(render STATIC
        render_thread.cpp
        render_thread.h
        resource_manager.cpp
        resource_manager.h
        frame_resources.cpp
        frame_resources.h
        render_config.cpp
        render_config.h
        vulkan/vk_context.cpp
        vulkan/vk_context.h
        vulkan/vk_swapchain.cpp
        vulkan/vk_swapchain.h
        vulkan/vk_synchronization.cpp
        vulkan/vk_synchronization.h
        vulkan/vk_utils.cpp
        vulkan/vk_utils.h
        vulkan/vk_config.cpp
        vulkan/vk_config.h
        vulkan/vk_resources.cpp
        vulkan/vk_resources.h
        vulkan/vk_helpers.cpp
        vulkan/vk_helpers.h
        vulkan/vk_render_extents.cpp
        vulkan/vk_render_extents.h
        vulkan/vk_imgui_wrapper.cpp
        vulkan/vk_imgui_wrapper.h
        types/render_types.cpp
        types/render_types.h
        shaders/common_interop.h
        descriptors/vk_bindless_resources_storage.cpp
        descriptors/vk_bindless_resources_storage.h
        descriptors/vk_bindless_resources_sampler_images.cpp
        descriptors/vk_bindless_resources_sampler_images.h
        descriptors/vk_bindless_resources_combined.cpp
        descriptors/vk_bindless_resources_combined.h
        descriptors/vk_descriptors.cpp
        descriptors/vk_descriptors.h
        types/vk_types.cpp
        types/vk_types.h
        pipelines/vk_pipeline.cpp
        pipelines/vk_pipeline.h
        pipelines/basic_compute_pipeline.cpp
        pipelines/basic_compute_pipeline.h
        pipelines/basic_render_pipeline.cpp
        pipelines/basic_render_pipeline.h
        pipelines/mesh_shader_pipeline.cpp
        pipelines/mesh_shader_pipeline.h
        shaders/model_interop.h
        model/model_format.cpp
        model/model_format.h
        model/model_types.cpp
        model/model_types.h
        model/model_serialization.cpp
        model/model_serialization.h
        model/will_model_asset.cpp
        model/will_model_asset.h
        shaders/constants_interop.h
        texture_asset.cpp
        texture_asset.h
        render-graph/render_graph.cpp
        render-graph/render_graph.h
        render-graph/render_graph_resources.cpp
        render-graph/render_graph_resources.h
        render-graph/render_pass.cpp
        render-graph/render_pass.h
        descriptors/vk_bindless_transient_rdg_resources.cpp
        descriptors/vk_bindless_transient_rdg_resources.h
        render-graph/render_graph_config.h
)

target_include_directories(render PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(render PUBLIC
        core
        asset-load
        platform
        SDL3::SDL3
        volk
        VulkanMemoryAllocator
        vk-bootstrap::vk-bootstrap
        spdlog::spdlog
        enkiTS
        offsetAllocator
        ktx
        miniz
)

if(WILL_ENGINE_BUILD_EDITOR)
    target_link_libraries(render PUBLIC
            editor
    )
endif()

find_program(SLANGC slangc REQUIRED)

function(preprocess_shader_interop INPUT_HEADER OUTPUT_SLANG)
    find_program(GCC_COMPILER gcc)
    if(GCC_COMPILER)
        add_custom_command(
                OUTPUT ${OUTPUT_SLANG}
                COMMAND ${GCC_COMPILER} -E -P -D__SLANG__ -x c++ ${INPUT_HEADER} -o ${OUTPUT_SLANG}
                DEPENDS ${INPUT_HEADER}
                COMMENT "Preprocessing ${OUTPUT_SLANG}"
        )
    elseif(MSVC)
        add_custom_command(
                OUTPUT ${OUTPUT_SLANG}
                COMMAND ${CMAKE_CXX_COMPILER} /P /EP /D__SLANG__ /TP ${INPUT_HEADER} /Fi${OUTPUT_SLANG}
                DEPENDS ${INPUT_HEADER}
                COMMENT "Preprocessing ${OUTPUT_SLANG}"
        )
    else()
        add_custom_command(
                OUTPUT ${OUTPUT_SLANG}
                COMMAND ${CMAKE_CXX_COMPILER} -E -P -D__SLANG__ -x c++ ${INPUT_HEADER} -o ${OUTPUT_SLANG}
                DEPENDS ${INPUT_HEADER}
                COMMENT "Preprocessing ${OUTPUT_SLANG}"
        )
    endif()

    set_property(GLOBAL APPEND PROPERTY SHADER_INTEROP_FILES ${OUTPUT_SLANG})
endfunction()

function(compile_shader TARGET SHADER_SOURCE STAGE ENTRY)
    # Parse optional OUTPUT_NAME argument
    set(options "")
    set(oneValueArgs OUTPUT_NAME)
    set(multiValueArgs "")
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Use custom output name if provided, otherwise generate from source filename
    if(ARG_OUTPUT_NAME)
        set(SHADER_NAME ${ARG_OUTPUT_NAME})
    else()
        get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME_WE)
    endif()

    get_filename_component(SHADER_SOURCE_ABS ${SHADER_SOURCE} ABSOLUTE)
    get_filename_component(SHADER_INCLUDE_DIR ${SHADER_SOURCE_ABS} DIRECTORY)

    set(SHADER_OUTPUT ${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}_${STAGE}.spv)
    get_property(INTEROP_FILES GLOBAL PROPERTY SHADER_INTEROP_FILES)

    add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
            COMMAND ${SLANGC} ${SHADER_SOURCE_ABS}
            -target spirv
            -entry ${ENTRY}
            -stage ${STAGE}
            -I ${SHADER_INCLUDE_DIR}
            -o ${SHADER_OUTPUT}
            DEPENDS ${SHADER_SOURCE_ABS} ${INTEROP_FILES}
            COMMENT "Compiling ${STAGE} shader: ${SHADER_NAME}_${STAGE}.spv"
            VERBATIM
    )

    set_property(TARGET ${TARGET} APPEND PROPERTY SHADER_OUTPUTS ${SHADER_OUTPUT})
endfunction()


function(compile_shaders TARGET)
    set(ARGS ${ARGN})
    list(LENGTH ARGS NUM_ARGS)

    set(i 0)
    while(i LESS NUM_ARGS)
        list(GET ARGS ${i} SHADER_SOURCE)
        math(EXPR i "${i} + 1")
        list(GET ARGS ${i} STAGE)
        math(EXPR i "${i} + 1")
        list(GET ARGS ${i} ENTRY)
        math(EXPR i "${i} + 1")

        # Check if next arg exists and is not a path (i.e., it's an output name)
        if(i LESS NUM_ARGS)
            list(GET ARGS ${i} NEXT_ARG)
            # If it doesn't end in .slang, treat as OUTPUT_NAME
            if(NOT NEXT_ARG MATCHES "\\.(slang|hlsl)$")
                compile_shader(${TARGET} ${SHADER_SOURCE} ${STAGE} ${ENTRY} OUTPUT_NAME ${NEXT_ARG})
                math(EXPR i "${i} + 1")
            else()
                compile_shader(${TARGET} ${SHADER_SOURCE} ${STAGE} ${ENTRY})
            endif()
        else()
            compile_shader(${TARGET} ${SHADER_SOURCE} ${STAGE} ${ENTRY})
        endif()
    endwhile()

    # Create single custom target for all shaders
    get_property(SHADER_OUTPUTS TARGET ${TARGET} PROPERTY SHADER_OUTPUTS)
    add_custom_target(${TARGET}_shaders ALL DEPENDS ${SHADER_OUTPUTS})
    add_dependencies(${TARGET}_shaders shader_interop)
    add_dependencies(${TARGET} ${TARGET}_shaders)
endfunction()