add_executable(engine-tests
        test_main.cpp
        render-graph/render_graph_tests.cpp
        containers/container_tests.cpp
        core/string_id_tests.cpp
        core/hash_tests.cpp
        core/math_tests.cpp
        engine/wimage_tests.cpp
        engine/text_format_tests.cpp
)

target_link_libraries(engine-tests PRIVATE
        engine
        Catch2::Catch2
)

target_include_directories(engine-tests PRIVATE ${CMAKE_SOURCE_DIR}/extern/xxhash)

add_custom_command(TARGET engine-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:engine-tests>
)
add_custom_command(TARGET engine-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3_mixer::SDL3_mixer>
        $<TARGET_FILE_DIR:engine-tests>
)
add_custom_command(TARGET engine-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:tracy>
        $<TARGET_FILE_DIR:engine-tests>
)
