add_library(engine STATIC
        will_engine.cpp
        will_engine.h
        engine_api.cpp
        engine_api.h
)

target_include_directories(engine PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(engine PUBLIC
        core
        render
        # physics
        # audio
        utils
        platform
        SDL3::SDL3
        enkiTS
)

if(GAME_STATIC)
    target_link_libraries(engine PUBLIC game)
endif()

target_compile_definitions(engine PRIVATE ENGINE_EXPORTS)

if(WILL_BUILD_EDITOR)
    target_link_libraries(engine PUBLIC imgui)
endif()


add_custom_command(TARGET engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:engine>
        $<TARGET_FILE_DIR:will-engine>
)