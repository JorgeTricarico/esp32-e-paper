#!/usr/bin/env python3
"""
Genera audios de voz humana consistente (Google TTS Latino es-419, voz única)
para las 26 letras del abecedario (data/letters/*.wav) y los 15 nombres
(data/names/*.wav) en formato PCM 16 kHz, 16-bit, Mono, -1 dBFS con pre-roll limpio.

Mantiene también synthesize_piper() para el Studio local (tools/studio_server.py).
"""

from __future__ import annotations

import math
import shutil
import struct
import subprocess
import sys
import urllib.parse
import urllib.request
import wave
from pathlib import Path
from typing import List, Optional, Tuple

SAMPLE_RATE_OUT = 16000
TARGET_PEAK = int(32767 * (10 ** (-1.0 / 20.0)))  # -1 dBFS (~29204)

ROOT_DIR = Path(__file__).resolve().parent.parent
VOICES_DIR = ROOT_DIR / "tools" / "voices"
OUT_LETTERS_DIR = ROOT_DIR / "data" / "letters"
OUT_NAMES_DIR = ROOT_DIR / "data" / "names"

LETTER_TEMPO = 0.95
WORD_TEMPO = 0.84

LETTER_ITEMS: List[Tuple[str, str]] = [
    ("a", "a."), ("b", "b larga"), ("c", "Ce"), ("d", "De"), ("e", "E"),
    ("f", "Efe"), ("g", "Ge"), ("h", "Hache"), ("i", "I"), ("j", "Jota"),
    ("k", "Ka"), ("l", "Ele"), ("m", "Eme"), ("n", "Ene"), ("o", "O"),
    ("p", "Pe"), ("q", "Cu"), ("r", "Erre"), ("s", "Ese"), ("t", "Te"),
    ("u", "U"), ("v", "Ve corta"), ("w", "Doble ve"), ("x", "Equis"), ("y", "I griega"),
    ("z", "Zeta"),
]

NAME_ITEMS: List[Tuple[str, str]] = [
    ("perro", "Perro"), ("gato", "Gato"), ("vaca", "Vaca"), ("pato", "Pato"),
    ("leon", "León"), ("oveja", "Oveja"), ("sapo", "Sapo"), ("elefante", "Elefante"),
    ("mono", "Mono"), ("caballo", "Caballo"), ("tigre", "Tigre"), ("grillo", "Grillo"),
    ("cerdo", "Cerdo"), ("esqueleto", "Esqueleto"), ("monstruo", "Monstruo"),
]


def find_real_ffmpeg() -> Optional[str]:
    candidate = shutil.which("ffmpeg")
    if candidate and "ffmpeg-stub" not in candidate.lower():
        return candidate
    winget_dir = Path.home() / "AppData" / "Local" / "Microsoft" / "WinGet" / "Packages"
    if winget_dir.exists():
        for exe in winget_dir.glob("Gyan.FFmpeg*/*/bin/ffmpeg.exe"):
            return str(exe)
        for exe in winget_dir.glob("Gyan.FFmpeg*/*/*/bin/ffmpeg.exe"):
            return str(exe)
    return None


def _normalize_wav(tmp_16k: Path, dst_wav: Path) -> float:
    with wave.open(str(tmp_16k), "rb") as wf:
        raw = wf.readframes(wf.getnframes())
    samples = list(struct.unpack(f"<{len(raw) // 2}h", raw))
    peak = max((abs(s) for s in samples), default=0)
    if peak > 0:
        scale = TARGET_PEAK / float(peak)
        samples = [max(-32768, min(32767, int(s * scale))) for s in samples]

    dst_wav.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(dst_wav), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE_OUT)
        wf.writeframes(struct.pack(f"<{len(samples)}h", *samples))

    return math.sqrt(sum(x * x for x in samples) / max(1, len(samples)))


