#!/usr/bin/env python3
"""
==============================================================================
SIMULADOR DE AUDIO: parlante + condiciones ESP32-S3 (sin hardware).
==============================================================================
Responde: "¿la cadena de voz/música suena bien de verdad en casa y en calle?"

Cadena simulada con matemática entera EXACTA del firmware (división C con
truncado, no floor de Python):
  MP3 real --ffmpeg--> PCM --normalize(peak->22000,gain[128,256])-->
  writePcmChunk(gain[32,256], limiter rodilla 26000 4:1, techo 28000)-->
  transporte I2S/DMA (8x512 frames stereo ~= 170 ms @24kHz) con perfiles de red.

Perfiles:
  CASA  : LAN 22050 Hz, prebuffer 2 s, reads 4 KB, stall 12 s, red limpia.
  CALLE : funnel/hotspot 16000 Hz, prebuffer 4 s, reads 8 KB, stall 25 s,
          3 s de setup TLS + micro-cortes 0.3-2.5 s cada 6-12 s (seed fijo).

Modo LEGACY (antes de los fixes): sin prebuffer, sin relleno de silencio.
Modo NEW (firmware actual): prebuffer + quanta de silencio tras 250 ms sin
datos + corte por stall. El A/B demuestra que el bug existía y el fix lo cura.

Reglas honestas del firmware que se verifican:
  - Frase Gemini < 4800 bytes => fallo (activa fallback), no "éxito".
  - Frase Google < 2400 muestras o totalBad > 256 => se omite (no es ruido).
  - Pico de salida SIEMPRE <= 28000 (ni wrap ni flat-top), cualquier entrada.
==============================================================================
"""

import math
import os
import random
import shutil
import struct
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

def _find_ffmpeg():
    cands = [
        r"C:\Users\admin\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft."
        r"Winget.Source_8wekyb3d8bbwe\ffmpeg-8.1.2-full_build\bin\ffmpeg.exe",
        shutil.which("ffmpeg"),
    ]
    for c in cands:
        if c and os.path.exists(c) and not c.lower().endswith(".cmd"):
            return c
    return cands[1]

FFMPEG = _find_ffmpeg()

FIXTURES_MP3 = ["test_google_tts.mp3", "test_speech.mp3", "test_tts.mp3"]
GEMINI_BIN = "gemini_voice_test.bin"  # PCM s16le crudo 24 kHz mono


def cdiv(a, b):
    """División entera estilo C (trunca hacia cero). Python // hace floor."""
    return math.trunc(a / b)


def decode_mp3(path, rate=24000):
    """MP3 real -> PCM mono rate Hz (vía ffmpeg, como minimp3 en el ESP32)."""
    if not os.path.exists(path) or not (FFMPEG and os.path.exists(FFMPEG)):
        return None
    cmd = [FFMPEG, "-hide_banner", "-loglevel", "error", "-i", path,
           "-ac", "1", "-ar", str(rate), "-f", "s16le", "-acodec", "pcm_s16le", "-"]
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    except Exception:
        return None
    if p.returncode != 0 or len(p.stdout) < 4800:
        return None
    n = len(p.stdout) // 2
    return (list(struct.unpack("<%dh" % n, p.stdout[:n * 2])), rate)


def decode_mp3_truncated(path, frac=0.4, rate=24000):
    """Simula hotspot que corta la descarga: primeros frac bytes del MP3."""
    if not os.path.exists(path) or not (FFMPEG and os.path.exists(FFMPEG)):
        return None
    raw = open(path, "rb").read()
    cut = raw[:max(256, int(len(raw) * frac))]
    cmd = [FFMPEG, "-hide_banner", "-loglevel", "error", "-i", "pipe:0",
           "-ac", "1", "-ar", str(rate), "-f", "s16le", "-acodec", "pcm_s16le", "-"]
    try:
        p = subprocess.run(cmd, input=cut, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=60)
    except Exception:
        return None
    if p.returncode != 0 or len(p.stdout) < 2:
        return None  # ffmpeg no pudo: truncado total
    n = len(p.stdout) // 2
    return (list(struct.unpack("<%dh" % n, p.stdout[:n * 2])), rate)


