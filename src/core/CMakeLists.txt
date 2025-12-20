add_library(core STATIC
        allocators/free_list.cpp
        allocators/free_list.h
        allocators/handle_allocator.cpp
        allocators/handle_allocator.h
        allocators/ring_buffer.cpp
        allocators/ring_buffer.h
        include/render_interface.cpp
        include/render_interface.h
        allocators/handle.cpp
        allocators/handle.h
        include/frame_sync.cpp
        include/frame_sync.h
        include/game_interface.cpp
        include/game_interface.h
        include/engine_context.cpp
        include/engine_context.h
        input/input_frame.cpp
        input/input_frame.h
        input/input_utils.cpp
        input/input_utils.h
        input/input_manager.cpp
        input/input_manager.h
        time/time_manager.cpp
        time/time_manager.h
        time/time_frame.cpp
        time/time_frame.h
        math/transform.cpp
        math/transform.h
        math/constants.cpp
        math/constants.h
)


target_include_directories(core PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(core PUBLIC
        spdlog::spdlog
        SDL3::SDL3
)