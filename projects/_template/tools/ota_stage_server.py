#!/usr/bin/env python3
"""
Servidor de espera OTA para proyectos basados en _template (ESP32-S3 e-Paper).
Escucha en UDP:32320 el check-in del ESP32, responde OTA_READY y ejecuta pio run.

Uso:
  python tools/ota_stage_server.py            # Sube firmware
  python tools/ota_stage_server.py --fs       # Sube LittleFS
  python tools/ota_stage_server.py --both     # Sube firmware + LittleFS
"""

from __future__ import annotations

import argparse
import socket
import subprocess
import sys
from pathlib import Path

UDP_PORT = 32320
ROOT_DIR = Path(__file__).resolve().parent.parent


def main() -> int:
    parser = argparse.ArgumentParser(description="Espera al ESP32 para flashear por OTA")
    parser.add_argument("--fs", action="store_true", help="Subir LittleFS (uploadfs)")
    parser.add_argument("--both", action="store_true", help="Subir firmware y LittleFS")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", UDP_PORT))

    print(f"[OTA-WAIT] Esperando check-in del ESP32 en UDP:{UDP_PORT}...")

    while True:
        data, addr = sock.recvfrom(256)
        msg = data.decode("utf-8", errors="ignore").strip()
        if not ("OTA_CHECK" in msg):
            continue

        parts = msg.split()
        esp_ip = parts[1] if len(parts) > 1 else addr[0]
        print(f"[OTA-WAIT] ESP32 detectado en {esp_ip}! Enviando OTA_READY...")
        sock.sendto(b"OTA_READY", addr)

        targets = ["upload", "uploadfs"] if args.both else (["uploadfs"] if args.fs else ["upload"])
        for tgt in targets:
            cmd = ["pio", "run", "-e", "esp32-s3-paper-ota", "-t", tgt, "--upload-port", esp_ip]
            print(f"[OTA-WAIT] Ejecutando: {' '.join(cmd)}")
            res = subprocess.run(cmd, cwd=str(ROOT_DIR))
            if res.returncode != 0:
                print(f"[OTA-ERR] Fallo {tgt} (codigo {res.returncode}). Sigo escuchando...")
                break
        else:
            print("[OTA-OK] Flasheo OTA completado!")
            return 0


if __name__ == "__main__":
    sys.exit(main())
