@echo off
REM build_engine.bat [debug|development] [targets...]
REM Defaults: debug dir, targets will-engine and engine-tests.
setlocal
set ROOT=%~dp0
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=debug
shift
set TARGETS=
:collect
if "%~1"=="" goto built
set TARGETS=%TARGETS% %~1
shift
goto collect
:built
if "%TARGETS%"=="" set TARGETS=will-engine engine-tests

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cmake --build "%ROOT%cmake-build-%CONFIG%-visual-studio" --target %TARGETS%
exit /b %errorlevel%
