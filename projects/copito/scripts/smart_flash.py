#!/usr/bin/env python3
"""
==============================================================================
COPITO AI - SMART FLASH (USB CDC AUTO-DETECT + OTA WI-FI FAILOVER)
==============================================================================
Intenta flashear primero por USB serie (esperando reconexion si es necesario).
Si el USB no esta disponible o falla, busca a Copito en la red Wi-Fi (LAN)
y sube el firmware via OTA (/update) aprovechando la ventana de check-in.
==============================================================================
"""

import sys
import os
import time
import subprocess
import socket
import urllib.request
import urllib.parse
import json

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BIN_PATH = os.path.join(ROOT_DIR, ".pio", "build", "esp32-s3-paper", "firmware.bin")

def log(msg, symbol="INFO"):
    print(f"[{symbol}] {msg}", flush=True)

def find_com_port(timeout_s=10):
    log(f"Buscando puerto COM USB (esperando hasta {timeout_s}s)...", "BUSCAR")
    import serial.tools.list_ports
    start = time.time()
    while time.time() - start < timeout_s:
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            hwid = (p.hwid or "").upper()
            desc = (p.description or "").upper()
            dev = p.device
            if "303A" in hwid or "USB" in desc or "COM3" in dev:
                log(f"Dispositivo detectado en {dev} ({p.description})", "OK")
                return dev
        time.sleep(1)
    return None

def flash_via_usb(port):
    log(f"Iniciando flasheo USB en {port} a 460800 baudios...", "FLASH")
    cmd = f"pio run -e esp32-s3-paper -t upload --upload-port {port}"
    res = subprocess.run(cmd, shell=True, cwd=ROOT_DIR)
    if res.returncode == 0:
        log("Flasheo por USB completado exitosamente!", "EXITO")
        return True
    else:
        log(f"Flasheo USB fallo (codigo {res.returncode}). Pasando a contingencia OTA...", "AVISO")
        return False

def get_lan_ips():
    ips = []
    try:
        hostname = socket.gethostname()
        for ip in socket.gethostbyname_ex(hostname)[2]:
            if not ip.startswith("127.") and not ip.startswith("169.254."):
                ips.append(ip)
    except Exception:
        pass
    return ips

def check_copito_ip(ip, timeout=1.0):
    url = f"http://{ip}/api/sensor"
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "SmartFlash/1.0"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            if resp.status == 200:
                data = json.loads(resp.read().decode("utf-8"))
                return data
    except Exception:
        pass
    return None

def discover_copito_lan(max_wait_seconds=60):
    log("Iniciando descubrimiento OTA en red local Wi-Fi...", "WIFI")
    log(f"Esperando a que Copito despierte o abra su ventana OTA (hasta {max_wait_seconds}s)...", "ESPERA")

    for name in ["copito.local", "esp32-paper.local", "192.168.4.1"]:
        info = check_copito_ip(name, timeout=1.2)
        if info:
            log(f"Copito detectado en {name} (RSSI: {info.get('wifi_rssi')} dBm)", "ENCONTRADO")
            return name

    lan_ips = get_lan_ips()
    subnets = []
    for my_ip in lan_ips:
        parts = my_ip.split(".")
        if len(parts) == 4:
            subnets.append(".".join(parts[:3]))

    start_t = time.time()
    while time.time() - start_t < max_wait_seconds:
        for sub in subnets:
            candidate_hosts = list(range(100, 200)) + list(range(2, 50))
            for h in candidate_hosts:
                ip = f"{sub}.{h}"
                info = check_copito_ip(ip, timeout=0.20)
                if info:
                    log(f"Copito detectado en {ip} (RSSI: {info.get('wifi_rssi')} dBm)", "ENCONTRADO")
                    return ip
        remaining = int(max_wait_seconds - (time.time() - start_t))
        if remaining > 0:
            log(f"Copito aun no responde en LAN; esperando ventana de check-in ({remaining}s restantes)...", "ESPERA")
            time.sleep(3)
    return None

