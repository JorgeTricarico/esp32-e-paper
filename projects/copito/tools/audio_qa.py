#!/usr/bin/env python3
"""
audio_qa.py — QA de voz TTS sin oído humano (puerta DSP).
Uso:
    python tools/audio_qa.py muestra.wav [otra.mp3 ...]
    python tools/audio_qa.py --selftest        # 4 señales sintéticas: deben dar
                                               # LIMPIO/SATURADO/BAJO/CORTADO
    python tools/audio_qa.py --expect 3.0 muestra.wav   # CORTADO si dura menos

Veredicto: LIMPIO | SATURADO | BAJO | CORTADO (ver analyze_samples).
"""
import sys

import numpy as np

try:
    import soundfile as sf

    HAVE_SF = True
except Exception:
    HAVE_SF = False

try:
    from scipy.signal import resample_poly

    HAVE_SCIPY = True
except Exception:
    HAVE_SCIPY = False


def analyze_samples(x, sr=16000, expected_sec=None):
    """x: mono float en [-1, 1] (o int16). Retorna dict con métricas + veredicto."""
    x = np.asarray(x, dtype=np.float64).ravel()
    if x.dtype == np.int16 or np.max(np.abs(x)) > 1.5:
        x = x / 32768.0
    n = len(x)
    if n == 0:
        return {"verdict": "CORTADO", "dur": 0.0}
    peak = float(np.max(np.abs(x))) + 1e-12
    peak_db = 20 * np.log10(peak)
    rms = float(np.sqrt(np.mean(x ** 2))) + 1e-12
    rms_db = 20 * np.log10(rms)
    cf = peak_db - rms_db
    dc = float(np.mean(x))
    clip_pct = 100 * float(np.mean(np.abs(x) >= 0.999))
    # rachas de >=5 muestras consecutivas en full-scale
    m = np.abs(x) >= 0.999
    runs = 0
    longest = 0
    c = 0
    for v in m:
        if v:
            c += 1
        else:
            if c >= 5:
                runs += 1
                longest = max(longest, c)
            c = 0
    if c >= 5:
        runs += 1
        longest = max(longest, c)
    dur = n / float(sr)
    verdict = "LIMPIO"
    if expected_sec and dur < 0.85 * expected_sec:
        verdict = "CORTADO"
    elif clip_pct > 0.1 or runs >= 3 or (peak_db >= -0.3 and cf < 6):
        verdict = "SATURADO"
    elif rms_db < -30 or peak_db < -20:
        verdict = "BAJO"
    return {
        "peak_db": round(peak_db, 2),
        "rms_db": round(rms_db, 2),
        "cf_db": round(cf, 2),
        "dc": round(dc, 4),
        "clip_pct": round(clip_pct, 3),
        "runs": runs,
        "run_max": longest,
        "dur": round(dur, 2),
        "verdict": verdict,
    }


def analyze_file(path, expected_sec=None, sr_target=16000):
    if not HAVE_SF:
        raise RuntimeError("falta soundfile: pip install soundfile")
    y, sr = sf.read(path, always_2d=True)
    x = y.mean(axis=1, dtype=np.float64)
    if HAVE_SCIPY and sr != sr_target:
        from math import gcd

        g = gcd(sr_target, sr)
        x = resample_poly(x, sr_target // g, sr // g).astype(np.float64)
        sr = sr_target
    r = analyze_samples(x, sr, expected_sec)
    r["file"] = path
    return r


def selftest():
    sr = 16000
    t = np.arange(sr * 2) / sr
    ok = True

    def check(name, x, want, **kw):
        nonlocal ok
        r = analyze_samples(x, sr, **kw)
        good = r["verdict"] == want
        ok = ok and good
        print(f"{'PASS' if good else 'FAIL'} {name}: {r['verdict']} (esperado {want}) {r}")

    # LIMPIO: voz simulada (senos mezclados, pico -3dBFS, CF~10)
    clean = 0.5 * np.sin(2 * np.pi * 220 * t) + 0.2 * np.sin(2 * np.pi * 440 * t)
    check("limpio", clean, "LIMPIO")
    # SATURADO: cuadrada full-scale recortada
    check("saturado", np.sign(np.sin(2 * np.pi * 220 * t)) * 0.999, "SATURADO")
    # BAJO: seno a -40dBFS
    check("bajo", 0.01 * np.sin(2 * np.pi * 220 * t), "BAJO")
    # CORTADO: 1s cuando se esperaban 3
    check("cortado", clean[:sr], "CORTADO", expected_sec=3.0)
    print("SELFTEST", "OK" if ok else "FALLIDO")
    return ok


if __name__ == "__main__":
    args = sys.argv[1:]
    if not args or "--help" in args:
        print(__doc__)
        sys.exit(0 if args else 1)
    if "--selftest" in args:
        sys.exit(0 if selftest() else 1)
    expected = None
    if "--expect" in args:
        i = args.index("--expect")
        expected = float(args[i + 1])
        del args[i : i + 2]
    bad = 0
    for path in args:
        try:
            r = analyze_file(path, expected)
            print(f"{r['verdict']:9s} {r['file']} peak={r['peak_db']}dBFS rms={r['rms_db']}dBFS "
                  f"CF={r['cf_db']}dB clip={r['clip_pct']}% runs={r['runs']} dur={r['dur']}s")
            if r["verdict"] != "LIMPIO":
                bad += 1
        except Exception as e:
            print(f"ERROR {path}: {e}")
            bad += 1
    sys.exit(1 if bad else 0)
