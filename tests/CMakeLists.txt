add_executable(engine-tests
        data-structures/ring_buffer_tests.cpp
        data-structures/free_list_tests.cpp
        data-structures/handle_tests.cpp
        data-structures/handle_allocator_tests.cpp
)

target_link_libraries(engine-tests PRIVATE
        engine
        Catch2::Catch2WithMain
)