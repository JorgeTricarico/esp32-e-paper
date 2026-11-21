@echo off
echo Deteniendo servidor del simulador en puerto 8080...
for /f "tokens=5" %%a in ('netstat -aon ^| findstr :8080 ^| findstr LISTENING') do (
    taskkill /F /PID %%a >nul 2>nul
)
echo [OK] Servidor detenido correctamente.
