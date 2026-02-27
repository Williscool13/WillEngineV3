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

        components/component_include.h
        fwd_components.h

        components/camera_components.cpp
        components/camera_components.h
        components/physics_components.cpp
        components/physics_components.h
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
        systems/core_systems.cpp
        systems/core_systems.h
        core/string_id_game.cpp
        core/scene.cpp
        core/scene.h
        systems/scene_system.cpp
        systems/scene_system.h
        components/components.cpp
        components/components.h


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