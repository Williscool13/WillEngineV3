set(GAME_SOURCES
        game.cpp


        systems/camera_system.cpp
        systems/camera_system.h
        systems/debug_system.cpp
        systems/debug_system.h
        systems/physics_system.cpp
        systems/physics_system.h
        systems/render_systems.h
        systems/render_systems.cpp
        systems/editor_systems.cpp
        systems/editor_systems.h

        fwd_components.h

        components/camera_components.cpp
        components/camera_components.h
        components/debug_gizmo_component.cpp
        components/debug_gizmo_component.h

        components/common/stable_id_component.cpp
        components/common/stable_id_component.h

        components/physics/physics_components.cpp
        components/physics/physics_components.h
        components/physics/physics_body_component.cpp
        components/physics/physics_body_component.h
        components/physics/physics_body_desc.cpp
        components/physics/physics_body_desc.h
        components/physics/physics_shape_helpers.cpp
        components/physics/physics_shape_helpers.h

        components/render/static_mesh_component.cpp
        components/render/static_mesh_component.h
        components/render/text_component.cpp
        components/render/text_component.h
        components/render/procedural_mesh_component.cpp
        components/render/procedural_mesh_component.h
        components/render/procedural_mesh_component.cpp
        components/render/spline_mesh_component.cpp
        components/render/spline_mesh_component.h

        components/gameplay/checkpoint_component.cpp
        components/gameplay/checkpoint_component.h
        components/gameplay/death_zone_component.cpp
        components/gameplay/death_zone_component.h
        components/gameplay/player_spawn_component.cpp
        components/gameplay/player_spawn_component.h
        components/gameplay/path_mover_component.cpp
        components/gameplay/path_mover_component.h

        components/debug_components.cpp
        components/debug_components.h
        components/render_components.cpp
        components/render_components.h
        components/portal_components.cpp
        components/portal_components.h
        components/core_components.cpp
        components/core_components.h
        components/scene_components.cpp
        components/scene_components.h
        components/common_components.cpp
        components/common_components.h
        component-registry/component_registry.cpp
        component-registry/component_registry.h
        components/component_types.cpp
        components/component_types.h
        components/editor_components.cpp
        components/editor_components.h
        components/character_components.h
        component-registry/component_editor.h

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
        components/physics/physics_body_desc.cpp



        systems/core_systems.cpp
        systems/core_systems.h
        systems/scene_system.cpp
        systems/scene_system.h
        systems/common_systems.cpp
        systems/common_systems.h
        systems/gameplay_systems.cpp
        systems/gameplay_systems.h
        systems/component_editor.cpp
        systems/component_editor.h
        components/render/light_components.cpp
        components/render/light_components.h
        component-registry/editor_gizmo_helpers.cpp
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