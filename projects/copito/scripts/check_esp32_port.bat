@echo off
title Diagnostico de Conexion ESP32-S3
color 0E
cd /d "%~dp0\.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check_esp32_port.ps1"
pause
