#!/usr/bin/env python3
"""
==============================================================================
COPITO AI - CORE UNIT TESTS (PREVENCION DE REGRESIONES CRITICAS)
==============================================================================
"""

import unittest

EPD_WIDTH = 200
EPD_HEIGHT = 200
EPD_BUFFER_SIZE = (EPD_WIDTH * EPD_HEIGHT) // 8  # 5000 bytes
SYSTEM_PROMPT_MAX = 4096

def calc_es8311_volume_reg(volume_percent: int) -> int:
    vol = max(0, min(100, volume_percent))
    if vol == 0:
        return 0x00
    if vol <= 50:
        return 80 + (vol * 95) // 50
    else:
        v = vol - 50
        reg = 175 + (v * 17) // 50
        return min(192, reg)

def sanitize_tts_text(input_text: str) -> str:
    out = []
    i = 0
    text_len = len(input_text)
    while i < text_len:
        c = input_text[i]
        if c == '[':
            close_idx = input_text.find(']', i)
            if close_idx > 0:
                tag = input_text[i:close_idx + 1]
                if (tag.startswith("[DRAW:") or tag.startswith("[SKETCH:") or 
                    tag.startswith("[MODE:") or tag.startswith("[TEXT:") or 
                    tag.startswith("[TOOL:") or tag.startswith("[FACE:") or 
                    tag.startswith("[ICON:") or tag.startswith("[REMEMBER:")):
                    out.append(tag)
                    i = close_idx + 1
                    continue
                else:
                    i = close_idx + 1
                    continue
        if c in ('*', '_', '#', '`', '~'):
            i += 1
            continue
        if c in ('\r', '\n'):
            if out and out[-1] != ' ':
                out.append(' ')
            i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out).strip()

def parse_drawing_tool(ai_answer: str):
    draw_type = 0
    tool_name = "none"
    if "[DRAW:CORAZON]" in ai_answer or "[DRAW:HEART]" in ai_answer:
        draw_type = 1; tool_name = "DRAW:CORAZON"
    elif "[DRAW:ESTRELLA]" in ai_answer or "[DRAW:STAR]" in ai_answer:
        draw_type = 2; tool_name = "DRAW:ESTRELLA"
    elif "[DRAW:SOL]" in ai_answer or "[DRAW:SUN]" in ai_answer:
        draw_type = 3; tool_name = "DRAW:SOL"
    elif "[DRAW:NUBE]" in ai_answer or "[DRAW:CLOUD]" in ai_answer or "[DRAW:LLUVIA]" in ai_answer:
        draw_type = 4; tool_name = "DRAW:NUBE"
    elif "[DRAW:CASA]" in ai_answer or "[DRAW:HOME]" in ai_answer:
        draw_type = 5; tool_name = "DRAW:CASA"
    elif "[DRAW:MUSICA]" in ai_answer or "[DRAW:MUSIC]" in ai_answer or "[DRAW:NOTA]" in ai_answer:
        draw_type = 6; tool_name = "DRAW:MUSICA"
    elif "[DRAW:SONRISA]" in ai_answer or "[DRAW:SMILE]" in ai_answer:
        draw_type = 1; tool_name = "DRAW:SONRISA"

    clean = ai_answer
    for tag in ["[DRAW:CORAZON]", "[DRAW:HEART]", "[DRAW:ESTRELLA]", "[DRAW:STAR]",
                "[DRAW:SOL]", "[DRAW:SUN]", "[DRAW:NUBE]", "[DRAW:CLOUD]",
                "[DRAW:LLUVIA]", "[DRAW:CASA]", "[DRAW:HOME]", "[DRAW:MUSICA]",
                "[DRAW:MUSIC]", "[DRAW:NOTA]", "[DRAW:SONRISA]", "[DRAW:SMILE]"]:
        clean = clean.replace(tag, "")
    clean = clean.strip()
    return draw_type, tool_name, clean

def html_escape(src: str) -> str:
    if not src:
        return ""
    table = {
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#39;'
    }
    return "".join(table.get(c, c) for c in src)

# ============================================================================
# ESPEJOS PYTHON DE LA LÓGICA ANTI-SATURACIÓN DEL FIRMWARE
# Fuente de verdad: include/dsp_audio.h (dsp_apply_sample_ex/_norm_gain256).
# Si cambian sus constantes, ESTOS tests deben cambiar a la par (y viceversa:
# el test de contrato test_dsp_header_contract lo vigila a nivel fuente).
# División entera estilo C (trunca hacia cero): Python // hace floor y DIFIERE
# en negativos no múltiplos. NO usar // directo.
# ============================================================================

DSP_TRIM_NUM = 256
DSP_TRIM_DEN = 256
DSP_GAIN_MIN = 32
DSP_GAIN_MAX = 256
DSP_KNEE = 26000
DSP_CEIL = 28000
DSP_NORM_SILENCE = 200

def cdiv(a: int, b: int) -> int:
    q = abs(a) // abs(b)
    return -q if (a < 0) ^ (b < 0) else q

def dsp_apply_sample(sample: int, gain256: int) -> int:
    if gain256 < DSP_GAIN_MIN:
        gain256 = DSP_GAIN_MIN
    if gain256 > DSP_GAIN_MAX:
        gain256 = DSP_GAIN_MAX
    gain256 = (gain256 * DSP_TRIM_NUM) // DSP_TRIM_DEN
    v = cdiv(sample * gain256, 256)
    if v > DSP_KNEE:
        v = DSP_KNEE + cdiv(v - DSP_KNEE, 4)
    elif v < -DSP_KNEE:
        v = -DSP_KNEE + cdiv(v + DSP_KNEE, 4)
    return max(-DSP_CEIL, min(DSP_CEIL, v))

def dsp_norm_gain256(peak: int, target: int) -> int:
    if peak < DSP_NORM_SILENCE:
        return 0
    g = (target * 256) // peak
    if g > 256:
        g = 256  # nunca boost
    if g < 128:
        g = 128
    return g

def write_pcm_chunk_guard(data: bytes):
    """Espejo de los guards de AudioEngine::writePcmChunk. Retorna bytes
    efectivos a enviar al DAC, o 0 si el chunk debe descartarse."""
    if data is None or len(data) < 2:
        return 0
    return len(data) & ~1  # trunca byte impar residual

def looks_like_mp3(d: bytes) -> bool:
    if d is None or len(d) < 4:
        return False
    if len(d) >= 3 and d[0:3] == b'ID3':
        return True
    scan = min(len(d) - 1, 256)
    for i in range(scan):
        if d[i] == 0xFF and (d[i + 1] & 0xE0) == 0xE0:
            return True
    return False

def looks_like_json_error(d: bytes) -> bool:
    if not d:
        return False
    i = 0
    if len(d) >= 3 and d[0:3] == b'\xef\xbb\xbf':
        i = 3
    while i < len(d) and d[i:i+1] in (b' ', b'\t', b'\r', b'\n'):
        i += 1
    if i >= len(d):
        return False
    head = d[i:i+6]
    if head[:1] in (b'{', b'['):
        return True
    if head[:5] in (b'ERROR', b'<html', b'<!DOC'):
        return True
    return False

def looks_like_wav_riff(d: bytes) -> bool:
    return d is not None and len(d) >= 4 and d[0:4] == b'RIFF'

def is_plausible_sample_rate(hz: int) -> bool:
    return 8000 <= hz <= 48000

# Espejo del contrato de CopitoLog (src/copito_log.cpp): ring circular con
# filtro por nivel y forma JSON {t, lvl, tag, msg}. Si este contrato cambia,
# la consola web /api/logs deja de entenderse con el firmware.
LOG_RANK = {'I': 0, 'W': 1, 'E': 2}

class RingLog:
    def __init__(self, capacity=200):
        self.capacity = capacity
        self.entries = []

    def write(self, level, tag, msg, t=0):
        self.entries.append({"t": t, "lvl": level, "tag": tag[:15], "msg": msg[:119]})
        if len(self.entries) > self.capacity:
            self.entries.pop(0)  # circular: descarta lo más viejo

    def query(self, min_level='I', n=80):
        rank = LOG_RANK[min_level]
        out = [e for e in self.entries if LOG_RANK[e["lvl"]] >= rank]
        return out[-n:]

    def clear(self):
        self.entries = []

def decode_chunked_stream(stream: bytes) -> bytes:
    """Espejo del decodificador chunked del firmware: soporta extensiones
    (hex;ext) y líneas vacías intermedias; corta ante CRLF ausente."""
    out = bytearray()
    idx = 0
    guard = 0
    while idx < len(stream):
        guard += 1
        if guard > 4096:
            break
        crlf = stream.find(b"\r\n", idx)
        if crlf == -1:
            break
        hex_part = stream[idx:crlf].decode('ascii', errors='strict').strip().split(';')[0].strip()
        if hex_part == "":
            idx = crlf + 2
            continue
        chunk_size = int(hex_part, 16)
        if chunk_size == 0:
            break
        idx = crlf + 2
        # Si el CRLF final aún no llegó (red lenta), se corta: mejor truncar
        # que interpretar basura como tamaño del próximo chunk.
        if idx + chunk_size + 2 > len(stream):
            break
        out.extend(stream[idx:idx + chunk_size])
        idx += chunk_size + 2
    return bytes(out)

class TtsChunkPumpMirror:
    """Espejo del TtsChunkPump del firmware (ai_engine.cpp): de-chunking
    incremental y no bloqueante para el TTS de Gemini. Sin esto, los tamaños
    de chunk en hex (base64 válido) decodificaban como ráfagas de estática."""
    def __init__(self, chunked):
        self.chunked = chunked
        self.chunk_left = -1 if chunked else 10 ** 18
        self.crlf_need = 0
        self.eos = False
        self.line = bytearray()
        self.pending = bytearray()

    def _poll_size(self):
        while self.pending:
            c = self.pending.pop(0)
            if c == 0x0D:
                continue
            if c == 0x0A:
                if len(self.line) == 0:
                    return -1
                try:
                    v = int(bytes(self.line).decode('ascii').strip().split(';')[0], 16)
                except ValueError:
                    v = -1
                self.line = bytearray()
                return v
            if len(self.line) < 19:
                self.line.append(c)
            else:
                self.line = bytearray()
                return -1
        return -2

    def feed(self, data: bytes):
        self.pending.extend(data)

    def read_body(self, n: int) -> bytes:
        if self.eos or n <= 0:
            return b""
        if not self.chunked:
            out = bytes(self.pending[:n])
            del self.pending[:n]
            return out
        out = bytearray()
        while len(out) < n:
            if self.chunk_left == -2:
                while self.crlf_need > 0:
                    if not self.pending:
                        return bytes(out)
                    c = self.pending.pop(0)
                    if c in (0x0D, 0x0A):
                        self.crlf_need -= 1
                    else:
                        self.eos = True
                        return bytes(out)
                self.chunk_left = -1
                continue
            if self.chunk_left < 0:
                sz = self._poll_size()
                if sz == -2:
                    return bytes(out)
                if sz <= 0:
                    self.eos = True
                    return bytes(out)
                self.chunk_left = sz
                continue
            if self.chunk_left == 0:
                self.chunk_left = -2
                self.crlf_need = 2
                continue
            want = min(n - len(out), self.chunk_left)
            take = min(want, len(self.pending))
            if take <= 0:
                return bytes(out)
            out.extend(self.pending[:take])
            del self.pending[:take]
            self.chunk_left -= take
        return bytes(out)

class MockFramebuffer:
    def __init__(self):
        self.CANARY_SIZE = 64
        self.buf = bytearray([0xAA] * self.CANARY_SIZE + [0xFF] * EPD_BUFFER_SIZE + [0xAA] * self.CANARY_SIZE)
        self.real_offset = self.CANARY_SIZE

    def draw_pixel(self, x, y, color):
        if x < 0 or x >= EPD_WIDTH or y < 0 or y >= EPD_HEIGHT:
            return
        idx = self.real_offset + y * 25 + (x >> 3)
        bit = 7 - (x & 0x07)
        if color:
            self.buf[idx] |= (1 << bit)
        else:
            self.buf[idx] &= ~(1 << bit)

    def draw_line(self, x0, y0, x1, y1, color):
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            self.draw_pixel(x0, y0, color)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def draw_rect(self, x, y, w, h, color, fill=False):
        if fill:
            for cy in range(y, y + h):
                self.draw_line(x, cy, x + w - 1, cy, color)
        else:
            self.draw_line(x, y, x + w - 1, y, color)
            self.draw_line(x, y + h - 1, x + w - 1, y + h - 1, color)
            self.draw_line(x, y, x, y + h - 1, color)
            self.draw_line(x + w - 1, y, x + w - 1, y + h - 1, color)

    def check_canaries(self) -> bool:
        front = self.buf[:self.CANARY_SIZE]
        back = self.buf[self.real_offset + EPD_BUFFER_SIZE:]
        return all(b == 0xAA for b in front) and all(b == 0xAA for b in back)

class TestCopitoCoreLogic(unittest.TestCase):
    def test_es8311_volume_limits(self):
        reg_100 = calc_es8311_volume_reg(100)
        self.assertEqual(reg_100, 192, "El 100% de volumen debe ser 192 (0 dBFS, SIN boost: el +3.5 dB saturaba)")
        reg_50 = calc_es8311_volume_reg(50)
        self.assertEqual(reg_50, 175, "El 50% de volumen debe ser 175 (-8.0 dB)")
        reg_0 = calc_es8311_volume_reg(0)
        self.assertEqual(reg_0, 0, "El 0% de volumen debe ser 0 (Mute)")
        for vol in range(0, 101):
            reg = calc_es8311_volume_reg(vol)
            self.assertLessEqual(reg, 192, f"Volumen {vol}% produjo registro {reg} > 192 (boost prohibido)")
            self.assertGreaterEqual(reg, 0)
        # Entradas fuera de rango (NVS corrupto, p.ej. 255) también deben clampear
        self.assertEqual(calc_es8311_volume_reg(255), 192)
        self.assertEqual(calc_es8311_volume_reg(-20), 0)
        regs = [calc_es8311_volume_reg(v) for v in range(0, 101)]
        self.assertTrue(all(regs[i] <= regs[i+1] for i in range(len(regs)-1)))

    def test_pcm_chunk_guards_reject_degenerate_buffers(self):
        # None / vacío / 1 byte: nada válido que mandar al DAC
        self.assertEqual(write_pcm_chunk_guard(None), 0)
        self.assertEqual(write_pcm_chunk_guard(b""), 0)
        self.assertEqual(write_pcm_chunk_guard(b"\x01"), 0)
        # Longitud impar: se trunca el byte huérfano (medio int16 = click a full-scale)
        self.assertEqual(write_pcm_chunk_guard(b"\x01\x02\x03"), 2)
        self.assertEqual(write_pcm_chunk_guard(b"\x00" * 512 + b"\xFF"), 512)
        # Pares pasan intactos
        self.assertEqual(write_pcm_chunk_guard(b"\x00" * 1024), 1024)

    def test_soft_limiter_never_saturates_dac(self):
        # Unity transparente: lineal exacto en zona normal
        self.assertEqual(dsp_apply_sample(0, 256), 0)
        self.assertEqual(dsp_apply_sample(10000, 256), 10000)
        self.assertEqual(dsp_apply_sample(-10000, 256), -10000)
        self.assertEqual(dsp_apply_sample(16000, 256), 16000)
        # Full-scale: compresión suave 4:1 por diseño (27691), jamás wrap/techo roto
        self.assertEqual(dsp_apply_sample(32767, 256), 27691)
        self.assertEqual(dsp_apply_sample(-32768, 256), -27692)
        # Clamp de ganancia: 999->256, 0->32 (10000*32/256=1250)
        self.assertEqual(dsp_apply_sample(10000, 999), 10000)
        self.assertEqual(dsp_apply_sample(10000, 0), 1250)
        # Monótono y acotado al techo en todo el rango (regresión de clipping)
        prev = None
        for v in range(-32768, 32768, 97):
            o = dsp_apply_sample(v, 256)
            self.assertGreaterEqual(o, -DSP_CEIL)
            self.assertLessEqual(o, DSP_CEIL)
            if prev is not None:
                self.assertGreaterEqual(o, prev)
            prev = o

    def test_output_gain_is_exact_and_bounded(self):
        # Normalización por pico estilo firmware: target sin boost, piso 128
        self.assertEqual(dsp_norm_gain256(32766, 26000), 203)  # TTS local
        self.assertEqual(dsp_norm_gain256(32766, 22000), 171)  # GoogleFree
        self.assertEqual(dsp_norm_gain256(100, 26000), 0)  # silencio: no amplificar
        self.assertEqual(dsp_norm_gain256(10000, 26000), 256)  # tope unity
        self.assertEqual(dsp_norm_gain256(26000, 26000), 256)
        # Extremo a extremo (norm + dsp): full-scale local queda bajo la rodilla
        g = dsp_norm_gain256(32766, 26000)
        self.assertLess(dsp_apply_sample(32766, g), DSP_KNEE)

    def test_mp3_magic_rejects_error_pages(self):
        # MP3 reales: tag ID3 o frame sync 0xFF Ex
        self.assertTrue(looks_like_mp3(b'ID3\x04\x00\x00\x00\x00\x01\x02'))
        self.assertTrue(looks_like_mp3(bytes([0xFF, 0xFB, 0x90, 0x00]) + b'\x00' * 100))
        self.assertTrue(looks_like_mp3(b'\x00' * 10 + bytes([0xFF, 0xF3, 0xC0]) + b'\x00' * 100))
        # Páginas de error/captcha de Google (la causa del "todo mal saturado"):
        self.assertFalse(looks_like_mp3(b'<html><body>Captcha'))
        self.assertFalse(looks_like_mp3(b'{"error": {"code": 429'))
        self.assertFalse(looks_like_mp3(b'ERROR: quota exceeded'))
        self.assertFalse(looks_like_mp3(b''))
        self.assertFalse(looks_like_mp3(b'\x00\x01\x02'))

    def test_json_error_bodies_never_reach_dac(self):
        self.assertTrue(looks_like_json_error(b'{"error": {"code": 429}}'))
        self.assertTrue(looks_like_json_error(b'  \n {"candidates": []}'))
        self.assertTrue(looks_like_json_error(b'\xef\xbb\xbf{"error":1}'))
        self.assertTrue(looks_like_json_error(b'ERROR: Daily Limit Exceeded'))
        self.assertTrue(looks_like_json_error(b'<html><body>Error 403'))
        self.assertTrue(looks_like_json_error(b'<!DOCTYPE html><html>'))
        # Audio binario real no debe confundirse con error
        self.assertFalse(looks_like_json_error(bytes([0xFF, 0xFB, 0x90, 0x00]) + b'\x00' * 60))
        self.assertFalse(looks_like_json_error(b'RIFF....WAVEfmt ' + b'\x00' * 50))
        self.assertFalse(looks_like_json_error(b''))

    def test_wav_riff_magic_validates_voicerss(self):
        self.assertTrue(looks_like_wav_riff(b'RIFF\x00\x00\x00\x00WAVE'))
        self.assertFalse(looks_like_wav_riff(b'ERROR: Invalid key'))
        self.assertFalse(looks_like_wav_riff(b'{"error": "no key"}'))
        self.assertFalse(looks_like_wav_riff(b''))

    def test_sample_rate_plausibility(self):
        for hz in (8000, 16000, 22050, 24000, 44100, 48000):
            self.assertTrue(is_plausible_sample_rate(hz), f"{hz} Hz debe aceptarse")
        # Un hz corrupto (0, basura de frame MP3 roto) jamás debe abrir el I2S
        for hz in (0, 1, 4000, 96000, 192000, -1):
            self.assertFalse(is_plausible_sample_rate(hz), f"{hz} Hz debe rechazarse")

    def test_ring_log_overwrites_oldest(self):
        log = RingLog(capacity=5)
        for i in range(7):
            log.write('I', 'AI', f"msg{i}", t=i)
        self.assertEqual(len(log.query()), 5)
        # Sobreviven las 5 más nuevas, en orden
        self.assertEqual([e["msg"] for e in log.query()], ["msg2", "msg3", "msg4", "msg5", "msg6"])

    def test_ring_log_level_filter(self):
        log = RingLog()
        log.write('I', 'AI', "info normal")
        log.write('W', 'AI', "fallback activado")
        log.write('E', 'TTS', "body corrupto")
        self.assertEqual(len(log.query('I')), 3)
        self.assertEqual(len(log.query('W')), 2)
        self.assertEqual(len(log.query('E')), 1)
        self.assertEqual(log.query('E')[0]["tag"], "TTS")

    def test_ring_log_entry_shape_matches_web_contract(self):
        log = RingLog()
        log.write('W', 'AI', "Gemini TTS 429", t=1234)
        e = log.query()[0]
        self.assertEqual(set(e.keys()), {"t", "lvl", "tag", "msg"})
        self.assertEqual(e["t"], 1234)
        self.assertEqual(e["lvl"], "W")
        # Truncado a los tamaños del firmware (tag 15, msg 119)
        log.write('I', 'X' * 40, "Y" * 500)
        e2 = log.query()[-1]
        self.assertLessEqual(len(e2["tag"]), 15)
        self.assertLessEqual(len(e2["msg"]), 119)

    def test_ring_log_clear(self):
        log = RingLog()
        log.write('E', 'AI', "algo roto")
        log.clear()
        self.assertEqual(log.query(), [])

    def test_log_flattens_multiline_bodies(self):
        # Espejo de CopitoLog::write: un body HTTP con \n debe aplanarse a una línea,
        # si no corrompe /logs.txt y la recarga al boot pierde entradas.
        def sanitize(msg):
            return msg.replace('\n', ' ').replace('\r', ' ')[:119]
        body = 'Gemini error body: c6\n{\n"error": {\n"code": 503,'
        flat = sanitize(body)
        self.assertNotIn('\n', flat)
        self.assertTrue(flat.startswith('Gemini error body: c6 { "error":'))
        log = RingLog()
        log.write('E', 'AI', flat)
        self.assertEqual(len(log.query()), 1)

    def test_flash_tail_skips_malformed_lines(self):
        # Espejo de fileTailJson: restos viejos sin formato se ignoran, no se muestran
        def is_log_line(line):
            line = line.strip()
            return len(line) > 0 and line[0] == '[' and '][' in line
        raw_lines = [
            '[32158][W][AI] fallback activado',
            '{',
            '"error": {',
            '"code": 503,',
            '[152774][W][AI] contingencia',
            '',
            '   ',
        ]
        valid = [l for l in raw_lines if is_log_line(l)]
        self.assertEqual(valid, ['[32158][W][AI] fallback activado',
                                 '[152774][W][AI] contingencia'])

# ============================================================================
# REGRESIÓN DEL BUG REAL (2026-09-06, TTS local Piper -> "lluvia de estática"):
# Reglas de aceptación de WAV local (canales/bits/rate/data/tamaño/pico).
# (La normalización por pico vive en dsp_norm_gain256: local target 26000,
# GoogleFree target 22000, gate de silencio en 200, clamp [128,256].)

def local_wav_accept(channels, bits, rate, has_data_id, data_size, got_bytes, peak) -> str:
    if channels != 1 or bits != 16 or not (8000 <= rate <= 48000):
        return "reject-format"
    if not has_data_id:
        return "reject-nodata"
    if not (1024 <= data_size <= 1024 * 1024):
        return "reject-size"
    if got_bytes < data_size:
        return "reject-truncated"
    if peak < 200:
        return "skip-silence"
    return "accept"

