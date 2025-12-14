add_library(game SHARED
        game.cpp
)

target_include_directories(game PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(game PUBLIC
        core
        spdlog::spdlog
)

target_compile_definitions(game PRIVATE GAME_EXPORTS)

add_custom_command(TARGET game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:game>
        $<TARGET_FILE_DIR:will-engine>
)