# ==============================================================================
# SCRIPT DE DETECCIÓN, COMPILACIÓN Y FLASHEO AUTÓNOMO PARA ESP32-S3 E-PAPER
# ==============================================================================

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "  ASISTENTE ESP32-S3 E-INK: SCRIPT DE FLASHEO AUTONOMO" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

# 1. Detección de Puertos COM Conectados
Write-Host "`n[1/4] Escaneando puertos serie COM..." -ForegroundColor Yellow
$ports = [System.IO.Ports.SerialPort]::getportnames()
if ($ports.Count -eq 0) {
    # Probar con WMI si SerialPort no devolvió
    $wmiPorts = Get-CimInstance Win32_SerialPort | Select-Object -ExpandProperty DeviceID
    if ($wmiPorts) { $ports = $wmiPorts }
}

if ($ports.Count -eq 0) {
    Write-Host "[!] ALERTA: No se detecto ningun puerto COM conectado." -ForegroundColor Red
    Write-Host "    Asegurese de conectar la placa Waveshare ESP32-S3 al puerto USB." -ForegroundColor Yellow
    Write-Host "    Si la placa esta en modo normal, mantenga presionado BOOT y pulse RESET para forzar modo bootloader." -ForegroundColor Yellow
} else {
    Write-Host "[+] Puerto(s) COM detectado(s): $($ports -join ', ')" -ForegroundColor Green
}

# 2. Verificación de PlatformIO Core (pio)
Write-Host "`n[2/4] Verificando entorno de compilacion (PlatformIO)..." -ForegroundColor Yellow
$pioCmd = Get-Command pio -ErrorAction SilentlyContinue
if (-not $pioCmd) {
    $pioCmd = Get-Command platformio -ErrorAction SilentlyContinue
}

if (-not $pioCmd) {
    $vscodePio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
    if (Test-Path $vscodePio) {
        $pioCmd = $vscodePio
        Write-Host "[+] PlatformIO detectado en perfil de VS Code: $vscodePio" -ForegroundColor Green
    }
}

if (-not $pioCmd) {
    Write-Host "[!] PlatformIO CLI no esta en el PATH global." -ForegroundColor Yellow
    Write-Host "    Intentando ejecutar a traves de Python..." -ForegroundColor Yellow
    $pythonCmd = Get-Command python -ErrorAction SilentlyContinue
    if ($pythonCmd) {
        Write-Host "[+] Instalando/Verificando PlatformIO Core mediante Python..." -ForegroundColor Green
        python -m pip install -U platformio
        $pioCmd = "python -m platformio"
    } else {
        Write-Host "[!] Python tampoco fue detectado en el PATH." -ForegroundColor Red
        Write-Host "    Instale VS Code con la extension PlatformIO IDE o instale Python." -ForegroundColor Yellow
        exit 1
    }
} else {
    Write-Host "[+] PlatformIO detectado: $($pioCmd.Source)" -ForegroundColor Green
}

# 3. Compilación del Firmware
Write-Host "`n[3/4] Compilando firmware (ESP32-S3, 8MB Flash, PSRAM OPI)..." -ForegroundColor Yellow
if ($pioCmd -is [string]) {
    Invoke-Expression "$pioCmd run -e esp32-s3-paper"
} else {
    & $pioCmd run -e esp32-s3-paper
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "[!] Error durante la compilacion del firmware." -ForegroundColor Red
    exit 1
}

Write-Host "[+] Compilacion exitosa!" -ForegroundColor Green

# 4. Flasheo al Dispositivo
Write-Host "`n[4/4] Flasheando al ESP32-S3..." -ForegroundColor Yellow
$targetPort = $ports[0]
if ($targetPort) {
    Write-Host "[+] Flasheando en el puerto: $targetPort" -ForegroundColor Green
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
    Write-Host "  [OK] FIRMWARE SUBIDO CON EXITO AL ESP32-S3!" -ForegroundColor Green
    Write-Host "========================================================" -ForegroundColor Green
    Write-Host "Iniciando monitor serie (115200 baudios)... Presione Ctrl+C para salir.`n" -ForegroundColor Cyan
    if ($pioCmd -is [string]) {
        Invoke-Expression "$pioCmd device monitor -b 115200"
    } else {
        & $pioCmd device monitor -b 115200
    }
} else {
    Write-Host "[!] Error durante la subida al ESP32. Verifique la conexion USB y los permisos." -ForegroundColor Red
}
