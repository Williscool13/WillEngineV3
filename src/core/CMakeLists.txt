add_library(core STATIC
        memory/free_list.cpp
        memory/free_list.h
        memory/handle_allocator.cpp
        memory/handle_allocator.h
        memory/ring_buffer.cpp
        memory/ring_buffer.h
        include/render_interface.cpp
        include/render_interface.h
        memory/handle.cpp
        memory/handle.h
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
        memory/linear_allocator.cpp
        memory/linear_allocator.h
        math/math_helpers.cpp
        math/math_helpers.h
        memory/lock_free_handle_allocator.cpp
        memory/lock_free_handle_allocator.h
        string_id.cpp
        string_id.h
        memory/inline_vector.cpp
        memory/inline_vector.h
        hash/fnv_1_a.cpp
        hash/fnv_1_a.h
        memory/arena.h
        memory/arena.cpp
        memory/arena_vector.h
        memory/buddy_allocator.h
        memory/buddy_allocator.cpp
        memory/memory_manager.h
        memory/memory_manager.cpp
)


target_include_directories(core PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(core PUBLIC
        spdlog::spdlog
        SDL3::SDL3
        render
)