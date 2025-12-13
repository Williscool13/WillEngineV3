add_library(platform STATIC
        crash_handler.h
        crash_context.h
        paths.h
)

if(WIN32)
    target_sources(platform PRIVATE
            crash_handler_win32.cpp
            crash_context_win32.cpp
            paths.cpp
    )
    target_link_libraries(platform PUBLIC
            dbghelp
            fmt::fmt
            spdlog::spdlog
            SDL3::SDL3
    )
else()
    target_sources(platform PRIVATE
            crash_handler_linux.cpp
            crash_context_linux.cpp
            paths.cpp
    )
    target_link_libraries(platform PUBLIC
            fmt::fmt
            spdlog::spdlog
            SDL3::SDL3
    )
endif()

target_include_directories(platform PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