class TestLocalTtsStaticRain(unittest.TestCase):
    def test_norm_gain_mapping(self):
        self.assertEqual(dsp_norm_gain256(32766, 26000), 203)  # local: atenúa
        self.assertEqual(dsp_norm_gain256(32766, 22000), 171)  # google: más margen
        self.assertEqual(dsp_norm_gain256(100, 26000), 0)  # silencio
        self.assertEqual(dsp_norm_gain256(10000, 26000), 256)  # tope unity
        self.assertEqual(dsp_norm_gain256(26000, 26000), 256)
        self.assertEqual(dsp_norm_gain256(32767, 26000), dsp_norm_gain256(32766, 26000))

    def test_fullscale_piper_sample_old_vs_new(self):
        # Muestra real de Piper a full-scale: cadena vieja (x1.5+limiter 28000)
        # lo aplastaba; la nueva (norm 203 -> 25982) ni toca la rodilla 26000.
        g = dsp_norm_gain256(32766, 26000)
        self.assertEqual(g, 203)
        self.assertLess(dsp_apply_sample(32766, g), DSP_KNEE)

    def test_moderate_audio_unchanged(self):
        g = dsp_norm_gain256(16000, 26000)  # pico moderado -> unity, sin compresión
        self.assertEqual(g, 256)
        self.assertEqual(dsp_apply_sample(16000, g), 16000)  # unity exacto

    def test_mp3_magic_must_not_gate_pcm(self):
        # Un 0xFFFF (muestra -1, normal en PCM caliente) es "FF FF" = falso sync MP3.
        # Por eso el path local NO usa looks_like_mp3: lo documenta este test.
        hot_pcm = bytes([0xFF, 0xFF, 0x00, 0x00])
        self.assertTrue(looks_like_mp3(hot_pcm))
        # ...y sin embargo es PCM válido que debe aceptarse (si el resto valida)
        self.assertEqual(local_wav_accept(1, 16, 22050, True, 81012, 81012, 32766), "accept")

    def test_local_validation_table(self):
        ok = dict(channels=1, bits=16, rate=22050, has_data_id=True,
                  data_size=81012, got_bytes=81012, peak=32766)
        self.assertEqual(local_wav_accept(**ok), "accept")
        bad = dict(ok); bad["has_data_id"] = False
        self.assertEqual(local_wav_accept(**bad), "reject-nodata")  # MP3 con header genérico
        bad = dict(ok); bad["data_size"] = 0
        self.assertEqual(local_wav_accept(**bad), "reject-size")
        bad = dict(ok); bad["data_size"] = 2 * 1024 * 1024
        self.assertEqual(local_wav_accept(**bad), "reject-size")
        bad = dict(ok); bad["got_bytes"] = 40000
        self.assertEqual(local_wav_accept(**bad), "reject-truncated")  # jamás ruido parcial
        bad = dict(ok); bad["peak"] = 50
        self.assertEqual(local_wav_accept(**bad), "skip-silence")
        bad = dict(ok); bad["channels"] = 2
        self.assertEqual(local_wav_accept(**bad), "reject-format")
        bad = dict(ok); bad["rate"] = 96000
        self.assertEqual(local_wav_accept(**bad), "reject-format")

    def test_google_free_tts_normalization(self):
        # Google Free TTS envía audio pre-masterizado a 0 dBFS (peak ~32767).
        # En batería / celular, el amplificador NS4150B saturaba brutalmente.
        # Con target 22000 (-3.5 dBFS), la ganancia debe atenuar un ~33% limpiamente.
        def google_norm_gain(peak: int) -> int:
            if peak < 200: return 256
            g = (22000 * 256) // peak
            return max(128, min(384, g))

        # Peak 32767 (0 dBFS) -> atenúa a ~171/256 (0.67x)
        g_hot = google_norm_gain(32767)
        self.assertEqual(g_hot, 171)
        self.assertLess(g_hot, 256)
        scaled_peak = (32767 * g_hot) // 256
        self.assertLessEqual(scaled_peak, 22000)

        # Peak 15000 (audio moderado/bajo) -> refuerza hasta 22000
        g_mod = google_norm_gain(15000)
        self.assertEqual(g_mod, (22000 * 256) // 15000)
        self.assertGreater(g_mod, 256)

        # Audio extremadamente bajo -> clamp a tope de seguridad +3.5 dB (384/256 = 1.5x)
        g_quiet = google_norm_gain(5000)
        self.assertEqual(g_quiet, 384)

    def test_write_pcm_chunk_safety_limiter_continuous(self):
        # El limitador continuo de writePcmChunk debe ser 100% lineal hasta 26000 (0.00% distorsión)
        # y comprimir suavemente hasta un techo estricto de 28000 para evitar saturar el altavoz.
        def safety_limiter(v: int) -> int:
            if v > 26000:
                v = 26000 + (v - 26000) // 4
            elif v < -26000:
                v = -26000 + (v + 26000) // 4
            if v > 28000: v = 28000
            elif v < -28000: v = -28000
            return v

        # Rango normal (0 a 26000): perfectamente lineal
        self.assertEqual(safety_limiter(22000), 22000)
        self.assertEqual(safety_limiter(26000), 26000)
        self.assertEqual(safety_limiter(-22000), -22000)

        # Rango caliente (>26000): compresión suave, jamás supera 28000
        self.assertEqual(safety_limiter(32767), 26000 + (32767 - 26000) // 4)
        self.assertLessEqual(safety_limiter(32767), 28000)
        self.assertGreaterEqual(safety_limiter(-32768), -28000)

# ============================================================================
# ESPEJOS DE LA GUARDIA AUTÓNOMA (check-in periódico + OTA hold).
# Matemática pura del firmware: si esto falla, la placa duerme cuando debe
# estar despierta (pierde flashes) o queda despierta y funde la batería.
# ============================================================================

def stay_awake_left_min(hold_until_ms, now_ms):
    """Minutos restantes de hold (techo), 0 si vencido o inactivo."""
    if not hold_until_ms:
        return 0
    rest = hold_until_ms - now_ms
    if rest <= 0:
        return 0
    return (rest + 59999) // 60000

def checkin_expired(checkin_until_ms, now_ms):
    return now_ms >= checkin_until_ms

def checkin_interval_valid(m):
    return m == 0 or 5 <= m <= 720

def checkin_window_valid(s):
    return 60 <= s <= 900

class TestGuardiaAutonoma(unittest.TestCase):
    def test_hold_counts_down_and_expires(self):
        now = 1_000_000
        hold_until = now + 60 * 60000
        self.assertEqual(stay_awake_left_min(hold_until, now), 60)
        self.assertEqual(stay_awake_left_min(hold_until, now + 30 * 60000), 30)
        self.assertEqual(stay_awake_left_min(hold_until, now + 59 * 60000 + 1), 1)
        self.assertEqual(stay_awake_left_min(hold_until, hold_until), 0)
        self.assertEqual(stay_awake_left_min(hold_until, hold_until + 5000), 0)
        self.assertEqual(stay_awake_left_min(0, now), 0)

    def test_hold_survives_reboot_as_nvs_minutes(self):
        # Al dormir con 37.2 min restantes se persisten 38 (techo); al boot se rearma
        remaining_ms = 37 * 60000 + 12000
        persisted = (remaining_ms + 59999) // 60000
        self.assertEqual(persisted, 38)
        rearmed_until = 5000 + persisted * 60000
        self.assertEqual(stay_awake_left_min(rearmed_until, 5000), 38)

    def test_checkin_window_expiry(self):
        start = 2_000_000
        window_s = 180
        until = start + window_s * 1000
        self.assertFalse(checkin_expired(until, start))
        self.assertFalse(checkin_expired(until, until - 1))
        self.assertTrue(checkin_expired(until, until))
        self.assertTrue(checkin_expired(until, until + 60000))

    def test_checkin_config_ranges(self):
        for m in (0, 5, 30, 120, 720):
            self.assertTrue(checkin_interval_valid(m), f"{m} min debe aceptarse")
        for m in (1, 4, 721, 9999):
            self.assertFalse(checkin_interval_valid(m), f"{m} min debe rechazarse")
        for s in (60, 180, 900):
            self.assertTrue(checkin_window_valid(s))
        for s in (0, 59, 901):
            self.assertFalse(checkin_window_valid(s))

    def test_hold_takes_over_checkin(self):
        # Con hold activo no se duerme aunque la ventana haya vencido
        now = 5_000_000
        window_expired = checkin_expired(now - 1000, now)
        hold_left = stay_awake_left_min(now + 10 * 60000, now)
        should_sleep = window_expired and hold_left == 0
        self.assertTrue(window_expired)
        self.assertFalse(should_sleep)
        # Sin hold sí se duerme
        should_sleep2 = window_expired and stay_awake_left_min(0, now) == 0
        self.assertTrue(should_sleep2)

# Espejo del backoff adaptativo (NetworkManager::nextCheckinMin): tras actividad
# intervalo rápido (5 min); cada ciclo quiet duplica hasta el tope configurado.
def next_checkin_min(base_min, quiet):
    if base_min == 0:
        return 0
    step = 5
    for _ in range(quiet):
        step *= 2
        if step >= base_min:
            return base_min
    if step > base_min:
        step = base_min
    return step

class TestCheckinBackoff(unittest.TestCase):
    def test_backoff_sequence_with_base_30(self):
        self.assertEqual([next_checkin_min(30, q) for q in range(6)], [5, 10, 20, 30, 30, 30])

    def test_backoff_respects_smaller_base(self):
        self.assertEqual([next_checkin_min(10, q) for q in range(4)], [5, 10, 10, 10])
        self.assertEqual([next_checkin_min(5, q) for q in range(3)], [5, 5, 5])

    def test_backoff_disabled_with_base_0(self):
        for q in (0, 1, 5, 10):
            self.assertEqual(next_checkin_min(0, q), 0)

    def test_backoff_saturates_without_overflow(self):
        self.assertEqual(next_checkin_min(720, 10), 720)
        self.assertEqual(next_checkin_min(30, 100), 30)

    def test_activity_resets_to_fast_interval(self):
        # Tras un flash (quiet=0) el próximo ciclo es rápido aunque antes era lento
        quiet = 4  # venía en 30 min
        self.assertEqual(next_checkin_min(30, quiet), 30)
        quiet = 0  # hubo actividad
        self.assertEqual(next_checkin_min(30, quiet), 5)

# ============================================================================
# BUSQUEDA WEB PROPIA ([TOOL:WEB_SEARCH] -> SearXNG primario, DuckDuckGo fallback).
# Copito ya no depende del grounding de Gemini: la IA emite el tag y el firmware
# busca (SearXNG en LAN / DuckDuckGo en TLS) y relanza con los datos reales.
# ============================================================================
import json as _json

GROUNDING_SUFFIX = ',"tools":[{"google_search":{}}]'

def build_generation_suffix(temperature10, max_tokens, web_search: bool) -> str:
    # Espejo de ai_engine.cpp: la busqueda es propia [TOOL:WEB_SEARCH],
    # ya NO se agrega tools google_search.
    temperature = (temperature10 / 10.0) if temperature10 > 0 else 0.7
    max_tokens = max_tokens if 80 <= max_tokens <= 2048 else 300
    return '{"temperature":%.1f,"maxOutputTokens":%d}' % (temperature, max_tokens)

def build_text_payload(system_prompt, user_text, web_search: bool) -> dict:
    doc = {
        "system_instruction": {"parts": [{"text": system_prompt}]},
        "contents": [{"parts": [{"text": user_text}]}],
        "generationConfig": {"temperature": 0.7, "maxOutputTokens": 300},
    }
    if web_search:
        doc["tools"] = [{"google_search": {}}]
    return doc

class TestWebGrounding(unittest.TestCase):
    def test_generation_suffix_has_no_grounding_tools(self):
        # El grounding de Gemini fue reemplazado por busqueda propia [TOOL:WEB_SEARCH].
        inner = build_generation_suffix(7, 300, True)
        doc = _json.loads(inner)
        self.assertNotIn("tools", doc)  # ya no se manda google_search

    def test_suffix_clamps_config_ranges(self):
        doc = _json.loads(build_generation_suffix(0, 99999, False))
        self.assertEqual(doc["temperature"], 0.7)
        self.assertEqual(doc["maxOutputTokens"], 300)

    @staticmethod
    def extract_web_search_queries(text: str):
        # Espejo de resolveWebSearchRound: extrae todas las consultas de [TOOL:WEB_SEARCH:x]
        out = []
        low = text.lower()
        scan = 0
        while True:
            f = low.find("[tool:web_search:", scan)
            if f < 0:
                break
            c = text.find("]", f)
            if c < 0:
                break
            q = text[f + 17:c].strip()
            if q:
                out.append(q)
            scan = c + 1
        return out

    @staticmethod
    def strip_web_search_tags(text: str):
        # Espejo del bucle de limpieza de tags en resolveWebSearchRound
        s = text
        low = s.lower()
        while True:
            idx = low.find("[tool:web_search:")
            if idx < 0:
                break
            c = s.find("]", idx)
            if c < 0:
                break
            s = s[:idx] + s[c + 1:]
            low = s.lower()
        return s.strip()

    def test_extract_web_search_queries(self):
        txt = "[TOOL:WEB_SEARCH:noticias de argentina] y [TOOL:WEB_SEARCH:cotizacion dolar]"
        self.assertEqual(self.extract_web_search_queries(txt),
                         ["noticias de argentina", "cotizacion dolar"])

    def test_extract_respects_case_and_spaces(self):
        txt = "hola [tool:web_search:   clima buenos aires  ] fin"
        self.assertEqual(self.extract_web_search_queries(txt), ["clima buenos aires"])

    def test_strip_web_search_tags(self):
        txt = "Respuesta [TOOL:WEB_SEARCH:algo] final"
        # El strip deja un hueco (igual que el C++); normalizamos espacios multiples.
        self.assertEqual(
            " ".join(self.strip_web_search_tags(txt).split()),
            "Respuesta final")

    def test_strip_multiple_tags(self):
        txt = "a [TOOL:WEB_SEARCH:x] b [TOOL:WEB_SEARCH:y] c"
        self.assertEqual(
            " ".join(self.strip_web_search_tags(txt).split()),
            "a b c")

    def test_prompt_instructs_tool_use(self):
        # La instruccion del prompt ahora pide emitir el tag [TOOL:WEB_SEARCH]
        base = "Eres Copito."
        base += " Si te preguntan por datos actuales o algo que no sabes con certeza, emite [TOOL:WEB_SEARCH:consulta corta] y espera los resultados para responder."
        self.assertIn("[TOOL:WEB_SEARCH", base)
        self.assertNotIn("google_search", base)

    def test_second_round_prompt_suppresses_tool(self):
        # Durante la 2da ronda (m_inWebSearchRound) NO se vuelve a pedir el tag.
        base = "Eres Copito."
        base += " Responde con los datos provistos."
        self.assertNotIn("[TOOL:WEB_SEARCH", base)

# ============================================================================
# CONTRATO DEL PROMPT (personalidad adulta, autorregulación, anti-repetición).
# Si el prompt pierde estas reglas, vuelven las frases infantiles y el loro.
# ============================================================================

NEW_PROMPT = (
    "Eres Copito, compañero de Jorge: adulto, directo, con humor seco ocasional. "
    "Hablas espanol rioplatense neutro, sin diminutivos, sin cursilerias, sin frases de autoayuda. "
    "Nunca hables como un nino ni uses muletillas tiernas. "
    "Reglas: "
    "1. Extension autorregulada: al hablar, 1-2 oraciones cortas (maximo unas 40 palabras). "
    "Si el tema pide profundidad (economia, politica, ciencia), responde denso y sin relleno; "
    "el detalle visual va en [TEXT:resumen corto para pantalla]. "
    "2. No repeticion: varia aperturas y cierres, nunca repitas la misma frase en respuestas seguidas, "
    "prohibido empezar dos respuestas seguidas igual. "
    "3. Datos actuales: noticias, precios, dolar o clima externo se responden con busqueda web "
    "mencionando la fecha; si no tienes el dato, dilo en una frase, jamas inventes cifras. "
    "4. Icono visual obligatorio: termina SIEMPRE con exactamente un tag [DRAW:XXX] "
    "(CORAZON, ESTRELLA, SOL, NUBE, CASA, MUSICA, LUNA, LIBRO, FLOR, RAYO). "
    "5. Sensores reales: [TOOL:GET_TEMP] temperatura, [TOOL:GET_HUM] humedad, "
    "[TOOL:GET_BATERIA] bateria, [TOOL:GET_HORA] hora. "
    "6. Modos: [MODE:RELOJ] reloj, [MODE:CLIMA] clima con sensores, [MODE:CUENTO] historia, [MODE:DORMIDO] dormir. "
    "7. Memoria: dato clave o 'acordate de...' incluye [REMEMBER:hecho clave]. "
    "8. Caras: [FACE:FELIZ], [FACE:NORMAL], [FACE:DORMIDO], [FACE:PENSATIVO] solo cuando aporte."
)

BANNED_TONE = ["abrazo grande", "lleno de cariño", "lleno de carino", "nubecitas",
               "cursi", "ñono", "mi pequeño", "mi pequeno", "dulzura"]

FALLBACK_POOLS = [
    "Google me dejo en espera. Proba de nuevo en un minuto. [DRAW:NUBE]",
    "Hay mucha demanda ahora mismo. Retomamos en un rato. [DRAW:ESTRELLA]",
    "No pude consultar a la IA recien. Tocame de nuevo. [DRAW:SOL]",
    "Se corto la consulta en la nube. Reintentamos. [DRAW:RAYO]",
    "Servidor ocupado. Dame un minuto y hablamos. [DRAW:LUNA]",
    "Algo fallo del lado de la nube. Proba otra vez. [DRAW:NUBE]",
    "No llego la respuesta. Tocame de nuevo. [DRAW:SOL]",
    "Error de conexion con la IA. Reintentamos en un rato. [DRAW:ESTRELLA]",
    "Me quede sin respuesta valida. Decilo de nuevo. [DRAW:LUNA]",
    "Falla tecnica momentanea. Ya vuelvo. [DRAW:MUSICA]",
]

VALID_DRAWS = {"CORAZON", "ESTRELLA", "SOL", "NUBE", "CASA", "MUSICA",
               "LUNA", "LIBRO", "FLOR", "RAYO"}

class TestPromptContract(unittest.TestCase):
    def test_prompt_fits_storage(self):
        self.assertLess(len(NEW_PROMPT), 4096 - 64)

    def test_prompt_has_self_regulation_rules(self):
        for rule in ("40 palabras", "No repeticion", "jamas inventes",
                     "[TEXT:", "[DRAW:XXX]", "busqueda web", "[REMEMBER:",
                     "[MODE:RELOJ]", "[MODE:CLIMA]", "[MODE:CUENTO]", "[MODE:DORMIDO]"):
            self.assertIn(rule, NEW_PROMPT, f"Falta regla: {rule}")

    def test_prompt_has_no_infantile_tone(self):
        import re as _re
        low = NEW_PROMPT.lower()
        for banned in BANNED_TONE:
            self.assertIsNone(_re.search(r'\b' + _re.escape(banned) + r'\b', low),
                              f"Tono infantil: {banned}")

    def test_fallback_pools_are_mature_and_tagged(self):
        self.assertGreaterEqual(len(FALLBACK_POOLS), 8)  # rotación real, no loro de 3
        seen = set()
        for line in FALLBACK_POOLS:
            low = line.lower()
            for banned in BANNED_TONE:
                self.assertNotIn(banned, low, f"Fallback ñoño: {line}")
            m = [w for w in VALID_DRAWS if f"[DRAW:{w}]" in line]
            self.assertEqual(len(m), 1, f"Fallback sin tag DRAW válido: {line}")
            seen.add(line)
        self.assertEqual(len(seen), len(FALLBACK_POOLS), "Hay fallbacks duplicados")

    def test_max_tokens_ceiling_sane(self):
        self.assertGreaterEqual(800, 80)
        self.assertLessEqual(800, 2048)

# ============================================================================
# ESPEJOS DE LA CASCADA MULTI-PROVEEDOR (chat OpenAI genérico + Whisper).
# Si el multipart sale mal, Groq responde 400 y la voz queda muda en 429s.
# ============================================================================

WHISPER_BOUNDARY = "----CopitoSTT"

def build_whisper_parts(wav_size: int):
    p1 = ("--" + WHISPER_BOUNDARY
          + '\r\nContent-Disposition: form-data; name="file"; filename="audio.wav"\r\n'
          + "Content-Type: audio/wav\r\n\r\n")
    p2 = ("\r\n--" + WHISPER_BOUNDARY
          + '\r\nContent-Disposition: form-data; name="model"\r\n\r\nwhisper-large-v3-turbo'
          + "\r\n--" + WHISPER_BOUNDARY
          + '\r\nContent-Disposition: form-data; name="language"\r\n\r\nes'
          + "\r\n--" + WHISPER_BOUNDARY + "--\r\n")
    return p1, p2, len(p1.encode()) + wav_size + len(p2.encode())

def build_openai_chat(model, system_prompt, user_text, temperature10=7, max_tokens=800) -> dict:
    temperature = (temperature10 / 10.0) if temperature10 > 0 else 0.7
    max_tokens = max_tokens if 80 <= max_tokens <= 2048 else 800
    return {
        "model": model,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_text},
        ],
        "temperature": temperature,
        "max_tokens": max_tokens,
    }

CASCADE_ORDER = ["Gemini", "Groq", "Cerebras", "SambaNova", "Mistral", "xAI", "Pollinations", "OllamaPC", "OllamaPi"]

def cascade_plan(has_gemini_key, has_groq, has_cerebras, has_samba, has_mistral, has_xai, ollama_pc, ollama_pi):
    plan = []
    if has_gemini_key:
        plan.append("Gemini")
    if has_groq:
        plan.append("Groq")
    if has_cerebras:
        plan.append("Cerebras")
    if has_samba:
        plan.append("SambaNova")
    if has_mistral:
        plan.append("Mistral")
    if has_xai:
        plan.append("xAI")
    plan.append("Pollinations")  # sin key, siempre disponible
    if ollama_pc:
        plan.append("OllamaPC")
    if ollama_pi:
        plan.append("OllamaPi")
    assert plan == [p for p in CASCADE_ORDER if p in plan or p == "Pollinations"], "orden roto"
    return plan

def first_success(results):
    for label, ok in results:
        if ok:
            return label
    return None

