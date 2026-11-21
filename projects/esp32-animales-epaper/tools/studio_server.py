#!/usr/bin/env python3
"""
Servidor multi-hilo para el Studio Interactivo (tools/studio.html).
Sirve los audios de data/ y sintetiza bajo demanda múltiples fuentes de voz
(Google Translate TTS, Piper Local H/M, Windows SAPI5, SpanishDict) con locks
por clave y escritura atomica.
"""

from __future__ import annotations

import http.server
import json
import math
import os
import re
import struct
import subprocess
import sys
import threading
import urllib.parse
import urllib.request
import uuid
import wave
import webbrowser
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent
CACHE_DIR = ROOT_DIR / "tools" / "voice_cache"
PORT = 8765

sys.path.insert(0, str(ROOT_DIR / "tools"))
import prepare_letters as pl  # noqa: E402

_PIPER_LOCK = threading.Lock()
_PIPER_CACHE: dict[str, object] = {}
_KEY_LOCKS_GUARD = threading.Lock()
_KEY_LOCKS: dict[tuple, threading.Lock] = {}

ALLOWED_VOICES = {
    "gtts_es_419": "Google TTS (Latino / Mujer, voz única humana)",
    "gtts_es_es": "Google TTS (España / Mujer, voz única humana)",
    "es_MX-claude-high": "Piper Local: Claude (México / Mujer, voz única)",
    "es_AR-daniela-high": "Piper Local: Daniela (Argentina / Mujer, voz única)",
    "es_MX-ald-medium": "Piper Local: Ald (México / Hombre, voz única)",
    "es_ES-davefx-medium": "Piper Local: Davefx (España / Hombre, voz única)",
    "sapi_helena": "Windows SAPI5: Microsoft Helena (España / Mujer)",
    "spanishdict": "SpanishDict (Referencia: mezcla locutores H/M en origen)",
}

ALLOWED_STYLES = {
    "natural": "Natural (tal cual)",
    "puntuado": "Puntuado (con punto final para claridad)",
    "letra_prefijo": "Con prefijo ('Letra A' / palabra normal)",
}


def get_key_lock(cache_key: tuple) -> threading.Lock:
    with _KEY_LOCKS_GUARD:
        if cache_key not in _KEY_LOCKS:
            _KEY_LOCKS[cache_key] = threading.Lock()
        return _KEY_LOCKS[cache_key]


def get_piper(model_name: str):
    with _PIPER_LOCK:
        if model_name not in _PIPER_CACHE:
            from piper import PiperVoice
            onnx_path = ROOT_DIR / "tools" / "voices" / f"{model_name}.onnx"
            if not onnx_path.exists():
                raise FileNotFoundError(f"Falta modelo ONNX: {onnx_path.name}")
            _PIPER_CACHE[model_name] = PiperVoice.load(str(onnx_path))
        return _PIPER_CACHE[model_name]


def _normalize_and_write_wav(raw_16k_wav: Path, dst_wav: Path) -> None:
    with wave.open(str(raw_16k_wav), "rb") as wf:
        raw = wf.readframes(wf.getnframes())
    samples = list(struct.unpack(f"<{len(raw) // 2}h", raw))
    peak = max((abs(s) for s in samples), default=0)
    if peak > 0:
        scale = pl.TARGET_PEAK / float(peak)
        samples = [max(-32768, min(32767, int(s * scale))) for s in samples]
    dst_wav.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(dst_wav), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(pl.SAMPLE_RATE_OUT)
        wf.writeframes(struct.pack(f"<{len(samples)}h", *samples))


def _postprocess_ffmpeg(ffmpeg: str, src_path: Path, dst_wav: Path, atempo: float = 1.0) -> None:
    tmp_16k = dst_wav.with_suffix(f".{uuid.uuid4().hex[:6]}.16k.wav")
    filters = [
        "silenceremove=start_periods=1:start_silence=0.01:start_threshold=-58dB",
    ]
    if abs(atempo - 1.0) >= 0.03:
        # ffmpeg atempo soporta 0.5..2.0
        t = max(0.5, min(2.0, atempo))
        filters.append(f"atempo={t:.2f}")
    filters.append("acompressor=threshold=-24dB:ratio=6:attack=4:release=60:makeup=8dB")
    filters.append("apad=pad_dur=0.06")

    cmd = [
        ffmpeg, "-y", "-i", str(src_path),
        "-af", ",".join(filters),
        "-ar", str(pl.SAMPLE_RATE_OUT), "-ac", "1", "-sample_fmt", "s16",
        str(tmp_16k),
    ]
    try:
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        _normalize_and_write_wav(tmp_16k, dst_wav)
    finally:
        tmp_16k.unlink(missing_ok=True)


