cmake_minimum_required(VERSION 4.0)
project(will-engine)

set(CMAKE_CXX_STANDARD 23)

add_subdirectory(extern/fmt)
set(SPDLOG_FMT_EXTERNAL ON)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(LOG_LEVEL=0) # >= trace
elseif (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_compile_definitions(LOG_LEVEL=1) # >= debug
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(LOG_LEVEL=2) # >= info
endif ()
add_subdirectory(extern/spdlog)

add_subdirectory(src/platform)
add_subdirectory(src/utils)

add_executable(will-engine main.cpp
        src/core/will_engine.cpp
        src/core/will_engine.h
        src/render/render_thread.cpp
        src/render/render_thread.h
        src/render/shaders/shader_interop.h
        src/utils/logging/logging.cpp
        src/utils/logging/logging.h
)

target_include_directories(will-engine PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(will-engine PUBLIC
        fmt::fmt
        platform
        utils
)