def synthesize_piper(voice, ffmpeg: str, text: str, dst_wav: Path, length_scale: float) -> float:
    from piper import SynthesisConfig
    tmp_raw = dst_wav.with_suffix(".piper.wav")
    tmp_16k = dst_wav.with_suffix(".16k.wav")

    cfg = SynthesisConfig(length_scale=length_scale, noise_scale=0.35, noise_w_scale=0.35)
    with wave.open(str(tmp_raw), "wb") as wf:
        voice.synthesize_wav(text, wf, syn_config=cfg)

    af = (
        "highpass=f=90,"
        "silenceremove=start_periods=1:start_duration=0.01:start_silence=0.035:start_threshold=-48dB,"
        "afade=t=in:st=0:d=0.005,"
        "acompressor=threshold=-18dB:ratio=3:attack=5:release=80:makeup=4dB,"
        "apad=pad_dur=0.07"
    )
    cmd = [
        ffmpeg, "-y", "-i", str(tmp_raw),
        "-af", af,
        "-ar", str(SAMPLE_RATE_OUT), "-ac", "1", "-sample_fmt", "s16",
        str(tmp_16k),
    ]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    tmp_raw.unlink(missing_ok=True)
    try:
        return _normalize_wav(tmp_16k, dst_wav)
    finally:
        tmp_16k.unlink(missing_ok=True)


def synthesize_gtts(ffmpeg: str, text: str, dst_wav: Path, tempo: float, tl: str = "es-419") -> float:
    url = (
        "https://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob"
        f"&tl={urllib.parse.quote(tl)}&q={urllib.parse.quote(text)}"
    )
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    tmp_mp3 = dst_wav.with_suffix(".gtts.mp3")
    tmp_16k = dst_wav.with_suffix(".16k.wav")

    with urllib.request.urlopen(req, timeout=15) as r:
        tmp_mp3.write_bytes(r.read())

    filters = [
        "highpass=f=85",
        "silenceremove=start_periods=1:start_duration=0.01:start_silence=0.035:start_threshold=-54dB",
        "afade=t=in:st=0:d=0.005",
    ]
    if abs(tempo - 1.0) >= 0.03:
        t = max(0.5, min(2.0, tempo))
        filters.append(f"atempo={t:.2f}")
    filters.append("acompressor=threshold=-20dB:ratio=4:attack=5:release=70:makeup=5dB")
    filters.append("apad=pad_dur=0.06")

    cmd = [
        ffmpeg, "-y", "-i", str(tmp_mp3),
        "-af", ",".join(filters),
        "-ar", str(SAMPLE_RATE_OUT), "-ac", "1", "-sample_fmt", "s16",
        str(tmp_16k),
    ]
    try:
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        return _normalize_wav(tmp_16k, dst_wav)
    finally:
        tmp_mp3.unlink(missing_ok=True)
        tmp_16k.unlink(missing_ok=True)


def main() -> int:
    ffmpeg = find_real_ffmpeg()
    if not ffmpeg:
        print("[ERR] No se encontro ffmpeg real.")
        return 1

    # Limpiar audios previos para evitar mezclas de voces antiguas
    for d in (OUT_LETTERS_DIR, OUT_NAMES_DIR):
        d.mkdir(parents=True, exist_ok=True)
        for old in d.glob("*.wav"):
            old.unlink()

    print(f"[TTS] Generando 26 letras (gtts es-419, tempo={LETTER_TEMPO})...", flush=True)
    for key, spoken in LETTER_ITEMS:
        dst = OUT_LETTERS_DIR / f"{key}.wav"
        rms = synthesize_gtts(ffmpeg, spoken, dst, tempo=LETTER_TEMPO)
        print(f"  [OK] {key}.wav <- '{spoken}' ({dst.stat().st_size} B, RMS={rms:.0f})")

    print(f"[TTS] Generando 15 nombres (gtts es-419, tempo={WORD_TEMPO})...", flush=True)
    for key, spoken in NAME_ITEMS:
        dst = OUT_NAMES_DIR / f"{key}.wav"
        rms = synthesize_gtts(ffmpeg, spoken, dst, tempo=WORD_TEMPO)
        print(f"  [OK] {key}.wav <- '{spoken}' ({dst.stat().st_size} B, RMS={rms:.0f})")

    return 0


if __name__ == "__main__":
    sys.exit(main())
