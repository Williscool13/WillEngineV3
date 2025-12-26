set(GAME_SOURCES
        game.cpp
        fwd_components.h
        components/component_include.h
        components/transform_component.h
        components/renderable_component.h
        components/camera/free_camera_component.cpp
        components/camera/free_camera_component.h
        components/camera/camera_component.h
        components/physics/physics_body_component.h
        components/dirty_physics_transform_component.h
        components/render/gather_renderables_component.h

        systems/camera_system.cpp
        systems/camera_system.h
        systems/debug_system.cpp
        systems/debug_system.h
        systems/physics_system.cpp
        systems/physics_system.h
        components/render/gather_renderables_component.cpp

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

if (WILL_ENGINE_GAME_STATIC)
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