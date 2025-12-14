set(GAME_SOURCES
        game.cpp
)

set(GAME_INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

set(GAME_LIBRARIES
        core
        spdlog::spdlog
)

if (GAME_STATIC)
    add_library(game STATIC ${GAME_SOURCES})
else()
    add_library(game SHARED ${GAME_SOURCES})
    target_compile_definitions(game PRIVATE GAME_EXPORTS)

    # Copy DLL to engine directory for hot-reload
    add_custom_command(TARGET game POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:game>
            $<TARGET_FILE_DIR:will-engine>
    )
endif()

# Common configuration for both
target_include_directories(game PRIVATE ${GAME_INCLUDE_DIRS})
target_link_libraries(game PUBLIC ${GAME_LIBRARIES})