def flash_via_ota(target_ip):
    log(f"Preparando actualizacion OTA hacia http://{target_ip}/update...", "OTA")
    if not os.path.exists(BIN_PATH):
        log(f"No se encontro el binario en {BIN_PATH}. Compilando...", "BUILD")
        subprocess.run("pio run -e esp32-s3-paper", shell=True, cwd=ROOT_DIR)

    if not os.path.exists(BIN_PATH):
        log("Error: binario firmware.bin no disponible.", "ERROR")
        return False

    try:
        hold_url = f"http://{target_ip}/api/ota_hold"
        hold_data = urllib.parse.urlencode({"minutes": 10}).encode("utf-8")
        req_hold = urllib.request.Request(hold_url, data=hold_data, headers={"Content-Type": "application/x-www-form-urlencoded"})
        with urllib.request.urlopen(req_hold, timeout=3) as r:
            log("Hold de 10 min activado en Copito para garantizar ventana despierto.", "HOLD")
    except Exception as e:
        log(f"Aviso al activar hold: {e}", "AVISO")

    file_size = os.path.getsize(BIN_PATH)
    log(f"Subiendo firmware.bin ({file_size} bytes) a http://{target_ip}/update...", "UPLOAD")

    boundary = "----SmartFlashBoundary" + str(int(time.time()))
    body = []
    body.append(f"--{boundary}\r\n".encode())
    body.append(b'Content-Disposition: form-data; name="update"; filename="firmware.bin"\r\n')
    body.append(b'Content-Type: application/octet-stream\r\n\r\n')
    with open(BIN_PATH, "rb") as f:
        file_bytes = f.read()
    body.append(file_bytes)
    body.append(f"\r\n--{boundary}--\r\n".encode())
    payload = b"".join(body)

    update_url = f"http://{target_ip}/update"
    req = urllib.request.Request(update_url, data=payload, headers={
        "Content-Type": f"multipart/form-data; boundary={boundary}",
        "Content-Length": str(len(payload))
    })

    try:
        with urllib.request.urlopen(req, timeout=45) as resp:
            resp_txt = resp.read().decode("utf-8", errors="replace")
            log(f"Respuesta OTA: {resp_txt}", "RESPUESTA")
            if resp.status == 200:
                log("Actualizacion OTA completada con exito! Copito se reiniciara automaticamente.", "EXITO")
                return True
    except Exception as e:
        log(f"Error durante la subida OTA: {e}", "ERROR")
    return False

def main():
    print("=" * 60)
    print("  COPITO AI - SMART FLASH (USB + OTA FAILOVER)")
    print("=" * 60)
    wait_forever = ("--wait" in sys.argv or "-w" in sys.argv)
    round_n = 0
    while True:
        round_n += 1
        if wait_forever:
            log(f"Ronda {round_n}: USB primero, luego OTA (quedando a la espera)...", "TURNO")
        port = find_com_port(timeout_s=8)
        if port:
            success = flash_via_usb(port)
            if success:
                sys.exit(0)

        log("USB no disponible o desconectado. Pasando automaticamente a flasheo OTA...", "CAMBIO")
        target_ip = discover_copito_lan(max_wait_seconds=60)
        if target_ip:
            if flash_via_ota(target_ip):
                sys.exit(0)

        if not wait_forever:
            break
        log("Sin USB ni OTA en esta ronda. Reintentando en 15 s (Ctrl+C para salir)...", "ESPERA")
        time.sleep(15)

    log("No fue posible flashear por USB ni por OTA en esta pasada.", "ERROR")
    log("Sugerencia: Conecta el cable USB al ESP32 o presiona el boton BOOT para despertarlo en Wi-Fi.", "AYUDA")
    sys.exit(1)

if __name__ == "__main__":
    main()
