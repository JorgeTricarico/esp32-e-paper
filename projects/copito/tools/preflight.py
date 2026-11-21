#!/usr/bin/env python3
"""Pre-vuelo antes de flashear a Copito (push /update o staged pull-OTA).

2026-09-09: un push con musica sonando brickio el equipo (ping vivo,
HTTP/serie/botones muertos). Este script es la validacion del lado PC:
  1. Corre las pruebas unitarias (deben estar 100% verdes).
  2. Lee /status del equipo: debe responder, estar idle (busy=false) y con
     bateria sana (o cargando). Si esta ocupado -> NO-GO (esperar o parar
     la musica/voz primero).
  3. Compara el MD5 local del .bin con el fw corriendo (si es igual, no hay
     nada que flashear).

Uso:
  python tools/preflight.py --bin .pio/build/esp32-s3-paper/firmware.bin [--ip 192.168.0.176]

Sale con codigo 0 (GO) o 1 (NO-GO) y explica el motivo.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import urllib.request

LOW_BATT_PCT = 15


def md5_of(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def run_unit_tests():
    r = subprocess.run([sys.executable, "tests/test_core_logic.py"],
                       capture_output=True, text=True, timeout=300)
    tail = (r.stdout + r.stderr).strip().splitlines()
    ok = (r.returncode == 0)
    return ok, "; ".join(tail[-3:])


def get_status(ip):
    url = "http://%s/status" % ip
    req = urllib.request.Request(url)
    with urllib.request.urlopen(req, timeout=12) as resp:
        return json.loads(resp.read().decode("utf-8"))


def main():
    ap = argparse.ArgumentParser(description="Pre-vuelo de flash para Copito")
    ap.add_argument("--bin", required=True, help="firmware .bin a flashear")
    ap.add_argument("--ip", default=os.environ.get("COPITO_IP", "192.168.0.176"),
                    help="IP del equipo (o env COPITO_IP)")
    args = ap.parse_args()

    problems = []

    # 1. Pruebas unitarias
    print("[1/3] pruebas unitarias...")
    tests_ok, tests_tail = run_unit_tests()
    print("      %s" % tests_tail)
    if not tests_ok:
        problems.append("pruebas unitarias en rojo")

    # 2. MD5 local
    try:
        local_md5 = md5_of(args.bin)
        print("[2/3] MD5 local: %s" % local_md5)
    except OSError as e:
        print("NO-GO: no se puede leer el .bin: %s" % e)
        return 1

    # 3. Estado del equipo
    print("[3/3] estado del equipo (%s)..." % args.ip)
    try:
        st = get_status(args.ip)
    except Exception as e:
        problems.append("equipo no responde /status (%s)" % e)
        st = None
    if st is not None:
        print("      fw=%s boot=%s uptime=%ss vol=%s face=%s" % (
            st.get("fw_md5"), st.get("boot_count"), st.get("uptime_s"),
            st.get("volume"), st.get("face_pack")))
        if st.get("fw_md5", "").lower() == local_md5.lower():
            print("      el equipo ya corre este firmware: nada que flashear")
            return 0
        if st.get("busy"):
            problems.append("equipo ocupado: %s (parar musica/voz y reintentar)"
                            % st.get("busy_reason", "?"))
        batt = st.get("battery_pct", st.get("batt_pct", None))
        charging = st.get("charging", st.get("is_charging", None))
        if batt is not None:
            print("      bateria: %s%% cargando=%s" % (batt, charging))
            if int(batt) <= LOW_BATT_PCT and not charging:
                problems.append("bateria baja (%s%%) sin cargador" % batt)
        else:
            print("      bateria: sin dato en /status (verificar a ojo)")

    if problems:
        print("NO-GO:")
        for p in problems:
            print("  - %s" % p)
        return 1
    print("GO: pruebas verdes, equipo idle, firmware distinto. A flashear.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