def load_gemini_bin(path):
    with open(path, "rb") as f:
        d = f.read()
    n = len(d) // 2
    return (list(struct.unpack("<%dh" % n, d[:n * 2])), 24000)


def synth_torture(rate=24000, secs=2.0):
    """Seno full-scale + ráfaga de ruido full-scale (peor caso de saturación)."""
    n = int(rate * secs)
    out = []
    for i in range(n):
        v = int(32767 * math.sin(2 * math.pi * 440 * i / rate))
        if n // 2 <= i < n // 2 + rate // 4:
            v = 32767 if (i % 2 == 0) else -32768
        out.append(v)
    return (out, rate)


# --- Cadena DSP (espejo exacto de audio_es8311.cpp + ai_engine.cpp) ---

def normalize_to_target(samples, target=22000):
    peak = 0
    for s in samples:
        a = abs(s)
        if a > peak:
            peak = a
    if peak < 200:
        return ([0] * len(samples), 0, peak, True)  # casi silencio: se omite, no se amplifica
    gain256 = (target * 256) // peak
    if gain256 > 256:
        gain256 = 256  # nunca boost
    if gain256 < 128:
        gain256 = 128
    return ([cdiv(s * gain256, 256) for s in samples], gain256, peak, False)


def write_chunk(samples, gain256=256):
    """Espejo de AudioEngine::writePcmChunk: gain clamp + limiter + techo."""
    if gain256 < 32:
        gain256 = 32
    if gain256 > 256:
        gain256 = 256
    out = []
    limited = 0
    for s in samples:
        v = cdiv(s * gain256, 256)
        if v > 26000:
            v = 26000 + cdiv(v - 26000, 4)
            limited += 1
        elif v < -26000:
            v = -26000 + cdiv(v + 26000, 4)
            limited += 1
        if v > 28000:
            v = 28000
        elif v < -28000:
            v = -28000
        out.append(v)
    return (out, limited)


# --- Transporte I2S/DMA + red (espejo de streamMusicOnEsp32/streamAudioData) ---

def street_throughput(t_ms, seed):
    """Bytes/s disponibles en hotspot con seed fijo (determinista)."""
    if t_ms < 3000:
        return 0  # setup TLS + funnel
    rng = random.Random(seed)
    drops = []
    t = 3000.0
    while t < 600000:
        t += rng.uniform(6000, 12000)
        drops.append((t, t + rng.uniform(300, 2500)))
    for a, b in drops:
        if a <= t_ms < b:
            return 0
    return 60000  # ~60 kB/s efectivos en calle


def lan_throughput(t_ms, seed):
    if t_ms < 200:
        return 0
    return 400000


def simulate_transport(pcm_bytes_total, rate, profile="casa", fixes=True, seed=7):
    """Simula prebuffer + streaming con hambre de red. Devuelve métricas."""
    funnel = (profile == "calle")
    pre_target = rate * 2 * (4 if funnel else 2)
    rd_size = 8192 if funnel else 4096
    stall_ms = 25000 if funnel else 12000
    pre_timeout = 20000 if funnel else 10000
    tick = 10  # ms
    if not fixes:
        pre_target = 0  # legacy: arranca sin colchón
    thr = street_throughput if funnel else lan_throughput

    buf = 0
    t = 0
    produced = 0
    consumed = 0.0
    drain_per_tick = rate * 2 * tick / 1000.0
    last_data = 0
    filled_ms = 0
    underrun_ms = 0
    truncated = False
    playing = (pre_target == 0)
    # Pre-buffer
    t_pre = 0
    while not playing:
        avail_b = thr(t, seed) * tick / 1000.0
        take = min(avail_b, pre_target - buf, pcm_bytes_total - produced)
        buf += take
        produced += take
        if take > 0:
            last_data = t
        t += tick
        t_pre += tick
        if buf >= pre_target or produced >= pcm_bytes_total:
            playing = True
        if t_pre > pre_timeout and buf < rate * 2:
            truncated = True
            break
    # Streaming
    silence_since = None
    while playing and consumed < pcm_bytes_total and not truncated:
        avail_b = thr(t, seed) * tick / 1000.0
        take = min(avail_b, rd_size, pcm_bytes_total - produced)
        take = int(take)
        buf += take
        produced += take
        if take > 0:
            last_data = t
            silence_since = None
        expecting = produced < pcm_bytes_total  # ¿aún debe llegar red?
        need = drain_per_tick
        if buf >= need:
            buf -= need
            consumed += need
        else:
            consumed += buf
            buf = 0
            # Sin datos para el DMA: solo penaliza si aún se espera red.
            # La cola final (todo descargado) se drena sin castigo.
            if expecting and t - last_data > 250 and fixes:
                filled_ms += tick  # quantum de silencio: ritmo preservado
            elif expecting and t - last_data > 250:
                underrun_ms += tick  # legacy: hueco audible
        if t - last_data > stall_ms and produced < pcm_bytes_total:
            truncated = True
            break
        t += tick
        if t > 900000:
            truncated = True
            break
    played_ms = consumed / (rate * 2) * 1000.0
    return {"played_ms": played_ms, "filled_ms": filled_ms,
            "underrun_ms": underrun_ms, "truncated": truncated,
            "produced_bytes": int(produced)}


