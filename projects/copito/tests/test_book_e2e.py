#!/usr/bin/env python3
"""E2E del libro v2 contra servidor HTTP real (ThreadingHTTPServer).

Ollama/ComfyUI/Kokoro se mockean; el cableado HTTP, el cache en disco
y el loop automatizado (assets -> layout +0.15s) son reales.
"""

import io
import json
import os
import sys
import tempfile
import threading
import unittest
import urllib.parse
import urllib.request
import urllib.error
import wave
from http.server import ThreadingHTTPServer

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "pi5"))
import copito_story as c

AD = {"pose_action": "kneeling and reaching under bed",
      "scene_environment": "bedroom with wooden floor and toys",
      "lighting_and_camera": "medium shot, eye-level, diffused daylight"}

BOOK_BODY = {"nombre": "Lucas", "edad": 5,
             "descripcion": "pelo castano rizado, peto vaquero y zapatillas rojas",
             "tema": "dinosaurios", "conflicto": "se rompe su camion",
             "paginas": 6, "estilo": "storybook watercolor"}


def canned_raw():
    pages = []
    for i in range(6):
        words = ["Pagina%d" % i] + ["palabra%d" % k for k in range(1, 20)]
        pages.append({"page_number": i + 1, "dramatic_beat": "Beat%d" % i,
                      "story_text": " ".join(words) + ".",
                      "art_director": dict(AD), "mood": "calm and curious"})
    return json.dumps({"story_title": "El camion", "pages": pages})


def write_wav(path, seconds=0.5, rate=24000):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"\x01\x00" * int(rate * seconds))


