add_library(core STATIC
        include/render_interface.cpp
        include/render_interface.h
)


target_include_directories(core PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(core PUBLIC
        imgui
)