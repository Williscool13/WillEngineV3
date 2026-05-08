@echo off
set DOT_FILE=%~dp0assets\visualizations\rdg.dot
set PNG_FILE=%~dp0assets\visualizations\rdg.png

where dot >nul 2>&1
if errorlevel 1 (
    echo Graphviz not found. Install from https://graphviz.org/download/ and ensure dot.exe is on PATH.
    pause
    exit /b 1
)

dot -Tpng "%DOT_FILE%" -o "%PNG_FILE%"
if errorlevel 1 (
    echo Conversion failed. Make sure rdg.dot exists at: %DOT_FILE%
    pause
    exit /b 1
)

echo Wrote %PNG_FILE%
start "" "%PNG_FILE%"
