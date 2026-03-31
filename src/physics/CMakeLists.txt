add_library(physics STATIC
        physics_job_system.cpp
        physics_job_system.h
        physics_config.h
        physics_system.cpp
        physics_system.h
        layers/layer_interface.cpp
        layers/layer_interface.h
        contact_listener.cpp
        contact_listener.h
        body_activation_listener.cpp
        body_activation_listener.h
        layers/broad_phase_layer_interface.cpp
        layers/broad_phase_layer_interface.h
        layers/object_vs_broad_phase_layer_filter.cpp
        layers/object_vs_broad_phase_layer_filter.h
        layers/object_layer_pair_filter.cpp
        layers/object_layer_pair_filter.h
        physics_debug_renderer.cpp
        physics_debug_renderer.h
        physics_helpers.cpp
        physics_helpers.h
        physics_debug_filter.cpp
        physics_debug_filter.h

)

target_include_directories(physics PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(physics PUBLIC
        core
        render # for debug rendering
        fmt::fmt
        Jolt
        enkiTS
)
