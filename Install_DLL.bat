@echo off
REM SPDX-License-Identifier: MIT
REM Copies the built dxgi.dll proxy into the Crimson Desert game folder.

setlocal enabledelayedexpansion

set "DLL=client\build\dxgi.dll"
if not exist "%DLL%" (
    set "DLL=client\build\Release\dxgi.dll"
)
if not exist "!DLL!" (
    echo [ERROR] dxgi.dll not found. Build the client first:
    echo     cd client ^&^& cmake -B build -A x64 ^&^& cmake --build build --config Release
    pause
    exit /b 1
)

set "GAMEDIR="
set "REGKEY=HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 3321460"
for /f "tokens=2,*" %%a in ('reg query "%REGKEY%" /v InstallLocation 2^>nul ^| findstr /i InstallLocation') do (
    set "GAMEDIR=%%b"
)

if defined GAMEDIR (
    if exist "!GAMEDIR!" (
        echo [INFO] Found Crimson Desert at: !GAMEDIR!
        copy /Y "!DLL!" "!GAMEDIR!\dxgi.dll" >nul
        if errorlevel 1 (
            echo [ERROR] Copy failed. Try running this script as Administrator.
        ) else (
            echo [OK] Installed dxgi.dll. Launch Crimson Desert from Steam and press your
            echo      Multiplayer Beacon item ^(or F9^) in-game.
        )
        pause
        exit /b 0
    )
)

echo [WARN] Could not auto-detect the Crimson Desert install folder.
echo Manual install:
echo   1. Open the Crimson Desert game folder ^(where the game .exe lives^).
echo   2. Copy "!DLL!" into that folder, renamed to dxgi.dll.
echo   3. Launch the game from Steam.
pause
endlocal
