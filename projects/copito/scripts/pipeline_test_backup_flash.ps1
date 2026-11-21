# ==============================================================================
# PIPELINE INTEGRAL: PRUEBAS -> RESPALDO DE 16 MB -> COMPILACIÓN -> FLASHEO
# ==============================================================================

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "  PIPELINE DE SEGURIDAD Y DESPLIEGUE AUTONOMO ESP32-S3" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

# --- PASO 1: EJECUTAR SUITE DE PRUEBAS ---
Write-Host "`n[PASO 1/4] Ejecutando Pack de Pruebas Unitarias e Integracion..." -ForegroundColor Yellow
$pyCmd = Get-Command python -ErrorAction SilentlyContinue
if (-not $pyCmd) { $pyCmd = Get-Command py -ErrorAction SilentlyContinue }

if ($pyCmd) {
    & $pyCmd "$PSScriptRoot\..\tests\test_suite.py"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "`n[!] ALERTA CRITICA: Las pruebas fallaron. Operacion abortada por seguridad." -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host "[!] Advertencia: Python no detectado, continuando con pruebas de compilacion..." -ForegroundColor Yellow
}

# --- PASO 2: RESPALDAR MEMORIA FLASH ACTUAL (8 MB) ---
Write-Host "`n[PASO 2/4] Respaldando los 8 MB completos de Flash del ESP32-S3 antes de flashear..." -ForegroundColor Yellow
& "$PSScriptRoot\backup_firmware.ps1"

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[!] ALERTA CRITICA: El respaldo de la Flash fallo. Por seguridad, NO se sobreescribira el chip." -ForegroundColor Red
    exit 1
}

# --- PASO 3: COMPILAR FIRMWARE ---
Write-Host "`n[PASO 3/4] Compilando Firmware Asistente Copito (8 MB Flash, PSRAM OPI)..." -ForegroundColor Yellow

# Limpiar posibles bloqueos previos de PlatformIO
Remove-Item -Path "$env:USERPROFILE\.platformio\packages.lock" -Force -ErrorAction SilentlyContinue
Remove-Item -Path "$env:USERPROFILE\.platformio\.cache\tmp\*" -Recurse -Force -ErrorAction SilentlyContinue

$pioCmd = Get-Command pio -ErrorAction SilentlyContinue
if (-not $pioCmd) { $pioCmd = Get-Command platformio -ErrorAction SilentlyContinue }
if (-not $pioCmd) {
    $hermesPio = "$env:LOCALAPPDATA\hermes\hermes-agent\venv\Scripts\pio.exe"
    if (Test-Path $hermesPio) { $pioCmd = $hermesPio }
}
if (-not $pioCmd) {
    $vscodePio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
    if (Test-Path $vscodePio) { $pioCmd = $vscodePio }
}

if (-not $pioCmd) {
    if (Get-Command python -ErrorAction SilentlyContinue) {
        $null = python -c "import platformio" 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Host "[+] Instalando PlatformIO Core mediante pip..." -ForegroundColor Yellow
            python -m pip install platformio
        }
        $pioCmd = "python -m platformio"
    } else {
        Write-Host "[!] Error: No se encontro PlatformIO ni Python para compilar." -ForegroundColor Red
        exit 1
    }
}

if ($pioCmd -is [string]) {
    Invoke-Expression "$pioCmd run -e esp32-s3-paper"
} else {
    & $pioCmd run -e esp32-s3-paper
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[!] Error durante la compilacion del firmware." -ForegroundColor Red
    exit 1
}

# --- PASO 4: FLASHEAR FIRMWARE ---
Write-Host "`n[PASO 4/4] Flasheando firmware al ESP32-S3 de forma desatendida..." -ForegroundColor Yellow

$ports = [System.IO.Ports.SerialPort]::getportnames()
if ($ports.Count -eq 0) {
    $wmiPorts = Get-CimInstance Win32_SerialPort | Select-Object -ExpandProperty DeviceID
    if ($wmiPorts) { $ports = $wmiPorts }
}
if ($ports.Count -eq 0) {
    $pnpCom = Get-CimInstance Win32_PnPEntity | Where-Object { $_.Caption -match '(COM\d+)' } | Select-Object -ExpandProperty Caption
    if ($pnpCom -match '(COM\d+)') { $ports = @($matches[1]) }
}
$targetPort = if ($ports.Count -gt 0) { $ports[0] } else { $null }

if ($targetPort) {
    Write-Host "[+] Subiendo al puerto $targetPort a 921600 baudios..." -ForegroundColor Green
    if ($pioCmd -is [string]) {
        Invoke-Expression "$pioCmd run -e esp32-s3-paper -t upload --upload-port $targetPort"
    } else {
        & $pioCmd run -e esp32-s3-paper -t upload --upload-port $targetPort
    }
} else {
    if ($pioCmd -is [string]) {
        Invoke-Expression "$pioCmd run -e esp32-s3-paper -t upload"
    } else {
        & $pioCmd run -e esp32-s3-paper -t upload
    }
}

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n========================================================" -ForegroundColor Green
    Write-Host "  [EXITO COMPLETO] EL ESP32-S3 HA SIDO FLASHEADO!" -ForegroundColor Green
    Write-Host "========================================================" -ForegroundColor Green
    Write-Host "  1. El respaldo previo esta a salvo en la carpeta 'backup/'" -ForegroundColor White
    Write-Host "  2. Puedes restaurar el firmware previo con 'scripts\restore_backup.bat'" -ForegroundColor White
    Write-Host "  3. Abriendo monitor serie a 115200 baudios... (Ctrl+C para salir)`n" -ForegroundColor Cyan
    
    if ($pioCmd -is [string]) {
        Invoke-Expression "$pioCmd device monitor -b 115200"
    } else {
        & $pioCmd device monitor -b 115200
    }
} else {
    Write-Host "[!] Error durante el flasheo del firmware." -ForegroundColor Red
}
