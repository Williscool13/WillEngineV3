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

)


target_include_directories(core PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(core PUBLIC
        imgui
)