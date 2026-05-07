@echo off
REM EV Planner Build Script for Windows
REM This script compiles the EV Charging & Range Planner with MinGW

setlocal enabledelayedexpansion

echo.
echo ========================================
echo EV Charging & Range Planner - Build
echo ========================================
echo.

REM Check if g++ is available
where g++ >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: g++ compiler not found!
    echo Please install MinGW-w64 and add it to your PATH.
    echo Download from: https://www.mingw-w64.org/downloads/
    echo.
    pause
    exit /b 1
)

echo [1/3] Checking compiler...
g++ --version
echo.

echo [2/3] Compiling ev_planner.cpp...
g++ ev_planner.cpp -std=c++11 -mwindows -lgdi32 -luser32 -lwinhttp -o EVPlanner.exe

if %ERRORLEVEL% EQU 0 (
    echo.
    echo [3/3] Build successful!
    echo.
    echo Output: EVPlanner.exe
    echo Size: 
    for %%A in (EVPlanner.exe) do echo %%~zA bytes
    echo.
    echo To run the application:
    echo   EVPlanner.exe
    echo.
    pause
) else (
    echo.
    echo ERROR: Compilation failed!
    echo.
    echo Try running these commands individually:
    echo   g++ ev_planner.cpp -std=c++11 -mwindows -lgdi32 -luser32 -lwinhttp -o EVPlanner.exe
    echo.
    pause
    exit /b 1
)
