@echo off
title Pipeline Completo: Pruebas, Respaldo y Flasheo ESP32-S3
color 0A
cd /d "%~dp0\.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0pipeline_test_backup_flash.ps1"
pause
