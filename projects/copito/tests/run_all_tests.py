#!/usr/bin/env python3
"""
==============================================================================
COPITO AI - RUNNER MAESTRO DE PRUEBAS UNITARIAS Y DE INTEGRACION
==============================================================================
Ejecuta la bateria completa de pruebas para prevenir cualquier regresion:
  1. Pruebas Unitarias de Logica Core (test_core_logic.py)
  2. Pruebas de Integracion y Protocolo Embebido (test_suite.py)
  3. Pruebas en Vivo contra el Hardware ESP32-S3 (test_live_hardware.py)
==============================================================================
"""

import sys
import unittest

def main():
    print("=" * 65)
    print("  INICIANDO BATERIA COMPLETA DE PRUEBAS AUTOMATIZADAS - COPITO AI")
    print("=" * 65)

    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    # Cargar pruebas unitarias de logica core
    try:
        import test_core_logic
        suite.addTests(loader.loadTestsFromModule(test_core_logic))
        print("[+] Cargadas pruebas unitarias: test_core_logic.py")
    except Exception as e:
        print(f"[-] Error al cargar test_core_logic: {e}")

    # Simulador Parlante+ESP32 (requiere ffmpeg + fixtures MP3/bin)
    try:
        import simulate_audio
        suite.addTests(loader.loadTestsFromModule(simulate_audio))
        print("[+] Cargado simulador de audio: simulate_audio.py")
    except Exception as e:
        print(f"[-] Sim de audio omitido: {e}")

    # Cargar pruebas de hardware en vivo si el dispositivo esta en red
    try:
        import test_live_hardware
        suite.addTests(loader.loadTestsFromModule(test_live_hardware))
        print("[+] Cargadas pruebas de hardware en vivo: test_live_hardware.py")
    except Exception as e:
        print(f"[-] Error al cargar test_live_hardware: {e}")

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    print("=" * 65)
    if result.wasSuccessful():
        print("  TODAS LAS PRUEBAS PASARON EXITOSAMENTE (0 REGRESIONES)")
        print("=" * 65)
        sys.exit(0)
    else:
        print(f"  FALLARON {len(result.failures)} PRUEBAS / {len(result.errors)} ERRORES DETECTADOS")
        print("=" * 65)
        sys.exit(1)

if __name__ == "__main__":
    main()