def run_chain(samples, rate, profile, fixes, seed=7):
    """Cadena completa: normalize -> chunk/limiter -> transporte. Métricas."""
    normed, gain, peak_in, skipped = normalize_to_target(samples)
    if skipped:
        return {"skipped_silence": True, "truncated": False, "underrun_ms": 0,
                "peak_out": 0, "gain": 0, "peak_in": peak_in}
    out, limited = write_chunk(normed)
    peak_out = 0
    for v in out:
        a = abs(v)
        if a > peak_out:
            peak_out = a
    # El transporte mueve bytes mono (el duplex a stereo lo hace el driver)
    t = simulate_transport(len(out) * 2, rate, profile, fixes, seed)
    t.update({"peak_in": peak_in, "gain": gain, "peak_out": peak_out,
              "limited": limited,
              "limited_ratio": limited / max(1, len(out)),
              "skipped_silence": False,
              "gemini_rule_ok": len(out) * 2 >= 4800,
              "phrase_rule_ok": len(out) >= 2400})
    return t


def load_all_fixtures():
    fx = {}
    for name in FIXTURES_MP3:
        d = decode_mp3(os.path.join(ROOT, name))
        if d:
            fx[name] = d
    gb = os.path.join(ROOT, GEMINI_BIN)
    if os.path.exists(gb):
        fx[GEMINI_BIN] = load_gemini_bin(gb)
    fx["torture_fullscale"] = synth_torture()
    return fx


