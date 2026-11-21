# ==============================================================================
# DIAGNÓSTICO DE PUERTO COM Y ESTADO DE CONEXIÓN USB CON EL ESP32-S3
# ==============================================================================

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "  DIAGNOSTICO DE CONEXION USB Y PUERTO COM ESP32-S3" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

# 1. Buscar Puertos Serie por WMI y PnP
Write-Host "`n[1/3] Escaneando dispositivos USB y Puertos COM en Windows..." -ForegroundColor Yellow

$comPorts = Get-CimInstance Win32_SerialPort | Select-Object DeviceID, Description, PNPDeviceID
$pnpDevices = Get-CimInstance Win32_PnPEntity | Where-Object { 
    $_.Caption -match 'COM\d+' -or $_.HardwareID -match 'VID_303A' -or $_.HardwareID -match 'VID_10C4' -or $_.HardwareID -match 'VID_1A86'
} | Select-Object Name, Caption, DeviceID, HardwareID

$detectedPort = $null

if ($comPorts) {
    Write-Host "`n[+] Puertos COM detectados (Win32_SerialPort):" -ForegroundColor Green
    foreach ($p in $comPorts) {
        Write-Host "    - $($p.DeviceID): $($p.Description)" -ForegroundColor White
        if (-not $detectedPort) { $detectedPort = $p.DeviceID }
    }
}

if ($pnpDevices) {
    Write-Host "`n[+] Dispositivos PnP Serie / USB detectados:" -ForegroundColor Green
    foreach ($dev in $pnpDevices) {
        Write-Host "    - $($dev.Caption) [$($dev.HardwareID[0])]" -ForegroundColor White
        if ($dev.Caption -match '(COM\d+)') {
            if (-not $detectedPort) { $detectedPort = $matches[1] }
        }
    }
}

if (-not $detectedPort) {
    Write-Host "`n[!] ALERTA CRITICA: No se detecto ningun puerto COM activo." -ForegroundColor Red
    Write-Host "    Causas posibles:" -ForegroundColor Yellow
    Write-Host "    1. El cable USB conectado podria ser de solo carga (sin lineas de datos D+/D-)." -ForegroundColor Gray
    Write-Host "    2. Falta el driver USB (CH343/CH340 o CP2102) o el dispositivo esta apagado." -ForegroundColor Gray
    Write-Host "    3. La placa no esta bien encajada en el puerto USB." -ForegroundColor Gray
    exit 1
}

Write-Host "`n[+] Puerto principal detectado: $detectedPort" -ForegroundColor Green

# 2. Localizar esptool o PlatformIO
Write-Host "`n[2/3] Localizando herramienta de comunicacion con el chip (esptool)..." -ForegroundColor Yellow

$esptoolCmd = $null
$vscodePio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"

if (Get-Command esptool.py -ErrorAction SilentlyContinue) {
    $esptoolCmd = "esptool.py"
} elseif (Get-Command esptool -ErrorAction SilentlyContinue) {
    $esptoolCmd = "esptool"
} elseif (Test-Path $vscodePio) {
    $pioPkgEsptool = "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py"
    $pioPython = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
    if ((Test-Path $pioPkgEsptool) -and (Test-Path $pioPython)) {
        $esptoolCmd = "& `"$pioPython`" `"$pioPkgEsptool`""
    } else {
        $esptoolCmd = "& `"$vscodePio`" pkg exec -p tool-esptoolpy -- esptool.py"
    }
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    $null = python -c "import esptool" 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[+] Instalando esptool mediante pip..." -ForegroundColor Yellow
        python -m pip install esptool
    }
    $esptoolCmd = "python -m esptool"
}

# 3. Probar comunicación remota con el ESP32-S3 (Sin tocar botones)
Write-Host "`n[3/3] Probando handshake de entrada en modo bootloader via USB (Sin intervencion manual)..." -ForegroundColor Yellow

if ($esptoolCmd) {
    Write-Host "[+] Ejecutando consulta de chip en $detectedPort..." -ForegroundColor Gray
    try {
        if ($esptoolCmd -match '^&') {
            Invoke-Expression "$esptoolCmd --port $detectedPort chip_id"
        } else {
            Invoke-Expression "$esptoolCmd --port $detectedPort chip_id"
        }

        if ($LASTEXITCODE -eq 0) {
            Write-Host "`n========================================================" -ForegroundColor Green
            Write-Host "  [EXITO TOTAL] CONEXION Y ACCESO USB CONFIRMADOS!" -ForegroundColor Green
            Write-Host "========================================================" -ForegroundColor Green
            Write-Host "  El ESP32-S3 responde perfectamente a los comandos serie." -ForegroundColor White
            Write-Host "  El reinicio por software a modo bootloader funciona." -ForegroundColor White
            Write-Host "  --> ESTAMOS 100% EN CONDICIONES DE FLASHEAR SIN TOCAR LA PLACA." -ForegroundColor Green
        } else {
            Write-Host "`n[!] esptool no pudo conectar con el chip automaticamente." -ForegroundColor Yellow
            Write-Host "    Si la placa esta ejecutando un codigo previo con USB CDC bloqueado," -ForegroundColor Yellow
            Write-Host "    podria requerir entrar en bootloader o ajustar la velocidad." -ForegroundColor Yellow
        }
    } catch {
        Write-Host "[!] Error al ejecutar esptool: $_" -ForegroundColor Red
    }
} else {
    Write-Host "[+] Puerto detectado en $detectedPort, pero esptool no esta en PATH." -ForegroundColor Yellow
    Write-Host "    Ejecuta scripts\flash_tool.bat directamente para compilar y flashear." -ForegroundColor Green
}
