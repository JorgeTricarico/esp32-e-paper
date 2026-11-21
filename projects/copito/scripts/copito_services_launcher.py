#!/usr/bin/env python3
"""
Copito AI - Resilient Background Services Supervisor & Watchdog
=============================================================
Inicia y mantiene activos de manera permanente los servicios locales de Copito:
1. Copito Image Gateway (Puerto 8885): Generacion libre (nube gratis/local) y dithering e-paper.
2. Copito Dashboard Server (Puerto 8080): Monitor web y simulador.

Modo Watchdog:
- Monitorea los puertos cada 10 segundos.
- Si algun servicio se cae o falla, lo reinicia de forma automatica y transparente.
- Registra la salida en logs/image_gateway.log y logs/dashboard.log.
"""

import os
import sys
import time
import socket
import subprocess

BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
LOG_DIR = os.path.join(BASE_DIR, 'logs')
os.makedirs(LOG_DIR, exist_ok=True)

# Prevenir crash cuando se ejecuta con pythonw.exe (sys.stdout/stderr son None en Windows)
if sys.stdout is None:
    try:
        sys.stdout = open(os.path.join(LOG_DIR, "supervisor.log"), "a", encoding="utf-8", buffering=1)
    except Exception:
        sys.stdout = open(os.devnull, "w")
if sys.stderr is None:
    try:
        sys.stderr = open(os.path.join(LOG_DIR, "supervisor.log"), "a", encoding="utf-8", buffering=1)
    except Exception:
        sys.stderr = open(os.devnull, "w")

PYTHON_EXE = sys.executable
if os.name == 'nt':
    scripts_dir = os.path.dirname(sys.executable)
    py_candidate = os.path.join(scripts_dir, "python.exe")
    if os.path.exists(py_candidate):
        PYTHON_EXE = py_candidate

SERVICES = [
    {
        "name": "Copito Image Gateway",
        "script": os.path.join(BASE_DIR, "tools", "image_gateway.py"),
        "port": 8885,
        "log": os.path.join(LOG_DIR, "image_gateway.log"),
        "proc": None,
        "cwd": BASE_DIR
    },
    {
        "name": "Copito Dashboard Server",
        "script": os.path.join(BASE_DIR, "tools", "dashboard_server.py"),
        "port": 8080,
        "log": os.path.join(LOG_DIR, "dashboard.log"),
        "proc": None,
        "cwd": BASE_DIR
    }
]

KOKORO_SCRIPT = r"C:\Users\admin\copito-server\kokoro_server.py"
if os.path.exists(KOKORO_SCRIPT):
    SERVICES.append({
        "name": "Copito Kokoro TTS",
        "script": KOKORO_SCRIPT,
        "port": 8880,
        "log": os.path.join(LOG_DIR, "kokoro.log"),
        "proc": None,
        "cwd": r"C:\Users\admin\copito-server"
    })

CUENTOS_SCRIPT = r"C:\Users\admin\Documents\Github\Cuentos-Backend\main.py"
if os.path.exists(CUENTOS_SCRIPT):
    SERVICES.append({
        "name": "Copito Cuentos Backend",
        "script": CUENTOS_SCRIPT,
        "port": 8890,
        "log": os.path.join(LOG_DIR, "cuentos_backend.log"),
        "proc": None,
        "cwd": r"C:\Users\admin\Documents\Github\Cuentos-Backend"
    })

def is_port_in_use(port):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(1.0)
        return s.connect_ex(('127.0.0.1', port)) == 0

def check_and_start_service(svc):
    port = svc["port"]
    name = svc["name"]
    
    if is_port_in_use(port):
        return True

    # Si el proceso ya fue lanzado y aún sigue corriendo (ej. cargando modelos pesados), no lanzar otro
    if svc.get("proc") is not None and svc["proc"].poll() is None:
        return True

    print(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] [+] Iniciando {name} en puerto {port}...")
    try:
        out_f = open(svc["log"], "a", encoding="utf-8")
        flags = 0
        startupinfo = None
        if os.name == 'nt':
            flags = subprocess.CREATE_NO_WINDOW
            startupinfo = subprocess.STARTUPINFO()
            startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startupinfo.wShowWindow = 0  # SW_HIDE

        svc_cwd = svc.get("cwd", BASE_DIR)
        proc = subprocess.Popen(
            [PYTHON_EXE, svc["script"]],
            cwd=svc_cwd,
            stdout=out_f,
            stderr=subprocess.STDOUT,
            creationflags=flags,
            startupinfo=startupinfo
        )
        svc["proc"] = proc
        # Esperar 2.0s para verificar inicio
        time.sleep(2.0)
        if is_port_in_use(port):
            print(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] [OK] {name} iniciado exitosamente en puerto {port}.")
            return True
        else:
            print(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] [WARN] {name} iniciado (PID: {proc.pid}), esperando puerto...")
            return True
    except Exception as e:
        print(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] [ERROR] No se pudo iniciar {name}: {e}")
        return False

def run_supervisor_loop(watchdog=True):
    print("=" * 60)
    print("  COPITO AI - SUPERVISOR DE SERVICIOS RESISTENTE")
    print("=" * 60)
    print(f"Directorio base: {BASE_DIR}")
    print(f"Python: {PYTHON_EXE}")
    print(f"Modo Watchdog: {'Activo (auto-recuperacion continua)' if watchdog else 'Unico paso'}")
    print("=" * 60)

    for svc in SERVICES:
        check_and_start_service(svc)

    if not watchdog:
        return

    # Bucle centinela permanente
    while True:
        try:
            time.sleep(10)
            for svc in SERVICES:
                if not is_port_in_use(svc["port"]):
                    # Si el proceso sigue vivo y aún no abrió el puerto, darle tiempo (ej. cargando modelos)
                    if svc.get("proc") is not None and svc["proc"].poll() is None:
                        continue
                    print(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] [WATCHDOG] Puerto {svc['port']} no responde. Reiniciando {svc['name']}...")
                    check_and_start_service(svc)
        except KeyboardInterrupt:
            print("\n[!] Deteniendo supervisor a solicitud del usuario...")
            break
        except Exception as ex:
            print(f"[!] Error en bucle watchdog: {ex}")
            time.sleep(5)

if __name__ == '__main__':
    is_daemon = "--daemon" in sys.argv or "-d" in sys.argv or True
    run_supervisor_loop(watchdog=is_daemon)
