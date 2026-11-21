@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"
chcp 65001 > nul
echo ========================================================
echo   🤖 Copito AI - Flasheador Automatico ESP32
echo ========================================================
echo.
echo Directorio de trabajo: %CD%
echo.
echo Esperando conexion del dispositivo ESP32 por USB...
echo (Asegurate de que el cable este conectado y el switch en ON)
echo.

:wait_loop
for /f "tokens=*" %%a in ('powershell -Command "[System.IO.Ports.SerialPort]::GetPortNames()"') do (
    set COMPORT=%%a
    goto found_port
)
timeout /t 1 /nobreak > nul
goto wait_loop

:found_port
echo.
echo [OK] Dispositivo detectado en: %COMPORT%
echo.
echo Flasheando firmware con audio gratuito de Google y pantalla grande...
echo.
pio run -e esp32-s3-paper -t upload --upload-port %COMPORT%

if %ERRORLEVEL% equ 0 (
    echo.
    echo ========================================================
    echo   ✅ Flasheo completado con exito!
    echo ========================================================
    echo Abriendo Monitor Serial en %COMPORT% a 115200 baud...
    echo (Presiona Ctrl+C para salir)
    echo.
    pio device monitor -p %COMPORT% -b 115200
) else (
    echo.
    echo [ERROR] Error al flashear en %COMPORT%.
    echo Si es necesario, mantén presionado BOOT al conectar el USB.
    pause
)
