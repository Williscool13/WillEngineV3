set(GAME_SOURCES
        game.cpp
        common/transform_component.h
        camera/free_camera_component.cpp
        camera/free_camera_component.h
        camera/camera_component.h

        systems/camera_system.cpp
        systems/camera_system.h
        fwd_components.h
        systems/debug_system.cpp
        systems/debug_system.h
        common/renderable_component.h
)

set(GAME_INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

set(GAME_LIBRARIES
        core
        engine
        render
        platform
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
    add_custom_command(TARGET game POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_PDB_FILE:game>
            $<TARGET_FILE_DIR:will-engine>
    )
endif()

# Common configuration for both
target_include_directories(game PRIVATE ${GAME_INCLUDE_DIRS})
target_link_libraries(game PUBLIC ${GAME_LIBRARIES})