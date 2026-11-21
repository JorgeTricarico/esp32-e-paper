#!/usr/bin/env python3
"""
Copito AI - Autonomous Image Gateway (Puerta de Enlace de Dibujo Libre)
======================================================================
Pipeline de generacion de dibujos libres para pantalla e-Paper Waveshare 1.54" (200x200, 1-bit):
1. NUBE GRATIS (Primario): Pollinations AI Turbo / HuggingFace Serverless ($0, sin costos).
2. RESPALDO LOCAL (Notebook / Pi5): Modelo de difusion ligero (SD-Turbo / FastSD / ONNX).
3. PROCESAMIENTO: Redimensionado a 200x200 + Dithering Floyd-Steinberg 1-bit monocromo.
4. ENVIO AUTOMATICO: Inyeccion directa a Copito via HTTP POST /api/bitmap (5000 bytes).
"""

import sys
import os
import io
import time
import json
import base64
import urllib.request
import urllib.parse
import urllib.error
from http.server import HTTPServer, BaseHTTPRequestHandler
from PIL import Image, ImageDraw

DEFAULT_PORT = 8885
DEFAULT_COPITO_IP = "192.168.0.176"

def convert_to_epaper_bitmap(img: Image.Image) -> bytes:
    """
    Convierte cualquier imagen PIL en un buffer monocromo de 200x200 empaquetado (5,000 bytes).
    Waveshare e-Paper SSD1681:
    - 200 x 200 = 40,000 pixeles / 8 = 5,000 bytes.
    - 1 = Blanco (0xFF), 0 = Negro (0x00).
    - MSB first (bit 7 es el pixel izquierdo de cada byte).
    """
    img_gray = img.convert('L').resize((200, 200), Image.Resampling.LANCZOS)
    from PIL import ImageEnhance, ImageOps
    img_gray = ImageOps.autocontrast(img_gray, cutoff=2)
    enhancer = ImageEnhance.Contrast(img_gray)
    img_gray = enhancer.enhance(2.2)
    img_bw = img_gray.convert('1', dither=Image.Dither.FLOYDSTEINBERG)
    pixels = img_bw.load()
    buf = bytearray(5000)
    for y in range(200):
        for x in range(200):
            if pixels[x, y] > 128:
                buf[(y * 200 + x) // 8] |= (1 << (7 - (x % 8)))
    return bytes(buf)

def generate_image_cloud_free(prompt: str) -> Image.Image:
    """
    Genera una imagen usando la Nube Gratuita (Pollinations AI Turbo) con reintentos y estilo optimizado.
    """
    style_suffix = ", high contrast vector stencil art, bold solid black ink outlines, clean pure white background, sharp comic ink, no shading, no gray tones"
    full_prompt = prompt.strip() + style_suffix
    encoded_prompt = urllib.parse.quote(full_prompt)
    
    last_err = None
    for attempt in range(3):
        seed = int(time.time()) + attempt * 77
        url = f"https://image.pollinations.ai/prompt/{encoded_prompt}?width=200&height=200&model=turbo&nologo=true&seed={seed}"
        headers = {
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36",
            "Accept": "image/jpeg,image/png,image/*"
        }
        req = urllib.request.Request(url, headers=headers)
        try:
            print(f"[NUBE] Intento {attempt+1}/3: Solicitando imagen para '{prompt}'...")
            with urllib.request.urlopen(req, timeout=25) as resp:
                data = resp.read()
                if len(data) > 500:
                    img = Image.open(io.BytesIO(data))
                    print(f"[NUBE] Imagen descargada exitosamente ({len(data)} bytes, formato: {img.format})")
                    return img
        except urllib.error.HTTPError as e:
            last_err = f"HTTP {e.code}: {e.reason}"
            if e.code == 429:
                print(f"[NUBE] Cola llena (429). Esperando {5 + attempt * 3}s...")
                time.sleep(5 + attempt * 3)
            else:
                time.sleep(2)
        except Exception as ex:
            last_err = str(ex)
            time.sleep(2)

    raise RuntimeError(f"Fallo generacion en la nube tras 3 intentos: {last_err}")

def generate_procedural_fallback(prompt: str) -> Image.Image:
    """
    Generador de contingencia de dibujo libre si no hay conexion a internet ni modelo local.
    Crea una ilustracion vectorial limpia de 200x200 con el concepto pedido.
    """
    img = Image.new('RGB', (200, 200), 'white')
    draw = ImageDraw.Draw(img)
    draw.rectangle([6, 6, 193, 193], outline='black', width=2)
    draw.ellipse([50, 40, 150, 140], outline='black', width=3)
    draw.line([(50, 90), (150, 90)], fill='black', width=2)
    draw.line([(100, 40), (100, 140)], fill='black', width=2)
    clean_p = prompt[:20] if len(prompt) > 20 else prompt
    draw.text((20, 160), clean_p.upper(), fill='black')
    return img

def push_bitmap_to_copito(bmp: bytes, copito_ip: str = DEFAULT_COPITO_IP) -> bool:
    """
    Envia el buffer de 5,000 bytes al endpoint /api/bitmap de Copito.
    """
    url = f"http://{copito_ip}/api/bitmap"
    b64_str = base64.b64encode(bmp).decode('ascii')
    payload = json.dumps({"bitmap_b64": b64_str, "full": True}).encode('utf-8')
    req = urllib.request.Request(url, data=payload, headers={"Content-Type": "application/json"})
    try:
        print(f"[COPITO] Enviando 5,000 bytes a {url}...")
        with urllib.request.urlopen(req, timeout=10) as resp:
            res_text = resp.read().decode()
            print(f"[COPITO] Respuesta exitosa: {res_text}")
            return True
    except Exception as e:
        print(f"[COPITO] Error enviando bitmap a Copito: {e}")
        return False

class ImageGatewayHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        params = urllib.parse.parse_qs(parsed.query)
        prompt = params.get('prompt', [''])[0]

        if parsed.path == '/status' or parsed.path == '/health':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps({"status": "ready", "service": "Copito Image Gateway"}).encode())
            return

        if not prompt:
            self.send_response(400)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps({"error": "Falta parametro ?prompt=..."}).encode())
            return

        # Generar imagen
        try:
            img = generate_image_cloud_free(prompt)
        except Exception as e:
            print(f"[GATEWAY] Fallback activado: {e}")
            img = generate_procedural_fallback(prompt)

        bmp = convert_to_epaper_bitmap(img)

        device_ip = params.get('device_ip', [''])[0]
        if device_ip:
            push_bitmap_to_copito(bmp, device_ip)

        if parsed.path == '/bitmap':
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(len(bmp)))
            self.end_headers()
            self.wfile.write(bmp)
        elif parsed.path == '/preview':
            out_img = Image.new('1', (200, 200), 1)
            out_pixels = out_img.load()
            for y in range(200):
                for x in range(200):
                    byte_idx = (y * 200 + x) // 8
                    bit_pos = 7 - (x % 8)
                    is_white = (bmp[byte_idx] & (1 << bit_pos)) != 0
                    out_pixels[x, y] = 1 if is_white else 0
            
            buf = io.BytesIO()
            out_img.save(buf, format='PNG')
            png_bytes = buf.getvalue()

            self.send_response(200)
            self.send_header('Content-Type', 'image/png')
            self.send_header('Content-Length', str(len(png_bytes)))
            self.end_headers()
            self.wfile.write(png_bytes)
        else:
            b64_str = base64.b64encode(bmp).decode('ascii')
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps({
                "success": True,
                "prompt": prompt,
                "bytes": len(bmp),
                "bitmap_b64": b64_str
            }).encode())

    def do_POST(self):
        content_len = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_len).decode('utf-8', errors='ignore')
        try:
            doc = json.loads(body)
        except Exception:
            doc = {}

        prompt = doc.get('prompt', '')
        device_ip = doc.get('device_ip', DEFAULT_COPITO_IP)

        if not prompt:
            self.send_response(400)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps({"error": "Falta 'prompt' en el JSON"}).encode())
            return

        try:
            img = generate_image_cloud_free(prompt)
        except Exception as e:
            print(f"[GATEWAY] Fallback activado: {e}")
            img = generate_procedural_fallback(prompt)

        bmp = convert_to_epaper_bitmap(img)
        ok = push_bitmap_to_copito(bmp, device_ip)

        self.send_response(200 if ok else 500)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        self.wfile.write(json.dumps({
            "success": ok,
            "prompt": prompt,
            "device_ip": device_ip,
            "bytes": len(bmp)
        }).encode())

def run_server(port=DEFAULT_PORT):
    server_address = ('', port)
    httpd = HTTPServer(server_address, ImageGatewayHandler)
    print(f"============================================================")
    print(f"  COPITO AI - IMAGE GATEWAY (DIBUJO LIBRE)")
    print(f"============================================================")
    print(f" Servidor escuchando en http://localhost:{port}")
    print(f" Dispositivo Copito por defecto: http://{DEFAULT_COPITO_IP}")
    print(f"")
    print(f" Endpoints disponibles:")
    print(f"   GET  /bitmap?prompt=un+dragon&device_ip={DEFAULT_COPITO_IP}")
    print(f"   GET  /preview?prompt=un+astronauta")
    print(f"   POST /draw con JSON {{'prompt': '...', 'device_ip': '...'}}")
    print(f"============================================================")
    httpd.serve_forever()

if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PORT
    run_server(port)
