#!/usr/bin/env python3
"""
Flasheo inteligente USB -> fallback OTA:
1. Verifica si el ESP32-S3 esta conectado por USB. Si esta, flashea al instante por USB.
2. Si NO hay puerto USB disponible, queda en modo espera OTA (UDP:32320) hasta que
   el ESP32 despierte en su ventana de check-in y lo flashea por WiFi.

Uso:
  python tools/flash.py            # Solo firmware
  python tools/flash.py --fs       # Solo LittleFS
  python tools/flash.py --both     # Firmware + LittleFS
"""

from __future__ import annotations

import argparse
import json
import socket
import subprocess
import sys
from pathlib import Path
from typing import Optional

UDP_PORT = 32320
ROOT_DIR = Path(__file__).resolve().parent.parent


def find_esp32_usb_port() -> Optional[str]:
    """Detecta un puerto serie conectado (prioriza VID 303A de ESP32-S3)."""
    res = subprocess.run(
        ["pio", "device", "list", "--serial", "--json-output"],
        capture_output=True,
        text=True,
    )
    if res.returncode != 0 or not res.stdout.strip():
        return None
    try:
        ports = json.loads(res.stdout)
    except json.JSONDecodeError:
        return None

    for p in ports:
        hwid = (p.get("hwid") or "").upper()
        if "303A" in hwid or "CP210" in hwid or "CH340" in hwid:
            return p.get("port")
    if ports:
        return ports[0].get("port")
    return None


def flash_usb(port: str, targets: list[str]) -> int:
    print(f"[FLASH-USB] ESP32 detectado en {port}. Flasheando al instante...")
    for tgt in targets:
        cmd = ["pio", "run", "-e", "esp32-s3-paper", "-t", tgt, "--upload-port", port]
        print(f"[FLASH-USB] Ejecutando: {' '.join(cmd)}")
        res = subprocess.run(cmd, cwd=str(ROOT_DIR))
        if res.returncode != 0:
            return res.returncode
    print("[FLASH-USB] Flasheo por USB completado!")
    return 0


def wait_and_flash_ota_or_usb(targets: list[str]) -> int:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.settimeout(0.8)
    sock.bind(("0.0.0.0", UDP_PORT))

    print(f"[FLASH-WAIT] Esperando conexion USB o check-in OTA en UDP:{UDP_PORT}...")
    while True:
        # 1. Si se conecto por USB mientras esperaba, flashea por USB al instante
        port = find_esp32_usb_port()
        if port:
            sock.close()
            return flash_usb(port, targets)

        # 2. Si no hay USB, escucha check-in OTA
        try:
            data, addr = sock.recvfrom(256)
        except socket.timeout:
            continue

        msg = data.decode("utf-8", errors="ignore").strip()
        if "OTA_CHECK" not in msg:
            continue

        parts = msg.split()
        esp_ip = parts[1] if len(parts) > 1 else addr[0]
        print(f"[FLASH-OTA] ESP32 detectado en {esp_ip}! Enviando OTA_READY...")
        sock.sendto(b"OTA_READY", addr)

        for tgt in targets:
            cmd = ["pio", "run", "-e", "esp32-s3-paper-ota", "-t", tgt, "--upload-port", esp_ip]
            print(f"[FLASH-OTA] Ejecutando: {' '.join(cmd)}")
            res = subprocess.run(cmd, cwd=str(ROOT_DIR))
            if res.returncode != 0:
                print(f"[FLASH-OTA] Fallo {tgt} (codigo {res.returncode}). Sigo escuchando...")
                break
        else:
            sock.close()
            print("[FLASH-OTA] Flasheo OTA completado con exito!")
            return 0


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
    parser = argparse.ArgumentParser(description="Flash inteligente USB o espera USB/OTA")
    parser.add_argument("--fs", action="store_true", help="Subir LittleFS (uploadfs)")
    parser.add_argument("--both", action="store_true", help="Subir firmware y LittleFS")
    args = parser.parse_args()

    targets = ["upload", "uploadfs"] if args.both else (["uploadfs"] if args.fs else ["upload"])

    port = find_esp32_usb_port()
    if port:
        return flash_usb(port, targets)
    return wait_and_flash_ota_or_usb(targets)


if __name__ == "__main__":
    sys.exit(main())
