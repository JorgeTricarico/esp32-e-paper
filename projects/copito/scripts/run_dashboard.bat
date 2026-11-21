@echo off
title Tablero de Control y Simulador - Copito AI ESP32-S3
color 0B
echo ========================================================
echo   TABLERO DE CONTROL PRE-FLASHEO Y SIMULADOR COPITO AI
echo ========================================================
echo.
echo Abriendo el navegador en http://localhost:8080...
echo.

start http://localhost:8080

cd /d "%~dp0.."

where python >nul 2>nul
if %ERRORLEVEL% equ 0 (
    python tools/dashboard_server.py
    goto end
)

where py >nul 2>nul
if %ERRORLEVEL% equ 0 (
    py tools/dashboard_server.py
    goto end
)

echo [!] Python no detectado, abriendo tablero en modo directo...
start "" "%~dp0..\tools\dashboard.html"

:end
pause
