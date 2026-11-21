@echo off
title Servidor Persistente Simulador E-Paper
color 0A

:: Comprobar si ya esta activo en el puerto 8080
netstat -ano | findstr /R /C:":8080 *LISTENING" >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo.
    echo [*] El servidor ya esta activo en segundo plano en el puerto 8080.
    goto show_urls
)

:: Lanzar en segundo plano desacoplado de la terminal SSH
wscript.exe "%~dp0start_server_background.vbs"

:: Breve espera de inicio
ping 127.0.0.1 -n 3 >nul

:show_urls
echo ================================================================
echo   [OK] SERVIDOR PERSISTENTE ACTIVO (NO MUERE AL CERRAR SSH)
echo ================================================================
echo.
echo El servidor esta ejecutandose de forma invisible en segundo plano.
echo Seguirá activo de manera permanente hasta que apagues la notebook.
echo.
echo Enlaces directos para abrir en tu otro dispositivo:
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /i "IPv4"') do (
    for /f "tokens=*" %%b in ("%%a") do (
        echo    👉 http://%%b:8080
    )
)
echo.
echo (O tambien usando el nombre de red o IP que usaste para el SSH: http://^<IP_SSH^>:8080)
echo ================================================================
