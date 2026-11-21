#!/usr/bin/env python3
"""Proxy del funnel para el llavero nomade (Pi3, sin root, por cron).

El llavero fuera de casa no ve la LAN: este proxy, servido por el funnel
existente de Tailscale, le publica los servicios del Pi5 con UNA clave
compartida (cabecera X-Copito-Key). Sin la clave -> 403. Sin clave
configurada (falta ~/.config/copito/funnel_key) -> abierto (modo LAN).

Rutas (el funnel mapea /copito/ -> 127.0.0.1:8899):
   POST /copito/tts            -> Pi5:8881 /tts (Piper, WAV)
   GET  /copito/story?topic=.. -> Pi5:8890 /story (JSON cuento v1)
   GET  /copito/story/img/..   -> Pi5:8890 (bitmap 1-bit 5000 B)
   GET/POST /copito/book?..    -> Pi5:8890 /book (JSON libro v2)
   GET  /copito/book/img|jpg|audio|layout/..
                               -> Pi5:8890 (assets del libro v2)
   GET  /copito/demo           -> Pi5:8890 demo web de cuentos
   GET  /copito/health         -> estado propio + Pi5

El back del otro lado ahora vive en el proyecto hermano Cuentos-Backend
(Documents/Github/Cuentos-Backend). Contrato congelado con el firmware:
Cuentos-Backend/CONTRACT_ESP32.md — estos paths no se renombran.

Seguridad:
  - TLS lo termina tailscaled (funnel publico, cert valido).
  - X-Copito-Key obligatoria si hay clave configurada (403 si no).
  - Rate-limit simple por IP (120 req/min) + tope de body 1 MB.
  - Solo estas rutas existen; nada mas se proxea (no es proxy abierto).

Arranque (sin sudo): cron @reboot + arranque manual. Ver README.
Solo stdlib.
"""

import os
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(os.environ.get("COPITO_PROXY_PORT", "8899"))
PI5 = os.environ.get("COPITO_PI5", "100.66.50.66")
KEY_FILE = os.path.expanduser("~/.config/copito/funnel_key")
RATE_MAX = 120
RATE_WIN = 60.0
BODY_MAX = 1024 * 1024


def load_key():
    try:
        with open(KEY_FILE) as f:
            return f.read().strip()
    except OSError:
        return ""


KEY = load_key()
_hits = {}
_hits_lock = threading.Lock()


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def rate_ok(ip):
    now = time.time()
    with _hits_lock:
        lst = [t for t in _hits.get(ip, []) if now - t < RATE_WIN]
        if len(lst) >= RATE_MAX:
            _hits[ip] = lst
            return False
        lst.append(now)
        _hits[ip] = lst
        return True


def forward(method, path, headers, body):
    target = "http://%s:8881%s" % (PI5, path) if path.startswith("/tts") \
        else "http://%s:8890%s" % (PI5, path)
    req = urllib.request.Request(target, data=body, method=method)
    ctype = headers.get("Content-Type")
    if ctype:
        req.add_header("Content-Type", ctype)
    # /story y /book generan en Ollama y estiran (minutos); el resto es
    # voz/imagen/estático y responde rápido.
    _to = 240 if ("/story" in path or "/book" in path) else 90
    try:
        with urllib.request.urlopen(req, timeout=_to) as resp:
            return resp.status, dict(resp.getheaders()), resp.read(BODY_MAX + 1)
    except Exception as e:
        code = getattr(e, "code", None)
        if isinstance(code, int):
            try:
                return code, {"Content-Type": "application/json"}, e.read()[:500]
            except Exception:
                return code, {"Content-Type": "application/json"}, b"{}"
        return 502, {"Content-Type": "application/json"}, b'{"error":"pi5 sin respuesta"}'


class Handler(BaseHTTPRequestHandler):
    server_version = "CopitoFunnel/1.0"

    def _reply(self, code, ctype, body):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _json(self, code, obj):
        import json
        self._reply(code, "application/json", json.dumps(obj))

    def _serve(self):
        ip = self.client_address[0]
        if not rate_ok(ip):
            self._json(429, {"error": "rate limit"})
            return
        if KEY and self.headers.get("X-Copito-Key", "") != KEY:
            self._json(403, {"error": "falta clave"})
            return
        # /copito/xxx -> /xxx (el funnel ya pelo el prefijo? no: llega completo)
        path = self.path
        if path.startswith("/copito/"):
            path = path[len("/copito"):]
        length = int(self.headers.get("Content-Length") or 0)
        if length > BODY_MAX:
            self._json(413, {"error": "body muy grande"})
            return
        body = self.rfile.read(length) if length > 0 else None
        if self.command == "GET" and (path == "/health" or path.startswith("/health")):
            self._json(200, {"ok": True, "key": bool(KEY), "pi5": PI5})
            return
        if path.startswith("/tts") or path.startswith("/story") \
                or path.startswith("/book") or path.startswith("/demo"):
            code, hdrs, data = forward(self.command, path,
                                       {"Content-Type": self.headers.get("Content-Type", "")},
                                       body)
            ctype = "application/octet-stream"
            for k, v in hdrs.items():
                if k.lower() == "content-type":
                    ctype = v
                    break
            self._reply(code, ctype, data)
            return
        self._json(404, {"error": "nada aca"})

    def do_GET(self):
        self._serve()

    def do_POST(self):
        self._serve()

    def log_message(self, fmt, *args):
        log("http", fmt % args)


def main():
    log("CopitoFunnel proxy en 127.0.0.1:%d -> Pi5 %s (clave %s)" %
        (PORT, PI5, "configurada" if KEY else "ABIERTA"))
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()


if __name__ == "__main__":
    main()
