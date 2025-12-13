cmake_minimum_required(VERSION 4.0)
project(will-engine)

set(CMAKE_CXX_STANDARD 23)


option(WILL_BUILD_EDITOR "Build editor features" ON)

# Configuration ==============================
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(LOG_LEVEL=0) # >= trace
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=0) # >= trace
elseif (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_compile_definitions(LOG_LEVEL=1) # >= debug
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=1) # >= debug
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(LOG_LEVEL=2) # >= info
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=2) # >= info
endif ()

if (WILL_BUILD_EDITOR)
    add_compile_definitions(WILL_EDITOR)
endif ()

# Dependencies ================================
add_subdirectory(extern/fmt)
set(SPDLOG_FMT_EXTERNAL ON)
add_subdirectory(extern/spdlog)
add_subdirectory(extern/SDL EXCLUDE_FROM_ALL)
add_subdirectory(extern/volk)
add_subdirectory(extern/vma)
add_subdirectory(extern/vk-bootstrap)
set(ENKITS_BUILD_EXAMPLES OFF)
add_subdirectory(extern/enkiTS)
add_subdirectory(extern/OffsetAllocator)

add_subdirectory(src/engine)
add_subdirectory(src/core)
add_subdirectory(src/render)
add_subdirectory(src/utils)
add_subdirectory(src/platform)


# Imgui =======================================
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/extern/imgui)
add_library(imgui STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imgui_threaded_rendering.h
        ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
        ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
        ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_compile_definitions(imgui PUBLIC
        IMGUI_IMPL_VULKAN_USE_VOLK
        VK_NO_PROTOTYPES
)
target_include_directories(imgui PUBLIC
        ${CMAKE_SOURCE_DIR}/extern/imgui
        ${CMAKE_SOURCE_DIR}/extern/volk
        ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(imgui PUBLIC
        volk
        SDL3::SDL3
)

# Builds ======================================
add_executable(will-engine main.cpp)

target_include_directories(will-engine PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(will-engine PUBLIC engine)

# Shaders =====================================
set(SHADER_COMMON_INCLUDES
        ${CMAKE_SOURCE_DIR}/src/render/shaders/shader_interop.h
)
compile_shaders(will-engine
        ${CMAKE_SOURCE_DIR}/shaders/basicCompute.slang compute computeMain)


# Post-Build Commands =========================
add_custom_command(TARGET will-engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:will-engine>
)