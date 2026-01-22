@echo off
cmake --build cmake-build-debug-visual-studio --target will-engine_shaders

REM COMPILE SHADER INTEROP FILES
REM   - Use this command to compile any shader interop that needs to be recompiled.
REM     cmake --build cmake-build-debug-visual-studio --target shader_interop
REM   - Or just this if you're in the build folder
REM     cmake --build . --target shader_interop

REM COMPILE SHADERS
REM   - Recompile shaders with
REM     cmake --build cmake-build-debug-visual-studio --target will-engine_shaders
REM   - Or just this if you're in the build folder
REM     cmake --build . --target will-engine_shaders