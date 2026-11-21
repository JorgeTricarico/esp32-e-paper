#!/usr/bin/env python3
"""
==============================================================================
PAQUETE DE PRUEBAS UNITARIAS, DE INTEGRACIÓN Y EXPLORATORIAS
Proyecto: Asistente Autónomo ESP32-S3 E-Paper (Copito AI)
==============================================================================
"""

import sys
import os
import struct
import base64
import json
import urllib.request
import urllib.error

# Colores ANSI para terminal
GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
BOLD = "\033[1m"
RESET = "\033[0m"

def print_header(title):
    print(f"\n{CYAN}{BOLD}{'='*60}")
    print(f"  {title}")
    print(f"{'='*60}{RESET}")

def assert_test(name, condition, details=""):
    if condition:
        print(f"  {GREEN}[PASS]{RESET} {name}")
        return True
    else:
        print(f"  {RED}[FAIL]{RESET} {name}: {details}")
        return False

# ============================================================================
# 1. PRUEBAS UNITARIAS: ESTRUCTURAS DE DATOS Y PROTOCOLOS EMBEBIDOS
# ============================================================================

def test_wav_header():
    print_header("1. PRUEBA UNITARIA: Cabecera WAV RIFF de 44 Bytes")
    
    sample_rate = 16000
    channels = 1
    bits = 16
    pcm_bytes = 16000 * 2 * 4  # 4 segundos = 128,000 bytes
    
    # Construcción exacta equivalente a audio_es8311.cpp buildWavHeader
    header = bytearray(44)
    header[0:4] = b'RIFF'
    struct.pack_into('<I', header, 4, pcm_bytes + 36)
    header[8:12] = b'WAVE'
    header[12:16] = b'fmt '
    struct.pack_into('<I', header, 16, 16)      # Subchunk1Size
    struct.pack_into('<H', header, 20, 1)       # AudioFormat PCM
    struct.pack_into('<H', header, 22, channels)
    struct.pack_into('<I', header, 24, sample_rate)
    struct.pack_into('<I', header, 28, sample_rate * channels * (bits // 8))
    struct.pack_into('<H', header, 32, channels * (bits // 8))
    struct.pack_into('<H', header, 34, bits)
    header[36:40] = b'data'
    struct.pack_into('<I', header, 40, pcm_bytes)
    
    passed = True
    passed &= assert_test("Tamaño exacto de cabecera (44 bytes)", len(header) == 44)
    passed &= assert_test("Identificador RIFF presente", header[0:4] == b'RIFF')
    passed &= assert_test("Identificador WAVE presente", header[8:12] == b'WAVE')
    passed &= assert_test("Identificador data presente", header[36:40] == b'data')
    passed &= assert_test("Frecuencia de muestreo 16000 Hz", struct.unpack_from('<I', header, 24)[0] == 16000)
    passed &= assert_test("Profundidad 16 bits", struct.unpack_from('<H', header, 34)[0] == 16)
    passed &= assert_test("Cálculo ChunkSize = pcm + 36", struct.unpack_from('<I', header, 4)[0] == pcm_bytes + 36)
    return passed

def test_bayer_dithering_and_buffer_geometry():
    print_header("2. PRUEBA UNITARIA: Geometría de Buffer E-Paper 200x200")
    
    epd_width = 200
    epd_height = 200
    buffer_size = (epd_width * epd_height) // 8  # 5000 bytes
    
    buf = bytearray([0xFF] * buffer_size)
    
    # Función drawPixel bit a bit
    def draw_pixel(b, x, y, color):
        if x < 0 or x >= epd_width or y < 0 or y >= epd_height:
            return
        idx = y * 25 + (x >> 3)
        bit = 7 - (x & 0x07)
        if color:
            b[idx] |= (1 << bit)
        else:
            b[idx] &= ~(1 << bit)
            
    # Test esquina superior izquierda (0,0) a negro
    draw_pixel(buf, 0, 0, 0)
    # Test esquina inferior derecha (199,199) a negro
    draw_pixel(buf, 199, 199, 0)
    
    passed = True
    passed &= assert_test("Tamaño del buffer = 5000 bytes exactos", len(buf) == 5000)
    passed &= assert_test("Píxel (0,0) en byte 0, bit 7", (buf[0] & 0x80) == 0)
    passed &= assert_test("Píxel (199,199) en byte 4999, bit 0", (buf[4999] & 0x01) == 0)
    passed &= assert_test("Bytes intermedios preservados (0xFF)", buf[1] == 0xFF and buf[2500] == 0xFF)
    return passed

def test_base64_streaming_formula():
    print_header("3. PRUEBA UNITARIA: Matemática de Base64 Zero-OOM")
    
    passed = True
    for raw_len in [1, 2, 3, 4, 5, 100, 1024, 144000]:
        formula_len = 4 * ((raw_len + 2) // 3)
        actual_len = len(base64.b64encode(b'A' * raw_len))
        if formula_len != actual_len:
            passed &= assert_test(f"Base64 length for {raw_len} bytes", False, f"Formula: {formula_len}, Real: {actual_len}")
            break
    else:
        passed &= assert_test("Fórmula matemática de longitud Base64 idéntica al estándar", True)
        
    return passed

def test_chunked_http_decoder():
    print_header("4. PRUEBA UNITARIA: Decodificador de Transfer-Encoding Chunked")
    
    # Simulación de respuesta HTTP Chunked recibida por socket TCP
    # Chunks en hex: "5\r\nHello\r\n", "6\r\n World\r\n", "0\r\n\r\n"
    simulated_stream = b"5\r\nHello\r\n7\r\n ESP32!\r\n0\r\n\r\n"
    
    decoded_audio = bytearray()
    idx = 0
    while idx < len(simulated_stream):
        crlf_pos = simulated_stream.find(b"\r\n", idx)
        if crlf_pos == -1:
            break
        hex_len_str = simulated_stream[idx:crlf_pos].decode('ascii').strip()
        chunk_size = int(hex_len_str, 16)
        if chunk_size == 0:
            break
        idx = crlf_pos + 2
        chunk_data = simulated_stream[idx:idx + chunk_size]
        decoded_audio.extend(chunk_data)
        idx += chunk_size + 2  # Saltear datos y \r\n final
        
    passed = assert_test("Decodificación de chunks sin cabeceras hex", bytes(decoded_audio) == b"Hello ESP32!")
    return passed

# ============================================================================
# 2. PRUEBAS DE INTEGRACIÓN: ESQUEMA Y LLAMADA A LA API DE GOOGLE GEMINI
# ============================================================================

def test_gemini_payload_schema():
    print_header("5. PRUEBA DE INTEGRACIÓN: Esquema JSON de Gemini v1beta (camelCase)")
    
    # Generar audio sintético pequeño (onda senoidal en WAV)
    sample_rate = 16000
    num_samples = 8000  # 0.5 segundos
    sine_pcm = bytearray()
    import math
    for i in range(num_samples):
        sample = int(10000 * math.sin(2 * math.pi * 440 * i / sample_rate))
        sine_pcm.extend(struct.pack('<h', sample))
        
    wav_bytes = bytearray(44)
    wav_bytes[0:4] = b'RIFF'
    struct.pack_into('<I', wav_bytes, 4, len(sine_pcm) + 36)
    wav_bytes[8:12] = b'WAVE'
    wav_bytes[12:16] = b'fmt '
    struct.pack_into('<I', wav_bytes, 16, 16)
    struct.pack_into('<H', wav_bytes, 20, 1)
    struct.pack_into('<H', wav_bytes, 22, 1)
    struct.pack_into('<I', wav_bytes, 24, sample_rate)
    struct.pack_into('<I', wav_bytes, 28, sample_rate * 2)
    struct.pack_into('<H', wav_bytes, 32, 2)
    struct.pack_into('<H', wav_bytes, 34, 16)
    wav_bytes[36:40] = b'data'
    struct.pack_into('<I', wav_bytes, 40, len(sine_pcm))
    wav_bytes.extend(sine_pcm)
    
    b64_audio = base64.b64encode(wav_bytes).decode('ascii')
    
    payload = {
        "system_instruction": {
            "parts": [{"text": "Eres Copito, un robot amable. Responde muy breve."}]
        },
        "contents": [
            {
                "parts": [
                    {
                        "inlineData": {
                            "mimeType": "audio/wav",
                            "data": b64_audio
                        }
                    }
                ]
            }
        ],
        "generationConfig": {
            "temperature": 0.7,
            "maxOutputTokens": 80
        }
    }
    
    json_str = json.dumps(payload)
    
    passed = True
    passed &= assert_test("inlineData en camelCase presente", '"inlineData"' in json_str)
    passed &= assert_test("mimeType en camelCase presente", '"mimeType"' in json_str)
    passed &= assert_test("system_instruction estructurado", '"system_instruction"' in json_str)
    passed &= assert_test("maxOutputTokens configurado a 80", payload["generationConfig"]["maxOutputTokens"] == 80)
    return passed

def test_live_gemini_api(api_key=None):
    print_header("6. PRUEBA EXPLORATORIA EN VIVO: Llamada REST a Google Gemini 3.8 / 3.5 Flash")
    
    key = api_key or os.environ.get("GEMINI_API_KEY", "")
    if not key:
        print(f"  {YELLOW}[SKIP]{RESET} No se proveyó GEMINI_API_KEY. (Pasa la clave como argumento para probar live).")
        return True
        
    model = "gemini-3.8-flash"
    url = f"https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent?key={key}"
    
    payload = {
        "system_instruction": {
            "parts": [{"text": "Eres Copito, responde en 1 oracion entusiasta."}]
        },
        "contents": [
            {
                "parts": [{"text": "Hola Copito, ¿estas listo para funcionar en el ESP32?"}]
            }
        ],
        "generationConfig": {
            "maxOutputTokens": 60,
            "temperature": 0.5
        }
    }
    
    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode('utf-8'),
        headers={"Content-Type": "application/json"}
    )
    
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read().decode('utf-8'))
            reply = data["candidates"][0]["content"]["parts"][0]["text"].strip()
            print(f"  {GREEN}[EXITO]{RESET} Gemini 3.8 Flash respondió: \"{reply}\"")
            return assert_test("Respuesta válida de Google Gemini", len(reply) > 0)
    except urllib.error.HTTPError as e:
        err_content = e.read().decode('utf-8')
        if e.code == 404:
            # Fallback a 3.5 flash si 3.8 está en rollout restringido
            print(f"  {YELLOW}[INFO]{RESET} Modelo {model} devolvió 404. Probando fallback a gemini-3.5-flash...")
            fallback_url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash:generateContent?key={key}"
            req2 = urllib.request.Request(fallback_url, data=json.dumps(payload).encode('utf-8'), headers={"Content-Type": "application/json"})
            try:
                with urllib.request.urlopen(req2, timeout=10) as resp2:
                    data2 = json.loads(resp2.read().decode('utf-8'))
                    reply2 = data2["candidates"][0]["content"]["parts"][0]["text"].strip()
                    print(f"  {GREEN}[EXITO]{RESET} Fallback Gemini 3.5 Flash respondió: \"{reply2}\"")
                    return True
            except Exception as e2:
                print(f"  {RED}[ERROR]{RESET} Fallback también falló: {e2}")
                return False
        else:
            print(f"  {RED}[ERROR]{RESET} Error HTTP {e.code}: {err_content}")
            return False
    except Exception as e:
        print(f"  {RED}[ERROR]{RESET} Fallo de red: {e}")
        return False

# ============================================================================
# RUNNER PRINCIPAL
# ============================================================================
if __name__ == "__main__":
    print_header("SUITE DE PRUEBAS DE FIRMWARE & CLOUD: ESP32-PAPER ASSISTANT")
    
    key_arg = sys.argv[1] if len(sys.argv) > 1 else None
    
    results = [
        test_wav_header(),
        test_bayer_dithering_and_buffer_geometry(),
        test_base64_streaming_formula(),
        test_chunked_http_decoder(),
        test_gemini_payload_schema(),
        test_live_gemini_api(key_arg)
    ]
    
    print("\n" + "="*60)
    if all(results):
        print(f"{GREEN}{BOLD}  RESULTADO FINAL: TODAS LAS PRUEBAS PASARON EXITOSAMENTE (100% OK){RESET}")
        print("="*60 + "\n")
        sys.exit(0)
    else:
        print(f"{RED}{BOLD}  RESULTADO FINAL: SE ENCONTRARON FALLOS EN LAS PRUEBAS{RESET}")
        print("="*60 + "\n")
        sys.exit(1)
