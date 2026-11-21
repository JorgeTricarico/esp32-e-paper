@echo off
title Respaldo Completo de Memoria Flash ESP32-S3
color 0B
cd /d "%~dp0\.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0backup_firmware.ps1"
pause
