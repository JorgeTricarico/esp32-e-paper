@echo off
title Simulador E-Paper - Servidor Web Local
color 0A
echo ========================================================
echo   SIMULADOR WEB ESP32-S3 E-PAPER (ACCESO EN RED LOCAL)
echo ========================================================
echo.

echo Buscando la IP local de esta notebook...
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /i "IPv4"') do (
    for /f "tokens=*" %%b in ("%%a") do (
        echo  --^> Desde tu otro dispositivo ingresa a: http://%%b:8080
    )
)
echo.
echo Iniciando servidor web en el puerto 8080...
echo (Presiona Ctrl+C para detener el servidor)
echo.

cd /d "%~dp0..\simulator"

where python >nul 2>nul
if %ERRORLEVEL% equ 0 (
    python -m http.server 8080 --bind 0.0.0.0
    goto end
)

where py >nul 2>nul
if %ERRORLEVEL% equ 0 (
    py -m http.server 8080 --bind 0.0.0.0
    goto end
)

where node >nul 2>nul
if %ERRORLEVEL% equ 0 (
    npx -y serve -l 8080
    goto end
)

echo [!] No se encontro Python ni Node.js en el PATH.
echo     Instala Python para levantar el servidor web local.
pause

:end
