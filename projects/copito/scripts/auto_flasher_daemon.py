#!/usr/bin/env python3
"""
==============================================================================
COPITO AI - DAEMON DE FLASHEO AUTONOMO DESATENDIDO (USB + OTA RECEPTIVO)
==============================================================================
Escucha en bucle continuo:
1. Si se reconecta el USB (puerto COM), flashea inmediatamente por cable.
2. Si el dispositivo esta desconectado de USB, monitorea la red Wi-Fi LAN
   esperando a que Copito despierte en su ventana periodica de check-in OTA.
3. En cuanto Copito asoma en Wi-Fi, asegura el hold de 15 minutos y sube
   el firmware por HTTP POST multipart (/update).
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
import concurrent.futures

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BIN_PATH = os.path.join(ROOT_DIR, ".pio", "build", "esp32-s3-paper", "firmware.bin")

def log(msg, tag="INFO"):
    ts = time.strftime("%H:%M:%S")
    print(f"[{ts}] [{tag}] {msg}", flush=True)

def find_com_port():
    try:
        import serial.tools.list_ports
        for p in serial.tools.list_ports.comports():
            hwid = (p.hwid or "").upper()
            desc = (p.description or "").upper()
            dev = p.device
            if "303A" in hwid or "USB" in desc or "COM3" in dev:
                return dev
    except Exception:
        pass
    return None

def flash_via_usb(port):
    log(f"¡Puerto USB detectado en {port}! Iniciando flasheo...", "USB")
    cmd = f"pio run -e esp32-s3-paper -t upload --upload-port {port}"
    res = subprocess.run(cmd, shell=True, cwd=ROOT_DIR)
    if res.returncode == 0:
        log("¡Flasheo por USB completado con éxito absoluto!", "EXITO")
        return True
    else:
        log(f"Flasheo por USB falló (código {res.returncode}). Continuando escucha...", "AVISO")
        return False

def probe_ip(ip):
    url = f"http://{ip}/api/sensor"
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "CopitoAutoFlash/1.0"})
        with urllib.request.urlopen(req, timeout=0.35) as resp:
            if resp.status == 200:
                data = json.loads(resp.read().decode())
                return (ip, data)
    except Exception:
        pass
    return None

def find_copito_lan():
    # Primero nombres directos e IP conocida
    for host in ["192.168.0.176", "copito.local", "esp32-paper.local", "192.168.4.1"]:
        res = probe_ip(host)
        if res:
            return res[0], res[1]

    # Subred de la máquina
    ips = [f"192.168.0.{i}" for i in range(2, 254)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=40) as ex:
        futures = {ex.submit(probe_ip, ip): ip for ip in ips}
        for f in concurrent.futures.as_completed(futures):
            res = f.result()
            if res:
                return res[0], res[1]
    return None, None

def flash_via_ota(target_ip):
    log(f"¡Copito detectado en la red ({target_ip})! Activando ventana de hold y subiendo firmware...", "OTA")
    if not os.path.exists(BIN_PATH):
        log("Compilando firmware.bin antes de subir...", "BUILD")
        subprocess.run("pio run -e esp32-s3-paper", shell=True, cwd=ROOT_DIR)

    # 1. Hold de 15 min para evitar que entre a deep-sleep durante el flasheo
    try:
        hold_url = f"http://{target_ip}/api/ota_hold"
        data = urllib.parse.urlencode({"minutes": 15}).encode("utf-8")
        req = urllib.request.Request(hold_url, data=data, headers={"Content-Type": "application/x-www-form-urlencoded"})
        with urllib.request.urlopen(req, timeout=3) as r:
            log("Hold de 15 min confirmado por Copito.", "HOLD")
    except Exception as e:
        log(f"Aviso al activar hold: {e}", "AVISO")

    # 2. Subir multipart firmware.bin
    file_size = os.path.getsize(BIN_PATH)
    log(f"Enviando {file_size} bytes a http://{target_ip}/update...", "UPLOAD")
    boundary = "----AutoFlashBoundary" + str(int(time.time()))
    body = []
    body.append(f"--{boundary}\r\n".encode())
    body.append(b'Content-Disposition: form-data; name="update"; filename="firmware.bin"\r\n')
    body.append(b'Content-Type: application/octet-stream\r\n\r\n')
    with open(BIN_PATH, "rb") as f:
        body.append(f.read())
    body.append(f"\r\n--{boundary}--\r\n".encode())
    payload = b"".join(body)

    req = urllib.request.Request(f"http://{target_ip}/update", data=payload, headers={
        "Content-Type": f"multipart/form-data; boundary={boundary}",
        "Content-Length": str(len(payload))
    })

    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            txt = resp.read().decode("utf-8", errors="replace")
            log(f"Respuesta de Copito: {txt}", "RESPUESTA")
            if resp.status == 200:
                log("¡Actualización OTA completada con éxito! Copito se reiniciará con el nuevo firmware.", "EXITO")
                return True
    except Exception as e:
        log(f"Error durante subida OTA: {e}", "ERROR")
    return False

def main():
    print("=" * 65)
    print("  COPITO AI - DAEMON DE FLASHEO AUTONOMO (USB + OTA FAILOVER)")
    print("=" * 65)
    log("Iniciando escucha continua. Puedes conectar el cable USB o esperar")
    log("a que Copito despierte en su ventana Wi-Fi de check-in.")
    log("Presiona Ctrl+C en cualquier momento para detener.")

    iteration = 0
    while True:
        iteration += 1

        # 1. Chequeo USB prioritario
        port = find_com_port()
        if port:
            if flash_via_usb(port):
                log("Operación completada con éxito por USB. Finalizando daemon.", "FIN")
                sys.exit(0)

        # 2. Chequeo Wi-Fi OTA (en caso de que Copito despierte o esté conectado)
        copito_ip, info = find_copito_lan()
        if copito_ip:
            log(f"Copito activo en {copito_ip} (Batería: {info.get('uptime_s', 0)}s uptime, RSSI: {info.get('wifi_rssi', 0)} dBm)", "ENCONTRADO")
            if flash_via_ota(copito_ip):
                log("Operación completada con éxito por OTA Wi-Fi. Finalizando daemon.", "FIN")
                sys.exit(0)

        if iteration % 4 == 1:
            log("Escuchando... Esperando conexión USB o ventana Wi-Fi de Copito...", "ESPERANDO")
        time.sleep(3)

if __name__ == "__main__":
    main()
