# engine-runtime.dll: the engine and every library it uses, in one DLL that will-engine.exe and game.dll both load.
# Hot-reload builds only; shipping (WILL_ENGINE_GAME_STATIC) links everything into the exe.

# 1. Every static library the engine links, directly or through another library. The .def is generated from this list, so it decides what the game can call.
set(ENGINE_RUNTIME_LIBRARIES
        engine core render asset-load audio physics platform utils
        imgui imguizmo clay harfbuzz spdlog fmt enkiTS meshoptimizer volk vk-bootstrap offsetAllocator
        miniz lz4 zstd bc7enc_rdo NRD ShaderMakeBlob
)

if (WILL_ENGINE_BUILD_EDITOR)
    list(APPEND ENGINE_RUNTIME_LIBRARIES editor)
endif ()
if (NOT WILL_ENGINE_ENABLE_PROFILER)
    list(APPEND ENGINE_RUNTIME_LIBRARIES tracy)
endif ()

# 2. Export list: gen_engine_def.py.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
cmake_path(GET CMAKE_LINKER PARENT_PATH WILL_LINKER_DIR)
find_program(WILL_DUMPBIN dumpbin HINTS ${WILL_LINKER_DIR} NO_DEFAULT_PATH REQUIRED)

set(ENGINE_RUNTIME_DEF ${CMAKE_BINARY_DIR}/generated/engine-runtime.def)
set(ENGINE_RUNTIME_DEF_GENERATOR ${CMAKE_SOURCE_DIR}/src/engine/tools/gen_engine_def.py)
set(ENGINE_RUNTIME_LIBRARY_FILES)
foreach (library ${ENGINE_RUNTIME_LIBRARIES})
    list(APPEND ENGINE_RUNTIME_LIBRARY_FILES $<TARGET_FILE:${library}>)
endforeach ()

add_custom_command(
        OUTPUT ${ENGINE_RUNTIME_DEF}
        COMMAND ${Python3_EXECUTABLE} ${ENGINE_RUNTIME_DEF_GENERATOR} ${WILL_DUMPBIN} ${ENGINE_RUNTIME_DEF} engine-runtime.dll ${ENGINE_RUNTIME_LIBRARY_FILES}
        DEPENDS ${ENGINE_RUNTIME_DEF_GENERATOR} ${ENGINE_RUNTIME_LIBRARIES}
        COMMENT "Generating engine-runtime exports"
        VERBATIM
)

# 3. The DLL itself: no code of its own, just the libraries plus the .def. Linking also writes engine-runtime.lib,
# the import library that the exe and the game link against.
add_library(engine-runtime SHARED ${ENGINE_RUNTIME_DEF})
set_target_properties(engine-runtime PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(engine-runtime PRIVATE ${ENGINE_RUNTIME_LIBRARIES})
# Whoever links engine-runtime compiles against the engine's headers and defines, without linking a second copy of it
target_link_libraries(engine-runtime INTERFACE $<COMPILE_ONLY:engine>)
