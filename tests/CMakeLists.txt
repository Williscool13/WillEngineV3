add_executable(engine-tests
        data-structures/ring_buffer_tests.cpp
        data-structures/free_list_tests.cpp
        data-structures/handle_tests.cpp
        data-structures/handle_allocator_tests.cpp
        renderer/model_data_tests.cpp
        renderer/render_extents_tests.cpp
        renderer/shader_interop_tests.cpp
        renderer/vk_helpers_tests.cpp
        renderer/pipeline_builder_tests.cpp
        renderer/allocated_resources_tests.cpp
        renderer/descriptor_layout_builder_tests.cpp
        renderer/frustum_tests.cpp
)

target_link_libraries(engine-tests PRIVATE
        engine
        Catch2::Catch2WithMain
)

# Copy SDL3.dll to test output directory
add_custom_command(TARGET engine-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:engine-tests>
)