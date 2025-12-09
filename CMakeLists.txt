cmake_minimum_required(VERSION 4.0)
project(will-engine)

set(CMAKE_CXX_STANDARD 23)

add_subdirectory(extern/fmt)

add_executable(will-engine main.cpp
        src/core/will_engine.cpp
        src/core/will_engine.h
        # src/render/render_thread.cpp
        # src/render/render_thread.h
        # src/render/shaders/shader_interop.h
        # src/platform/paths_win32.cpp
        # src/platform/crash_handler.h
        # src/platform/crash_handler_win_32.cpp
        # src/platform/crash_context.h
        # src/platform/crash_context_win32.cpp
)

target_include_directories(will-engine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(will-engine PUBLIC
        fmt::fmt
)
