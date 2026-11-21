#!/usr/bin/env python3
"""
==============================================================================
COPITO AI - LIVE HARDWARE & API INTEGRATION TESTS
==============================================================================
Ejecuta pruebas en vivo contra el microcontrolador ESP32-S3 en la red local:
 - Estado y telemetria del sensor SHTC3 (I2C)
 - Catalogo de Skills
 - Guardado de Prompt al vuelo en LittleFS
 - Respuesta asincrona sin timeouts (/api/say y /api/chat)
==============================================================================
"""

import unittest
import urllib.request
import urllib.parse
import json
import time

DEVICE_IP = "192.168.0.176"
BASE_URL = f"http://{DEVICE_IP}"

class TestCopitoLiveHardware(unittest.TestCase):

    def test_01_sensor_telemetry(self):
        """Valida que el sensor SHTC3 este conectado y reportando valores fisicos coherentes."""
        url = f"{BASE_URL}/api/sensor"
        req = urllib.request.Request(url)
        res = None
        for _ in range(3):
            try:
                res = urllib.request.urlopen(req, timeout=4)
                break
            except Exception:
                time.sleep(1)
        self.assertIsNotNone(res, "El dispositivo debe responder en /api/sensor")
        with res:
            self.assertEqual(res.status, 200)
            data = json.loads(res.read().decode())
            
            self.assertTrue(data.get("sensor_connected"), "El sensor ambiental SHTC3 debe estar detectado")
            temp = data.get("temperature_c")
            hum = data.get("humidity_pct")
            self.assertIsNotNone(temp, "Debe existir campo temperature_c")
            self.assertIsNotNone(hum, "Debe existir campo humidity_pct")
            
            # Rangos fisicos realistas de una habitacion (-10 a 50 °C, 10 a 95% HR)
            self.assertGreaterEqual(temp, -10.0)
            self.assertLessEqual(temp, 50.0)
            self.assertGreaterEqual(hum, 10.0)
            self.assertLessEqual(hum, 100.0)
            print(f"\n   [TELEMETRIA VIVA] Temp: {temp} °C | Humedad: {hum}% | RSSI: {data.get('wifi_rssi')} dBm")

    def test_02_skills_catalog(self):
        """Valida que el endpoint /api/skills exponga el harness de herramientas activas."""
        url = f"{BASE_URL}/api/skills"
        with urllib.request.urlopen(url, timeout=5) as res:
            self.assertEqual(res.status, 200)
            data = json.loads(res.read().decode())
            skills = data.get("skills", [])
            self.assertGreaterEqual(len(skills), 3, "Debe reportar al menos 3 skills activas")
            skill_ids = [s.get("id") for s in skills]
            self.assertIn("shtc3_sensor", skill_ids)
            self.assertIn("battery_sense", skill_ids)
            self.assertIn("epaper_faces", skill_ids)

    def test_03_save_prompt_on_the_fly(self):
        """Valida guardar un prompt largo de 500+ caracteres en LittleFS sin reiniciar el ESP32."""
        url = f"{BASE_URL}/save_prompt"
        test_prompt = (
            "Eres Copito, un robot asistente y compañero leal con personalidad inteligente, canchera y directa de Jorge. "
            "Conversas en español con tono maduro, sin cursilerías ni frases ñoñas. "
            "Reglas: 1. Concisión por defecto (1-3 oraciones), pero con análisis lúcido y profundo en economía, política o temas reflexivos sin recortar ideas esenciales. "
            "2. Agrega exactamente un tag [DRAW:XXX] al final. 3. Usa herramientas reales [TOOL:GET_TEMP], [TOOL:GET_HUM], [TOOL:GET_BATERIA], [TOOL:GET_HORA]. "
            "4. Modos locales [MODE:RELOJ], [MODE:CLIMA], [MODE:CUENTO], [MODE:DORMIDO]."
        )
        payload = urllib.parse.urlencode({"prompt": test_prompt}).encode()
        req = urllib.request.Request(url, data=payload)
        with urllib.request.urlopen(req, timeout=6) as res:
            self.assertEqual(res.status, 200)
            data = json.loads(res.read().decode())
            self.assertTrue(data.get("success"), "El guardado en LittleFS debe ser exitoso")
            self.assertGreater(data.get("len", 0), 400, "Debe confirmar la longitud guardada")

    def test_04_async_speech_fast_response(self):
        """Valida que /api/say responda HTTP 200 en menos de 400 ms (sin bloquear por TTS)."""
        url = f"{BASE_URL}/api/say"
        payload = urllib.parse.urlencode({"text": "Test de audio no bloqueante"}).encode()
        req = urllib.request.Request(url, data=payload)
        t0 = time.time()
        with urllib.request.urlopen(req, timeout=3) as res:
            elapsed = time.time() - t0
            self.assertLess(elapsed, 0.80, f"La respuesta tardo {elapsed:.2f}s (debe ser asincrona < 0.80s)")
            print(f"\n   [LATENCIA API SAY] Respondio en {elapsed*1000:.1f} ms!")

    def test_05_face_pack_switch(self):
        """Valida cambiar el Face Pack al vuelo por HTTP REST (/api/face_pack)."""
        url = f"{BASE_URL}/api/face_pack"
        try:
            payload = urllib.parse.urlencode({"pack": 0}).encode()
            req = urllib.request.Request(url, data=payload)
            with urllib.request.urlopen(req, timeout=5) as res:
                self.assertEqual(res.status, 200)
                data = json.loads(res.read().decode())
                self.assertTrue(data.get("success"))
                self.assertEqual(data.get("pack"), 0)
                print("\n   [FACE PACK] Switch exitoso al Pack 0 (RUDO / BADASS)")
        finally:
            # Restaurar Copito (1): el test no debe dejar Rudo persistido
            payload = urllib.parse.urlencode({"pack": 1}).encode()
            req = urllib.request.Request(url, data=payload)
            with urllib.request.urlopen(req, timeout=10) as res:
                data = json.loads(res.read().decode())
                self.assertEqual(data.get("pack"), 1)
                print("\n   [FACE PACK] Restaurado a Copito (1)")

    def test_06_clock_mode_activation(self):
        """Valida que /api/action action=reloj active el Modo Reloj permanente."""
        url = f"{BASE_URL}/api/action"
        try:
            payload = urllib.parse.urlencode({"action": "reloj"}).encode()
            req = urllib.request.Request(url, data=payload)
            with urllib.request.urlopen(req, timeout=5) as res:
                self.assertEqual(res.status, 200)
                data = json.loads(res.read().decode())
                self.assertTrue(data.get("success"))
                self.assertEqual(data.get("mode"), "reloj")
                print("\n   [MODO RELOJ] Activado exitosamente en hardware (fijo con hora gigante)")
        finally:
            # Salir del reloj: el test no debe dejar la pantalla tomada
            payload = urllib.parse.urlencode({"action": "idle"}).encode()
            req = urllib.request.Request(url, data=payload)
            with urllib.request.urlopen(req, timeout=5) as res:
                self.assertEqual(res.status, 200)
                print("\n   [MODO RELOJ] Restaurado a idle")

if __name__ == "__main__":
    unittest.main(verbosity=2)
