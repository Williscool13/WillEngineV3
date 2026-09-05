set(GAME_SOURCES
        game.cpp
        game_state.h

        fwd_components.h

        systems/debug_system.cpp
        systems/debug_system.h
        systems/gameplay_systems.cpp
        systems/gameplay_systems.h

        mcp/game_mcp_tools.cpp
        mcp/game_mcp_tools.h

        logging/game_log_category.h
        logging/game_log.h

        components/component_registration.cpp
        components/component_registration.h
        components/debug_components.cpp
        components/debug_components.h

        components/checkpoint_component.cpp
        components/checkpoint_component.h
        components/death_zone_component.cpp
        components/death_zone_component.h
        components/player_spawn_component.cpp
        components/player_spawn_component.h
        components/path_mover_component.cpp
        components/path_mover_component.h
        components/rotate_in_place_component.cpp
        components/rotate_in_place_component.h

        gameplay/player/character.cpp
        gameplay/player/character.h
        gameplay/camera/gameplay_camera.cpp
        gameplay/camera/gameplay_camera.h
        gameplay/player/physics_character.cpp
        gameplay/player/physics_character.h
        gameplay/player/physics_player_controller.cpp
        gameplay/player/physics_player_controller.h
        gameplay/player/player_controller.cpp
        gameplay/player/player_controller.h

        input/game_actions.h
        input/input_action_registry.cpp
        input/input_action_registry.h
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
        imgui
        imguizmo
)


if (WILL_ENGINE_GAME_STATIC)
    add_library(game STATIC ${GAME_SOURCES})
else()
    add_library(game SHARED ${GAME_SOURCES})
    target_compile_definitions(game PRIVATE GAME_EXPORTS)
    if (MSVC)
        target_link_options(game PRIVATE $<$<CONFIG:Debug>:/DEBUG:FASTLINK> $<$<CONFIG:Debug>:/time>)
    endif ()

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