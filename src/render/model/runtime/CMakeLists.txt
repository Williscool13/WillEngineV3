add_library(render-model-runtime STATIC
    model_format.cpp
    model_format.h
)

target_include_directories(render-model-runtime PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(render-model-runtime PUBLIC
        core
        # meshoptimizer - Re-add if necessary, but atm no meshoptimizer functions used at runtime
        ktx
        VulkanMemoryAllocator
)