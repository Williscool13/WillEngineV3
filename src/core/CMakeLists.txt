find_package(Python3 COMPONENTS Interpreter REQUIRED)

# CORE_ALLOC_TAGS_DIR: dir holding alloc_tags.h
if(NOT DEFINED CORE_ALLOC_TAGS_DIR OR CORE_ALLOC_TAGS_DIR STREQUAL "")
    set(CORE_ALLOC_TAGS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/memory/default_tags)
    set(CORE_USE_DEFAULT_ALLOC_TAGS ON)
else()
    set(CORE_USE_DEFAULT_ALLOC_TAGS OFF)
endif()

# CORE_STRING_LITERAL_ROOTS: where to look for string IDs for code gen.
if(NOT DEFINED CORE_STRING_LITERAL_ROOTS OR CORE_STRING_LITERAL_ROOTS STREQUAL "")
    set(CORE_STRING_LITERAL_ROOTS ${CMAKE_CURRENT_SOURCE_DIR})
endif()


set(STRING_LITERAL_TABLE ${CMAKE_BINARY_DIR}/generated/string_id_table.gen.cpp)
set(STRING_LITERAL_GENERATOR ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_string_literals.py)

set(STRING_LITERAL_GLOBS)
set(STRING_LITERAL_ROOT_ARGS)
foreach(root ${CORE_STRING_LITERAL_ROOTS})
    list(APPEND STRING_LITERAL_GLOBS ${root}/*.cpp ${root}/*.h ${root}/*.hpp)
    list(APPEND STRING_LITERAL_ROOT_ARGS --root ${root})
endforeach()
file(GLOB_RECURSE STRING_LITERAL_SOURCES CONFIGURE_DEPENDS ${STRING_LITERAL_GLOBS})
list(FILTER STRING_LITERAL_SOURCES EXCLUDE REGEX "/extern/")

# The generator rewrites its output only when the literal set actually changes, so editing an unrelated source reruns the scan but does not recompile the table.
add_custom_command(
        OUTPUT ${STRING_LITERAL_TABLE}
        COMMAND ${Python3_EXECUTABLE} ${STRING_LITERAL_GENERATOR} ${STRING_LITERAL_TABLE} ${STRING_LITERAL_ROOT_ARGS}
        DEPENDS ${STRING_LITERAL_GENERATOR} ${CMAKE_CURRENT_SOURCE_DIR}/tools/xxh3.py ${STRING_LITERAL_SOURCES}
        COMMENT "Generating StringID literal table"
        VERBATIM
)

add_library(core STATIC
        ${STRING_LITERAL_TABLE}
        memory/free_list.cpp
        memory/free_list.h
        memory/handle_allocator.cpp
        memory/handle_allocator.h
        containers/ring_buffer.h
        memory/handle.cpp
        memory/handle.h
        time/time_manager.cpp
        time/time_manager.h
        time/time_frame.cpp
        time/time_frame.h
        time/frame_stamp.h
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
        string_id_table.cpp
        string_id_table.h
        hash/xxh3.h
        memory/arena.h
        memory/arena.cpp
        memory/buddy_allocator.h
        memory/buddy_allocator.cpp
        memory/memory_manager.h
        memory/memory_manager.cpp
        memory/tlsf_allocator.cpp
        memory/tlsf_allocator.h
        memory/range_allocator.cpp
        memory/range_allocator.h
        memory/concurrent_queue_traits.cpp
        memory/concurrent_queue_traits.h

        types/math.h
        types/transform.h

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
        containers/array.h
        containers/vector.h
        containers/string.h
        containers/map.h
        containers/arena_fixed_map.h
        containers/arena_fixed_vector.h
        containers/inline_path.h
        containers/inline_string.h
        containers/inline_function.h
        containers/arena_function.h
        containers/function.h
        containers/arena_array.h
        containers/heap_array.h
        containers/heap_ring_buffer.h
        containers/arena_vector.h
        containers/inline_set.h
        containers/set.h
        containers/fixed_set.h
        containers/arena_fixed_set.h
        containers/inline_queue.h
        containers/queue.h
        containers/arena_queue.h
        containers/virtual_array.h
        memory/virtual_memory_manager.cpp
        memory/virtual_memory_manager.h
        memory/virtual_arena.h
        threading/semaphore.h
)

target_sources(core PRIVATE extern/tlsf/tlsf.c extern/tlsf/tlsf.h)

if(CORE_USE_DEFAULT_ALLOC_TAGS)
    target_sources(core PRIVATE memory/default_tags/alloc_tags.h memory/default_tags/alloc_tags.cpp)
endif()

target_include_directories(core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/extern
        ${CORE_ALLOC_TAGS_DIR}
)

target_include_directories(core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/tlsf
)

target_compile_definitions(core PUBLIC
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_ENABLE_EXPERIMENTAL
)
