add_library(core STATIC
        memory/free_list.cpp
        memory/free_list.h
        memory/handle_allocator.cpp
        memory/handle_allocator.h
        containers/ring_buffer.h
        memory/handle.cpp
        memory/handle.h
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
        types/transform.h
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
        containers/hash.h
        containers/inline_vector.h
        containers/inline_map.h
        containers/fixed_vector.h
        containers/fixed_map.h
        containers/span.h
        # TODO: containers to add (follow Inline/Fixed/Dynamic tiers where applicable):
        #   - RingBuffer: existing ring_buffer.h needs Inline variant; consider Fixed too
        #   - Set: Inline, Fixed, Dynamic (open-addressing, value-only map)
        #   - Bitset: Inline<N>, Fixed (runtime size), possibly Dynamic
        hash/fnv_1_a.cpp
        hash/fnv_1_a.h
        memory/arena.h
        memory/arena.cpp
        memory/buddy_allocator.h
        memory/buddy_allocator.cpp
        memory/memory_manager.h
        memory/memory_manager.cpp
        memory/tlsf_allocator.cpp
        memory/tlsf_allocator.h
        types/math.h
        containers/array.h
        containers/vector.h
        containers/string.h
        containers/map.h
)


target_include_directories(core PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(core PUBLIC
        spdlog::spdlog
        SDL3::SDL3
        tlsf
)