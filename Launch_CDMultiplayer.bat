@echo off
REM SPDX-License-Identifier: MIT
REM Launches the Crimson Desert multiplayer session server.

setlocal

where node >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Node.js was not found on your PATH.
    echo Install Node.js 18+ from https://nodejs.org/ and re-run this script.
    pause
    exit /b 1
)

if not exist "server\node_modules" (
    echo [INFO] Installing server dependencies...
    pushd server
    call npm install
    popd
)

if not exist "server\config.json" (
    echo [INFO] Creating server\config.json from the example.
    copy /Y "server\config.example.json" "server\config.json" >nul
)

echo [INFO] Starting CD Multiplayer Server...
start "CD Multiplayer Server" cmd /k "cd server && npm start"

echo.
echo Server running on port 7777.
echo Copy client\build\dxgi.dll to your Crimson Desert game folder, then launch from Steam.
echo (You can run Install_DLL.bat to copy it automatically.)
echo.
endlocal
