add_library(engine STATIC
        will_engine.cpp
        will_engine.h
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

if(WILL_BUILD_EDITOR)
    target_link_libraries(engine PUBLIC imgui)
endif()
