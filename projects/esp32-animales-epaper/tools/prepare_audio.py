#!/usr/bin/env python3
"""
Preparacion de audios WAV PCM (16 kHz, 16-bit, Mono) para LittleFS (/sounds/).

- Si existe un archivo real en assets/raw_audio/<id>.(wav|mp3|ogg|flac|m4a):
  recorta silencio inicial, normaliza a -3 dBFS, recorta a max 2.5 s y exporta
  a data/sounds/<id>.wav en formato PCM 16 kHz 16-bit Mono.
- Si NO existe archivo real: genera un audio placeholder sintetizado amigable y
  distintivo por animal para probar el firmware desde el primer minuto.

Uso:
  python tools/prepare_audio.py
"""

from __future__ import annotations

import math
import shutil
import struct
import subprocess
import sys
import wave
from pathlib import Path
from typing import Dict, List, Optional

SAMPLE_RATE = 16000
MAX_DURATION_SEC = 2.5
TARGET_PEAK = int(32767 * (10 ** (-1.0 / 20.0)))  # -1 dBFS (~29204, alto y claro en 1W)

ROOT_DIR = Path(__file__).resolve().parent.parent
RAW_DIR = ROOT_DIR / "assets" / "raw_audio"
OUT_DIR = ROOT_DIR / "data" / "sounds"

ANIMAL_IDS: List[str] = [
    "perro",
    "gato",
    "vaca",
    "pato",
    "leon",
    "oveja",
    "sapo",
    "elefante",
    "mono",
    "caballo",
    "tigre",
    "grillo",
    "cerdo",
    "esqueleto",
    "monstruo",
]


def save_wav_mono16(path: Path, samples: List[int]) -> None:
    max_samples = int(SAMPLE_RATE * MAX_DURATION_SEC)
    clipped = [max(-32768, min(32767, int(s))) for s in samples[:max_samples]]
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(struct.pack(f"<{len(clipped)}h", *clipped))


def normalize_samples(samples: List[int]) -> List[int]:
    if not samples:
        return samples
    peak = max(abs(s) for s in samples)
    if peak == 0:
        return samples
    scale = TARGET_PEAK / float(peak)
    return [int(s * scale) for s in samples]


def trim_leading_silence(samples: List[int], threshold: int = 350) -> List[int]:
    for idx, s in enumerate(samples):
        if abs(s) >= threshold:
            return samples[max(0, idx - 160) :]
    return samples


def find_real_ffmpeg() -> Optional[str]:
    """Localiza ffmpeg real evitando wrappers stub en Windows."""
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


def convert_with_ffmpeg(src: Path, dst: Path) -> bool:
    ffmpeg = find_real_ffmpeg()
    if not ffmpeg:
        return False
    tmp = dst.with_suffix(".tmp.wav")
    cmd = [
        ffmpeg,
        "-y",
        "-i",
        str(src),
        "-t",
        str(MAX_DURATION_SEC),
        "-af",
        "silenceremove=start_periods=1:start_silence=0.05:start_threshold=-45dB,acompressor=threshold=-20dB:ratio=4:attack=5:release=60:makeup=6dB",
        "-ar",
        str(SAMPLE_RATE),
        "-ac",
        "1",
        "-sample_fmt",
        "s16",
        str(tmp),
    ]
    res = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if res.returncode != 0 or not tmp.exists():
        if tmp.exists():
            tmp.unlink()
        return False

    with wave.open(str(tmp), "rb") as wf:
        raw = wf.readframes(wf.getnframes())
    tmp.unlink()
    samples = list(struct.unpack(f"<{len(raw) // 2}h", raw))
    samples = normalize_samples(trim_leading_silence(samples))
    save_wav_mono16(dst, samples)
    return True


def find_raw_audio(animal_id: str) -> Optional[Path]:
    for ext in (".wav", ".mp3", ".ogg", ".flac", ".m4a"):
        candidate = RAW_DIR / f"{animal_id}{ext}"
        if candidate.exists():
            return candidate
    return None


