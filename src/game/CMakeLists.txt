set(GAME_SOURCES
        game.cpp
        game_pch.h

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

        ballance/crusher/crusher_component.h
        ballance/crusher/crusher_systems.cpp
        ballance/crusher/crusher_systems.h

        ballance/culler/culler_component.h
        ballance/culler/culler_systems.cpp
        ballance/culler/culler_systems.h

        ballance/lava/lava_component.h
        ballance/lava/lava_systems.cpp
        ballance/lava/lava_systems.h

        ballance/cane/cane_lift_component.h
        ballance/cane/cane_lift_systems.cpp
        ballance/cane/cane_lift_systems.h

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

        ballance/player/gameplay_camera.cpp
        ballance/player/gameplay_camera.h
        ballance/player/physics_character.cpp
        ballance/player/physics_character.h
        ballance/player/physics_player_controller.cpp
        ballance/player/physics_player_controller.h

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
endif()

target_include_directories(game PRIVATE ${GAME_INCLUDE_DIRS})
if (NOT WILL_ENGINE_GAME_STATIC)
    target_link_libraries(game PRIVATE engine-runtime SDL3::SDL3)
    if (WILL_ENGINE_ENABLE_PROFILER)
        target_link_libraries(game PRIVATE tracy_interface)
    endif ()
else ()
    target_link_libraries(game PUBLIC ${GAME_LIBRARIES})
endif ()

# Engine and third-party headers
target_precompile_headers(game PRIVATE game_pch.h)