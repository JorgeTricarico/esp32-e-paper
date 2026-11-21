#!/usr/bin/env python3
"""
Flasheo inteligente USB -> fallback OTA (Base _template ESP32-S3 e-Paper).
1. Si hay USB conectado, flashea al instante por USB.
2. Si no hay USB, queda esperando el check-in OTA en UDP:32320.
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


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
    parser = argparse.ArgumentParser(description="Flash inteligente USB con fallback OTA")
    parser.add_argument("--fs", action="store_true", help="Subir LittleFS (uploadfs)")
    parser.add_argument("--both", action="store_true", help="Subir firmware y LittleFS")
    args = parser.parse_args()

    targets = ["upload", "uploadfs"] if args.both else (["uploadfs"] if args.fs else ["upload"])

    port = find_esp32_usb_port()
    if port:
        print(f"[FLASH-USB] ESP32 detectado en {port}. Flasheando...")
        for tgt in targets:
            res = subprocess.run(
                ["pio", "run", "-e", "esp32-s3-paper", "-t", tgt, "--upload-port", port],
                cwd=str(ROOT_DIR),
            )
            if res.returncode != 0:
                return res.returncode
        return 0

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.settimeout(0.8)
    sock.bind(("0.0.0.0", UDP_PORT))
    print(f"[FLASH-WAIT] Esperando conexion USB o check-in OTA en UDP:{UDP_PORT}...")

    while True:
        port = find_esp32_usb_port()
        if port:
            sock.close()
            print(f"[FLASH-USB] ESP32 detectado en {port}. Flasheando...")
            for tgt in targets:
                res = subprocess.run(
                    ["pio", "run", "-e", "esp32-s3-paper", "-t", tgt, "--upload-port", port],
                    cwd=str(ROOT_DIR),
                )
                if res.returncode != 0:
                    return res.returncode
            return 0

        try:
            data, addr = sock.recvfrom(256)
        except socket.timeout:
            continue

        msg = data.decode("utf-8", errors="ignore").strip()
        if "OTA_CHECK" not in msg:
            continue
        parts = msg.split()
        esp_ip = parts[1] if len(parts) > 1 else addr[0]
        sock.sendto(b"OTA_READY", addr)
        for tgt in targets:
            res = subprocess.run(
                ["pio", "run", "-e", "esp32-s3-paper-ota", "-t", tgt, "--upload-port", esp_ip],
                cwd=str(ROOT_DIR),
            )
            if res.returncode != 0:
                break
        else:
            print("[FLASH-OTA] Flasheo OTA completado!")
            return 0


if __name__ == "__main__":
    sys.exit(main())