def synth_envelope(t: float, dur: float, attack: float = 0.02, release: float = 0.04) -> float:
    if t < 0 or t > dur:
        return 0.0
    if t < attack:
        return t / attack
    if t > dur - release:
        return max(0.0, (dur - t) / release)
    return 1.0


def generate_placeholder_samples(animal_id: str) -> List[int]:
    """Sintetiza un sonido amigable y distintivo por animal (16 kHz, 16-bit)."""
    dur = 1.45 if animal_id == "mono" else 0.95
    n = int(SAMPLE_RATE * dur)
    out: List[float] = [0.0] * n

    for i in range(n):
        t = i / float(SAMPLE_RATE)
        val = 0.0

        if animal_id == "perro":
            # Dos ladridos cortos: 0.05..0.28 y 0.40..0.65
            for start in (0.05, 0.40):
                lt = t - start
                if 0 <= lt <= 0.22:
                    freq = 360.0 - 150.0 * (lt / 0.22)
                    env = synth_envelope(lt, 0.22, 0.015, 0.06)
                    val += env * (0.7 * math.sin(2 * math.pi * freq * lt) + 0.3 * math.sin(4 * math.pi * freq * lt))

        elif animal_id == "gato":
            # Maullido suave ascendente y descendente
            env = synth_envelope(t, 0.85, 0.08, 0.18)
            freq = 520.0 + 260.0 * math.sin(math.pi * min(1.0, t / 0.85))
            val = env * (0.75 * math.sin(2 * math.pi * freq * t) + 0.25 * math.sin(4 * math.pi * freq * t))

        elif animal_id == "vaca":
            # Mugido grave sostenido
            env = synth_envelope(t, 0.92, 0.12, 0.20)
            freq = 135.0 + 18.0 * math.sin(2 * math.pi * 1.5 * t)
            val = env * (0.6 * math.sin(2 * math.pi * freq * t) + 0.3 * math.sin(4 * math.pi * freq * t) + 0.1 * math.sin(6 * math.pi * freq * t))

        elif animal_id == "pato":
            # Dos cuacs nasales
            for start in (0.05, 0.36):
                lt = t - start
                if 0 <= lt <= 0.22:
                    env = synth_envelope(lt, 0.22, 0.015, 0.05)
                    freq = 420.0 - 60.0 * (lt / 0.22)
                    val += env * (0.5 * math.sin(2 * math.pi * freq * lt) + 0.35 * math.sin(6 * math.pi * freq * lt))

        elif animal_id == "leon":
            # Rugido grave con modulacion
            env = synth_envelope(t, 0.90, 0.08, 0.25)
            freq = 110.0 - 25.0 * (t / 0.90)
            mod = 0.6 + 0.4 * math.sin(2 * math.pi * 24.0 * t)
            val = env * mod * (0.7 * math.sin(2 * math.pi * freq * t) + 0.3 * math.sin(3 * math.pi * freq * t))

        elif animal_id == "oveja":
            # Balido con vibrato (tremolo)
            env = synth_envelope(t, 0.88, 0.06, 0.16)
            vib = 1.0 + 0.08 * math.sin(2 * math.pi * 14.0 * t)
            freq = 310.0 * vib
            val = env * (0.65 * math.sin(2 * math.pi * freq * t) + 0.35 * math.sin(4 * math.pi * freq * t))

        elif animal_id == "sapo":
            # Croac doble grave-medio
            for start in (0.06, 0.42):
                lt = t - start
                if 0 <= lt <= 0.24:
                    env = synth_envelope(lt, 0.24, 0.02, 0.06)
                    freq = 190.0 + 90.0 * (lt / 0.24)
                    pulse = 0.6 + 0.4 * math.sin(2 * math.pi * 30.0 * lt)
                    val += env * pulse * math.sin(2 * math.pi * freq * lt)

        elif animal_id == "elefante":
            # Barrito tipo trompeta ascendente
            env = synth_envelope(t, 0.90, 0.06, 0.20)
            freq = 280.0 + 340.0 * math.sin(math.pi * min(1.0, t / 0.75))
            val = env * (0.65 * math.sin(2 * math.pi * freq * t) + 0.35 * math.sin(4 * math.pi * freq * t))

        elif animal_id == "esqueleto":
            # Huesos chocando: impacto seco corto + resonancia hueca en ritmo doble-triple
            hits = [
                (0.05, 860.0), (0.14, 1080.0),
                (0.32, 790.0), (0.41, 1020.0), (0.50, 910.0),
                (0.70, 840.0), (0.80, 1120.0),
            ]
            for idx_h, (h_start, h_freq) in enumerate(hits):
                lt = t - h_start
                if 0 <= lt <= 0.085:
                    # Ruido determinista corto de golpe seco (10 ms) + cuerpo hueco de hueso
                    click = math.sin(2 * math.pi * (h_freq * 3.17) * lt) * math.exp(-lt * 180.0)
                    body = (
                        0.65 * math.sin(2 * math.pi * h_freq * lt)
                        + 0.35 * math.sin(2 * math.pi * (h_freq * 1.62) * lt)
                    ) * math.exp(-lt * 62.0)
                    val += 0.45 * click + 0.85 * body

        elif animal_id == "mono":
            # Clasico "¡Uh-Uh-Uh-Ah-Ah!" animado infantil resaltando la "U"
            # 3 "Uh" (vocal U: fundamental + 2do armonico suave, curva ascendente-descendente)
            calls = [
                (0.04, 0.16, 330.0, 470.0, 0.25),
                (0.24, 0.16, 350.0, 500.0, 0.25),
                (0.44, 0.16, 370.0, 530.0, 0.25),
                # 2 "Ah" finales mas brillantes, alegres y con cierre largo suave
                (0.66, 0.18, 460.0, 640.0, 0.45),
                (0.88, 0.34, 490.0, 680.0, 0.45),
            ]
            for c_start, c_dur, f_low, f_high, bright in calls:
                lt = t - c_start
                if 0 <= lt <= c_dur:
                    p = lt / c_dur
                    rel = 0.12 if c_dur > 0.25 else 0.045
                    env = synth_envelope(lt, c_dur, 0.025, rel)
                    freq = f_low + (f_high - f_low) * math.sin(math.pi * p)
                    # Formante de "U" (fundamental dominante) -> "A" (mas armonicos)
                    val += env * (
                        (1.0 - 0.35 * bright) * math.sin(2 * math.pi * freq * lt)
                        + (0.30 + 0.30 * bright) * math.sin(4 * math.pi * freq * lt)
                        + (0.15 * bright) * math.sin(6 * math.pi * freq * lt)
                    )

        elif animal_id == "grillo":
            # Canto clasico de grillo ("cri-cri... cri-cri...")
            chirps = [0.06, 0.16, 0.45, 0.55, 0.84, 0.94]
            for c_start in chirps:
                lt = t - c_start
                if 0 <= lt <= 0.075:
                    env = synth_envelope(lt, 0.075, 0.01, 0.02)
                    pulse = 0.5 + 0.5 * math.sin(2 * math.pi * 55.0 * lt)
                    val += env * pulse * (
                        0.7 * math.sin(2 * math.pi * 3400.0 * lt)
                        + 0.3 * math.sin(2 * math.pi * 2200.0 * lt)
                    )

        out[i] = val * TARGET_PEAK

    return normalize_samples([int(x) for x in out])


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for animal_id in ANIMAL_IDS:
        dst = OUT_DIR / f"{animal_id}.wav"
        raw = find_raw_audio(animal_id)
        if raw is not None and convert_with_ffmpeg(raw, dst):
            print(f"[REAL] {animal_id}: {raw.name} -> {dst.name}")
        else:
            samples = generate_placeholder_samples(animal_id)
            save_wav_mono16(dst, samples)
            print(f"[SYNTH] {animal_id}: placeholder generado -> {dst.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
