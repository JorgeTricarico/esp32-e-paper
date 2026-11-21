# ==============================================================================
# LEVANTAR SERVIDOR WEB LOCAL PARA VER EL SIMULADOR DESDE OTRO DISPOSITIVO
# ==============================================================================

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "  SIMULADOR WEB ESP32-S3 E-PAPER (ACCESO EN RED LOCAL)" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

$ips = Get-NetIPAddress -AddressFamily IPv4 | Where-Object { 
    $_.InterfaceAlias -notmatch 'Loopback' -and $_.IPAddress -notlike '169.254*' 
} | Select-Object -ExpandProperty IPAddress

Write-Host "`n[+] Desde tu otro dispositivo (conectado al mismo WiFi), abre en el navegador:" -ForegroundColor Yellow
foreach ($ip in $ips) {
    Write-Host "    👉 http://${ip}:8080" -ForegroundColor Green
}
Write-Host ""

$simDir = "$PSScriptRoot\..\simulator"
Set-Location $simDir

Write-Host "[+] Iniciando servidor HTTP en puerto 8080..." -ForegroundColor Cyan
Write-Host "    (Presiona Ctrl+C para detener el servidor)`n" -ForegroundColor Gray

$python = Get-Command python -ErrorAction SilentlyContinue
if ($python) {
    python -m http.server 8080 --bind 0.0.0.0
} else {
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        py -m http.server 8080 --bind 0.0.0.0
    } else {
        $node = Get-Command npx -ErrorAction SilentlyContinue
        if ($node) {
            npx -y serve -l 8080
        } else {
            Write-Host "[!] No se encontro Python ni Node.js en el sistema." -ForegroundColor Red
        }
    }
}
