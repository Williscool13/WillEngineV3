@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake --build cmake-build-debug-visual-studio --target game

REM BUILD GAME
REM  In the MSVC terminal "x64 Native Tools Command Prompt for VS 2022"
REM  cd C:\Users\William\source\repos\WillEngineV3
REM  cmake --build cmake-build-debug-visual-studio --target game
