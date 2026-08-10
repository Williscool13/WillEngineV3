add_executable(engine-tests
        render-graph/render_graph_tests.cpp
        containers/container_tests.cpp
        core/string_id_tests.cpp
        core/math_tests.cpp
)

target_link_libraries(engine-tests PRIVATE
        engine
        Catch2::Catch2WithMain
)

add_custom_command(TARGET engine-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:engine-tests>
)