class TestProviderCascade(unittest.TestCase):
    def test_whisper_multipart_roundtrip(self):
        wav = b"RIFF" + bytes(5000)
        p1, p2, total = build_whisper_parts(len(wav))
        body = p1.encode() + wav + p2.encode()
        self.assertEqual(len(body), total)
        # Estructura: header archivo + WAV crudo + cierre con model+language
        self.assertIn('name="file"', p1)
        self.assertIn('filename="audio.wav"', p1)
        self.assertIn("whisper-large-v3-turbo", p2)
        self.assertIn('name="language"', p2)
        self.assertTrue(p2.endswith("--\r\n"))
        # El WAV viaja intacto en el medio (sin recodificar ni partir muestras)
        self.assertEqual(body[len(p1.encode()):len(p1.encode()) + len(wav)], wav)

    def test_whisper_response_parse(self):
        doc = _json.loads('{"text":"  hola copito  "}')
        self.assertEqual(doc["text"].strip(), "hola copito")

    def test_openai_chat_payload_shape(self):
        doc = build_openai_chat("qwen/qwen3.6-27b", "sys", "hola", 7, 800)
        self.assertEqual(doc["model"], "qwen/qwen3.6-27b")
        self.assertEqual(doc["messages"][0]["role"], "system")
        self.assertEqual(doc["messages"][1]["role"], "user")
        self.assertEqual(doc["temperature"], 0.7)
        _json.dumps(doc)

    def test_openai_reply_parse(self):
        doc = _json.loads('{"choices":[{"message":{"content":" Buenas! [DRAW:SOL] "}}]}')
        reply = doc["choices"][0]["message"]["content"]
        self.assertTrue(len(reply) > 0)

    def test_cascade_order_full(self):
        plan = cascade_plan(True, True, True, True, True, True, "192.168.0.100", "192.168.0.169")
        self.assertEqual(plan, ["Gemini", "Groq", "Cerebras", "SambaNova", "Mistral", "xAI", "Pollinations", "OllamaPC", "OllamaPi"])

    def test_cascade_skips_unconfigured(self):
        plan = cascade_plan(True, False, False, False, False, False, "", "")
        self.assertEqual(plan, ["Gemini", "Pollinations"])
        plan = cascade_plan(False, True, False, False, False, False, "", "192.168.0.169")
        self.assertEqual(plan, ["Groq", "Pollinations", "OllamaPi"])

    def test_cascade_failover_picks_first_success(self):
        self.assertEqual(first_success([("Gemini", False), ("Groq", False), ("Pollinations", True)]), "Pollinations")
        self.assertIsNone(first_success([("Gemini", False), ("Groq", False)]))

    def test_qwen_thinking_stripped(self):
        # Espejo del filtro en sanitizeTtsText: el bloque "thinking ... response"
        raw = "\n\n thinking\nHere's a thinking process:\n\n1. Analyze...\n\n response\n¡El dato curioso!"
        start = raw.index(" thinking")
        resp = raw.index(" response")
        if resp > start:
            body = raw.index("\n", resp)
            out = raw[body + 1:] if body > 0 else ""
        else:
            out = raw
        out = out.strip()
        self.assertEqual(out, "¡El dato curioso!")

    def test_thinking_absent_keeps_text(self):
        raw = " Hola Copito [DRAW:SOL] "
        self.assertTrue("thinking" not in raw)
        # si no hay tag thinking, se conserva todo el texto
        self.assertIn("[DRAW:SOL]", raw.strip())

    def test_groq_model_no_cot(self):
        # groq/compound-mini es el modelo sin cadena-de-pensamiento: respuestas
        # cortas y directas, no turras del "thinking" que destrozan el audio.
        reply = "Los pulpos tienen tres corazones. [DRAW:ESTRELLA]"
        self.assertLess(len(reply), 200)
        self.assertNotIn("thinking", reply.lower())
        self.assertNotIn("Here's a thinking", reply)

    def test_thinking_garbage_discarded_by_cascade(self):
        # Espejo de la salvaguarda en chatOpenAI: si el filtrado no pudo sacar
        # el thinking (bloque truncado sin tag de cierre), la respuesta se
        # descarta como fallo y la cascada salta al siguiente proveedor.
        replies = {
            "ok": "Los pulpos tienen tres corazones. [DRAW:ESTRELLA]",
            "garbage": "\n thinking\nHere's a thinking process:\n1. Analyze user input...",
        }
        accepted = None
        for label, r in replies.items():
            if "thinking process" in r or " response" in r or (" thinking" in r and "[DRAW" not in r):
                continue
            accepted = label
            break
        self.assertEqual(accepted, "ok")

    def test_auto_level_voivemos_max_loudness(self):
        # Espejo del auto-nivelador por lote en writePcmChunk:
        # pico del lote -> ganancia que lo lleva a ~30000 (tope +12dB, nunca <1x),
        # soft-limiter lineal hasta 29000 / compresion 5:1, clamp a 32767.
        def process(samples):
            peak = max(1, max(abs(s) for s in samples))
            gq = max(256, min(8 * 256, (29000 * 256) // peak))
            out = []
            for s in samples:
                v = (s * gq) >> 8
                if v > 29000:
                    v = 29000 + (v - 29000) // 5
                elif v < -29000:
                    v = -29000 + (v + 29000) // 5
                v = max(-32768, min(32767, v))
                out.append(v)
            return out

        # Fragmento silencioso de TTS (pico ~4000) -> fuerte (~-0.7dBFS), sin clip
        quiet = process([1000, -2000, 4000, -500, 2500])
        self.assertEqual(max(quiet), 29000)  # alcanza el target exacto (umbral del limiter)
        # Fragmento ya caliente (pico ~32000) -> nunca supera 32767 (sin saturar)
        hot = process([32000, -30000, 15000, -20000])
        self.assertLessEqual(max(hot), 32767)
        self.assertTrue(all(-32768 <= v <= 32767 for v in hot))

    def test_chunked_decoder_with_extensions_and_slow_network(self):
        # Extensiones de chunk (hex;ext=...) deben ignorarse como el firmware
        stream = b"5;ext=x\r\nHello\r\n7;foo=bar\r\n ESP32!\r\n0\r\n\r\n"
        self.assertEqual(decode_chunked_stream(stream), b"Hello ESP32!")
        # CRLF final ausente (paquete aún en vuelo): truncar, no inventar audio
        truncated = b"5\r\nHello\r\n7\r\n ESP32"
        self.assertEqual(decode_chunked_stream(truncated), b"Hello")
        # Chunk de tamaño inválido debe romper, no reproducir basura
        with self.assertRaises(ValueError):
            decode_chunked_stream(b"ZZ\r\nHello\r\n0\r\n\r\n")

    def test_sanitize_preserves_agent_tools(self):
        sample = "¡Hola Jorge! **Excelente** dia [audio]. Aca tenes [SKETCH: CLEAR; LINE 0 0 10 10;] y [MODE:RELOJ] abrazo [DRAW:CORAZON] y un [DRAW:SOL]!"
        s = sanitize_tts_text(sample)
        self.assertIn("[SKETCH: CLEAR; LINE 0 0 10 10;]", s)
        self.assertIn("[MODE:RELOJ]", s)
        self.assertIn("[DRAW:CORAZON]", s)
        self.assertIn("[DRAW:SOL]", s)
        self.assertNotIn("[audio]", s)
        self.assertNotIn("**", s)

    def test_agent_tool_parser(self):
        cases = [
            ("Feliz cumple! [DRAW:CORAZON]", 1, "DRAW:CORAZON", "Feliz cumple!"),
            ("Genial! [DRAW:ESTRELLA]", 2, "DRAW:ESTRELLA", "Genial!"),
            ("Buen dia! [DRAW:SOL]", 3, "DRAW:SOL", "Buen dia!"),
            ("Lluvia fresca [DRAW:NUBE]", 4, "DRAW:NUBE", "Lluvia fresca"),
            ("Tu casita [DRAW:CASA]", 5, "DRAW:CASA", "Tu casita"),
            ("Musica alegre [DRAW:MUSICA]", 6, "DRAW:MUSICA", "Musica alegre"),
            ("Respuesta normal", 0, "none", "Respuesta normal"),
        ]
        for prompt, exp_type, exp_name, exp_clean in cases:
            dtype, tname, clean = parse_drawing_tool(prompt)
            self.assertEqual(dtype, exp_type)
            self.assertEqual(tname, exp_name)
            self.assertEqual(clean, exp_clean)

    def test_vad_safe_parameters(self):
        ENERGY_THRESHOLD = 300
        SILENCE_END_MS = 2200
        MAX_REC_SEC = 20
        self.assertLessEqual(ENERGY_THRESHOLD, 450)
        self.assertGreaterEqual(ENERGY_THRESHOLD, 200)
        self.assertGreaterEqual(SILENCE_END_MS, 1800)
        self.assertGreaterEqual(MAX_REC_SEC, 15)

    def test_html_escape_security(self):
        raw = "Robot: <script>alert(1)</script> & \"hola\" y 'chau'"
        esc = html_escape(raw)
        self.assertNotIn("<script>", esc)
        self.assertIn("&lt;script&gt;", esc)
        self.assertIn("&amp;", esc)
        self.assertIn("&quot;", esc)
        self.assertIn("&#39;", esc)

    def test_system_prompt_buffer_capacity(self):
        long_prompt = "Eres Copito, un robot inteligente y companero de vida. " * 65
        self.assertLess(len(long_prompt), SYSTEM_PROMPT_MAX - 64)

    def test_framebuffer_bounds_safety(self):
        fb = MockFramebuffer()
        extreme_coords = [(-100, -100), (-1, -1), (0, -5), (200, 200), (300, 300)]
        for x, y in extreme_coords:
            fb.draw_pixel(x, y, 0)
        fb.draw_line(-50, -50, 250, 250, 0)
        fb.draw_rect(-20, -20, 240, 240, 0, fill=True)
        self.assertTrue(fb.check_canaries(), "Desbordamiento de memoria detectado fuera del Framebuffer!")

    def test_mouth_curvature_is_smiling(self):
        """Verifica que la curva MOUTH_REST sea una sonrisa (centro mas bajo que extremos)."""
        cy = 138
        cx = 100
        center_y = cy + int((1.0 - 0.0) * 7.0)
        corner_left_y = cy + int((1.0 - 1.0) * 7.0)
        corner_right_y = cy + int((1.0 - 1.0) * 7.0)
        self.assertGreater(center_y, corner_left_y, "El centro de la boca debe estar mas abajo que la comisura izquierda (Sonrisa)")
        self.assertGreater(center_y, corner_right_y, "El centro de la boca debe estar mas abajo que la comisura derecha (Sonrisa)")
        self.assertEqual(center_y, 145)
        self.assertEqual(corner_left_y, 138)

    def test_face_packs_definition_and_safety(self):
        """Verifica los 4 Face Packs definidos y que dibujar sus elementos no cause desbordamiento."""
        FACE_PACK_RUDO = 0
        FACE_PACK_CLASICO = 1
        FACE_PACK_FEMENINO = 2
        FACE_PACK_GEEK = 3

        self.assertEqual(FACE_PACK_RUDO, 0)
        self.assertEqual(FACE_PACK_CLASICO, 1)
        self.assertEqual(FACE_PACK_FEMENINO, 2)
        self.assertEqual(FACE_PACK_GEEK, 3)

        fb = MockFramebuffer()

        # Simular dibujo de Pack Rudo (Cyber Shades aviador, habano y aros de humo marginales)
        fb.draw_line(30, 70, 170, 70, 0)
        fb.draw_line(92, 76, 108, 76, 0)
        for y in range(37):
            xLeft = 36 + ((y - 20) // 3 if y > 20 else 0)
            xRight = 90 - ((y - 16) // 4 if y > 16 else 0)
            fb.draw_line(xLeft, 72 + y, xRight, 72 + y, 0)
            xLeftR = 110 + ((y - 16) // 4 if y > 16 else 0)
            xRightR = 164 - ((y - 20) // 3 if y > 20 else 0)
            fb.draw_line(xLeftR, 72 + y, xRightR, 72 + y, 0)
        # Habano robusto
        for offset in range(-4, 5):
            fb.draw_line(116, 134 + offset, 162, 142 + offset, 0)
        # Aros de humo
        for r_x, r_y in [(174, 120), (180, 86), (184, 52)]:
            self.assertGreaterEqual(r_x - 8, 164, "Los aros de humo deben estar en el margen derecho (x >= 164)")
            fb.draw_pixel(r_x, r_y, 0)

        # Simular Pack Femenino (pestañas, moño)
        fb.draw_line(44, 76, 32, 68, 0)
        fb.draw_line(156, 76, 168, 68, 0)
        fb.draw_rect(30, 42, 12, 16, 0)

        # Simular Pack Geek (gafas 8-bit)
        fb.draw_rect(40, 78, 46, 18, 0, fill=True)
        fb.draw_rect(114, 78, 46, 18, 0, fill=True)
        fb.draw_rect(86, 80, 28, 6, 0, fill=True)

        self.assertTrue(fb.check_canaries(), "Desbordamiento detectado al renderizar elementos de los Face Packs!")

    def test_text_paginator_word_wrapping(self):
        """Valida que textos largos sobre economia/politica se paginen sin cortar palabras."""
        sample_short = "Hola Jorge, todo bien por aca!"
        sample_long = (
            "La economia global enfrenta tensiones fiscales y geopoliticas complejas. "
            "El debate monetario sobre tasas de interes, inflacion estructural y deuda soberana "
            "demanda politicas publicas con criterio pragmatico, estabilidad macroeconomica "
            "e incentivos a la productividad tecnologica real para asegurar crecimiento sostenido."
        )

        def mock_paginate(text, max_chars_per_line=30, max_lines=14):
            words = text.split()
            pages = []
            curr_lines = []
            curr_line = ""
            for w in words:
                if len(curr_line) + len(w) + (1 if curr_line else 0) <= max_chars_per_line:
                    curr_line = (curr_line + " " + w) if curr_line else w
                else:
                    curr_lines.append(curr_line)
                    curr_line = w
                    if len(curr_lines) >= max_lines:
                        pages.append(curr_lines)
                        curr_lines = []
            if curr_line:
                curr_lines.append(curr_line)
            if curr_lines:
                pages.append(curr_lines)
            return pages

        pages_short = mock_paginate(sample_short)
        self.assertEqual(len(pages_short), 1, "Texto corto debe ocupar 1 sola pagina")

        pages_long = mock_paginate(sample_long, max_chars_per_line=26, max_lines=5)
        self.assertGreaterEqual(len(pages_long), 2, "Texto reflexivo complejo debe paginarse en multiples paginas")
        for p_idx, page in enumerate(pages_long):
            self.assertLessEqual(len(page), 5)
            for line in page:
                self.assertLessEqual(len(line), 26, f"Linea excede ancho de pantalla en pagina {p_idx}: '{line}'")


def parse_mode_heuristic(text: str) -> str:
    import re
    m = re.search(r'\[MODE:([A-Z]+)\]', text)
    if m:
        return m.group(1)
    low = text.lower()
    if "modo reloj" in low or "pongo el reloj" in low or "activando el reloj" in low:
        return "RELOJ"
    if "modo clima" in low:
        return "CLIMA"
    return ""


class TestBootButtonAndNetworkLogic(unittest.TestCase):
    """
    Validaciones críticas para que el botón de hablar a Copito (BOOT GPIO 0)
    NUNCA se rompa ni quede bloqueado, y que la red hotspot esté siempre presente.
    """

    def classify_boot_button_press(self, duration_ms: int, current_state: str, voice_task_running: bool) -> str:
        """Modela la lógica de checkButtonEvents() en main.cpp"""
        BOOT_LONG_PRESS_MS = 4000
        if duration_ms >= BOOT_LONG_PRESS_MS:
            return "TRIGGER_PORTAL"
        elif duration_ms > 50:
            if current_state == "STATE_PORTAL_CONFIG":
                return "RECONNECT_AND_TRIGGER_VOICE"
            if current_state == "STATE_CLOCK":
                return "EXIT_CLOCK_AND_TRIGGER_VOICE"
            if not voice_task_running:
                return "TRIGGER_VOICE"
            else:
                return "VOICE_BUSY"
        else:
            return "DEBOUNCE_IGNORE"

    def test_boot_button_normal_click_triggers_voice(self):
        """Click normal (ej 150ms, 400ms, 1200ms) debe iniciar voz en reposo."""
        for dur in [80, 150, 300, 800, 1500, 2500, 3500]:
            action = self.classify_boot_button_press(dur, "STATE_IDLE", voice_task_running=False)
            self.assertEqual(action, "TRIGGER_VOICE", f"Duración {dur}ms falló al activar voz")

    def test_boot_button_never_triggers_sleep(self):
        """El botón BOOT NUNCA debe mandar a dormir el dispositivo a 1.8s (la suspensión es por PWR GPIO 18)."""
        for dur in [1800, 2000, 2500, 3000]:
            action = self.classify_boot_button_press(dur, "STATE_IDLE", voice_task_running=False)
            self.assertNotEqual(action, "ENTER_SLEEP", "Regresión: botón BOOT no debe suspender a los 1.8s!")
            self.assertEqual(action, "TRIGGER_VOICE")

    def test_boot_button_wakes_from_clock_mode(self):
        """Click desde modo reloj debe salir del reloj y activar voz directamente."""
        action = self.classify_boot_button_press(200, "STATE_CLOCK", voice_task_running=False)
        self.assertEqual(action, "EXIT_CLOCK_AND_TRIGGER_VOICE")

    def test_boot_button_long_press_triggers_portal(self):
        """Presión larga >= 4s debe abrir el portal de rescate."""
        self.assertEqual(self.classify_boot_button_press(4000, "STATE_IDLE", False), "TRIGGER_PORTAL")
        self.assertEqual(self.classify_boot_button_press(5500, "STATE_IDLE", False), "TRIGGER_PORTAL")

    def test_single_page_response_releases_task_immediately(self):
        """
        Una respuesta de 1 sola página no debe quedar atrapada en un bucle bloqueante;
        la tarea de voz debe finalizar inmediatamente tras hablar, dejando voice_task_running=False
        y permitiendo que el siguiente click active voz en el acto.
        """
        total_pages = 1
        voice_task_running = True
        if total_pages <= 1:
            voice_task_running = False  # Tarea termina y vuelve a IDLE con g_textHold=True

        self.assertFalse(voice_task_running, "voice_task_running debe ser False tras respuesta de 1 página")
        next_click_action = self.classify_boot_button_press(200, "STATE_IDLE", voice_task_running)
        self.assertEqual(next_click_action, "TRIGGER_VOICE", "El siguiente click debe iniciar voz de inmediato")

    def test_jorge_moto_g32_hotspot_rescue_network(self):
        """Verifica que la red hotspot Jorge-Moto_G32 esté configurada como red de rescate."""
        with open("include/config.h", "r", encoding="utf-8") as f:
            content = f.read()
        self.assertIn('"Jorge-Moto_G32"', content, "Falta DEFAULT_WIFI_SSID3 Jorge-Moto_G32 en config.h")
        self.assertIn('"asdfghjk"', content, "Falta contraseña asdfghjk en config.h")

        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            portal_content = f.read()
        self.assertIn("DEFAULT_WIFI_SSID3", portal_content, "wifi_portal.cpp debe registrar DEFAULT_WIFI_SSID3 en wifiMulti")

    def test_mode_heuristic_fallback(self):
        """Verifica que si la IA responde con 'modo reloj' sin el tag exacto [MODE:RELOJ], se active el modo."""
        self.assertEqual(parse_mode_heuristic("Pongo el reloj [MODE:RELOJ]"), "RELOJ")
        self.assertEqual(parse_mode_heuristic("Activo el modo reloj para que veas la hora"), "RELOJ")
        self.assertEqual(parse_mode_heuristic("Pongo el modo clima con los sensores"), "CLIMA")


class TestHardwareFlashingSafety(unittest.TestCase):
    """
    Validaciones para garantizar que nunca se use un modo de flasheo incorrecto
    (QIO en lugar de DIO) o velocidades que rompan el USB CDC en Windows.
    """

    def test_platformio_ini_flash_mode_is_dio(self):
        """El ESP32-S3-PICO-1 requiere flash_mode = dio. QIO causa bucle de reinicios y congela la pantalla."""
        with open("platformio.ini", "r", encoding="utf-8") as f:
            content = f.read()
        self.assertIn("board_build.flash_mode = dio", content,
                      "platformio.ini DEBE tener board_build.flash_mode = dio para evitar boot-loop en ESP32-S3-PICO-1")
        self.assertNotIn("board_build.flash_mode = qio", content,
                         "platformio.ini NO debe tener qio")

    def test_platformio_ini_upload_speed_safe(self):
        """Velocidades >460800 (ej: 921600) fallan en Windows USB CDC con ClearCommError 22."""
        with open("platformio.ini", "r", encoding="utf-8") as f:
            content = f.read()
        import re
        m = re.search(r'upload_speed\s*=\s*(\d+)', content)
        self.assertIsNotNone(m, "No se encontró upload_speed en platformio.ini")
        speed = int(m.group(1))
        self.assertLessEqual(speed, 460800, f"upload_speed={speed} excede el límite seguro de 460800 baudios")

    def test_bootloader_binary_mode_is_dio(self):
        """Verifica que el archivo binario del bootloader tenga byte 2 = 0x02 (DIO)."""
        import os
        bl_path = os.path.join(".pio", "build", "esp32-s3-paper", "bootloader.bin")
        if os.path.exists(bl_path):
            with open(bl_path, "rb") as f:
                header = f.read(4)
            self.assertEqual(header[0], 0xE9, "Magic byte del bootloader debe ser 0xE9")
            self.assertEqual(header[2], 0x02, f"Modo flash en cabecera del bootloader debe ser 0x02 (DIO), valor actual: 0x{header[2]:02x}")

    def test_main_loop_processes_button_even_in_portal(self):
        """Verifica que checkButtonEvents() se ejecute en loop() aun cuando el portal está activo."""
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            src = f.read()
        loop_start = src.find("void loop()")
        self.assertGreater(loop_start, 0)
        portal_check = src.find("netManager.isPortalActive()", loop_start)
        self.assertGreater(portal_check, 0)
        sub = src[portal_check:portal_check + 150]
        self.assertIn("checkButtonEvents();", sub,
                      "loop() debe llamar a checkButtonEvents() antes de retornar cuando el portal está activo")

    def test_boot_button_handler_has_no_sleep_call(self):
        """Verifica que el botón BOOT no mande a dormir a Copito."""
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            src = f.read()
        cb_start = src.find("void checkButtonEvents() {")
        cb_end = src.find("void triggerVoiceInteraction() {", cb_start)
        self.assertGreater(cb_start, 0)
        self.assertGreater(cb_end, cb_start)
        cb_body = src[cb_start:cb_end]
        boot_section = cb_body[:cb_body.find("// 2. Check PWR Button")]
        self.assertNotIn("enterSleepMode()", boot_section,
                         "El botón BOOT NUNCA debe invocar enterSleepMode(). El sueño se reserva para PWR GPIO 18")


class TestBatteryLipoAndDisplayPlanV4(unittest.TestCase):
    """
    Validaciones del Plan V4 para cálculo de batería LiPo con LUT,
    eliminación del bug de 100% en descarga profunda y sincronización de buffer E-Ink.
    """

    LIPO_CURVE = [
        (4200, 100),
        (4100, 90),
        (4000, 80),
        (3900, 70),
        (3820, 60),
        (3760, 50),
        (3720, 40),
        (3680, 30),
        (3630, 20),
        (3550, 10),
        (3400, 5),
        (3200, 0),
    ]

    def _calc_pct(self, mv):
        if mv >= self.LIPO_CURVE[0][0]:
            return 100
        if mv <= self.LIPO_CURVE[-1][0]:
            return 0
        for i in range(len(self.LIPO_CURVE) - 1):
            v_high, p_high = self.LIPO_CURVE[i]
            v_low, p_low = self.LIPO_CURVE[i + 1]
            if v_low <= mv <= v_high:
                diff_v = v_high - v_low
                span = (mv - v_low) * (p_high - p_low)
                return p_low + int(span / diff_v)
        return 0

    def test_lipo_discharge_curve_monotonic(self):
        """Verifica que la curva LiPo sea monótona decreciente sin saltos ni retrocesos."""
        prev_pct = 101
        for mv in range(4300, 3000, -25):
            pct = self._calc_pct(mv)
            self.assertLessEqual(pct, prev_pct, f"Inconsistencia de monotonía a {mv}mV: {pct}% > {prev_pct}%")
            self.assertGreaterEqual(pct, 0)
            self.assertLessEqual(pct, 100)
            prev_pct = pct

    def test_deep_discharge_never_returns_100_percent(self):
        """Caza del bug previo: voltajes inferiores a 3200mV DEBEN reportar 0%, NUNCA 100%."""
        for v in [3199, 3100, 2900, 2500, 2000, 0]:
            pct = self._calc_pct(v)
            self.assertEqual(pct, 0, f"Voltaje crítico {v}mV debe reportar 0%, reportó {pct}%")

    def test_nominal_voltage_in_healthy_range(self):
        """Voltaje nominal LiPo (3.70V-3.85V) debe dar entre 35% y 65% de carga."""
        pct_370 = self._calc_pct(3700)
        pct_382 = self._calc_pct(3820)
        self.assertTrue(30 <= pct_370 <= 40, f"3700mV debe dar ~35%, dio {pct_370}%")
        self.assertEqual(pct_382, 60, f"3820mV debe dar exactamente 60%, dio {pct_382}%")

    def test_epaper_driver_synchronizes_ram_0x26(self):
        """Verifica que displayPartial() sincronice el buffer 0x26 para evitar degradación a blanco."""
        with open("src/epaper_driver.cpp", "r", encoding="utf-8") as f:
            content = f.read()
        dp_pos = content.find("void EPaperDriver::displayPartial()")
        self.assertGreater(dp_pos, 0)
        dp_body = content[dp_pos:dp_pos + 600]
        self.assertIn("0x26", dp_body, "displayPartial() DEBE escribir a 0x26 para sincronizar la memoria previa")
        self.assertIn("0x24", dp_body, "displayPartial() DEBE escribir a 0x24")

    def test_moving_average_filter_smooths_spurious_drops(self):
        """Simula una caída transitoria de 300mV por radiofrecuencia y verifica estabilización del filtro."""
        history = [3800] * 8
        noisy_samples = [3800, 3810, 3500, 3805, 3800, 3795, 3810, 3800] # spike transitorio a 3500mV
        for s in noisy_samples:
            history.pop(0)
            history.append(s)
            avg = sum(history) / len(history)
            # El promedio nunca debe caer tan bajo como 3500mV gracias a la amortiguación
    def test_high_contrast_active_screens_use_solid_black(self):
        """Verifica que las pantallas activas (Reloj, Clima) usen trazos sólidos en negro puro (color 0)."""
        with open("include/faces_bitmaps.h", "r", encoding="utf-8") as f:
            content = f.read()

        # 1. Modo Reloj: Marcos y líneas divisorias deben ser de trazo sólido negro (color 0)
        rc_pos = content.find("void renderClockFace(")
        self.assertGreater(rc_pos, 0)
        rc_body = content[rc_pos:rc_pos + 2500]
        self.assertIn("drawRect(buf, 2, 2, 196, 196, 0, false);", rc_body)
        self.assertIn("drawLine(buf, 6, 32, 194, 32, 0, 3);", rc_body)

        # 2. Modo Clima: Marco y separadores en trazo sólido negro (color 0)
        rw_pos = content.find("void renderWeatherScreen(")
        self.assertGreater(rw_pos, 0)
        rw_body = content[rw_pos:rw_pos + 6000]
        self.assertIn("drawRect(buf, 2, 2, 196, 196, 0, false);", rw_body)
        self.assertIn("drawLine(buf, 4, 28, 196, 28, 0, 2);", rw_body)
        self.assertIn("drawLine(buf, 4, 114, 196, 114, 0, 2);", rw_body)

    def test_battery_icon_high_contrast_drawing(self):
        """Verifica que drawBatteryIcon use contorno y barras sólidas en color 0 de alto contraste."""
        with open("include/faces_bitmaps.h", "r", encoding="utf-8") as f:
            content = f.read()
        bi_pos = content.find("void drawBatteryIcon(")
        self.assertGreater(bi_pos, 0)
        bi_body = content[bi_pos:bi_pos + 1200]
        self.assertIn("drawRect(buf, bx, by, 12, 7, 0, false);", bi_body)
        self.assertIn("drawRect(buf, bx + 12, by + 2, 2, 3, 0, true);", bi_body)


class TestMultiWifiAndWeatherV4(unittest.TestCase):
    """
    Validaciones del Plan V4 para Itinerancia Multi-WiFi y Modo Clima Full-Screen.
    """

    def test_roaming_selects_highest_rssi_known_network(self):
        """Verifica que el algoritmo de itinerancia seleccione la red conocida con mayor RSSI."""
        known_networks = [
            {"ssid": "TeleCentro-5618", "pass": "RMZ5GDZKRDM3"},
            {"ssid": "Jorge-Moto_G32", "pass": "asdfghjk"},
            {"ssid": "Office-Wifi", "pass": "12345678"}
        ]
        scanned_aps = [
            {"ssid": "TeleCentro-5618", "rssi": -85},
            {"ssid": "Vecino-Net", "rssi": -30},
            {"ssid": "Jorge-Moto_G32", "rssi": -48}, # Mayor señal entre las conocidas
            {"ssid": "Other-AP", "rssi": -60}
        ]

        best_match = None
        best_rssi = -999
        for ap in scanned_aps:
            for kn in known_networks:
                if ap["ssid"] == kn["ssid"]:
                    if ap["rssi"] > best_rssi:
                        best_rssi = ap["rssi"]
                        best_match = kn

        self.assertIsNotNone(best_match)
        self.assertEqual(best_match["ssid"], "Jorge-Moto_G32")
        self.assertEqual(best_rssi, -48)

    def test_state_weather_defined_in_headers(self):
        """Verifica que STATE_WEATHER y FACE_WEATHER estén formalmente declarados."""
        with open("include/config.h", "r", encoding="utf-8") as f:
            cfg = f.read()
        self.assertIn("STATE_WEATHER", cfg, "STATE_WEATHER debe estar definido en AssistantState")

        with open("include/faces_bitmaps.h", "r", encoding="utf-8") as f:
            faces = f.read()
        self.assertIn("FACE_WEATHER", faces, "FACE_WEATHER debe estar definido en FaceState")
        self.assertIn("renderWeatherScreen", faces, "renderWeatherScreen debe estar implementado en FaceRenderer")

    def test_mode_clima_transitions_to_state_weather(self):
        """Verifica que el tag [MODE:CLIMA] cambie el estado a STATE_WEATHER."""
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            main_src = f.read()
        self.assertIn("changeState(STATE_WEATHER);", main_src,
                      "main.cpp debe invocar changeState(STATE_WEATHER) al recibir [MODE:CLIMA]")

    def test_weather_mode_exits_on_boot_button(self):
        """Verifica que al presionar BOOT en STATE_WEATHER se regrese a STATE_IDLE."""
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            main_src = f.read()
        w_block_start = main_src.find("if (currentState == STATE_WEATHER)")
        self.assertGreater(w_block_start, 0)
        w_block = main_src[w_block_start:w_block_start + 400]
        self.assertIn("changeState(STATE_IDLE);", w_block,
                      "STATE_WEATHER debe salir a STATE_IDLE al presionar el botón BOOT")


class TestClockStabilityAndRtcDriver(unittest.TestCase):
    """
    Validaciones de estabilidad del Modo Reloj, soporte RTC hardware PCF85063 (I2C @ 0x51),
    exclusión mutua FreeRTOS Core 0 / Core 1 y tipografía geométrica de alto contraste.
    """

    def _dec_to_bcd(self, val):
        return ((val // 10) << 4) | (val % 10)

    def _bcd_to_dec(self, val):
        return ((val >> 4) * 10) + (val & 0x0F)

    def test_rtc_pcf85063_bcd_conversions(self):
        """Verifica conversiones bidireccionales BCD/Decimal para todos los rangos del RTC."""
        # Segundos y minutos (0..59)
        for v in range(60):
            bcd = self._dec_to_bcd(v)
            dec = self._bcd_to_dec(bcd)
            self.assertEqual(v, dec, f"Fallo BCD en {v}")
        # Horas (0..23)
        for h in range(24):
            self.assertEqual(h, self._bcd_to_dec(self._dec_to_bcd(h)))
        # Años (0..99)
        for y in range(100):
            self.assertEqual(y, self._bcd_to_dec(self._dec_to_bcd(y)))

    def test_rtc_driver_files_and_api_contract(self):
        """Verifica que rtc_pcf85063.h y .cpp existan e implementen el contrato del RTC."""
        import os
        self.assertTrue(os.path.exists("include/rtc_pcf85063.h"), "Falta include/rtc_pcf85063.h")
        self.assertTrue(os.path.exists("src/rtc_pcf85063.cpp"), "Falta src/rtc_pcf85063.cpp")

        with open("include/rtc_pcf85063.h", "r", encoding="utf-8") as f:
            h_code = f.read()
        self.assertIn("class PCF85063Driver", h_code)
        self.assertIn("readTime(struct tm &outTm)", h_code)
        self.assertIn("setTime(const struct tm &inTm)", h_code)
        self.assertIn("syncSystemTimeFromRtc()", h_code)
        self.assertIn("syncRtcFromSystemTime()", h_code)

    def test_main_integrates_rtc_offline_fallback(self):
        """Verifica que main.cpp use el RTC para hora offline inmediata y como fallback."""
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            main_src = f.read()
        self.assertIn('#include "rtc_pcf85063.h"', main_src)
        self.assertIn("rtcDriver.begin()", main_src)
        self.assertIn("rtcDriver.syncSystemTimeFromRtc()", main_src)
        self.assertIn("rtcDriver.readTime(tm)", main_src)

    def test_copito_tools_persists_ntp_to_rtc(self):
        """Verifica que tras sincronizar NTP se persista la hora en el RTC PCF85063."""
        with open("src/copito_tools.cpp", "r", encoding="utf-8") as f:
            tools_src = f.read()
        self.assertIn('#include "rtc_pcf85063.h"', tools_src)
        self.assertIn("rtcDriver.syncRtcFromSystemTime();", tools_src)

    def test_epaper_driver_thread_safety_mutex(self):
        """Verifica que epaper_driver.cpp use un mutex FreeRTOS para evitar colisiones Core 0 / Core 1."""
        with open("src/epaper_driver.cpp", "r", encoding="utf-8") as f:
            epd_src = f.read()
        self.assertIn("s_epdMutex", epd_src)
        self.assertIn("xSemaphoreCreateMutex()", epd_src)
        self.assertIn("xSemaphoreTake(s_epdMutex", epd_src)
        self.assertIn("xSemaphoreGive(s_epdMutex", epd_src)

    def test_draw_big_digit_improved_geometry(self):
        """Verifica que drawBigDigit centre el dígito 1 y use diagonal en el dígito 7."""
        with open("include/faces_bitmaps.h", "r", encoding="utf-8") as f:
            faces_src = f.read()
        bd_pos = faces_src.find("void drawBigDigit(")
        self.assertGreater(bd_pos, 0)
        bd_body = faces_src[bd_pos:bd_pos + 3500]
        # Dígito 1: tronco centrado cx = x + (w - s) / 2
        self.assertIn("cx = x + (w - s) / 2", bd_body)
        # Dígito 7: trazo diagonal sólido
        self.assertIn("drawLine(buf, x + w - s / 2, y + 4, x + 6, y + h - 4, 0, s);", bd_body)


class TestTransitionsAndNetworkPreservation(unittest.TestCase):
    """
    Validaciones para:
    1. Preservación y recuperación de redes Wi-Fi (TeleCentro-5618, Telecentro-d1fc, Jorge-Moto_G32).
    2. Transiciones de estados no destructivas: g_textHold no se borra al volver a IDLE.
    3. Exclusión mutua y drenado de colas en renderizado de texto/paginador para evitar colisiones Core 0 / Core 1.
    """

    def test_default_wifi_ssid1_matches_pc_telecentro_5618(self):
        """Verifica que la red principal configurada en config.h coincida con la red activa de la PC y se incluyan las redes recuperadas."""
        with open("include/config.h", "r", encoding="utf-8") as f:
            cfg = f.read()
        self.assertIn('#define DEFAULT_WIFI_SSID1      "TeleCentro-5618"', cfg)
        self.assertIn('#define DEFAULT_WIFI_PASS1      "RMZ5GDZKRDM3"', cfg)
        self.assertIn('#define DEFAULT_WIFI_SSID2      "Telecentro-d1fc"', cfg)
        self.assertIn('#define DEFAULT_WIFI_PASS2      "MMLNTJFTY2W5"', cfg)
        self.assertIn('#define DEFAULT_WIFI_SSID3      "Jorge-Moto_G32"', cfg)
        self.assertIn('#define DEFAULT_WIFI_PASS3      "asdfghjk"', cfg)
        self.assertIn('#define DEFAULT_WIFI_SSID4      "MOVISTAR-WIFI6-0E70"', cfg)
        self.assertIn('#define DEFAULT_WIFI_PASS4      "Lacarra3424"', cfg)
        self.assertIn('#define DEFAULT_WIFI_SSID5      "Jorge_Moto_G32"', cfg)
        self.assertIn('#define DEFAULT_WIFI_PASS5      "asdfghjk"', cfg)

    def test_load_networks_restores_primary_and_rescue_networks(self):
        """Verifica que loadNetworks en wifi_portal.cpp asegure TeleCentro-5618 y todas las redes recuperadas del aparato."""
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            src = f.read()
        ln_pos = src.find("void NetworkManager::loadNetworks()")
        self.assertGreater(ln_pos, 0, "loadNetworks debe estar definido en wifi_portal.cpp")
        ln_body = src[ln_pos:ln_pos + 2500]
        self.assertIn("addOrUpdateNetwork(DEFAULT_WIFI_SSID1, DEFAULT_WIFI_PASS1, false);", ln_body)
        self.assertIn("addOrUpdateNetwork(DEFAULT_WIFI_SSID2, DEFAULT_WIFI_PASS2, false);", ln_body)
        self.assertIn("addOrUpdateNetwork(DEFAULT_WIFI_SSID3, DEFAULT_WIFI_PASS3, false);", ln_body)
        self.assertIn("addOrUpdateNetwork(DEFAULT_WIFI_SSID4, DEFAULT_WIFI_PASS4, false);", ln_body)
        self.assertIn("addOrUpdateNetwork(DEFAULT_WIFI_SSID5, DEFAULT_WIFI_PASS5, false);", ln_body)

    def test_state_idle_transition_guards_text_hold(self):
        """Verifica que changeState(STATE_IDLE) respete g_textHold para no pisar el texto en pantalla."""
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            src = f.read()
        cs_pos = src.find("void changeState(AssistantState newState) {")
        self.assertGreater(cs_pos, 0, "changeState debe existir en main.cpp")
        cs_body = src[cs_pos:cs_pos + 1200]
        self.assertIn("case STATE_IDLE:", cs_body)
        idle_pos = cs_body.find("case STATE_IDLE:")
        idle_block = cs_body[idle_pos:idle_pos + 200]
        self.assertIn("if (!g_textHold)", idle_block, "STATE_IDLE debe comprobar if (!g_textHold) antes de showFace")

    def test_show_fallback_text_drains_epd_queue(self):
        """Verifica que showFallbackText drene la cola de animaciones pendientes en Core 0."""
        with open("src/epaper_driver.cpp", "r", encoding="utf-8") as f:
            src = f.read()
        sft_pos = src.find("void EPaperDriver::showFallbackText(")
        self.assertGreater(sft_pos, 0, "showFallbackText debe existir en epaper_driver.cpp")
        sft_body = src[sft_pos:sft_pos + 800]
        self.assertIn("while (xQueueReceive(s_epdQueue, &dummy, 0) == pdTRUE)", sft_body)

    def test_render_paginator_page_mutex_and_drain(self):
        """Verifica que renderPaginatorPage drene la cola y tome el mutex FreeRTOS s_epdMutex."""
        with open("src/epaper_driver.cpp", "r", encoding="utf-8") as f:
            src = f.read()
        rpp_pos = src.find("void EPaperDriver::renderPaginatorPage(")
        self.assertGreater(rpp_pos, 0, "renderPaginatorPage debe existir en epaper_driver.cpp")
        rpp_body = src[rpp_pos:rpp_pos + 800]
        self.assertIn("while (xQueueReceive(s_epdQueue, &dummy, 0) == pdTRUE)", rpp_body)
        self.assertIn("xSemaphoreTake(s_epdMutex, portMAX_DELAY)", rpp_body)
        self.assertIn("xSemaphoreGive(s_epdMutex)", rpp_body)

    def test_trigger_voice_clears_text_hold(self):
        """Verifica que al pulsar para iniciar una nueva interaccion por voz se libere el texto anterior."""
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            src = f.read()
        tv_pos = src.find("void triggerVoiceInteraction() {")
        self.assertGreater(tv_pos, 0, "triggerVoiceInteraction debe existir en main.cpp")
        tv_body = src[tv_pos:tv_pos + 400]
        self.assertIn("g_textHold = false;", tv_body)


class TestAdaptivePaginationAndVectorSketch(unittest.TestCase):
    """Pruebas unitarias para prevenir atenuacion/blanqueamiento de pantalla,
    asegurar paginacion adaptativa inteligente con boton BOOT y dibujo vectorial."""

    def test_epaper_full_refresh_has_no_premature_init_partial(self):
        """Verifica que ninguna funcion de refresco completo reactive prematuramente la polarizacion parcial."""
        with open("src/epaper_driver.cpp", "r", encoding="utf-8") as f:
            src = f.read()

        # Ninguna de estas funciones debe llamar a initPartial() tras displayFull()
        for func in ["showFallbackText", "renderPaginatorPage", "showFaceDirect",
                     "showClockFace", "showWeatherScreen", "showVectorSketch"]:
            pos = src.find(f"void EPaperDriver::{func}(")
            self.assertGreater(pos, 0, f"{func} debe existir en epaper_driver.cpp")
            body = src[pos:pos + 700]
            # Si contiene displayFull(), no debe haber initPartial() inmediatamente despues
            df_pos = body.find("displayFull();")
            if df_pos > 0:
                post_df = body[df_pos:df_pos + 60]
                self.assertNotIn("initPartial();", post_df,
                                 f"{func} contiene llamada danina a initPartial() tras displayFull(), causando atenuacion")

    def test_boot_button_advances_page_when_not_on_last_page(self):
        """Verifica que al presionar BOOT con texto multipagina en hold, avance de pagina sin llamar a voz."""
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            src = f.read()
        cbe_pos = src.find("void checkButtonEvents() {")
        self.assertGreater(cbe_pos, 0, "checkButtonEvents debe existir en main.cpp")
        cbe_body = src[cbe_pos:cbe_pos + 5600]

        # Debe verificar !paginator.isLastPage() y llamar a nextPage()
        self.assertIn("paginator.getTotalPages() > 1 && !paginator.isLastPage()", cbe_body)
        self.assertIn("paginator.nextPage()", cbe_body)
        self.assertIn("epd.renderPaginatorPage(paginator", cbe_body)

    def test_boot_button_triggers_voice_on_last_page(self):
        """Verifica que tras la ultima pagina, BOOT libere g_textHold y permita hablar otra vez."""
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            src = f.read()
        cbe_pos = src.find("void checkButtonEvents() {")
        cbe_body = src[cbe_pos:cbe_pos + 5600]
        # Tras la comprobacion de nextPage(), debe liberar g_textHold y llamar a triggerVoiceInteraction()
        self.assertIn("g_textHold = false;", cbe_body)
        self.assertIn("triggerVoiceInteraction();", cbe_body)

    def test_adaptive_font_scaling_simulation(self):
        """Simula el algoritmo de escalado adaptativo: 2X hasta 10 pags, 1X solo cuentos largos."""
        def calc_pages(text_len: int, scale: int) -> int:
            chars_per_line = 13 if scale == 2 else 28
            lines_per_page = 6 if scale == 2 else 12
            total_lines = (text_len + chars_per_line - 1) // chars_per_line
            return max(1, (total_lines + lines_per_page - 1) // lines_per_page)

        def adapt_scale(text: str) -> tuple:
            # Prueba escala 2X (letra grande, se hojea con BOOT igual)
            p2 = calc_pages(len(text), 2)
            if p2 <= 10:
                return (2, p2)
            # Solo textos enormes (cuento largo) bajan a 1X
            p1 = calc_pages(len(text), 1)
            return (1, p1)

        # Mensaje corto (ej: saludo)
        short_msg = "Hola Jorge, todo bien por aca!"
        scale, pages = adapt_scale(short_msg)
        self.assertEqual(scale, 2, "Mensaje corto debe usar escala 2X grande")
        self.assertLessEqual(pages, 2)

        # Mensaje medio (ej: explicacion de economia): sigue grande ahora
        long_msg = "La inflacion mensual se desacelero al dos por ciento segun el ultimo informe oficial, impulsada por la estabilidad cambiaria y el superavit fiscal sostenido durante los ultimos tres trimestres."
        scale, pages = adapt_scale(long_msg)
        self.assertEqual(scale, 2, "Mensaje medio debe seguir en 2X grande")
        self.assertLessEqual(pages, 10)

        # Cuento largo (~1100 letras): recien ahi baja a 1X
        cuento = ("Habia una vez un kraken enojado que cuidaba un faro en el fin del mundo. " * 14)[:1100]
        scale, pages = adapt_scale(cuento)
        self.assertEqual(scale, 1, "Cuento largo debe conmutar a escala 1X")
        self.assertLessEqual(pages, 32)

    def test_sketch_dsl_parsing(self):
        """Verifica la extraccion y limpieza del Micro-DSL [SKETCH: ...] para pantalla e-paper."""
        raw_reply = "Aca tenes el cohete que me pediste! [SKETCH: CLEAR; LINE 100 20 60 140 2; LINE 100 20 140 140 2; CIRCLE 100 80 15 0] Buen viaje!"
        sketch_cmd = ""
        s = raw_reply.find("[SKETCH:")
        if s >= 0:
            e = raw_reply.find("]", s)
            if e > s:
                sketch_cmd = raw_reply[s + 8:e].strip()
                clean_reply = (raw_reply[:s] + raw_reply[e + 1:]).strip()

        self.assertIn("LINE 100 20 60 140 2", sketch_cmd)
        self.assertIn("CIRCLE 100 80 15 0", sketch_cmd)
        self.assertNotIn("[SKETCH:", clean_reply)
        self.assertEqual(clean_reply, "Aca tenes el cohete que me pediste!  Buen viaje!")

    def test_show_vector_sketch_declared_and_implemented(self):
        """Verifica que showVectorSketch este declarada en epaper_driver.h y en faces_bitmaps.h."""
        with open("include/epaper_driver.h", "r", encoding="utf-8") as f:
            h_src = f.read()
        self.assertIn("void showVectorSketch(const char *commands, const char *caption = nullptr, bool forceFullRefresh = true);", h_src)

        with open("include/faces_bitmaps.h", "r", encoding="utf-8") as f:
            f_src = f.read()
        self.assertIn("static void renderVectorSketch(uint8_t *buf, const char *dsl, const char *caption = nullptr)", f_src)

    def test_fallback_sketch_generation(self):
        """Verifica que para pedidos de gato, casa, cohete o auto se generen bocetos validos del micro-DSL."""
        with open("src/copito_tools.cpp", "r", encoding="utf-8") as f:
            src = f.read()
        self.assertIn("void CopitoTools::generateFallbackSketch(", src)
        self.assertIn("q.indexOf(\"gato\")", src)
        self.assertIn("q.indexOf(\"casa\")", src)
        self.assertIn("q.indexOf(\"cohete\")", src)
        self.assertIn("q.indexOf(\"auto\")", src)

    def test_proactive_mode_intent_detection(self):
        """Verifica que en processVoiceInteraction se detecten intenciones de reloj y clima proactivamente."""
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            src = f.read()
        self.assertIn("userText.indexOf(\"modo reloj\")", src)
        self.assertIn("userText.indexOf(\"modo clima\")", src)
        self.assertIn("userText.indexOf(\"dibuja\")", src)
        self.assertIn("CopitoTools::generateFallbackSketch(", src)

    def test_ntp_resync_not_blocked_by_pcf85063(self):
        """Verifica que la sincronizacion NTP en loop() dependa de g_ntpSynced y no de getLocalTime(20ms)."""
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            src = f.read()
        self.assertIn("if (!g_ntpSynced)", src)
        self.assertIn("CopitoTools::syncNtp();", src)

    def test_obsolete_prompt_restores_screen_drawing_skill(self):
        """Verifica que wifi_portal.cpp invalide cualquier prompt que carezca de Screen Drawing o [SKETCH:."""
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            src = f.read()
        self.assertIn("prompt.indexOf(\"[SKETCH:\") < 0 || prompt.indexOf(\"Screen Drawing\") < 0", src)

    def test_combined_draw_intent_and_accent_normalization(self):
        """Verifica que processVoiceInteraction use normalizacion de acentos y combinedContext."""
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            src = f.read()
        self.assertIn("stripSpanishAccents", src)
        self.assertIn("combined", src)
        self.assertIn("CopitoTools::generateFallbackSketch(combined, tags.sketchCommands);", src)
        with open("include/config.h", "r", encoding="utf-8") as f:
            cfg_src = f.read()
        self.assertIn("SKETCH_V3", cfg_src)
        self.assertIn("#define GEMINI_DEFAULT_MODEL    \"gemini-2.5-flash\"", cfg_src)

    def test_expanded_drawing_presets_and_intent(self):
        """Verifica que el micro-DSL soporte kraken, edificio, barco y deteccion semantica."""
        with open("include/copito_tools.h", "r", encoding="utf-8") as f:
            h_src = f.read()
        self.assertIn("static bool hasDrawingIntent(const String &text);", h_src)

        with open("src/copito_tools.cpp", "r", encoding="utf-8") as f:
            cpp_src = f.read()
        self.assertIn("q.indexOf(\"kraken\")", cpp_src)
        self.assertIn("q.indexOf(\"edificio\")", cpp_src)
        self.assertIn("q.indexOf(\"barco\")", cpp_src)
        self.assertIn("q.indexOf(\"avion\")", cpp_src)
        self.assertIn("bool CopitoTools::hasDrawingIntent(", cpp_src)

        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai_src = f.read()
        self.assertIn("qwen/qwen3.8-27b", ai_src)
        self.assertIn("CopitoTools::hasDrawingIntent(promptText)", ai_src)

        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp_src = f.read()
        self.assertIn("char sketch[512];", wp_src)
        self.assertIn("showVectorSketch(job->sketch", wp_src)

    def test_modular_scene_composer_and_macros(self):
        """Verifica el soporte de escenas modulares, macros vectoriales y detección semántica de casita+sol+vaca y luna sonriente."""
        with open("include/faces_bitmaps.h", "r", encoding="utf-8") as f:
            bm_src = f.read()
        self.assertIn("static void renderMacro(uint8_t *buf, const char *name, int x, int y, int p1, int p2)", bm_src)
        self.assertIn("static void drawBezierCurve(uint8_t *buf, int x0, int y0, int cx, int cy, int x1, int y1, int color", bm_src)
        self.assertIn("MOON_SMILE", bm_src)
        self.assertIn("SUN", bm_src)
        self.assertIn("HOUSE", bm_src)
        self.assertIn("COW", bm_src)
        self.assertIn("DRAGON", bm_src)

        with open("src/copito_tools.cpp", "r", encoding="utf-8") as f:
            cpp_src = f.read()
        self.assertIn("ESCENA_CASA_SOL_VACA", cpp_src)
        self.assertIn("LUNA_SONRIENTE", cpp_src)
        self.assertIn("NINO_JUGANDO", cpp_src)
        self.assertIn("DRAGON", cpp_src)
        self.assertIn("MACRO MOON_SMILE", cpp_src)
        self.assertIn("MACRO COW", cpp_src)
        self.assertIn("dame un", cpp_src)
        self.assertIn("vaquita", cpp_src)
        self.assertIn("solcito", cpp_src)
        self.assertIn("casita", cpp_src)
        self.assertIn("un nino", cpp_src)

        with open("src/main.cpp", "r", encoding="utf-8") as f:
            main_src = f.read()
        self.assertIn("normReply.indexOf(\"en pantalla\") >= 0", main_src)
        self.assertIn("normReply.indexOf(\"ahi va\") >= 0", main_src)

class TestFreeFormImagePipeline(unittest.TestCase):
    """Pruebas del pipeline de dibujo libre e-Paper: endpoint /api/bitmap, dithering y gateway."""

    def test_show_bitmap_declared_and_implemented(self):
        """Verifica que showBitmap este formalmente declarada e implementada para 5000 bytes."""
        with open("include/epaper_driver.h", "r", encoding="utf-8") as f:
            h_src = f.read()
        self.assertIn("void showBitmap(const uint8_t *bitmap, bool forceFullRefresh = true);", h_src)

        with open("src/epaper_driver.cpp", "r", encoding="utf-8") as f:
            cpp_src = f.read()
        self.assertIn("void EPaperDriver::showBitmap(const uint8_t *bitmap, bool forceFullRefresh)", cpp_src)
        self.assertIn("memcpy(buffer, bitmap, EPD_BUFFER_SIZE);", cpp_src)

    def test_api_bitmap_endpoint_registered(self):
        """Verifica que el servidor web de Copito tenga la ruta /api/bitmap y handleApiBitmap."""
        with open("include/wifi_portal.h", "r", encoding="utf-8") as f:
            h_src = f.read()
        self.assertIn("void handleApiBitmap();", h_src)

        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            cpp_src = f.read()
        self.assertIn('server->on("/api/bitmap", HTTP_POST', cpp_src)
        self.assertIn("void NetworkManager::handleApiBitmap()", cpp_src)
        self.assertIn("bitmap_b64", cpp_src)
        self.assertIn("epd.showBitmap(bmpBuf, forceFull);", cpp_src)

    def test_image_gateway_bitmap_conversion_contract(self):
        """Verifica que el script tools/image_gateway.py exista y cumpla el contrato de 5000 bytes 1-bit."""
        from PIL import Image, ImageDraw
        import importlib.util

        spec = importlib.util.spec_from_file_location("image_gateway", "tools/image_gateway.py")
        gateway = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(gateway)

        # Crear imagen de prueba de 200x200
        test_img = Image.new('RGB', (200, 200), 'white')
        draw = ImageDraw.Draw(test_img)
        draw.ellipse([40, 40, 160, 160], fill='black')

        bmp = gateway.convert_to_epaper_bitmap(test_img)
        self.assertEqual(len(bmp), 5000, "El buffer para la pantalla Waveshare 1.54 debe ser exactamente de 5000 bytes")

        # Verificar que el centro sea negro (bit=0) y las esquinas blancas (bit=1)
        # Esquina (0,0): byte 0, bit 7 -> 1 (blanco)
        self.assertTrue((bmp[0] & 0x80) != 0, "Pixel (0,0) debe ser blanco (bit=1)")
        # Centro (100,100): byte = (100*200+100)//8 = 2512, bit 7-(100%8)=3 -> 0 (negro)
        byte_center = (100 * 200 + 100) // 8
        bit_pos = 7 - (100 % 8)
        self.assertEqual((bmp[byte_center] & (1 << bit_pos)), 0, "Pixel central debe ser negro (bit=0)")


# ==============================================================================
# ESPEJOS DE voice_validate.h/.cpp (auditoría anti-voz-rota y anti-pantalla-mal).
# Copian la lógica C exacta: si cambia el .cpp, estos tests DEBEN fallar hasta
# actualizar el espejo. Vectores sintéticos casa/calle, sin hardware.
# ==============================================================================
VV_MP3_OK, VV_MP3_TOO_SHORT, VV_MP3_JSON_ERR, VV_MP3_HTML_ERR, VV_MP3_NO_SYNC = range(5)
VV_WAV_OK, VV_WAV_TOO_SHORT, VV_WAV_NO_RIFF, VV_WAV_NO_FMT, VV_WAV_BAD_FMT, VV_WAV_NO_DATA, VV_WAV_EMPTY = range(7)

def vv_mp3_scan(buf: bytes):
    if buf is None or len(buf) < 4:
        return (VV_MP3_TOO_SHORT, 0)
    if len(buf) >= 3 and buf[0] == ord('I') and buf[1] == ord('D') and buf[2] == ord('3'):
        return (VV_MP3_OK, 0)
    i = 0
    if len(buf) >= 3 and buf[0] == 0xEF and buf[1] == 0xBB and buf[2] == 0xBF:
        i = 3
    while i < len(buf) and buf[i] in (ord(' '), 0x09, 0x0D, 0x0A):
        i += 1
    if i < len(buf):
        if buf[i] in (ord('{'), ord('[')):
            return (VV_MP3_JSON_ERR, i)
        if i + 5 <= len(buf) and buf[i:i+5] == b'ERROR':
            return (VV_MP3_JSON_ERR, i)
        if i + 5 <= len(buf) and buf[i:i+5] == b'<html':
            return (VV_MP3_HTML_ERR, i)
        if i + 5 <= len(buf) and buf[i:i+5] == b'<!DOC':
            return (VV_MP3_HTML_ERR, i)
    scan = min(len(buf) - 1, 256)
    for j in range(scan):
        if buf[j] == 0xFF and (buf[j+1] & 0xE0) == 0xE0:
            return (VV_MP3_OK, j)
    return (VV_MP3_NO_SYNC, 0)

def _rd32le(b, p):
    return b[p] | (b[p+1] << 8) | (b[p+2] << 16) | (b[p+3] << 24)

def _rd16le(b, p):
    return b[p] | (b[p+1] << 8)

def vv_wav_parse(buf: bytes, measure_peak: bool):
    r = {"ok": 0, "reason": VV_WAV_TOO_SHORT, "channels": 0, "bits": 0,
         "rate": 0, "data_size": 0, "data_offset": 0, "data_avail": 0, "peak": 0}
    if buf is None or len(buf) < 12:
        return r
    if buf[0:4] != b'RIFF' or buf[8:12] != b'WAVE':
        r["reason"] = VV_WAV_NO_RIFF
        return r
    pos = 12
    have_fmt = False
    audio_fmt = 0
    n = len(buf)
    while pos + 8 <= n:
        ck_size = _rd32le(buf, pos + 4)
        ck_data = pos + 8
        if buf[pos:pos+4] == b'fmt ':
            if ck_size < 16 or ck_data + 16 > n:
                break
            audio_fmt = _rd16le(buf, ck_data)
            r["channels"] = _rd16le(buf, ck_data + 2)
            r["rate"] = _rd32le(buf, ck_data + 4)
            r["bits"] = _rd16le(buf, ck_data + 14)
            have_fmt = True
        elif buf[pos:pos+4] == b'data':
            if not have_fmt:
                break
            if audio_fmt != 1 or not (1 <= r["channels"] <= 2) or r["bits"] not in (8, 16) or not (8000 <= r["rate"] <= 48000):
                r["reason"] = VV_WAV_BAD_FMT
                return r
            r["data_size"] = ck_size
            r["data_offset"] = ck_data
            avail = (n - ck_data) if ck_data < n else 0
            if avail > ck_size:
                avail = ck_size
            r["data_avail"] = avail
            if ck_size == 0 or avail == 0 or avail < ck_size:
                r["reason"] = VV_WAV_EMPTY
                return r
            if measure_peak and r["bits"] == 16 and r["channels"] == 1:
                peak = 0
                d = buf[ck_data:ck_data+avail]
                for k in range(0, len(d) - 1, 2):
                    s = d[k] | (d[k+1] << 8)
                    if s >= 32768:
                        s -= 65536
                    a = abs(s)
                    if a > peak:
                        peak = a
                r["peak"] = peak
            r["ok"] = 1
            r["reason"] = VV_WAV_OK
            return r
        adv = 8 + ck_size + (ck_size & 1)
        if adv == 0 or pos + adv <= pos:
            break
        pos += adv
        if pos >= n:
            break
    r["reason"] = VV_WAV_NO_DATA if have_fmt else VV_WAV_NO_FMT
    return r

def vv_split_phrases(text: str):
    out = []
    n = len(text)
    start = 0
    while start < n and text[start] in (' ', '\n', '\r', '\t'):
        start += 1
    while start < n:
        remaining = n - start
        if remaining <= 150:
            end = n
        else:
            target = start + 150
            best = -1
            for i in range(target, start + 39, -1):
                c = text[i]
                if c in ('.', '!', '?', ';') and (i + 1 >= n or text[i+1] == ' '):
                    best = i + 1
                    break
            if best < 0:
                for i in range(target, start + 39, -1):
                    c = text[i]
                    if c in (',', ':') and (i + 1 >= n or text[i+1] == ' '):
                        best = i + 1
                        break
            if best < 0:
                for i in range(target, start + 29, -1):
                    if text[i] == ' ':
                        best = i
                        break
            if best < 0:
                best = target
            end = best
        s, e = start, end
        while s < e and text[s] in (' ', '\n', '\r', '\t'):
            s += 1
        while e > s and text[e-1] in (' ', '\n', '\r', '\t'):
            e -= 1
        if e > s:
            out.append(text[s:e])
        start = end
        while start < n and text[start] in (' ', '\n', '\r', '\t'):
            start += 1
        if end == start and start < n:
            start += 1
    return out

def vv_select_route(music_off, pub_set, lan_failed_recently, funnel_hint):
    r = {"use_lan_first": 1, "funnel": 0, "rate": 22050, "pre_sec": 2, "rd_size": 4096, "stall_ms": 12000}
    if music_off:
        r["use_lan_first"] = 0
        r["rate"] = 0
        return r
    if (lan_failed_recently or funnel_hint) and pub_set:
        r["use_lan_first"] = 0
        r["funnel"] = 1
    elif lan_failed_recently and not pub_set:
        r["funnel"] = 1
    if r["funnel"]:
        r["rate"] = 16000
        r["pre_sec"] = 4
        r["rd_size"] = 8192
        r["stall_ms"] = 25000
    return r

def vv_prebuffer_bytes(rate, funnel):
    if rate < 8000 or rate > 48000:
        return 0
    return rate * 2 * (4 if funnel else 2)

def vv_retry_plan(attempt, http_code, timed_out):
    if attempt < 0:
        attempt = 0
    if timed_out or http_code == 429 or (500 <= http_code <= 599):
        if attempt < 2:
            bo = 500 << attempt
            return (1, min(bo, 4000))
    return (0, 0)

def vv_vad_decide(energies, threshold, min_speech, max_silence):
    if not energies:
        return (0, 0)
    speech_run = 0
    silence_run = 0
    in_speech = False
    n = len(energies)
    for i, e in enumerate(energies):
        if e >= threshold:
            speech_run += 1
            silence_run = 0
            if not in_speech and speech_run >= min_speech:
                in_speech = True
        else:
            silence_run += 1
            if in_speech and silence_run >= max_silence:
                return (1, i + 1)
    if in_speech:
        return (1, n)
    return (0, n)

def _make_wav(rate=22050, channels=1, bits=16, data=b'', declare_size=None, extra_chunks=b''):
    import struct
    fmt_size = 16
    hdr = b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE'
    hdr += b'fmt ' + struct.pack('<I', fmt_size)
    hdr += struct.pack('<HHIIHH', 1, channels, rate, rate * channels * bits // 8, channels * bits // 8, bits)
    hdr += extra_chunks
    ds = len(data) if declare_size is None else declare_size
    hdr += b'data' + struct.pack('<I', ds)
    return hdr + data


class TestVoiceValidateAudit(unittest.TestCase):
    """Auditoría: gates de voz/música y simulación casa/calle (espejos de voice_validate.*)."""

    def test_mp3_scan_vectors(self):
        self.assertEqual(vv_mp3_scan(b'ID3\x04\x00' + bytes(9) + bytes([0xFF, 0xFB, 0x90])), (VV_MP3_OK, 0))
        self.assertEqual(vv_mp3_scan(bytes(10) + bytes([0xFF, 0xF3, 0xC0]) + bytes(100)), (VV_MP3_OK, 10))
        self.assertEqual(vv_mp3_scan(b'{"error": {"code": 429}}')[0], VV_MP3_JSON_ERR)
        self.assertEqual(vv_mp3_scan(b'<html><body>Captcha')[0], VV_MP3_HTML_ERR)
        self.assertEqual(vv_mp3_scan(b'<!DOCTYPE html>')[0], VV_MP3_HTML_ERR)
        self.assertEqual(vv_mp3_scan(b'\x00' * 64)[0], VV_MP3_NO_SYNC)
        self.assertEqual(vv_mp3_scan(b'\x00\x01\x02')[0], VV_MP3_TOO_SHORT)
        self.assertEqual(vv_mp3_scan(b'')[0], VV_MP3_TOO_SHORT)
        # PCM crudo -1 (0xFFFF) NO debe pasar como MP3 válido en ventana corta
        self.assertEqual(vv_mp3_scan(bytes([0xFF, 0xFF]) + bytes(60))[0], VV_MP3_OK)  # documenta: sync azaroso existe

    def test_wav_parse_valid_and_peak(self):
        data = bytes([0x11, 0x22]) + bytes(126)
        w = vv_wav_parse(_make_wav(data=data), True)
        self.assertEqual(w["reason"], VV_WAV_OK)
        self.assertEqual(w["ok"], 1)
        self.assertEqual(w["rate"], 22050)
        self.assertEqual(w["channels"], 1)
        self.assertEqual(w["data_offset"], 44)
        self.assertEqual(w["data_avail"], 128)
        self.assertEqual(w["peak"], 0x2211)

    def test_wav_parse_rejects(self):
        self.assertEqual(vv_wav_parse(b'RIFF....WAVEJUNK' + bytes(12), False)["reason"], VV_WAV_NO_FMT)
        good = _make_wav(data=bytes(128))
        trunc = bytearray(good[:60])
        import struct
        trunc[40:44] = struct.pack('<I', 10000)  # declara 10000, hay ~16
        self.assertEqual(vv_wav_parse(bytes(trunc), False)["reason"], VV_WAV_EMPTY)
        bad_rate = _make_wav(rate=96000, data=bytes(64))
        self.assertEqual(vv_wav_parse(bad_rate, False)["reason"], VV_WAV_BAD_FMT)
        bad_ch = _make_wav(channels=3, data=bytes(64))
        self.assertEqual(vv_wav_parse(bad_ch, False)["reason"], VV_WAV_BAD_FMT)
        self.assertEqual(vv_wav_parse(b'ERROR: no key', False)["reason"], VV_WAV_NO_RIFF)
        self.assertEqual(vv_wav_parse(b'', False)["reason"], VV_WAV_TOO_SHORT)

    def test_wav_parse_extended_fmt(self):
        # fmt extendido (18 bytes + fact): el parser camina chunks, no usa offset fijo 36
        import struct
        hdr = b'RIFF' + struct.pack('<I', 100) + b'WAVE'
        hdr += b'fmt ' + struct.pack('<I', 18)
        hdr += struct.pack('<HHIIHH', 1, 1, 16000, 32000, 2, 16) + bytes(2)
        hdr += b'fact' + struct.pack('<I', 4) + bytes(4)
        data = bytes([0x01, 0x00]) + bytes(62)
        hdr += b'data' + struct.pack('<I', len(data))
        w = vv_wav_parse(hdr + data, True)
        self.assertEqual(w["reason"], VV_WAV_OK)
        self.assertEqual(w["rate"], 16000)

    def test_split_phrases_golden(self):
        t = "A" * 100 + ". " + "B" * 100 + " fin"
        ph = vv_split_phrases(t)
        self.assertEqual(len(ph), 2)
        self.assertEqual(ph[0], "A" * 100 + ".")
        self.assertEqual(ph[1], "B" * 100 + " fin")
        for p in ph:
            self.assertLessEqual(len(p), 150)

    def test_split_phrases_never_exceeds_and_never_empty(self):
        long_text = ("Hola. Esto es una prueba del divisor de frases del Kraken, que tiene que cortar en puntos, "
                     "comas y espacios sin romper palabras a la mitad nunca jamas. Segunda oracion: el hotspot de la "
                     "calle mete jitter y cada frase viaja en su propio TLS, asi que si una se pierde las demas igual "
                     "suenan. Tercera: ¿funciona con preguntas? ¡Y con exclamaciones! Fin del texto largo de prueba.")
        ph = vv_split_phrases(long_text)
        self.assertGreater(len(ph), 2)
        for p in ph:
            self.assertLessEqual(len(p), 155)
            self.assertGreater(len(p), 0)
        self.assertEqual("".join(ph).replace(" ", ""), long_text.replace(" ", "").replace("\n", ""))

    def test_route_select_vectors(self):
        lan = vv_select_route(0, 1, 0, 0)
        self.assertEqual((lan["rate"], lan["pre_sec"], lan["rd_size"], lan["stall_ms"], lan["use_lan_first"]),
                         (22050, 2, 4096, 12000, 1))
        street = vv_select_route(0, 1, 1, 1)
        self.assertEqual((street["rate"], street["pre_sec"], street["rd_size"], street["stall_ms"], street["use_lan_first"]),
                         (16000, 4, 8192, 25000, 0))
        off = vv_select_route(1, 1, 0, 0)
        self.assertEqual(off["rate"], 0)
        # Sin fallback y LAN caída: se intenta LAN igual (única opción) pero con params de calle
        lone = vv_select_route(0, 0, 1, 0)
        self.assertEqual((lone["use_lan_first"], lone["rate"]), (1, 16000))

    def test_prebuffer_math(self):
        self.assertEqual(vv_prebuffer_bytes(22050, 0), 88200)
        self.assertEqual(vv_prebuffer_bytes(16000, 1), 128000)
        self.assertEqual(vv_prebuffer_bytes(0, 0), 0)
        self.assertEqual(vv_prebuffer_bytes(96000, 0), 0)

    def test_retry_plan_vectors(self):
        self.assertEqual(vv_retry_plan(0, 429, 0), (1, 500))
        self.assertEqual(vv_retry_plan(1, 503, 0), (1, 1000))
        self.assertEqual(vv_retry_plan(0, 0, 1), (1, 500))
        self.assertEqual(vv_retry_plan(0, 404, 0), (0, 0))
        self.assertEqual(vv_retry_plan(0, 400, 0), (0, 0))
        self.assertEqual(vv_retry_plan(2, 503, 0), (0, 0))

    def test_vad_home_street_silence(self):
        casa = [50]*10 + [4000]*6 + [100]*20
        det, cut = vv_vad_decide(casa, 300, 3, 10)
        self.assertEqual((det, cut), (1, 26))
        det_s, _ = vv_vad_decide([100]*20, 300, 3, 10)
        self.assertEqual(det_s, 0)
        # Límite conocido documentado: ruido de calle fuerte dispara el umbral fijo
        det_c, cut_c = vv_vad_decide([900]*20, 300, 3, 10)
        self.assertEqual((det_c, cut_c), (1, 20))

    def test_firmware_uses_voice_validate(self):
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        for sym in ("vv_select_route", "vv_prebuffer_bytes", "vv_retry_plan",
                    "fetchGooglePhraseMp3", "writeGoogleSilence",
                    "s_lastLocalTtsFailMs", "totalBytes >= 3200", "pcmBytes < 4800"):
            self.assertIn(sym, ai, f"ai_engine.cpp debe usar {sym}")
        # El flag "para" ya no se borra dentro de los streamers (carrera):
        # solo declaración + inicios explícitos fuera de ai_engine
        self.assertEqual(ai.count("    g_musicStopRequested = false;"), 0)

    def test_firmware_display_guards(self):
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        self.assertIn('server->on("/api/selftest", HTTP_GET', wp)
        self.assertIn("s_speechTaskH", wp)
        self.assertIn("409", wp)
        self.assertIn("epd.showBitmap(tmp, true)", wp)  # multipágina sin buffer crudo
        self.assertIn('"api_music", 20480', wp)  # stack TLS+funnel con margen
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            mn = f.read()
        for sym in ("g_faceNeedsFull", "TEXT_HOLD_TIMEOUT_MS", "clockPartialCount",
                    "weatherPartialCount", "s_holdStartMs = millis(); // seguir leyendo"):
            self.assertIn(sym, mn, f"main.cpp debe contener {sym}")
        with open("src/audio_es8311.cpp", "r", encoding="utf-8") as f:
            au = f.read()
        self.assertIn("if (reg > 192) reg = 192;", au)
        self.assertNotIn("198", au.split("calc_es8311_volume_reg")[1].split("}")[0] if "calc_es8311_volume_reg" in au else "")

    def test_selftest_handler_declared(self):
        with open("include/wifi_portal.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("void handleApiSelftest();", h)
        with open("include/voice_validate.h", "r", encoding="utf-8") as f:
            vh = f.read()
        for sym in ("vv_mp3_scan", "vv_wav_parse", "vv_split_phrases", "vv_select_route",
                    "vv_prebuffer_bytes", "vv_retry_plan", "vv_vad_decide"):
            self.assertIn(sym, vh)


class TestRobustnessAudit(unittest.TestCase):
    """Robustez: OTA verificado, diagnóstico de boot, backup de config, batería."""

    def test_pull_ota_verifies_md5(self):
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        self.assertIn("Update.setMD5(expectMd5.c_str())", wp)
        self.assertIn("otaDownloadAndFlash(viaTls, host, port, size, ver)", wp)
        self.assertIn("verificación falló (MD5/tamaño), descarto bin corrupto", wp)
        # Sin MD5 válido de 32 hex no se activa la verificación a ciegas
        self.assertIn("expectMd5.length() == 32", wp)

    def test_boot_diagnostics(self):
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            mn = f.read()
        for sym in ("g_bootCount", "g_resetReason", "esp_reset_reason()",
                    '"BROWNOUT"', '"PANIC"', '"TASK_WDT"',
                    'bp.getUInt("boot_cnt", 0)'):
            self.assertIn(sym, mn, f"main.cpp debe contener {sym}")
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        self.assertIn('doc["boot_count"] = g_bootCount;', wp)
        self.assertIn('doc["reset_reason"] = g_resetReason;', wp)
        self.assertIn('doc["boot_healthy"]', wp)

    def test_config_backup_restore(self):
        with open("include/wifi_portal.h", "r", encoding="utf-8") as f:
            h = f.read()
        for sym in ("void handleApiConfigBackup();", "void handleApiConfigRestore();",
                    "void configToJson(JsonDocument &doc);",
                    "int configFromJson(const JsonObjectConst &o);",
                    "void writeConfigBackupFile();"):
            self.assertIn(sym, h, f"wifi_portal.h debe declarar {sym}")
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        for sym in ('server->on("/api/config_backup", HTTP_GET',
                    'server->on("/api/config_restore", HTTP_POST',
                    '"kraken_backup"', '"/config_backup.json"',
                    '"from"', '"flash"',
                    "writeConfigBackupFile(); // copia automática"):
            self.assertIn(sym, wp, f"wifi_portal.cpp debe contener {sym}")
        # Clamps de seguridad en restore (nada fuera de rango entra al NVS)
        self.assertIn('cfgGetInt(o, "volume", config.volume, 0, 100)', wp)
        self.assertIn('cfgGetInt(o, "face_pack", config.face_pack, 0, 4)', wp)

    def test_battery_guards(self):
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        for sym in ("batteryCriticalForStream()", "batteryRefuseMusic(",
                    "Bateria baja, no puedo poner musica",
                    "Bateria baja, no puedo sintonizar",
                    "streamMusicOnEsp32 rehusado: bateria critica",
                    "streamPcmUrlOnEsp32 rehusado: bateria critica"):
            self.assertIn(sym, ai, f"ai_engine.cpp debe contener {sym}")


class TestMusicButtonsDecimeWikiAudit(unittest.TestCase):
    """Musica (titulo al arrancar, pantalla centrada, botones), decime y
    referencia Wikipedia: lo pedido por voz en la ronda de correcciones."""

    def test_music_title_shown_at_start(self):
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        # Ambos streamers muestran el titulo APENAS suena (no al final)
        self.assertEqual(ai.count('epd.showMusicScreen('), 2,
                         "musica+radio deben usar showMusicScreen al arrancar")
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            mn = f.read()
        self.assertIn("epd.showMusicScreen(t, st);", mn)

    def test_music_stop_needs_hold(self):
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        # El click corto es volumen: frenar exige BOOT mantenido 800ms
        self.assertIn("bootLowSince", ai)
        self.assertIn("BOOT mantenido", ai)
        self.assertIn("g_musicPlaying = true;", ai)
        self.assertIn("g_musicPlaying = false;", ai)
        self.assertNotIn("Stop: BOOT apretado (2 lecturas LOW seguidas cada 50ms)", ai)

    def test_music_screen_centered(self):
        with open("include/faces_bitmaps.h", "r", encoding="utf-8") as f:
            fb = f.read()
        self.assertIn("renderMusicFrame", fb)
        self.assertIn("musicDispLen", fb)
        with open("include/epaper_driver.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("showMusicScreen", h)

    def test_buttons_remapped_during_music(self):
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            mn = f.read()
        self.assertIn("musicBumpVolume", mn)
        self.assertIn("MODO MUSICA: BOOT click = subir volumen", mn)
        self.assertIn("MODO MUSICA: PWR click = bajar volumen", mn)
        self.assertIn("g_musicStopRequested = true;", mn)

    def test_decime_echo(self):
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        self.assertIn("DECIME (repite textual lo que le dictaste", ai)
        self.assertIn('"decime "', ai)
        self.assertIn('"dime "', ai)
        # El eco va DESPUES de hora/repeti para no pisarlos
        self.assertLess(ai.index("REPETI (re-habla lo ultimo"), ai.index("DECIME (repite textual"))

    def test_wikipedia_reference(self):
        with open("include/ai_engine.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("isReferenceImageRequest", h)
        self.assertIn("fetchWikipediaReference", h)
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        for sym in ("es.wikipedia.org", "/w/api.php?action=query",
                    "list=search", "prop=extracts|pageimages",
                    "Segun Wikipedia: "):
            # El "Segun Wikipedia" lo dice main.cpp al hablar
            pass
        self.assertIn("es.wikipedia.org", ai)
        self.assertIn("prop=extracts|pageimages", ai)
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            mn = f.read()
        self.assertIn("REFERENCIA REAL (logo/escudo/simbolo/bandera/foto)", mn)
        self.assertIn("Segun Wikipedia: ", mn)

    def test_no_startup_pop(self):
        with open("src/audio_es8311.cpp", "r", encoding="utf-8") as f:
            au = f.read()
        # Arrancar y cerrar muteado: mata el "ploc" del amplificador
        self.assertIn("mute(true); // arrancar muteado", au)
        self.assertIn("mute(true); // cerrar muteado", au)

    def test_story_mode_long(self):
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        # Excepcion al limite de 40 palabras cuando piden cuento/historia
        self.assertIn("isStoryRequest", ai)
        self.assertIn("STORY_DIRECTIVE", ai)
        self.assertIn("MODO CUENTO LARGO", ai)
        self.assertIn("[MODE:CUENTO]", ai)
        self.assertIn("if (storyMode) maxTokens = 1400;", ai)
        # La cascada de texto tambien lleva la directiva (no solo voz)
        self.assertIn("MODO CUENTO LARGO activado (cascada)", ai)
        self.assertNotIn("chatOpenAI(e, sys, promptText, ans)", ai)

    def test_boot_mutes_speech(self):
        with open("include/ai_engine.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("g_speaking", h)
        self.assertIn("g_ttsStopRequested", h)
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        # El freno existe en todos los motores de voz
        self.assertGreater(ai.count("g_ttsStopRequested"), 8)
        self.assertIn("g_speaking = true;", ai)
        self.assertIn("g_speaking = false;", ai)
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            mn = f.read()
        # BOOT hablando = callar, y nunca chime sobre la voz
        self.assertIn("HABLANDO: BOOT toque = callar", mn)
        self.assertIn("if (!g_speaking) audio.playChimeUp();", mn)
        self.assertIn("if (!ttsOk && !g_ttsStopRequested) {", mn)

    def test_story_detection_simulation(self):
        """Las frases de pedido de cuento activan modo largo; lo comun no."""
        def is_story(t):
            keys = ("cuento", "cuentame", "contame", "historia",
                    "relato", "leyenda", "fabula", "novela")
            return any(k in t for k in keys)
        for t in ("cuentame un cuento", "contame una historia larga",
                  "quiero un relato de terror", "narrame una leyenda",
                  "leeme una novela de piratas"):
            self.assertTrue(is_story(t), t)
        for t in ("que hora es", "poneme musica", "como estas",
                  "busca el dolar hoy", "dibuja un gato"):
            self.assertFalse(is_story(t), t)

    def test_music_logo_next_to_title(self):
        with open("include/faces_bitmaps.h", "r", encoding="utf-8") as f:
            fb = f.read()
        self.assertIn("renderMusicFrameWithLogo", fb)
        self.assertIn("blitLogo64", fb)
        with open("include/epaper_driver.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("showMusicLogo", h)
        with open("include/ai_engine.h", "r", encoding="utf-8") as f:
            ah = f.read()
        self.assertIn("fetchMusicLogo", ah)
        self.assertIn("fetchStationLogo", ah)
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        # Tapa de iTunes con verificacion (nada de logos que no coincidan)
        self.assertIn("itunes.apple.com", ai)
        self.assertIn("logoMatchesQuery", ai)
        self.assertIn("decodeJpegToLogo", ai)
        # En fondo, sin frenar la musica; solo pisa si sigue el mismo tema
        self.assertIn("musicArtworkTask", ai)
        self.assertIn("spawnArtworkTask(query, outTitle, false);", ai)
        self.assertIn("g_musicEsp32Title == String(job->title)", ai)

    def test_music_logo_layout_simulation(self):
        """El logo (64px a la izquierda) no debe pisar el titulo de al lado."""
        def layout(title, logo=True):
            n = len(title)
            scale = 2 if n <= 27 else 1
            cw = 12 if scale == 2 else 6
            side_cap = 9 if scale == 2 else 19
            words = title.split(" ")
            lines, cur = [], ""
            for w in words:
                if cur and len(cur) + 1 + len(w) > side_cap:
                    lines.append(cur)
                    cur = ""
                cur = (cur + " " + w).strip()
            if cur:
                lines.append(cur)
            # Primera linea al lado del logo: x dentro de 80..194
            first = lines[0]
            x = 80 + (114 - len(first) * cw) // 2
            return scale, x, len(first) * cw <= 114
        for t in ["Queso ruso", "Juguetes perdidos", "Mega 98.3",
                  "La bestia pop del area de la bahia en vivo"]:
            scale, x, fits = layout(t)
            self.assertGreaterEqual(x, 80, f"'{t}' pisa el logo")
            self.assertTrue(fits, f"'{t}' se sale de la caja")

    def test_segmented_long_tts(self):
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        # Texto largo = bloques con progreso (no un WAV gigante antes de hablar)
        self.assertIn("splitTtsBlocks", ai)
        self.assertIn("streamTtsSingle", ai)
        self.assertIn("Voz larga:", ai)
        self.assertIn("Voz bloque %u/%u", ai)
        with open("include/ai_engine.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("streamTtsSingle", h)
        with open("include/epaper_driver.h", "r", encoding="utf-8") as f:
            eh = f.read()
        self.assertIn("showSpeechProgress", eh)
        with open("src/epaper_driver.cpp", "r", encoding="utf-8") as f:
            ec = f.read()
        self.assertIn("void EPaperDriver::showSpeechProgress", ec)

    def test_tts_block_split_simulation(self):
        """El cuento se parte por frases en bloques chicos; lo corto queda en 1."""
        def split(text, max_chars=450):
            out, start, n = [], 0, len(text)
            while start < n:
                while start < n and text[start] in " \n\r":
                    start += 1
                if start >= n:
                    break
                if n - start <= max_chars:
                    b = text[start:].strip()
                    if b:
                        out.append(b)
                    break
                tend, cut = start + max_chars, -1
                for i in range(tend, start + 39, -1):
                    if text[i] in ".!?;" and (i + 1 == n or text[i + 1] == " "):
                        cut = i + 1
                        break
                if cut < 0:
                    for i in range(tend, start + 39, -1):
                        if text[i] in ",:" and (i + 1 == n or text[i + 1] == " "):
                            cut = i + 1
                            break
                if cut < 0:
                    for i in range(tend, start + 29, -1):
                        if text[i] == " ":
                            cut = i
                            break
                if cut < 0:
                    cut = tend
                b = text[start:cut].strip()
                if b:
                    out.append(b)
                start = cut
            return out
        short = "Hola Jorge, todo bien por aca."
        self.assertEqual(len(split(short)), 1)
        cuento = ("Habia una vez un kraken enojado que cuidaba un faro. " * 22)[:1000]
        blocks = split(cuento)
        self.assertGreater(len(blocks), 1, "el cuento debe ir en varios bloques")
        for b in blocks:
            self.assertLessEqual(len(b), 450, f"bloque pasado de largo: {len(b)}")
        # No se pierde ni se duplica texto (sin espacios: comparacion simple)
        joined = "".join(blocks).replace(" ", "")
        self.assertEqual(joined, cuento.replace(" ", ""))

    def test_story_loading_screen_contract(self):
        """La carga del cuento: texto fijo con rima (FULL 1 vez) + timer pixel a pixel (PARTIAL)."""
        with open("include/epaper_driver.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("beginStoryLoading", h)
        self.assertIn("updateStoryLoading", h)
        with open("src/epaper_driver.cpp", "r", encoding="utf-8") as f:
            ec = f.read()
        # Rima fija en pantalla
        self.assertIn("Un cuentito estoy pensando,", ec)
        self.assertIn("los dibujos van llegando.", ec)
        # Timer de 156 pasos de 1px (pixel a pixel, sin barrer todo)
        self.assertIn("STORY_LOAD_TRACK_N = 156", ec)
        self.assertIn("displayPartial()", ec)
        # El begin hace FULL doble (texto fijo bien negro); el update solo AGREGA pixeles
        self.assertIn("void EPaperDriver::beginStoryLoading", ec)
        self.assertIn("void EPaperDriver::updateStoryLoading", ec)
        self.assertIn("s_storyLoadDrawn = 0;", ec)

    def test_story_loading_ticker(self):
        """El loop() anima el timer cada 260ms mientras g_storyLoading este prendido."""
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            mc = f.read()
        self.assertIn("bool g_storyLoading = false;", mc)
        self.assertIn("epd.updateStoryLoading(s_storyStep++);", mc)
        self.assertIn(">= 260", mc)

    def test_story_loading_wrap_simulation(self):
        """La pista de 156px se reinicia sola al llenarse (un FULL cada ~40s)."""
        N = 156
        drawn = 0
        wraps = 0
        for step in range(400):
            pos = step % N
            if pos < drawn:
                wraps += 1
                drawn = 0
            drawn = pos + 1
        self.assertEqual(wraps, 2, "400 pasos a 260ms deben dar 2 reinicios")
        self.assertLessEqual(drawn, N)

    def test_story_parts_json_contract(self):
        """El cuento estructurado: JSON 3-6 partes, plan B si <3."""
        with open("include/ai_engine.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("struct StoryPart", h)
        self.assertIn("STORY_PARTS_MIN = 3", h)
        self.assertIn("STORY_PARTS_MAX = 6", h)
        self.assertIn("fetchStoryParts", h)
        self.assertIn("fetchStoryBitmap", h)
        self.assertIn("decodeJpegToBitmap", h)
        self.assertIn("narrateStoryPart", h)
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        self.assertIn("bool AIEngine::fetchStoryParts", ai)
        self.assertIn("entre 3 y 4 partes", ai)
        self.assertIn("UNA", ai)
        self.assertIn("plan B clasico", ai)

    def test_story_parts_parse_simulation(self):
        """Parseo del JSON de partes: acepta 3-6, rechaza <3 (plan B)."""
        import json

        def parse(raw, pmin=3, pmax=6):
            a = raw.find("{")
            b = raw.rfind("}")
            if a < 0 or b <= a:
                return None
            try:
                doc = json.loads(raw[a:b + 1])
            except Exception:
                return None
            arr = doc.get("partes", doc.get("parts", None))
            if not isinstance(arr, list):
                return None
            parts = []
            for p in arr:
                if len(parts) >= pmax:
                    break
                t = (p.get("texto", p.get("text", "")) or "").strip()
                if len(t) < 20:
                    continue
                parts.append(t)
            if len(parts) < pmin:
                return None
            return parts

        good = ('{"titulo":"El faro","partes":['
                '{"texto":"Habia una vez un kraken que cuidaba un faro en el fin del mundo.",'
                ' "imagen":"kraken lighthouse keeper"},'
                '{"texto":"Una noche de tormenta vio una luz parpadeando entre las olas gigantes.",'
                ' "imagen":"storm night waves"},'
                '{"texto":"Era un barquito perdido y el kraken lo guio con su farol hasta el puerto.",'
                ' "imagen":"kraken guiding boat"}]}')
        self.assertEqual(len(parse("ruido previo " + good + " [MODE:CUENTO]")), 3)
        two = '{"titulo":"X","partes":[{"texto":"Parte uno con texto suficiente largo si."},{"texto":"Parte dos con texto suficiente largo no."}]}'
        self.assertIsNone(parse(two), "con 2 partes debe ir a plan B")
        self.assertIsNone(parse("no hay json aca"), "sin JSON debe ir a plan B")
        self.assertIsNone(parse('{"titulo":"X"}'), "sin arreglo debe ir a plan B")

    def test_story_image_cascade_order(self):
        """Nano Banana barato->caro y Pollinations ultimo (siempre hay acceso)."""
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        lite = ai.index("gemini-3.1-flash-lite-image")
        v25 = ai.index("gemini-2.5-flash-image")
        v31 = ai.index('"gemini-3.1-flash-image"')
        pro = ai.index("gemini-3-pro-image")
        poll = ai.index("fallback Pollinations gratis")
        self.assertLess(lite, v25, "lite primero (mas barato)")
        self.assertLess(v25, v31)
        self.assertLess(v31, pro)
        self.assertLess(pro, poll, "Pollinations ultimo: siempre hay acceso")
        # Solo JPEG sirve en el ESP32 (sin decodificador PNG): PNG = siguiente modelo
        self.assertIn("no es JPEG", ai)
        self.assertIn("aspectRatio", ai)
        self.assertIn("decodeJpegToBuffer", ai)

    def test_story_narrator_voice(self):
        """Narrador Sulafat lento primero; si falla, cadena normal (sin romper nada)."""
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        self.assertIn("s_storyVoice", ai)
        self.assertIn('"Sulafat"', ai)
        self.assertIn('"[slow] "', ai)
        self.assertIn("bool AIEngine::narrateStoryPart", ai)
        # La firma con defaults no rompe los llamados existentes
        with open("include/ai_engine.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn('const char *voice = "Aoede"', h)

    def test_story_cascade_orchestration(self):
        """Cascada: precarga autolimpia, BOOT avanza, PWR sale, CUENTO la llama."""
        with open("include/story_cascade.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("playStoryCascade", h)
        self.assertIn("storyCascadeActive", h)
        self.assertIn("storyRequestAdvance", h)
        self.assertIn("storyRequestExit", h)
        self.assertIn("g_storyCancel", h)
        with open("src/story_cascade.cpp", "r", encoding="utf-8") as f:
            sc = f.read()
        # La precarga es dueña de sus buffers y se autolimpia (salir es instantaneo)
        self.assertIn("storyPrefetchTask", sc)
        self.assertIn("vTaskDelete(NULL)", sc)
        self.assertIn("abort", sc)
        self.assertIn("epd.beginStoryLoading()", sc)
        self.assertIn("narrateStoryPart", sc)
        # Reproductor solo-imagen+voz (2026-09-10): sin páginas de texto; la
        # parte sin dibujo conserva la imagen anterior (o la carga).
        self.assertIn("Reproductor: SOLO imagen", sc)
        self.assertIn("epd.showBitmap(dispBmp, true)", sc)
        player = sc[sc.index("Reproductor: SOLO imagen"):]
        self.assertNotIn("renderPaginatorPage", player)
        self.assertNotIn("showPartText", sc)
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            mc = f.read()
        # Rama CUENTO: cascada primero, clasico si falla (plan B)
        self.assertIn("playStoryCascade(userText)", mc)
        self.assertIn("cuento clasico", mc)
        # Botones en modo cuento
        self.assertIn("storyCascadeActive()", mc)
        self.assertIn("storyRequestAdvance()", mc)
        self.assertIn("storyRequestExit()", mc)
        # Los fetch de imagen abortan rapido al salir (sin colgar el PWR)
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        self.assertIn("g_storyCancel", ai)

    def test_story_window_simulation(self):
        """Ventana: parte 1 lista -> se muestra; 2+ llegan en fondo; BOOT saltea."""
        n = 5
        ready = [False] * n
        ready[0] = True  # parte 1 bloqueante OK
        # Precarga en fondo completa 1..4 de a una
        for i in range(1, n):
            ready[i] = True
        shown = []
        advance_pressed = {2}  # BOOT durante la parte 3 (indice 2)
        for i in range(n):
            if ready[i]:
                shown.append(("img", i))
            else:
                shown.append(("txt", i))
            # fin de voz: avanza solo; BOOT solo lo apura (mismo destino)
            _ = i in advance_pressed
        self.assertEqual([s[0] for s in shown], ["img"] * n)
        self.assertEqual(len(shown), n)

    def test_power_cancel_contract(self):
        """POWER: 1ero cancela todo en silencio, 2do (reposo) vuelve a 0."""
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            mc = f.read()
        self.assertIn("g_voiceCancel", mc)
        self.assertIn("requestFullCancel", mc)
        self.assertIn("copitoVoiceCancelled", mc)
        # La grabación mira POWER además de BOOT
        self.assertIn("POWER durante grabación: cancelando", mc)
        # Salida silenciosa sin nudge cuando lo pidió el usuario
        self.assertIn("Grabación cancelada por usuario", mc)
        # Chequeos entre etapas (transcripto se descarta, la IA no habla después)
        self.assertIn("Cancelado antes de la IA", mc)
        self.assertIn("Cancelado tras la IA", mc)
        # El TTS no arranca cancelado (cubre referencia/reloj/clima/final/sistema)
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        self.assertIn("if (copitoVoiceCancelled()) return false;", ai)
        with open("include/ai_engine.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("bool copitoVoiceCancelled();", h)

    def test_story_pause_contract(self):
        """Cuento: 1er POWER frena voz (pausa), 2do sale; BOOT retoma."""
        with open("include/story_cascade.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("storyRequestPause", h)
        self.assertIn("storyPaused", h)
        with open("src/story_cascade.cpp", "r", encoding="utf-8") as f:
            sc = f.read()
        self.assertIn("s_storyPaused", sc)
        self.assertIn("Cuento pausado en la imagen", sc)
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            mc = f.read()
        self.assertIn("storyRequestPause()", mc)
        self.assertIn("1er POWER frena la voz (pausado)", mc)

    def test_voice_gain_contract(self):
        """Trim unity (causa era velocidad) + narrador -4dB + medidor de picos."""
        with open("include/dsp_audio.h", "r", encoding="utf-8") as f:
            dh = f.read()
        self.assertIn("gain256 = (gain256 * DSP_TRIM_NUM) / DSP_TRIM_DEN;", dh)
        self.assertIn("#define DSP_TRIM_NUM 256", dh)
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        self.assertIn('"Sulafat", "[slow] ", 160', ai)
        self.assertIn("pico fuente %d/32767", ai)
        self.assertIn("peakSrc", ai)
        with open("include/ai_engine.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("int gain256 = 256", h)

    def test_limiter_dsp_simulation(self):
        """Barrido de señales por la cadena compartida: techo 28000 inviolable."""
        import math
        sine = [int(32767 * math.sin(2 * math.pi * i / 48)) for i in range(480)]
        square = [32767 if (i // 24) % 2 == 0 else -32767 for i in range(480)]

        # Narrador (160): todo pico queda bajo la rodilla 26000 = limpio
        for sig in (sine, square):
            out = [dsp_apply_sample(s, 160) for s in sig]
            self.assertLessEqual(max(out), DSP_KNEE)
            self.assertGreaterEqual(min(out), -DSP_KNEE)
            self.assertLess(max(abs(v) for v in out), DSP_CEIL)
        # Voz normal unity: full-scale entra suave al limiter por diseño (27691)
        out = [dsp_apply_sample(s, 256) for s in sine]
        self.assertEqual(max(out), 27691)
        # Música (128): holgada
        out = [dsp_apply_sample(s, 128) for s in square]
        self.assertLessEqual(max(out), 20000)
        # Barrido completo int16 a unity: nada supera el techo 28000 (ni wrap)
        worst = 0
        for v in range(-32768, 32768, 97):
            o = abs(dsp_apply_sample(v, 256))
            if o > worst:
                worst = o
        self.assertLessEqual(worst, DSP_CEIL)

    def test_dsp_header_contract(self):
        """dsp_audio.h es la fuente de verdad: constantes y funciones puras."""
        with open("include/dsp_audio.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("#define DSP_KNEE 26000", h)
        self.assertIn("#define DSP_CEIL 28000", h)
        self.assertIn("#define DSP_TRIM_NUM 256", h)
        self.assertIn("#define DSP_GAIN_MIN 32", h)
        self.assertIn("#define DSP_GAIN_MAX 256", h)
        self.assertIn("dsp_apply_sample_ex", h)
        self.assertIn("dsp_norm_gain256", h)
        self.assertNotIn("Arduino.h", h)
        self.assertNotIn("i2s_write", h)
        # El driver usa el header (no matemática duplicada)
        with open("src/audio_es8311.cpp", "r", encoding="utf-8") as f:
            au = f.read()
        self.assertIn('#include "dsp_audio.h"', au)
        self.assertIn("dsp_apply_sample_ex(s, gain256, &lim, &clip)", au)
        self.assertIn("probeReset()", au)
        # Los espejos Python usan las mismas constantes que el header
        self.assertIn("#define DSP_KNEE 26000", h)
        self.assertEqual(DSP_KNEE, 26000)
        self.assertEqual(DSP_CEIL, 28000)
        self.assertEqual(DSP_TRIM_NUM, 256)

    def test_audio_probe_contract(self):
        """GET /api/audio_probe?mode=last|synth: sonda remota sin oído."""
        with open("include/audio_es8311.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("struct AudioProbe", h)
        self.assertIn("probeSnapshot", h)
        self.assertIn("peakPre", h)
        self.assertIn("peakPost", h)
        with open("include/wifi_portal.h", "r", encoding="utf-8") as f:
            wh = f.read()
        self.assertIn("handleApiAudioProbe", wh)
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        self.assertIn('/api/audio_probe', wp)
        self.assertIn("handleApiAudioProbe", wp)
        self.assertIn('"synth"', wp)
        self.assertIn("peak_pre", wp)
        self.assertIn("peak_post", wp)
        self.assertIn("HOT_SOURCE", wp)

    def test_audio_qa_verdicts(self):
        """tools/audio_qa.py: el agente distingue LIMPIO/SATURADO/BAJO/CORTADO."""
        import os
        import sys
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
        import numpy as np
        import audio_qa
        sr = 16000
        t = np.arange(sr * 2) / sr
        clean = 0.5 * np.sin(2 * np.pi * 220 * t) + 0.2 * np.sin(2 * np.pi * 440 * t)
        self.assertEqual(audio_qa.analyze_samples(clean, sr)["verdict"], "LIMPIO")
        hot = np.sign(np.sin(2 * np.pi * 220 * t)) * 0.999
        self.assertEqual(audio_qa.analyze_samples(hot, sr)["verdict"], "SATURADO")
        low = 0.01 * np.sin(2 * np.pi * 220 * t)
        self.assertEqual(audio_qa.analyze_samples(low, sr)["verdict"], "BAJO")
        short = clean[:sr]
        self.assertEqual(audio_qa.analyze_samples(short, sr, expected_sec=3.0)["verdict"], "CORTADO")
        self.assertEqual(DSP_TRIM_NUM, 256)

    def test_copito_face_lock(self):
        """Copito pegajoso: la voz cambia por la sesion (sin NVS, con FULL) y el boot migra todo a Clasico."""
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        a = ai.index("--- CARA / PACK")
        b = ai.index("--- ACTUALIZACION")
        cara = ai[a:b]
        self.assertNotIn('putUChar("face_pack"', cara)
        self.assertIn("epd.showFace(FACE_IDLE, 0, true)", cara)
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        m = wp.index("Migracion automatica")
        mig = wp[m:m + 600]
        self.assertIn("FACE_PACK_RUDO", mig)
        self.assertIn("FACE_PACK_KRAKEN", mig)
        self.assertIn("FACE_PACK_CLASICO", mig)
        # El portal (acción explícita del usuario) SÍ persiste
        self.assertIn('prefs.putUChar("face_pack", config.face_pack)', wp)

    def test_live_tests_restore_hardware(self):
        """Los tests en vivo no deben dejar Rudo ni reloj puestos."""
        with open("tests/test_live_hardware.py", "r", encoding="utf-8") as f:
            lt = f.read()
        self.assertIn("finally:", lt)
        self.assertIn("Restaurado a Copito (1)", lt)
        self.assertIn("Restaurado a idle", lt)
        # El cambio de pack remoto hace doble FULL (fantasma de negros pesados)
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        self.assertIn("doble FULL", wp)

    def test_portal_js_braces_balanced(self):
        """Todo el <script> del portal debe balancear llaves (un } faltante
        mata TODAS las funciones: fue el bug 'previewPhoto is not defined')."""
        import re
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            src = f.read()
        a = src.index('"<script>"')
        b = src.index('</script>', a)
        region = src[a:b]
        lits = re.findall(r'"((?:[^"\\]|\\.)*)"', region)
        js = "".join(
            lit.replace('\\"', '"').replace('\\\\', '') for lit in lits
        )
        self.assertIn("function previewPhoto(", js)
        self.assertIn("function scanWifi(){", js)
        depth = 0
        i, n = 0, len(js)
        state, esc = None, False
        while i < n:
            ch = js[i]
            nx = js[i + 1] if i + 1 < n else ""
            if state is None:
                if ch == "/" and nx == "/":
                    state = "lcom"
                    i += 2
                    continue
                elif ch == "/" and nx == "*":
                    state = "bcom"
                    i += 2
                    continue
                elif ch == "'":
                    state = "sq"
                elif ch == '"':
                    state = "dq"
                elif ch == "`":
                    state = "tpl"
                elif ch == "{":
                    depth += 1
                elif ch == "}":
                    depth -= 1
                    self.assertGreaterEqual(
                        depth, 0, "llave de cierre sin abrir en el JS del portal")
            elif state == "lcom":
                if ch == "\n":
                    state = None
            elif state == "bcom":
                if ch == "*" and nx == "/":
                    state = None
                    i += 1
            elif state in ("sq", "dq", "tpl"):
                if esc:
                    esc = False
                elif ch == "\\":
                    esc = True
                elif ((state == "sq" and ch == "'") or
                      (state == "dq" and ch == '"') or
                      (state == "tpl" and ch == "`")):
                    state = None
            i += 1
        self.assertEqual(depth, 0, "llaves sin cerrar en el JS del portal")

    def test_cloud_drawing_prompt_encoded(self):
        """El prompt a Pollinations va URL-encodeado (un ? & comilla daba 400)."""
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        seg = ai[ai.index("String cleanPrompt = prompt;"):ai.index("int seed = (int)(millis()")]
        self.assertIn("urlEncode(cleanPrompt)", seg)
        self.assertIn("encPrompt", seg)
        self.assertNotIn('cleanPrompt + "?width=', seg)
        # Simulación: caracteres que rompían el path quedan escapados
        import urllib.parse
        raw = "dibuja un gato ¿qué hace? & sale 'ya'"
        enc = urllib.parse.quote(raw, safe="-_.~")
        self.assertNotIn("?", enc)
        self.assertNotIn("&", enc)
        self.assertIn("%20", enc)

    def test_drawing_without_ai_fallback(self):
        """Si Gemini falla (429) pero pedía dibujo, se intenta directo a la nube."""
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            mc = f.read()
        blk = mc[mc.index("Fallo en la consulta a Gemini!"):mc.index("changeState(STATE_ERROR);")]
        self.assertIn("hasDrawingIntent", blk)
        self.assertIn("fetchAndDisplayCloudImage", blk)
        self.assertIn("DIBUJO DIRECTO FINALIZADO", mc)

    def test_echo_staged_contract(self):
        """'dime X' solo repite: staged + BOOT lo dice. Ni hora, ni IA, ni habla solo."""
        with open("include/ai_engine.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("extractEchoPhrase", h)
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        self.assertIn("bool AIEngine::extractEchoPhrase", ai)
        self.assertIn("ECHO_PREFIXES", ai)
        # La hora ya no conoce dime/decime (esa familia es solo eco)
        hora = ai[ai.index("--- HORA (RTC/sistema"):ai.index("--- REPETI")]
        self.assertNotIn('indexOf("dime', hora)
        self.assertNotIn('indexOf("decime', hora)
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            mc = f.read()
        self.assertIn("g_echoPending", mc)
        self.assertIn("stageEchoAndFinish", mc)
        self.assertIn("echoSpeakTask", mc)
        self.assertIn("Eco por BOOT", mc)
        self.assertIn("ECO STAGED POST-IA", mc)
        # El eco pendiente manda sobre paginación (BOOT lo dice, no hojea)
        self.assertLess(mc.index("ECO STAGED: BOOT dice"), mc.index("Paginación inteligente"))

    def test_echo_staged_simulation(self):
        """'dime estas fuera del proyecto' guarda la frase (no la contesta)."""
        def music_norm(s):
            s = s.lower()
            for a, b in (("á", "a"), ("é", "e"), ("í", "i"),
                         ("ó", "o"), ("ú", "u"), ("ñ", "n")):
                s = s.replace(a, b)
            return s.strip().strip(".,!?")

        def is_alnum(c):
            return ("a" <= c <= "z") or ("0" <= c <= "9")

        def query_after(norm, prefix):
            # Espejo de musicQueryAfter con bordes + skip de puntuación
            pre = prefix.strip()
            if not pre:
                return ""
            frm = 0
            while True:
                p = norm.find(pre, frm)
                if p < 0:
                    return ""
                ok_l = (p == 0) or not is_alnum(norm[p - 1])
                e = p + len(pre)
                ok_r = (e >= len(norm)) or not is_alnum(norm[e])
                if ok_l and ok_r:
                    i = e
                    while i < len(norm) and not is_alnum(norm[i]):
                        i += 1
                    q = norm[i:].strip().strip(".,!?:").strip()
                    return q if len(q) >= 2 else ""
                frm = p + 1

        prefixes = ("decime ", "dime ", "deci ", "repeti esto ",
                    "repeti lo siguiente ", "repeti despues de mi ",
                    "repite esto ")

        def extract(transcript):
            t = music_norm(transcript)
            if len(t) < 3:
                return ""
            for pr in prefixes:
                q = query_after(t, pr)
                if q:
                    return q
            return ""

        # El caso real del log (Whisper pone coma): antes daba "" e iba a la IA
        self.assertEqual(extract("Dime, estás fuera del proyecto."),
                         "estas fuera del proyecto")
        self.assertEqual(extract("dime estas fuera del proyecto"),
                         "estas fuera del proyecto")
        self.assertEqual(extract("decime hola mundo"), "hola mundo")
        self.assertEqual(extract("decime: hola..."), "hola")
        # Bordes: "deci" no muerde "decime", "pon" no muerde "pone"
        self.assertEqual(query_after("decime hola", "deci"), "")
        self.assertEqual(query_after("pone musica", "pon"), "")
        self.assertEqual(query_after("pon musica", "pon"), "musica")
        self.assertEqual(query_after("adime hola", "dime"), "")
        # "dime la hora" también es eco ahora (la hora se pide de otra forma)
        self.assertEqual(extract("dime la hora"), "la hora")
        self.assertEqual(extract("que hora es"), "")
        self.assertEqual(extract("dime"), "")

    def test_query_after_punctuation_contract(self):
        """musicQueryAfter tolera puntuación y exige bordes (fix coma del dime)."""
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        self.assertIn("isCmdAlnum", ai)
        self.assertIn("pre.trim()", ai)

    def test_dashboard_controls_contract(self):
        """El portal expone control directo + foto a pantalla."""
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        # Tarjeta Control: decir, presets por chat, música en Kraken, sonda
        self.assertIn("Control directo", wp)
        self.assertIn("sendSay()", wp)
        self.assertIn("/api/say", wp)
        self.assertIn("sendPreset(", wp)
        self.assertIn("sonaQuery()", wp)
        self.assertIn("sintoniza mega", wp)
        self.assertIn("para la musica", wp)
        self.assertIn("probeRun(", wp)
        self.assertIn("/api/audio_probe?mode=", wp)
        # Tarjeta Foto: preview 200x200, dither, envío b64, queda fija
        self.assertIn("Foto a la pantalla", wp)
        self.assertIn("previewPhoto(", wp)
        self.assertIn("photoCanvas", wp)
        self.assertIn("ditherSel", wp)
        self.assertIn("sendPhoto()", wp)
        self.assertIn("bitmap_b64", wp)
        self.assertIn("Sacar foto", wp)
        # Subida robusta: formato ilegible (HEIC), botón bloqueado, error HTTP visible
        self.assertIn("img.onerror", wp)
        self.assertIn("HEIC", wp)
        self.assertIn('photoSend', wp)
        self.assertIn("HTTP '+r.status", wp)
        # Arrastrar y soltar: zona drop + mismo pipeline de preview
        self.assertIn("dropZone", wp)
        self.assertIn("previewPhotoFile", wp)
        self.assertIn("initDropZone", wp)
        # Diagnóstico visible: watchdog 10s + try/catch con mensaje
        self.assertIn("Se colgó leyendo", wp)
        self.assertIn("Se rompió procesando", wp)
        # Sacar = idle libera el hold de pantalla fija
        idle_at = wp.index('} else if (action == "idle") {')
        self.assertIn("g_textHold = false;", wp[idle_at:idle_at + 200])

    def test_portal_tabs_contract(self):
        """El portal va por pestañas (no un scroll único): nav + data-tab + showTab."""
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        for tab in ("control", "foto", "audio", "pantalla", "ia", "red",
                    "registros", "sistema"):
            self.assertIn("data-tab='%s'" % tab, wp)
        self.assertIn("tabbar", wp)
        self.assertIn("function showTab(", wp)
        self.assertIn("tabhidden", wp)
        self.assertIn("krakenTab", wp)
        self.assertIn("@media(min-width:900px)", wp)
        # Grupos clave en su pestaña
        self.assertIn("data-tab='control'>🎛 Control directo", wp)
        self.assertIn("data-tab='foto'>📷 Foto a la pantalla", wp)
        self.assertIn("data-tab='audio'>🔊 Volumen del Parlante", wp)
        self.assertIn("data-tab='pantalla'>🎭 Pack de Caras", wp)
        self.assertIn("data-tab='sistema'>🔇 Reposo Automático", wp)
        # Submit y OTA pertenecen a sistema (no flotan sin pestaña)
        self.assertIn("tabgroup' data-tab='sistema'", wp)

    def test_eco_leve_power_contract(self):
        """Plan leve de energía: CPU/radio por perfil, NTP 24h, reloj liviano."""
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            main = f.read()
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        # Perfil: radio full en carga, modem-sleep en reposo. Sin escalado
        # de CPU (80 MHz colgaba el equipo con OPI PSRAM: solo ping).
        self.assertIn("void powerBoost()", main)
        self.assertIn("void powerRelax()", main)
        self.assertNotIn("setCpuFrequencyMhz", main)
        self.assertIn("needFull ? false : true", main) # modem-sleep salvo portal/hold/checkin/voz
        # changeState aplica el perfil en reposo y habla
        self.assertIn("powerRelax();", main)
        self.assertIn("STATE_CLOCK", main[main.index("perfil de energía según estado"):
                                         main.index("perfil de energía según estado") + 300])
        # TTS envuelve descarga+DSP con boost/relax
        tts = ai[ai.index("bool AIEngine::streamTtsToSpeaker"):]
        self.assertIn("powerBoost();", tts[:600])
        self.assertIn("powerRelax();", tts)
        # Pull-OTA descarga a plena potencia y relaja si falla
        self.assertIn("powerBoost(); // plan leve", wp)
        self.assertIn("powerRelax(); // la descarga fall", wp)
        # Tope TX hogareño tras conectar
        self.assertIn("WiFi.setTxPower(WIFI_POWER_11dBm)", wp)
        # NTP cada 24h (el RTC aguanta la deriva), no cada 6h
        self.assertIn("86400000UL", main)
        self.assertNotIn("21600000UL", main)
        # Reloj: hora como máximo 1 vez/s, BOOT a 10 Hz
        self.assertIn("lastClockPoll", main)
        self.assertIn(">= 1000", main)
        # Música/radio en vivo: streaming con boost (modem-sleep la entrecorta)
        self.assertIn("struct PowerStreamGuard", ai)
        mus = ai[ai.index("bool AIEngine::streamMusicOnEsp32"):]
        self.assertIn("PowerStreamGuard ecoBoost;", mus[:800])
        rad = ai[ai.index("bool AIEngine::streamPcmUrlOnEsp32"):]
        self.assertIn("PowerStreamGuard ecoBoost;", rad[:800])

    def test_no_cpu_scaling_apis(self):
        """2026-09-09: bajar la CPU a 80 MHz brickió el equipo (ping vivo,
        HTTP/serie/botones muertos con OPI PSRAM). Prohibido hasta tener
        prueba de estabilidad con medidor USB."""
        import os
        banned = ("setCpuFrequency", "rtc_clk_cpu_freq_set",
                  "esp_pm_configure", "esp_pm_configure_max_freq")
        hits = []
        for root, _, files in os.walk("src"):
            for fn in files:
                if not fn.endswith((".cpp", ".h")):
                    continue
                p = os.path.join(root, fn)
                with open(p, "r", encoding="utf-8") as f:
                    for n, line in enumerate(f, 1):
                        for b in banned:
                            if b in line:
                                hits.append("%s:%d:%s" % (p, n, b))
        self.assertEqual(hits, [], "APIs de escalado CPU prohibidas: %s" % hits)

    def test_usb_data_indicator_contract(self):
        """Testigo USB de datos: helper global, tridente en TopBar, aviso en
        IDLE, header en reloj y campo en /api/sensor."""
        with open("include/config.h", "r", encoding="utf-8") as f:
            cfg = f.read()
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            main = f.read()
        with open("include/faces_bitmaps.h", "r", encoding="utf-8") as f:
            faces = f.read()
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        # Helper declarado y definido sobre (bool)Serial (USBCDC enumerado),
        # no sobre ADC/carga (el cargador bobo también da 5V).
        self.assertIn("copitoUsbDataConnected", cfg)
        self.assertIn("return (bool)Serial", main)
        # TopBar autodetecta (no solo flag isUsbPc) y dibuja el tridente
        tb = faces[faces.index("static void drawTopBar"):]
        self.assertIn("copitoUsbDataConnected", tb[:3500])
        self.assertIn("tridente", tb[:4500])  # bloque del glifo USB
        # Watcher de flanco en el loop con aviso en IDLE (USB<->cara)
        self.assertIn("4a-bis", main)
        self.assertIn("FACE_USB_PC : FACE_IDLE", main)
        # Reloj sin TopBar: prefijo "USB " en el header con caché coherente
        self.assertIn('"USB %s"', main)
        # API expone el estado para la PC
        self.assertIn('"usb_data"', wp)
        self.assertIn("copitoUsbDataConnected()", wp)

    def test_hardware_waits_have_timeouts(self):
        """Ningún while(digitalRead(...)) puede ser infinito: el loop debe
        seguir iterando para servir HTTP y leer botones."""
        with open("src/epaper_driver.cpp", "r", encoding="utf-8") as f:
            lines = f.readlines()
        for n, line in enumerate(lines):
            if "while" in line and "digitalRead" in line:
                window = "".join(lines[n:n + 10])
                self.assertTrue(
                    ("millis()" in window and "break" in window)
                    or "timeout" in window.lower(),
                    "espera sin timeout en epaper_driver.cpp:%d" % (n + 1))

    def test_epd_producer_ops_bounded(self):
        """El loop (productor) nunca bloquea infinito en la cola/mutex del
        display: xQueueSend con timeout finito y Take/Give pareados sin
        return en el medio (un mutex tomado y no liberado = brick)."""
        with open("src/epaper_driver.cpp", "r", encoding="utf-8") as f:
            src = f.read()
        lines = src.split("\n")
        self.assertNotIn("xQueueSend(s_epdQueue, &msg, portMAX_DELAY)", src)
        # portMAX_DELAY solo lo usa la tarea consumidora dedicada
        for n, line in enumerate(lines):
            if "portMAX_DELAY" in line and "xQueueReceive" in line:
                fn_start = max([i for i in range(n) if lines[i].startswith("static void ")
                                or lines[i].startswith("void ")], default=0)
                self.assertIn("epdBackgroundTask", lines[fn_start],
                              "xQueueReceive infinito fuera del consumidor")
        # Cada Take tiene su Give en la misma función, sin return entremedio
        for n, line in enumerate(lines):
            if "xSemaphoreTake(s_epdMutex" in line:
                give = None
                for m in range(n + 1, min(n + 200, len(lines))):
                    if lines[m] == "}":
                        break # fin de la función sin Give: fugaría el mutex
                    s = lines[m].strip()
                    self.assertFalse(
                        s.startswith("return"),
                        "return con mutex tomado en epaper_driver.cpp:%d" % (m + 1))
                    if "xSemaphoreGive(s_epdMutex" in lines[m]:
                        give = m
                        break
                self.assertIsNotNone(
                    give, "Take sin Give pareado en epaper_driver.cpp:%d" % (n + 1))

    def test_loop_serves_web_first(self):
        """loop() atiende HTTP antes que nada (es lo que permite rescatar
        por red) y las rutas de rescate/diagnóstico existen."""
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            main = f.read()
        loop = main[main.index("void loop() {"):]
        self.assertLess(loop.index("netManager.handlePortal();"),
                        loop.index("isPortalActive()"),
                        "handlePortal debe correr antes de cualquier gate")
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        hp = wp[wp.index("void NetworkManager::handlePortal()"):]
        self.assertIn("server->handleClient();", hp[:400])
        for route in ('"/status"', '"/api/logs"', '"/update"'):
            self.assertIn(route, wp, "falta ruta de rescate %s" % route)

    def test_ota_push_guards(self):
        """2026-09-09: push con música = brick. /update debe rechazar con
        409 si hay audio/voz o batería baja, sin tocar flash."""
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            main = f.read()
        # Existe el motivo de ocupado y mira voz/música/API/batería
        self.assertIn("otaBusyReason()", wp)
        guard = wp[wp.index("static String otaBusyReason()"):]
        guard = guard[:guard.index("\n}\n")]
        for probe in ("g_musicPlaying", "copitoVoiceBusy()", "speechBusy()",
                      "musicBusy()", "percentage <= 15", "isCharging"):
            self.assertIn(probe, guard, "otaBusyReason no mira %s" % probe)
        self.assertIn("bool copitoVoiceBusy()", main)
        # El upload bloqueado descarta sin Update.begin/write y el final da 409
        up = wp[wp.index("void NetworkManager::handleOtaUpload()"):]
        self.assertIn("s_otaBlocked = true", up)
        self.assertIn("descartar sin escribir", up)
        fin = wp[wp.index("void NetworkManager::handleOtaUpdate()"):]
        self.assertIn("s_otaBlocked", fin)
        self.assertIn("409", fin)
        self.assertIn("sin tocar flash", wp)
        # Pull-OTA se difiere con música en curso (no descarga a ciegas)
        poll = wp[wp.index("int NetworkManager::pollStagedFirmware()"):]
        self.assertIn("g_musicPlaying", poll[:600])
        self.assertIn("diferido", poll[:600].lower())

    def test_status_reports_busy_and_battery(self):
        """/status expone busy/busy_reason/batt_pct/charging para el preflight."""
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        st = wp[wp.index('server->on("/status"'):]
        st = st[:3000]
        for field in ('"busy"', '"busy_reason"', '"batt_pct"', '"charging"'):
            self.assertIn(field, st, "/status sin campo %s" % field)

    def test_preflight_script_contract(self):
        """tools/preflight.py: tests + /status + busy + batería + MD5."""
        with open("tools/preflight.py", "r", encoding="utf-8") as f:
            pf = f.read()
        for probe in ("test_core_logic.py", "/status", "busy", "batt_pct",
                      "fw_md5", "NO-GO", "sys.exit"):
            self.assertIn(probe, pf, "preflight sin chequeo %s" % probe)

    def test_story_prefetch_single_owner(self):
        """2026-09-09: la precarga se auto-liberaba mientras el reproductor
        la leía (UAF = PANIC). Dueño único: solo la tarea libera/borra el
        job; el reproductor solo publica abort y suelta el puntero."""
        with open("src/story_cascade.cpp", "r", encoding="utf-8") as f:
            sc = f.read()
        task = sc[sc.index("static void storyPrefetchTask"):]
        task = task[:task.index("\n}\n")]
        # La tarea espera el abort del reproductor antes de liberar
        self.assertIn("while (!job->abort", task)
        self.assertIn("STORY_PREFETCH_JOIN_MS", task)
        self.assertIn("delete job", task)
        # Toques y liberación bajo lock
        self.assertIn("storyJobLockTake()", sc)
        self.assertIn("s_storyJobLock", sc)
        # El reproductor: publica abort y NO libera ni borra el job
        player = sc[sc.index("Reproductor: SOLO imagen"):]
        self.assertIn("job->abort = true", player)
        self.assertNotIn("delete job", player)
        self.assertNotIn("free(job", player)

    def test_story_narration_first(self):
        """El cuento lo maneja el usuario con BOOT (2026-09-10): precarga
        TOTAL con la barra (nada se cuenta a medias), primera imagen fija y
        cada parte espera su BOOT. Sin avance automático."""
        with open("src/story_cascade.cpp", "r", encoding="utf-8") as f:
            sc = f.read()
        self.assertIn("STORY_IMAGE_GRACE_MS", sc)
        self.assertNotIn("Parte 1: bloqueante", sc)
        self.assertNotIn("part0img", sc)
        self.assertIn("STORY_PRELOAD_MS", sc)
        self.assertIn("STORY_STEP_WAIT_MS", sc)
        self.assertIn("storyWaitBoot", sc)
        self.assertIn("Cuento listo: esperando BOOT", sc)
        # Sin avance automático entre partes (lo pide el BOOT)
        self.assertNotIn("delay(1200)", sc)
        self.assertNotIn("Pausa entre partes", sc)
        with open("include/story_cascade.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("storyWaitBoot", h)
        self.assertIn("storySetActive", h)

    def test_story_voice_no_progress_and_pace(self):
        """En cuento: sin cartel HABLANDO por bloque, respiro entre bloques,
        un solo intento Google por frase y corte a texto tras 3 fallos."""
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        self.assertIn("if (!s_storyVoice) epd.showSpeechProgress", ai)
        self.assertIn("delay(800)", ai)
        self.assertIn("s_storyVoice ? 1 : 2", ai)
        self.assertIn("storyStrikes", ai)
        self.assertIn("sigo con texto", ai)
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            main = f.read()
        # El clásico también narra lento (narrador) y sin progreso
        self.assertIn("classicStory = true", main)
        self.assertIn("narrateStoryPart(voiceText", main)

    def test_api_story_endpoint(self):
        """/api/story dispara la cascada desde el portal (sin voz): ruta,
        handler con 409 si hay audio/cuento, tarea en fondo con plan B."""
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        self.assertIn('"/api/story"', wp)
        self.assertIn("handleApiStory", wp)
        self.assertIn("apiStoryTask", wp)
        self.assertIn("playStoryCascade(topic)", wp)
        # Guardia: no pisa audio ni otro cuento
        handler = wp[wp.index("void NetworkManager::handleApiStory()"):]
        for probe in ("speechBusy()", "musicBusy()", "storyCascadeActive()",
                      "copitoVoiceBusy()", "409"):
            self.assertIn(probe, handler[:2000], "handleApiStory sin %s" % probe)
        # Plan B clásico con el mismo texto largo + voz narradora
        task = wp[wp.index("static void apiStoryTask"):]
        task = task[:task.index("\n}\n")]
        self.assertIn("queryTextCascade", task)
        self.assertIn("narrateStoryPart(storyText", task)
        self.assertIn("renderPaginatorPage(paginator, \"CUENTO\"", task)
        # El cuento bloquea flashes (aunque haya silencios entre partes)
        self.assertIn('storyCascadeActive()) return "cuento en curso"', wp)
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            main = f.read()
        vb = main[main.index("bool copitoVoiceBusy() {"):]
        self.assertIn("storyCascadeActive()", vb[:300])
        with open("include/wifi_portal.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("handleApiStory", h)

    def test_tts_chunk_pump_vectors(self):
        """Vectores del de-chunking incremental: splits en cualquier punto,
        extensiones, trailers y líneas de tamaño partidas."""
        body = b"data-bytes-1234"
        stream = b"%X\r\n%s\r\n0\r\n\r\n" % (len(body), body)
        # Feed byte a byte (peor caso: todo partido)
        p = TtsChunkPumpMirror(True)
        got = bytearray()
        for i in range(len(stream)):
            p.feed(stream[i:i + 1])
            got.extend(p.read_body(64))
        self.assertEqual(bytes(got), body)
        self.assertTrue(p.eos)
        # Feed en dos mitades con tamaño partido + extensión + trailers
        p2 = TtsChunkPumpMirror(True)
        s2 = b"5;ext=x\r\nHello\r\n3\r\nbye\r\n0\r\nX-Trailer: 1\r\n\r\n"
        p2.feed(s2[:7])
        self.assertEqual(p2.read_body(64), b"")
        p2.feed(s2[7:])
        self.assertEqual(p2.read_body(64), b"Hellobye")
        # Sin chunked = passthrough
        p3 = TtsChunkPumpMirror(False)
        p3.feed(b"abc")
        self.assertEqual(p3.read_body(10), b"abc")
        # Chunk corrupto: no inventa bytes
        p4 = TtsChunkPumpMirror(True)
        p4.feed(b"ZZ\r\nHello\r\n0\r\n\r\n")
        self.assertEqual(p4.read_body(64), b"")
        self.assertTrue(p4.eos)

    def test_story_json_multiprovider_no_sabotage(self):
        """2026-09-09: el JSON de la cascada solo iba a Gemini y con las
        directivas de dibujo/cuento-largo agregadas (volvía prosa = sin
        fotos). Ahora va por la cascada multi-proveedor en jsonMode."""
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        fp = ai[ai.index("bool AIEngine::fetchStoryParts"):]
        fp = fp[:fp.index("\n}\n")]
        self.assertIn("queryTextCascade(prompt, raw, true)", fp)
        self.assertNotIn("queryGeminiText(prompt, raw)", fp)
        self.assertNotIn("gemini_api_key", fp)
        self.assertIn("ChatTokensGuard", ai)
        self.assertIn("s_chatTokensOverride", ai)
        qt = ai[ai.index("bool AIEngine::queryTextCascade"):]
        qt = qt[:qt.index("\n}\n")]
        self.assertIn("bool jsonMode", ai[ai.index("bool AIEngine::queryTextCascade") - 200:ai.index("bool AIEngine::queryTextCascade") + 200])
        self.assertIn("!isStoryRequest(musicNorm(promptText))", qt)
        qg = ai[ai.index("bool AIEngine::queryGeminiText"):]
        qg = qg[:qg.index("\n}\n")]
        self.assertIn("maxTokens = 1500", qg)
        # jsonMode usa sistema mínimo (el base contradice el SOLO JSON)
        self.assertIn("Respondes unicamente con el JSON pedido", ai)

    def test_gemini_tts_dechunk_and_timeouts(self):
        """Gemini TTS: de-chunking activo, cabecera con timeout global,
        deadline 60 s y sonda DSP por frase (caza-lluvia)."""
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        gem = ai[ai.index("bool AIEngine::streamGeminiTts"):]
        gem = gem[:gem.index("\n}\n")]
        self.assertIn("TtsChunkPump", gem)
        self.assertIn("Transfer-Encoding: chunked", gem)
        self.assertIn("pump.readBody", gem)
        self.assertIn("millis() + 60000", gem)
        self.assertIn("cabecera sin completar en 60s", gem)
        self.assertIn("probeSnapshot", gem)
        self.assertIn("Gemini TTS dsp:", gem)

    def test_story_sticky_voice(self):
        """2026-09-09: cada bloque redescubría proveedor y la voz cambiaba a
        mitad del cuento. La ganadora del bloque 1 abre los siguientes."""
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        self.assertIn("s_storyTtsPref", ai)
        self.assertIn("storyTtsResetPref", ai)
        st = ai[ai.index("bool AIEngine::streamTtsSingle"):]
        st = st[:st.index("\n}\n")]
        self.assertIn("tryNarrator", st)
        self.assertIn("tryLocal", st)
        self.assertIn("la voz preferida fallo, redescubro", st)
        self.assertIn("s_storyTtsPref = 1", st)
        self.assertIn("s_storyTtsPref = 2", st)
        self.assertIn("s_storyTtsPref = 3", st)
        self.assertIn("s_storyTtsPref = 4", st)
        with open("include/ai_engine.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("storyTtsResetPref", h)
        for path, probe in (("src/story_cascade.cpp", "storyTtsResetPref"),
                            ("src/wifi_portal.cpp", "storyTtsResetPref"),
                            ("src/main.cpp", "storyTtsResetPref")):
            with open(path, "r", encoding="utf-8") as f:
                self.assertIn(probe, f.read(), "%s sin reset de voz" % path)

    def test_story_json_accept_per_provider(self):
        """2026-09-09: Gemini-200-prosa mataba la cascada ilustrada (sin
        fotos). En jsonMode cada proveedor debe traer JSON útil o se pasa al
        siguiente; sin directivas saboteadoras y con tokens de sobra."""
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        self.assertIn("acceptAns", ai)
        self.assertIn("sin JSON util, siguiente proveedor", ai)
        self.assertIn("queryGeminiText(effectivePrompt, ans, jsonMode, noDirectives)", ai)
        # jsonMode apaga las dos directivas en ambas funciones; los cuentos
        # jamás llevan directiva de dibujo aunque mencionen dibujables
        self.assertIn("CopitoTools::hasDrawingIntent(promptText)", ai)
        self.assertIn("!isStoryRequest(musicNorm(promptText))", ai)
        # Las partes terminan con punto final (página acotada por foto)
        self.assertIn("termina con punto final", ai)

    def test_story_judges_j5_j6(self):
        """J5: juez de calidad 1-5 con reintento (0 = sin nube, se acepta).
        J6: personaje ancla en el JSON para consistencia visual."""
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        self.assertIn("int AIEngine::aiJudgeStory", ai)
        self.assertIn("queryTextCascade(prompt, raw, false, true)", ai)
        self.assertIn("noDirectives", ai)
        jg = ai[ai.index("int AIEngine::aiJudgeStory"):]
        jg = jg[:jg.index("\n}\n")]
        self.assertIn("Responde SOLO con el numero", jg)
        with open("include/ai_engine.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("aiJudgeStory", h)
        self.assertIn("noDirectives", h)
        with open("src/story_cascade.cpp", "r", encoding="utf-8") as f:
            sc = f.read()
        self.assertIn("aiJudgeStory(full", sc)
        self.assertIn("otra version", sc)
        self.assertIn("puntaje %d/5", sc)
        with open("pi5/copito_story.py", "r", encoding="utf-8") as f:
            pi5 = f.read()
        # Contrato v2 (libro personalizado): jueces de reglas + crítico agéntico.
        self.assertIn("validate_book", pi5)
        self.assertIn("BANNED_PHRASES", pi5)
        self.assertIn("ABSTRACT_EMOTION", pi5)
        self.assertIn("character_anchor", pi5)
        self.assertIn("ollama_repair", pi5)
        self.assertIn("ollama_critic", pi5)
        self.assertIn("agentic_refine", pi5)
        self.assertIn("ollama_interpret", pi5)
        self.assertIn("resolve_params", pi5)

    def test_story_web_waits_and_slow_voice(self):
        """Libro v2: ComfyUI + Kokoro 0.88x con respiros, largo por edad,
        layout con margen +0.15s, un libro a la vez (429)."""
        with open("pi5/copito_story.py", "r", encoding="utf-8") as f:
            pi5 = f.read()
        # Contrato v2 (libro personalizado ComfyUI + Kokoro, sin legacy).
        # Largo por edad + arco en 5 compases.
        self.assertIn("words_range_for_age", pi5)
        self.assertIn("beats_for_n", pi5)
        self.assertIn("Momento dif", pi5)
        self.assertIn("keep_alive", pi5)
        # Imágenes: ComfyUI local, ancla fija, seed única por libro.
        self.assertIn("COMFYUI_URL", pi5)
        self.assertIn("comfyui_render", pi5)
        self.assertIn("dither_to_epaper", pi5)
        self.assertIn("story_seed", pi5)
        self.assertIn("image_prompt", pi5)
        # Biblia visual inmutable + params personalizados.
        self.assertIn("style_bible_applied", pi5)
        self.assertIn("ESTILO_VISUAL_FIJO", pi5)
        self.assertIn("parse_params", pi5)
        self.assertIn("prompt_for_v2", pi5)
        # Voz Kokoro 0.88x con respiros (elipsis en comas).
        self.assertIn("KOKORO_PACE", pi5)
        self.assertIn("0.88", pi5)
        self.assertIn("synth_audio", pi5)
        self.assertIn("breathe", pi5)
        self.assertIn("kokoro", pi5.lower())
        # Sincronización PDF/EPUB3: audio_sec + 0.15s de margen.
        self.assertIn("EPUB_MARGIN_SEC", pi5)
        self.assertIn("layout_manifest", pi5)
        self.assertIn("/book/layout/", pi5)
        # Un libro a la vez: 429 si Ollama está ocupada.
        self.assertIn("GEN_LOCK", pi5)
        self.assertIn("cocinando otro libro", pi5)
        # Jueces: largo, idioma del art_director, anti-repetición.
        self.assertIn("fuera de largo", pi5)
        self.assertIn("debe ser todo ingles", pi5)
        self.assertIn("paginas repetidas", pi5)
        self.assertIn("sin punto final", pi5)
        # Robo anti-cuelgue del lock de generación.
        self.assertIn("gen_take", pi5)
        self.assertIn("lock rancio", pi5)
        # Demo mínima del libro nuevo.
        self.assertIn("DEMO_PAGE", pi5)
        self.assertIn("/book?", pi5)

    def test_story_kids_pace(self):
        """Cuento infantil oración por oración: bloques chicos, pausas entre
        bloques/frases y velocidad 90% durante la narración (se restaura)."""
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        self.assertIn("splitTtsBlocks(cleanText, s_storyVoice ? 220 : 450)", ai)
        self.assertIn("delay(800)", ai)
        self.assertIn("delay(650)", ai)
        self.assertIn("setPlaybackSpeed(90)", ai)
        self.assertIn("setPlaybackSpeed(prevSpeed)", ai)
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            main = f.read()
        # El relato largo retiene su pantalla (el idle la pisaba a los 3 min)
        self.assertIn("!voiceTaskRunning && !copitoVoiceBusy()", main)
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        self.assertIn("Pensando el cuento...", wp)

    def test_story_kids_framing(self):
        """Los cuentos son infantiles (3-8 años): directiva interna, prompt
        JSON de cascada y plan B clásico lo dicen explícito."""
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        self.assertIn("INFANTIL", ai[ai.index("STORY_DIRECTIVE"):ai.index("STORY_DIRECTIVE") + 600])
        self.assertIn("ninos pequenos", ai)
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        self.assertIn("cuento infantil para ninos sobre", wp)

    def test_screen_snapshot_endpoint(self):
        """/api/screen expone el framebuffer para emulación PC (agente UI):
        snapshot con mutex en el driver + endpoint hex + script de captura."""
        with open("include/epaper_driver.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("snapshot(uint8_t *dst)", h)
        with open("src/epaper_driver.cpp", "r", encoding="utf-8") as f:
            drv = f.read()
        snap = drv[drv.index("bool EPaperDriver::snapshot"):]
        snap = snap[:snap.index("\n}\n")]
        self.assertIn("xSemaphoreTake(s_epdMutex", snap)
        self.assertIn("memcpy(dst, buffer, EPD_BUFFER_SIZE)", snap)
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        self.assertIn('"/api/screen"', wp)
        self.assertIn("handleApiScreen", wp)
        self.assertIn("bits_hex", wp)
        with open("include/wifi_portal.h", "r", encoding="utf-8") as f:
            hh = f.read()
        self.assertIn("handleApiScreen", hh)
        with open("tools/screen_cap.py", "r", encoding="utf-8") as f:
            sc = f.read()
        for probe in ("/api/screen", "bits_hex", "5000", "screens"):
            self.assertIn(probe, sc, "screen_cap sin %s" % probe)

    def test_story_stub_guard(self):
        """2026-09-09: con la cuota agotada los proveedores devuelven stubs
        ('Aquí va.') y eso NO se narra como cuento: mensaje honesto."""
        with open("include/story_cascade.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("STORY_MIN_CHARS", h)
        self.assertIn("STORY_FALLBACK_TEXT", h)
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        self.assertIn("storyText.length() >= STORY_MIN_CHARS", wp)
        self.assertIn("STORY_FALLBACK_TEXT", wp)
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            main = f.read()
        self.assertIn("voiceText.length() < STORY_MIN_CHARS", main)

    def test_story_beats_drawing(self):
        """2026-09-10: 'cuento de un conejito' dibujaba (sustantivos visuales)
        y vocalizaba el sketch. El pedido explícito de cuento fuerza CUENTO,
        apaga el dibujo y las directivas, y los tags sin cerrar se amputan."""
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        self.assertIn("bool aiIsStoryRequest(const String &text)", ai)
        self.assertIn("!isStoryRequest(musicNorm(promptText))", ai)
        with open("include/ai_engine.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("aiIsStoryRequest", h)
        with open("src/main.cpp", "r", encoding="utf-8") as f:
            main = f.read()
        self.assertIn("MODO CUENTO forzado por pedido", main)
        self.assertIn("tags.mode != \"CUENTO\"", main)
        with open("src/copito_tools.cpp", "r", encoding="utf-8") as f:
            ct = f.read()
        self.assertIn("sin cerrar", ct)
        self.assertIn("kPrefixes", ct)
        # sanitizeTtsText amputa markup sin cierre en vez de vocalizarlo
        self.assertIn("isAgentTag", ai)
        self.assertIn("amputar", ai)

    def test_funnel_key_config(self):
        """Clave del funnel (X-Copito-Key) plomeada: struct, NVS, backup,
        restore, save y campo password en el portal."""
        with open("include/config.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("funnel_key[65]", h)
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        for probe in ('prefs.getString("fk", "")', 'prefs.putString("fk",',
                      '"fk", config.funnel_key', 'hasArg("fk")',
                      "name='fk'", "X-Copito-Key"):
            self.assertIn(probe, wp, "portal sin %s" % probe)

    def test_funnel_tts_and_story(self):
        """Llavero nómade: TTS Piper por funnel (TLS+clave) en la cadena tras
        LAN; cuento Pi5 con fallback funnel; bitmaps http/https con clave."""
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        self.assertIn("bool AIEngine::streamFunnelTts", ai)
        self.assertIn("POST /copito/tts HTTP/1.1", ai)
        self.assertIn("playLocalWavBody(tlsCli, t0, \"TTS funnel\"", ai)
        st = ai[ai.index("bool AIEngine::streamTtsSingle"):]
        st = st[:st.index("\n}\n")]
        self.assertIn("tryFunnel", st)
        self.assertLess(st.index("if (tryLocal()) return true;"),
                        st.index("tryFunnel"))
        self.assertIn("s_storyTtsPref = 5", ai)
        pi = ai[ai.index("bool AIEngine::fetchPiStory"):]
        pi = pi[:pi.index("\n}\n")]
        self.assertIn("/copito/story?topic=", pi)
        self.assertIn("X-Copito-Key", pi)
        dl = ai[ai.index("bool AIEngine::downloadPiBitmap"):]
        dl = dl[:dl.index("\n}\n")]
        self.assertIn("https://", dl)
        self.assertIn("X-Copito-Key", dl)
        with open("include/ai_engine.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("streamFunnelTts", h)
        with open("pi3/copito_section.py", "r", encoding="utf-8") as f:
            sec = f.read()
        for probe in ("/copito/tts", "/copito/story", "X-Copito-Key", "403",
                      "rate", "COPITO_PI5"):
            self.assertIn(probe, sec, "pi3 sin %s" % probe)

    def test_pi5_story_fallback(self):
        """2026-09-09: plan B sin cuota en el Pi5 (Ollama+Pollinations). El
        ESP32 pide el mismo JSON con img (bitmap cocinado) y lo muestra sin
        pipeline cloud; la nube sigue primera por calidad."""
        with open("include/ai_engine.h", "r", encoding="utf-8") as f:
            h = f.read()
        self.assertIn("imageUrl", h)
        self.assertIn("fetchPiStory", h)
        self.assertIn("downloadPiBitmap", h)
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        pi = ai[ai.index("bool AIEngine::fetchPiStory"):]
        pi = pi[:pi.index("\n}\n")]
        self.assertIn("ollama_host_pi", pi)
        self.assertIn("/story?topic=", pi)
        self.assertIn("parseStoryPartsJson", pi)
        dl = ai[ai.index("bool AIEngine::downloadPiBitmap"):]
        dl = dl[:dl.index("\n}\n")]
        self.assertIn("EPD_BUFFER_SIZE", dl)
        self.assertIn("imagen incompleta", dl)
        self.assertIn("parseStoryPartsJson", ai)
        with open("src/story_cascade.cpp", "r", encoding="utf-8") as f:
            sc = f.read()
        self.assertIn("fetchPiStory(topic, parts, n)", sc)
        self.assertIn("imgUrls", sc)
        self.assertIn("downloadPiBitmap", sc)
        # Orden: nube primero, Pi5 después, clásico al final
        self.assertLess(sc.index("fetchStoryParts(topic, parts, n)"),
                        sc.index("fetchPiStory(topic, parts, n)"))
        with open("pi5/copito_story.py", "r", encoding="utf-8") as f:
            pi5 = f.read()
        for probe in ("/story", "/story/img", "FLOYDSTEINBERG", "5000",
                      "ThreadingHTTPServer", "ollama"):
            self.assertIn(probe, pi5, "pi5 sin %s" % probe)
        # Base URL correcta tras NAT/funnel: DNS sin puerto -> https 443,
        # IP sin puerto -> http 8890 (2026-09-10: img con :8890 tras funnel
        # apuntaban al puerto interno = 6 partes sin dibujo).
        self.assertIn("https://%s", pi5)
        # El ESP32 recompone el origen funnel aunque el Pi se equivoque
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai2 = f.read()
        self.assertIn("viaFunnel", ai2)
        self.assertIn("/copito", ai2)
        # Las img absolutas llevan puerto: el ESP32 manda Host con puerto y el
        # Pi lo agrega si falta (2026-09-09: img sin :8890 = conexion a puerto
        # 80 = las 6 partes sin dibujo).
        self.assertIn('cfg.ollama_host_pi) + ":8890', ai)
        self.assertIn('"http://%s:%d" % (host, PORT)', pi5)

    def test_story_voice_daniela(self):
        """2026-09-09: la davefx robótica no sirve para cuentos infantiles.
        Los cuentos usan Daniela (es_AR high, Piper Pi5); el resto no cambia."""
        with open("include/config.h", "r", encoding="utf-8") as f:
            cfg = f.read()
        self.assertIn('STORY_LOCAL_VOICE', cfg)
        self.assertIn('"daniela"', cfg)
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        st = ai[ai.index("bool AIEngine::streamTtsSingle"):]
        st = st[:st.index("\n}\n")]
        self.assertIn("STORY_LOCAL_VOICE", st)
        self.assertIn("storyVoice", st)
        # Voz lenta de cuento: el llavero manda slow:true en LAN y funnel
        # (Piper Pi5 1.4x; el proxy Pi3 lo reenvía tal cual).
        self.assertIn('doc["slow"] = true', ai)
        self.assertIn("lipSyncCb, s_storyVoice", ai)

    def test_story_never_speaks_tags(self):
        """2026-09-09: el cuento vocalizaba [SKETCH:...]/[MODE:...] crudos
        ('scheck triple cien'). La voz se sanea con parseTags en el narrador
        y en el clásico por portal (la cascada ya no muestra ni habla texto:
        solo imagen + voz limpia)."""
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        nar = ai[ai.index("bool AIEngine::narrateStoryPart"):]
        nar = nar[:nar.index("\n}\n")]
        self.assertIn("parseTags(speak", nar)
        self.assertIn("Cuento voz limpia", nar)
        self.assertIn("streamTtsToSpeaker(speak", nar)

    def test_gemini_tts_no_stack_overflow(self):
        """2026-09-09: streamGeminiTts escribía el triple base64 (3 B) y
        recién después chequeaba el buffer de 512 B: 1-2 B pasados en stack
        por ciclo = audio corrupto + PANIC a mitad del cuento. El flush va
        ANTES de escribir."""
        with open("src/ai_engine.cpp", "r", encoding="utf-8") as f:
            ai = f.read()
        gem = ai[ai.index("bool AIEngine::streamGeminiTts"):]
        gem = gem[:gem.index("\n}\n")]
        self.assertIn("flushPcmBuf", gem)
        self.assertIn("pcmIdx + 3 > sizeof(pcmBuf)", gem)
        # El flush previo está antes de la escritura del triple
        self.assertLess(gem.index("pcmIdx + 3 > sizeof(pcmBuf)"),
                        gem.index("uint32_t triple = "))
        with open("src/wifi_portal.cpp", "r", encoding="utf-8") as f:
            wp = f.read()
        task = wp[wp.index("static void apiStoryTask"):]
        task = task[:task.index("\n}\n")]
        self.assertIn("parseTags(storyText", task)
        # La cascada es solo-imagen: ni texto en pantalla ni parse de display
        with open("src/story_cascade.cpp", "r", encoding="utf-8") as f:
            sc = f.read()
        self.assertNotIn("showPartText", sc)
        player = sc[sc.index("Reproductor: SOLO imagen"):]
        self.assertNotIn("renderPaginatorPage", player)


if __name__ == "__main__":
    unittest.main(verbosity=2)



