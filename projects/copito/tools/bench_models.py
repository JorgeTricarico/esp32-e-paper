#!/usr/bin/env python3
"""Compara modelos Ollama con el prompt real de cuentos (1 llamada c/u).
Uso: python3 bench_models.py [modelo ...]
"""
import os
import sys
import time

sys.path.insert(0, os.path.expanduser("~/bot"))
import copito_story as cs

TOPIC = "un zorro que aprende a compartir su comida"
MODELS = sys.argv[1:] or ["qwen2:1.5b", "qwen2.5:1.5b"]

for model in MODELS:
    cs.OLLAMA_MODEL = model
    prompt = cs.prompt_for(TOPIC, 3, 4, 220, 300, "ninos de 3 a 8 anios")
    t0 = time.time()
    try:
        raw = cs.ollama_json(prompt, 0.2)
    except Exception as e:
        print("MODEL %s FALLO %s" % (model, str(e)[:100]), flush=True)
        continue
    dt = time.time() - t0
    title, personaje, parts, reason = cs.validate_story(raw)
    print("MODEL %s %.0fs partes=%d largos=%s motivo=%s titulo=%s" % (
        model, dt, len(parts), [len(p["texto"]) for p in parts],
        reason, title), flush=True)
print("BENCH_DONE", flush=True)
