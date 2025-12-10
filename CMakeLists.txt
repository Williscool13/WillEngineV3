cmake_minimum_required(VERSION 4.0)
project(will-engine)

set(CMAKE_CXX_STANDARD 23)


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

# Dependencies ================================
add_subdirectory(extern/fmt)
set(SPDLOG_FMT_EXTERNAL ON)
add_subdirectory(extern/spdlog)
add_subdirectory(extern/SDL EXCLUDE_FROM_ALL)
add_subdirectory(extern/volk)
add_subdirectory(extern/vma)
add_subdirectory(extern/vk-bootstrap)

add_subdirectory(src/render)
add_subdirectory(src/utils)
add_subdirectory(src/platform)

add_executable(will-engine main.cpp
        src/core/will_engine.cpp
        src/core/will_engine.h
        src/utils/logging/logging.cpp
        src/utils/logging/logging.h
)

target_include_directories(will-engine PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(will-engine PUBLIC
        render
        platform
        utils
        fmt::fmt
        SDL3::SDL3
)

# Post-Build Commands =========================
add_custom_command(TARGET will-engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:will-engine>
)