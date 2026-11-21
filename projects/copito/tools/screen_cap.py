#!/usr/bin/env python3
"""Foto de la pantalla del llavero (emulación para el agente UI).

GET /api/screen -> hex 1-bit 200x200 -> PNG escalado en screens/.

Uso:
  python tools/screen_cap.py --name cuento_parte1 [--ip 192.168.0.176] [--dir screens] [--scale 3]

Layout de bits (igual que el driver): byte = (y*200+x)//8, bit MSB primero
(bit 7 = x%8==0), 1 = blanco, 0 = negro.
"""
import argparse
import json
import os
import urllib.request

from PIL import Image


def fetch_screen(ip):
    url = "http://%s/api/screen" % ip
    with urllib.request.urlopen(url, timeout=20) as resp:
        d = json.loads(resp.read().decode("utf-8"))
    assert d.get("success") and d.get("w") == 200 and d.get("h") == 200, d
    raw = bytes.fromhex(d["bits_hex"])
    assert len(raw) == 5000, len(raw)
    return raw


def to_image(raw, scale=3):
    img = Image.new("1", (200, 200), 1)
    px = img.load()
    for y in range(200):
        for x in range(200):
            byte = raw[(y * 200 + x) // 8]
            bit = 7 - (x % 8)
            px[x, y] = 1 if (byte >> bit) & 1 else 0
    if scale != 1:
        img = img.resize((200 * scale, 200 * scale), Image.NEAREST)
    return img


def main():
    ap = argparse.ArgumentParser(description="Captura la pantalla del llavero")
    ap.add_argument("--name", required=True, help="nombre base del PNG")
    ap.add_argument("--ip", default=os.environ.get("COPITO_IP", "192.168.0.176"))
    ap.add_argument("--dir", default="screens")
    ap.add_argument("--scale", type=int, default=3)
    args = ap.parse_args()
    os.makedirs(args.dir, exist_ok=True)
    raw = fetch_screen(args.ip)
    path = os.path.join(args.dir, args.name + ".png")
    to_image(raw, args.scale).save(path)
    print("guardada %s (%d bytes de framebuffer)" % (path, len(raw)))


if __name__ == "__main__":
    main()