class TestBookE2E(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls._old_cache = c.CACHE_DIR
        c.CACHE_DIR = cls.tmp
        os.makedirs(cls.tmp, exist_ok=True)
        # Mocks: sin red externa.
        cls._old = (c.ollama_json, c.ollama_translate_spanish,
                    c.render_image, c.synth_audio,
                    c.AGENTIC_ENABLED)
        c.ollama_json = lambda prompt, temp: canned_raw()
        c.ollama_translate_spanish = lambda t: "curly brown hair, denim overalls, red sneakers"
        c.render_image = lambda img_id, ip, seed: False
        c.synth_audio = lambda audio_id, text, mood="x": (
            write_wav(os.path.join(cls.tmp, audio_id + ".wav")),
            os.path.join(cls.tmp, audio_id + ".wav"))[1]
        c.AGENTIC_ENABLED = False
        cls.srv = ThreadingHTTPServer(("127.0.0.1", 0), c.Handler)
        cls.port = cls.srv.server_address[1]
        cls.base = "http://127.0.0.1:%d" % cls.port
        cls.th = threading.Thread(target=cls.srv.serve_forever, daemon=True)
        cls.th.start()

    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown()
        (c.ollama_json, c.ollama_translate_spanish,
         c.render_image, c.synth_audio,
         c.AGENTIC_ENABLED) = cls._old
        c.CACHE_DIR = cls._old_cache

    def _req(self, method, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.base + path, data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                ct = r.headers.get("Content-Type", "")
                raw = r.read()
                if "json" in ct:
                    return r.status, json.loads(raw.decode())
                return r.status, raw
        except urllib.error.HTTPError as e:
            raw = e.read()
            try:
                return e.code, json.loads(raw.decode())
            except ValueError:
                return e.code, raw

    def test_01_health(self):
        code, doc = self._req("GET", "/health")
        self.assertEqual(code, 200)
        self.assertTrue(doc["ok"])
        self.assertIn("critic_model", doc)
        self.assertIn("agentic", doc)

    def test_02_post_book_shape(self):
        code, doc = self._req("POST", "/book", BOOK_BODY)
        self.assertEqual(code, 200, doc)
        self.assertEqual(doc["story_title"], "El camion")
        self.assertEqual(doc["target_age"], 5)
        self.assertEqual(doc["style_bible_applied"], "storybook watercolor")
        self.assertEqual(len(doc["pages"]), 6)
        for i, pg in enumerate(doc["pages"]):
            self.assertEqual(pg["page_number"], i + 1)
            self.assertIn("dramatic_beat", pg)
            self.assertTrue(pg["story_text"].endswith("."))
            ad = pg["art_director"]
            self.assertTrue(ad["character_anchor"].startswith("Lucas, a 5-year-old"))
            self.assertIn(ad["character_anchor"], pg["image_prompt"])
            self.assertIn("storybook watercolor", pg["image_prompt"])
            self.assertEqual(pg["audio_cues"]["recommended_pace"], 0.88)
            for k in ("img", "jpg", "audio"):
                self.assertTrue(pg["assets"][k].startswith(self.base + "/book/"), pg["assets"])
        self.assertIn("qa", doc)
        self.assertIn("sync", doc)
        type(self).slug = c.slug_of(c.parse_params(BOOK_BODY))
        # slug cacheado en disco con qa
        with open(os.path.join(self.tmp, type(self).slug + ".json"), encoding="utf-8") as f:
            cached = json.load(f)
        self.assertEqual(cached["titulo"], "El camion")
        self.assertIn("qa", cached)

    def test_03_get_book_cache_hit(self):
        q = urllib.parse.urlencode(BOOK_BODY)
        code, doc = self._req("GET", "/book?" + q)
        self.assertEqual(code, 200, doc)
        self.assertEqual(len(doc["pages"]), 6)

    def test_04_img_bin(self):
        slug = type(self).slug
        with open(os.path.join(self.tmp, "%s_p0.bin" % slug), "wb") as f:
            f.write(b"\xaa" * 5000)
        code, raw = self._req("GET", "/book/img/%s_p0" % slug)
        self.assertEqual(code, 200)
        self.assertEqual(len(raw), 5000)
        code, doc = self._req("GET", "/book/img/%s_p1" % slug)
        self.assertEqual(code, 502)  # comfyui mockeado en False
        code, _ = self._req("GET", "/book/img/mala!!id")
        self.assertEqual(code, 400)
        code, _ = self._req("GET", "/book/img/b2noexiste_p0")
        self.assertEqual(code, 404)

    def test_05_jpg(self):
        slug = type(self).slug
        with open(os.path.join(self.tmp, "%s_p0.jpg" % slug), "wb") as f:
            f.write(b"\xff\xd8" + b"x" * 2000)
        code, raw = self._req("GET", "/book/jpg/%s_p0" % slug)
        self.assertEqual(code, 200)
        self.assertTrue(len(raw) > 500)
        code, _ = self._req("GET", "/book/jpg/%s_p9" % slug)
        self.assertEqual(code, 404)

    def test_06_audio_and_layout(self):
        slug = type(self).slug
        code, raw = self._req("GET", "/book/audio/%s_p0" % slug)
        self.assertEqual(code, 200)
        self.assertTrue(raw[:4] == b"RIFF", raw[:8])
        code, doc = self._req("GET", "/book/layout/%s" % slug)
        self.assertEqual(code, 200)
        self.assertEqual(len(doc["pages"]), 6)
        self.assertAlmostEqual(doc["pages"][0]["audio_sec"], 0.5, places=1)
        for row in doc["pages"]:
            self.assertAlmostEqual(row["page_sec"] - row["audio_sec"], 0.15, places=2)
        code, _ = self._req("GET", "/book/layout/b2noexiste")
        self.assertEqual(code, 404)
        code, _ = self._req("GET", "/book/audio/b2noexiste_p0")
        self.assertEqual(code, 404)

    def test_07_errors(self):
        code, _ = self._req("POST", "/book", {"nombre": "X"})
        self.assertEqual(code, 400)
        code, _ = self._req("GET", "/book?nombre=X")
        self.assertEqual(code, 400)
        code, _ = self._req("GET", "/nada")
        self.assertEqual(code, 404)
        req = urllib.request.Request(self.base + "/book", data=b"{mal",
                                     method="POST",
                                     headers={"Content-Type": "application/json"})
        try:
            urllib.request.urlopen(req, timeout=10)
            self.fail("debió fallar")
        except urllib.error.HTTPError as e:
            self.assertEqual(e.code, 400)

    def test_08_demo(self):
        code, raw = self._req("GET", "/demo")
        self.assertEqual(code, 200)
        self.assertIn("Copito", raw.decode("utf-8", "replace"))
        self.assertIn("/book?texto=", raw.decode("utf-8", "replace"))

    def test_08b_story_legacy(self):
        # el firmware ESP32 habla v1: GET /story?topic= -> {titulo, partes[]}
        q = urllib.parse.urlencode({"topic": "un faro y las ballenas",
                                    "paginas": 6})
        code, doc = self._req("GET", "/story?" + q)
        self.assertEqual(code, 200, doc)
        self.assertIn("titulo", doc)
        self.assertEqual(len(doc["partes"]), 6)
        for p in doc["partes"]:
            self.assertIn("texto", p)
            self.assertIn("imagen", p)
            self.assertTrue(p["img"].startswith(self.base + "/story/img/"), p)


if __name__ == "__main__":
    unittest.main()
