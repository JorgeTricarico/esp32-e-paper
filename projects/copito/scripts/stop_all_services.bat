@echo off
echo [*] Deteniendo todos los servicios de Copito AI
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like '*copito_services_launcher*' -or $_.CommandLine -like '*kokoro_server*' -or $_.CommandLine -like '*image_gateway*' -or $_.CommandLine -like '*dashboard_server*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }"
echo [OK] Servicios de Copito detenidos.
