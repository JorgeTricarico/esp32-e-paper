#!/usr/bin/env python3
import os
import sys
import json
import subprocess
import webbrowser
from http.server import HTTPServer, SimpleHTTPRequestHandler
import urllib.parse

PORT = 8080
BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
CONFIG_PATH = os.path.join(BASE_DIR, 'copito_config.json')
CONFIG_H_PATH = os.path.join(BASE_DIR, 'include', 'config.h')
DASHBOARD_HTML = os.path.join(BASE_DIR, 'tools', 'dashboard.html')

def sync_config_h(cfg):
    try:
        with open(CONFIG_H_PATH, 'r', encoding='utf-8') as f:
            lines = f.readlines()

        new_lines = []
        for line in lines:
            if '#define DEFAULT_WIFI_SSID1' in line:
                new_lines.append(f'#define DEFAULT_WIFI_SSID1      "{cfg["wifi"]["primary"]["ssid"]}"\n')
            elif '#define DEFAULT_WIFI_PASS1' in line:
                new_lines.append(f'#define DEFAULT_WIFI_PASS1      "{cfg["wifi"]["primary"]["pass"]}"\n')
            elif '#define DEFAULT_WIFI_SSID2' in line:
                new_lines.append(f'#define DEFAULT_WIFI_SSID2      "{cfg["wifi"]["secondary"]["ssid"]}"\n')
            elif '#define DEFAULT_WIFI_PASS2' in line:
                new_lines.append(f'#define DEFAULT_WIFI_PASS2      "{cfg["wifi"]["secondary"]["pass"]}"\n')
            elif '#define DEFAULT_GEMINI_KEY' in line:
                new_lines.append(f'#define DEFAULT_GEMINI_KEY      "{cfg["ai"]["api_key"]}"\n')
            elif '#define GEMINI_DEFAULT_MODEL' in line:
                new_lines.append(f'#define GEMINI_DEFAULT_MODEL    "{cfg["ai"]["default_model"]}"\n')
            elif '#define GEMINI_FALLBACK_MODEL' in line:
                new_lines.append(f'#define GEMINI_FALLBACK_MODEL   "{cfg["ai"]["fallback_model"]}"\n')
            elif '#define DEFAULT_SYSTEM_PROMPT' in line:
                p = cfg["ai"]["system_prompt"].replace('"', '\"')
                new_lines.append(f'#define DEFAULT_SYSTEM_PROMPT    "{p}"\n')
            else:
                new_lines.append(line)

        with open(CONFIG_H_PATH, 'w', encoding='utf-8') as f:
            f.writelines(new_lines)
        return True, "include/config.h sincronizado correctamente."
    except Exception as e:
        return False, str(e)

class DashboardHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(200)
        self.end_headers()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == '/' or parsed.path == '/dashboard' or parsed.path == '/index.html':
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.end_headers()
            with open(DASHBOARD_HTML, 'rb') as f:
                self.wfile.write(f.read())
            return
        elif parsed.path == '/api/config':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.end_headers()
            if os.path.exists(CONFIG_PATH):
                with open(CONFIG_PATH, 'rb') as f:
                    self.wfile.write(f.read())
            else:
                self.wfile.write(b'{}')
            return
        elif parsed.path == '/api/status':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.end_headers()
            status = {
                'server': 'online',
                'config_path': CONFIG_PATH,
                'config_h_path': CONFIG_H_PATH,
                'com_port': 'COM3'
            }
            self.wfile.write(json.dumps(status).encode('utf-8'))
            return
        super().do_GET()

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        content_length = int(self.headers.get('Content-Length', 0))
        post_data = self.rfile.read(content_length)

        if parsed.path == '/api/config':
            try:
                cfg = json.loads(post_data.decode('utf-8'))
                with open(CONFIG_PATH, 'w', encoding='utf-8') as f:
                    json.dump(cfg, f, indent=2, ensure_ascii=False)
                ok, msg = sync_config_h(cfg)
                self.send_response(200)
                self.send_header('Content-Type', 'application/json; charset=utf-8')
                self.end_headers()
                self.wfile.write(json.dumps({'success': True, 'msg': msg}).encode('utf-8'))
            except Exception as e:
                self.send_response(500)
                self.send_header('Content-Type', 'application/json; charset=utf-8')
                self.end_headers()
                self.wfile.write(json.dumps({'success': False, 'error': str(e)}).encode('utf-8'))
            return

        elif parsed.path == '/api/flash':
            try:
                cmd = 'pio run -e esp32-s3-paper -t upload --upload-port COM3'
                proc = subprocess.Popen(cmd, cwd=BASE_DIR, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, shell=True)
                out, _ = proc.communicate(timeout=180)
                success = (proc.returncode == 0)
                self.send_response(200)
                self.send_header('Content-Type', 'application/json; charset=utf-8')
                self.end_headers()
                self.wfile.write(json.dumps({'success': success, 'output': out[-3000:] if out else ''}).encode('utf-8'))
            except Exception as e:
                self.send_response(500)
                self.send_header('Content-Type', 'application/json; charset=utf-8')
                self.end_headers()
                self.wfile.write(json.dumps({'success': False, 'error': str(e)}).encode('utf-8'))
            return

        self.send_response(404)
        self.end_headers()

if __name__ == '__main__':
    print(f'Iniciando Servidor del Tablero de Control en http://localhost:{PORT}...')
    server = HTTPServer(('0.0.0.0', PORT), DashboardHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print('\nServidor detenido.')