def _synth_gtts(tl: str, text: str, ffmpeg: str, dst_wav: Path, tempo: float) -> None:
    url = (
        "https://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob"
        f"&tl={urllib.parse.quote(tl)}&q={urllib.parse.quote(text)}"
    )
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    tmp_mp3 = dst_wav.with_suffix(f".{uuid.uuid4().hex[:6]}.mp3")
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            tmp_mp3.write_bytes(r.read())
        _postprocess_ffmpeg(ffmpeg, tmp_mp3, dst_wav, atempo=tempo)
    finally:
        tmp_mp3.unlink(missing_ok=True)


def _synth_sapi(text: str, ffmpeg: str, dst_wav: Path, tempo: float) -> None:
    tmp_raw = dst_wav.with_suffix(f".{uuid.uuid4().hex[:6]}.sapi.wav")
    # SAPI Rate va de -10 a 10; 0 es normal (~1.0x)
    sapi_rate = max(-6, min(6, int(round((tempo - 1.0) * 10))))
    safe_text = text.replace("'", "''")
    ps_script = (
        "Add-Type -AssemblyName System.Speech; "
        "$s = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
        "$v = $s.GetInstalledVoices() | Where-Object { $_.VoiceInfo.Culture.Name -like 'es*' } | Select-Object -First 1; "
        "if ($v) { $s.SelectVoice($v.VoiceInfo.Name) }; "
        f"$s.Rate = {sapi_rate}; "
        f"$s.SetOutputToWaveFile('{str(tmp_raw)}'); "
        f"$s.Speak('{safe_text}'); "
        "$s.Dispose()"
    )
    try:
        subprocess.run(["powershell", "-NoProfile", "-Command", ps_script],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        _postprocess_ffmpeg(ffmpeg, tmp_raw, dst_wav, atempo=1.0)
    finally:
        tmp_raw.unlink(missing_ok=True)


def build_manifest() -> dict:
    gfx_path = ROOT_DIR / "src" / "assets" / "animals_gfx.h"
    text = gfx_path.read_text(encoding="utf-8")
    table_m = re.search(r"static const AnimalAsset ANIMALS\[ANIMAL_COUNT\] = \{(.*?)\};", text, re.S)
    names_map = dict(pl.NAME_ITEMS)
    animals = []
    if table_m:
        pattern = re.compile(
            r'\{\s*"(\w+)",\s*gfx_\w+_a,\s*gfx_\w+_b,\s*gfx_\w+_name,\s*"(/sounds/\w+\.wav)",\s*(\d+),\s*\{([^}]+)\},\s*\{([^}]+)\},\s*"([^"]+)"\s*\}'
        )
        for m in pattern.finditer(table_m.group(1)):
            aid, audio_path, lcount, lx_str, lw_str, lkeys = m.groups()
            cnt = int(lcount)
            lx = [int(x.strip()) for x in lx_str.split(",")][:cnt]
            lw = [int(x.strip()) for x in lw_str.split(",")][:cnt]
            animals.append({
                "id": aid,
                "name": names_map.get(aid, aid).upper(),
                "sound": f"/data{audio_path}",
                "sprite_a": f"/assets/custom_sprites/{aid}_a.png",
                "sprite_b": f"/assets/custom_sprites/{aid}_b.png",
                "letter_keys": list(lkeys),
                "letter_x": lx,
                "letter_w": lw,
            })

    voices = []
    for vid, label in ALLOWED_VOICES.items():
        if vid.startswith("gtts_") or vid in ("spanishdict", "sapi_helena") or (ROOT_DIR / "tools" / "voices" / f"{vid}.onnx").exists():
            voices.append({"id": vid, "label": label})

    styles = [{"id": sid, "label": label} for sid, label in ALLOWED_STYLES.items()]
    return {"animals": animals, "voices": voices, "styles": styles}


def render_or_get_voice_wav(voice_id: str, kind: str, key: str, tempo: float, style: str = "natural") -> Path:
    if voice_id not in ALLOWED_VOICES:
        raise ValueError(f"Voz no permitida: {voice_id}")
    if kind not in ("letter", "name"):
        raise ValueError(f"Tipo invalido: {kind}")
    if style not in ALLOWED_STYLES:
        style = "natural"
    if not re.fullmatch(r"[a-z0-9_]+", key):
        raise ValueError(f"Clave invalida: {key}")

    tempo = max(0.55, min(1.50, tempo))
    tempo_int = round(tempo * 100)

    if voice_id == "spanishdict":
        p = ROOT_DIR / "data" / ("letters" if kind == "letter" else "names") / f"{key}.wav"
        if not p.exists():
            raise FileNotFoundError(f"No existe {p.name}")
        return p

    out_dir = CACHE_DIR / voice_id / kind
    out_dir.mkdir(parents=True, exist_ok=True)
    dst = out_dir / f"{key}_{tempo_int}_{style}.wav"

    lock = get_key_lock((voice_id, kind, key, tempo_int, style))
    with lock:
        if dst.exists() and dst.stat().st_size > 1000:
            return dst

        items = dict(pl.LETTER_ITEMS if kind == "letter" else pl.NAME_ITEMS)
        if key not in items:
            raise FileNotFoundError(f"Clave desconocida: {key}")
        spoken = items[key]
        if kind == "letter":
            if style == "puntuado":
                spoken = f"{spoken}."
            elif style == "letra_prefijo":
                spoken = f"Letra {spoken}"
        elif style == "puntuado":
            spoken = f"{spoken}."

        ffmpeg = pl.find_real_ffmpeg()
        if not ffmpeg:
            raise RuntimeError("No se encontro ffmpeg en el sistema")

        tmp_dst = out_dir / f".tmp_{key}_{uuid.uuid4().hex[:8]}.wav"
        try:
            if voice_id == "gtts_es_419":
                _synth_gtts("es-419", spoken, ffmpeg, tmp_dst, tempo)
            elif voice_id == "gtts_es_es":
                _synth_gtts("es-ES", spoken, ffmpeg, tmp_dst, tempo)
            elif voice_id == "sapi_helena":
                _synth_sapi(spoken, ffmpeg, tmp_dst, tempo)
            else:
                voice = get_piper(voice_id)
                ls = max(0.75, min(1.85, 1.05 / tempo))
                pl.synthesize_piper(voice, ffmpeg, spoken, tmp_dst, length_scale=ls)
            os.replace(tmp_dst, dst)
        finally:
            for leftover in out_dir.glob(f".tmp_{key}_*"):
                leftover.unlink(missing_ok=True)
        return dst


class StudioHandler(http.server.SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT_DIR), **kwargs)

    def send_json(self, code: int, obj: dict) -> None:
        payload = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        try:
            if parsed.path == "/favicon.ico":
                self.send_response(204)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return

            if parsed.path in ("/", "/index.html"):
                self.path = "/tools/studio.html"
                return super().do_GET()

            if parsed.path == "/api/manifest":
                self.send_json(200, build_manifest())
                return

            if parsed.path == "/api/audio":
                qs = urllib.parse.parse_qs(parsed.query)
                voice_id = qs.get("voice", ["gtts_es_419"])[0]
                kind = qs.get("kind", ["letter"])[0]
                key = qs.get("key", ["a"])[0]
                tempo = float(qs.get("tempo", ["0.85"])[0])
                style = qs.get("style", ["natural"])[0]
                wav_path = render_or_get_voice_wav(voice_id, kind, key, tempo, style)
                data = wav_path.read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", "audio/wav")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
                return

            if parsed.path.startswith(("/tools/studio.html", "/assets/custom_sprites/", "/data/")):
                return super().do_GET()

            self.send_json(404, {"error": "Ruta no permitida"})
        except ValueError as e:
            self.send_json(400, {"error": str(e)})
        except FileNotFoundError as e:
            self.send_json(404, {"error": str(e)})
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        except Exception as e:
            self.send_json(500, {"error": str(e)})

    def log_message(self, format, *args):
        if len(args) >= 2 and str(args[1]).startswith(("4", "5")):
            super().log_message(format, *args)


class ThreadingStudioServer(http.server.ThreadingHTTPServer):
    allow_reuse_address = True
    daemon_threads = True


def main() -> int:
    try:
        with ThreadingStudioServer(("127.0.0.1", PORT), StudioHandler) as httpd:
            url = f"http://127.0.0.1:{PORT}"
            print(f"[STUDIO] Abierto en {url}", flush=True)
            webbrowser.open(url)
            httpd.serve_forever()
    except KeyboardInterrupt:
        return 0
    except OSError as e:
        print(f"[STUDIO-ERR] Puerto {PORT} ocupado o error de red: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
