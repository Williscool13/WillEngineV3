add_library(editor STATIC
        model-generation/model_generator.cpp
        model-generation/model_generator.h
        model-generation/model_generation_types.cpp
        model-generation/model_generation_types.h
)

target_include_directories(editor PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(editor PUBLIC
        core
        render
        fastgltf
        imgui
)