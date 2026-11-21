@echo off
title Flasheador Asistente ESP32-S3 E-Paper
color 0B
echo ========================================================
echo   ASISTENTE AUTONOMO ESP32-S3 E-INK (WAVESHARE 1.54)
echo ========================================================
echo.

cd /d "%~dp0\.."

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash_tool.ps1"

pause
