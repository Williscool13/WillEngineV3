set(GAME_SOURCES
        game.cpp

        fwd_components.h

        mcp/game_mcp_tools.cpp
        mcp/game_mcp_tools.h

        logging/game_log_category.h
        logging/game_log.h

        ballance/ballance.cpp
        ballance/ballance.h
        ballance/fwd_components.h
        ballance/state.h

        ballance/breakable/breakable_component.h
        ballance/breakable/breakable_systems.cpp
        ballance/breakable/breakable_systems.h

        ballance/checkpoint/checkpoint_component.cpp
        ballance/checkpoint/checkpoint_component.h
        ballance/checkpoint/checkpoint_systems.cpp
        ballance/checkpoint/checkpoint_systems.h
        ballance/checkpoint/death_zone_component.cpp
        ballance/checkpoint/death_zone_component.h
        ballance/checkpoint/player_spawn_component.cpp
        ballance/checkpoint/player_spawn_component.h

        ballance/debug/debug_components.h
        ballance/debug/debug_system.cpp
        ballance/debug/debug_system.h

        ballance/player/character.cpp
        ballance/player/character.h
        ballance/player/gameplay_camera.cpp
        ballance/player/gameplay_camera.h
        ballance/player/physics_character.cpp
        ballance/player/physics_character.h
        ballance/player/physics_player_controller.cpp
        ballance/player/physics_player_controller.h
        ballance/player/player_controller.cpp
        ballance/player/player_controller.h

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