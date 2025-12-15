add_library(render-model-generation STATIC
        model_generator.cpp
        model_generator.h
        model_generation_types.cpp
        model_generation_types.h
)

target_include_directories(render-model-generation PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(render-model-generation PUBLIC
        core
        render
        render-model-runtime
        fastgltf
        meshoptimizer
        ktx
        VulkanMemoryAllocator
        volk
)