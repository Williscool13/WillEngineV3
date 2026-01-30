set(GAME_SOURCES
        game.cpp


        systems/camera_system.cpp
        systems/camera_system.h
        systems/debug_system.cpp
        systems/debug_system.h
        systems/physics_system.cpp
        systems/physics_system.h
        systems/gather_renderables_system.h
        systems/gather_renderables_system.cpp
        systems/editor_systems.cpp
        systems/editor_systems.h

        components/component_include.h
        fwd_components.h

        components/camera/camera_components.cpp
        components/camera/camera_components.h
        components/physics/physics_components.cpp
        components/physics/physics_components.h
        components/debug/debug_components.cpp
        components/debug/debug_components.h
        components/render/render_components.cpp
        components/render/render_components.h
        components/gameplay/portal_components.cpp
        components/gameplay/portal_components.h
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
        tracy_interface
)


if (WILL_ENGINE_GAME_STATIC)
    add_library(game STATIC ${GAME_SOURCES})
else()
    add_library(game SHARED ${GAME_SOURCES})
    target_compile_definitions(game PRIVATE GAME_EXPORTS)

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

target_include_directories(game PRIVATE ${GAME_INCLUDE_DIRS})
target_link_libraries(game PUBLIC ${GAME_LIBRARIES})