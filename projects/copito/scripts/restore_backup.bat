@echo off
title Restaurar Backup Original ESP32-S3
color 0C
echo ========================================================
echo   RESTAURAR BACKUP ORIGINAL ESP32-S3 (8 MB)
echo ========================================================
echo Archivo a flashear:
echo %~dp0..\backup\esp32s3_original_flash_20260905_131628.bin
echo.
set /p CONFIRM=Esta seguro de sobreescribir la memoria con este backup? (S/N): 
if /i not "%CONFIRM%"=="S" goto cancel

cd /d "%~dp0\.."
powershell -NoProfile -ExecutionPolicy Bypass -Command "esptool --port COM3 --baud 921600 write_flash 0x0 '%~dp0..\backup\esp32s3_original_flash_20260905_131628.bin'"

pause
goto end
:cancel
echo Restauracion cancelada por el usuario.
pause
:end
