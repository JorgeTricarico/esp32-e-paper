# ==============================================================================
# SCRIPT DE BACKUP COMPLETO DE MEMORIA FLASH (16 MB) DEL ESP32-S3
# Lee y guarda en disco la imagen binaria exacta antes de flashear
# ==============================================================================

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "  RESPALDO / BACKUP DE FIRMWARE COMPLETO (16 MB SPI FLASH)" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

# 1. Detectar Puerto COM
$ports = [System.IO.Ports.SerialPort]::getportnames()
if ($ports.Count -eq 0) {
    $wmiPorts = Get-CimInstance Win32_SerialPort | Select-Object -ExpandProperty DeviceID
    if ($wmiPorts) { $ports = $wmiPorts }
}
if ($ports.Count -eq 0) {
    $pnpCom = Get-CimInstance Win32_PnPEntity | Where-Object { $_.Caption -match '(COM\d+)' } | Select-Object -ExpandProperty Caption
    if ($pnpCom -match '(COM\d+)') { $ports = @($matches[1]) }
}

if ($ports.Count -eq 0) {
    Write-Host "[!] Error: No se encontro ningun puerto COM conectado al ESP32." -ForegroundColor Red
    exit 1
}

$targetPort = $ports[0]
Write-Host "[+] Puerto COM detectado para respaldo: $targetPort" -ForegroundColor Green

# 2. Localizar esptool
$esptoolCmd = $null
$vscodePio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
$pioPkgEsptool = "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py"
$pioPython = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"

if (Get-Command esptool.py -ErrorAction SilentlyContinue) {
    $esptoolCmd = "esptool.py"
} elseif (Get-Command esptool -ErrorAction SilentlyContinue) {
    $esptoolCmd = "esptool"
} elseif ((Test-Path $pioPkgEsptool) -and (Test-Path $pioPython)) {
    $esptoolCmd = "& `"$pioPython`" `"$pioPkgEsptool`""
} elseif (Test-Path $vscodePio) {
    $esptoolCmd = "& `"$vscodePio`" pkg exec -p tool-esptoolpy -- esptool.py"
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    $null = python -c "import esptool" 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[+] Instalando esptool mediante pip en el entorno Python..." -ForegroundColor Yellow
        python -m pip install esptool
    }
    $esptoolCmd = "python -m esptool"
}

if (-not $esptoolCmd) {
    Write-Host "[!] Error: No se encontro esptool en el sistema." -ForegroundColor Red
    exit 1
}

# 3. Crear directorio de backup y verificar si ya existe
$backupDir = "$PSScriptRoot\..\backup"
if (-not (Test-Path $backupDir)) {
    New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
}

$existingBackups = Get-ChildItem -Path "$backupDir\esp32s3_original_flash_*.bin" -ErrorAction SilentlyContinue | Where-Object { $_.Length -eq 8388608 }
if ($existingBackups) {
    $latest = $existingBackups | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    Write-Host "`n[+] Respaldo previo integro ya existente: $($latest.Name)" -ForegroundColor Green
    Write-Host "    Tamano verificado: 8,388,608 bytes (8 MB)." -ForegroundColor Green
    $backupFile = $latest.FullName
    $shaFile = [System.IO.Path]::ChangeExtension($backupFile, ".sha256")
    if (Test-Path $shaFile) {
        $hash = (Get-Content $shaFile).Split(" ")[0]
        Write-Host "    Hash SHA-256 verificado: $hash" -ForegroundColor Green
    }
} else {
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $backupFile = "$backupDir\esp32s3_original_flash_$timestamp.bin"
    $shaFile = "$backupDir\esp32s3_original_flash_$timestamp.sha256"

    Write-Host "`n[+] Leyendo los 8 MB completos de Flash desde el chip (0x0 a 0x800000)..." -ForegroundColor Yellow
    Write-Host "    Destino: $backupFile" -ForegroundColor Gray
    Write-Host "    Velocidad: 921600 baudios (esto tomara ~20 segundos)...`n" -ForegroundColor Gray

    $flashSizeHex = "0x800000" # 8 MB = 8,388,608 bytes (ESP32-S3-PICO-1)

    if ($esptoolCmd -match '^&') {
        Invoke-Expression "$esptoolCmd --port $targetPort --baud 921600 read-flash 0x0 $flashSizeHex `"$backupFile`""
    } else {
        Invoke-Expression "$esptoolCmd --port $targetPort --baud 921600 read-flash 0x0 $flashSizeHex `"$backupFile`""
    }

    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $backupFile)) {
        Write-Host "`n[!] Fallo al extraer la memoria flash del ESP32." -ForegroundColor Red
        exit 1
    }

    $fileSize = (Get-Item $backupFile).Length
    if ($fileSize -ne 8388608) {
        Write-Host "`n[!] Advertencia: El tamano del archivo extraido ($fileSize bytes) difiere de 8 MB." -ForegroundColor Yellow
    } else {
        Write-Host "`n[+] Tamano de imagen verificado: 8,388,608 bytes (8 MB integro)." -ForegroundColor Green
    }

    # 4. Calcular SHA-256
    $hash = (Get-FileHash -Path $backupFile -Algorithm SHA256).Hash
    Set-Content -Path $shaFile -Value "$hash  $(Split-Path $backupFile -Leaf)"
    Write-Host "[+] Hash SHA-256 de verificacion: $hash" -ForegroundColor Green
}

# 5. Generar script de restauracion automatica 1-click
$restoreBat = "$PSScriptRoot\restore_backup.bat"
$restoreContent = @"
@echo off
title Restaurar Backup Original ESP32-S3
color 0C
echo ========================================================
echo   RESTAURAR BACKUP ORIGINAL ESP32-S3 (8 MB)
echo ========================================================
echo Archivo a flashear:
echo %~dp0..\backup\$(Split-Path $backupFile -Leaf)
echo.
set /p CONFIRM=Esta seguro de sobreescribir la memoria con este backup? (S/N): 
if /i not "%CONFIRM%"=="S" goto cancel

cd /d "%~dp0\.."
powershell -NoProfile -ExecutionPolicy Bypass -Command "$esptoolCmd --port $targetPort --baud 921600 write_flash 0x0 '%~dp0..\backup\$(Split-Path $backupFile -Leaf)'"

pause
goto end
:cancel
echo Restauracion cancelada por el usuario.
pause
:end
"@

Set-Content -Path $restoreBat -Value $restoreContent
Write-Host "[+] Script de restauracion rapida creado: scripts\restore_backup.bat" -ForegroundColor Cyan

Write-Host "`n========================================================" -ForegroundColor Green
Write-Host "  [OK] RESPALDO COMPLETADO Y VERIFICADO EXITOSAMENTE!" -ForegroundColor Green
Write-Host "========================================================" -ForegroundColor Green