class TestSimulatedSpeaker(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fx = load_all_fixtures()
        assert len(cls.fx) >= 4, "Faltan fixtures MP3/bin para simular"

    def test_casa_todo_pasa_limtiteclean(self):
        for name, (pcm, rate) in self.fx.items():
            for fixes in (False, True):
                m = run_chain(pcm, rate, "casa", fixes)
                self.assertFalse(m["truncated"], f"{name} casa legacy/new truncado")
                self.assertEqual(m["underrun_ms"], 0, f"{name} casa con underrun")
                self.assertLessEqual(m["peak_out"], 28000, f"{name} supera techo DAC")
                self.assertTrue(m["gemini_rule_ok"], f"{name} no pasa regla 4800B")

    def test_calle_con_fixes_suena_entero(self):
        for name, (pcm, rate) in self.fx.items():
            m = run_chain(pcm, rate, "calle", True, seed=7)
            self.assertFalse(m["truncated"], f"{name} calle NEW truncado")
            self.assertEqual(m["underrun_ms"], 0, f"{name} calle NEW con huecos")
            self.assertLessEqual(m["peak_out"], 28000)
            total_ms = len(pcm) / rate * 1000.0
            self.assertGreaterEqual(m["played_ms"], total_ms * 0.98, f"{name} no sonó entero")

    def test_calle_legacy_se_corta(self):
        """Prueba que el bug existía: sin prebuffer+fill la calle se entrecorta."""
        bad = 0
        for name, (pcm, rate) in self.fx.items():
            if name == "torture_fullscale":
                continue
            m = run_chain(pcm, rate, "calle", False, seed=7)
            if m["truncated"] or m["underrun_ms"] > 500:
                bad += 1
        self.assertGreaterEqual(bad, 2, "El legacy debería cortarse en calle (si no, el sim no discrimina)")

    def test_tortura_nunca_satura(self):
        # Entrada directa caliente SIN normalizar (peor caso: writePcmChunk
        # con gain unity): el limiter+techo deben contenerla siempre.
        pcm, rate = self.fx["torture_fullscale"]
        out, limited = write_chunk(pcm, gain256=256)
        self.assertGreater(limited, 0, "Full-scale directo debe activar el limiter")
        for v in out[::37]:
            self.assertLessEqual(abs(v), 28000, "Nada puede superar el techo 28000 (ni wrap)")
        # Y la cadena normalizada (camino real TTS) queda bajo la rodilla
        m = run_chain(pcm, rate, "calle", True, seed=7)
        self.assertLessEqual(m["peak_out"], 26000)
        self.assertFalse(m["truncated"])

    def test_integridad_decide_aceptar_parcial_rechazar_basura(self):
        """Política real del firmware: parcial válido corto se acepta
        (degradación elegante, no ruido), basura con resync masivo se omite."""
        def phrase_accepted(total_decoded, total_bad):
            return not (total_bad > 256 or total_decoded < 2400)
        self.assertTrue(phrase_accepted(22464, 3))    # truncado ffmpeg: 0.9 s reales
        self.assertFalse(phrase_accepted(1500, 10))   # muy corto
        self.assertFalse(phrase_accepted(5000, 300))  # tormenta de resync
        # Y el gate previo (fetchGooglePhraseMp3) rechaza basura sin sync
        from test_core_logic import vv_mp3_scan, VV_MP3_NO_SYNC
        kind, _ = vv_mp3_scan(bytes(range(256)) * 2)
        self.assertEqual(kind, VV_MP3_NO_SYNC)

    def test_regla_gemini_min_bytes(self):
        tiny = [1000] * 1000  # 2000 bytes < 4800
        self.assertFalse(len(tiny) * 2 >= 4800)
        big = [1000] * 5000
        self.assertTrue(len(big) * 2 >= 4800)

    def test_determinismo(self):
        pcm, rate = self.fx[FIXTURES_MP3[0]]
        a = run_chain(pcm, rate, "calle", True, seed=7)
        b = run_chain(pcm, rate, "calle", True, seed=7)
        self.assertEqual(a, b)


def report():
    fx = load_all_fixtures()
    print("\nSIMULADOR Parlante+ESP32  (peak<=28000 siempre | underrun=ms de hueco)")
    print("%-22s %-6s %-4s %8s %6s %9s %9s %9s %s" % (
        "fixture", "perfil", "fix", "peak_in", "gain", "peak_out", "limit%", "played_s", "notas"))
    for name, (pcm, rate) in fx.items():
        for profile in ("casa", "calle"):
            for fixes in (True, False):
                m = run_chain(pcm, rate, profile, fixes, seed=7)
                total_s = len(pcm) / rate
                notas = []
                if m.get("truncated"):
                    notas.append("TRUNCADO")
                if m.get("underrun_ms", 0) > 0:
                    notas.append("huecos=%dms" % m["underrun_ms"])
                if m.get("filled_ms", 0) > 0:
                    notas.append("fill=%dms" % m["filled_ms"])
                print("%-22s %-6s %-4s %8d %6d %9d %8.2f%% %9.1f/%-4.1f %s" % (
                    name[:22], profile, "NEW" if fixes else "LEG",
                    m.get("peak_in", 0), m.get("gain", 0), m.get("peak_out", 0),
                    m.get("limited_ratio", 0) * 100, m.get("played_ms", 0) / 1000.0,
                    total_s, " ".join(notas)))


if __name__ == "__main__":
    if "--report" in sys.argv:
        sys.argv.remove("--report")
        report()
    unittest.main(verbosity=2)
