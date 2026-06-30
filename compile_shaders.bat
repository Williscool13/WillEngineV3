@echo off
REM cmake --build cmake-build-development-visual-studio --target will-engine_shaders
cmake --build cmake-build-debug-visual-studio --target will-engine_shaders

REM COMPILE SHADERS
REM   - Recompile shaders with
REM     cmake --build cmake-build-debug-visual-studio --target will-engine_shaders
REM   - Or just this if you're in the build folder
REM     cmake --build . --target will-engine_shaders