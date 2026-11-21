#!/usr/bin/env python3
"""Unit tests del libro personalizado v2 (pi5/copito_story.py).

Sin red: todo lo que llamaría a Ollama/ComfyUI/Kokoro se mockea.
"""

import json
import os
import sys
import tempfile
import unittest
import wave

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "pi5"))
import copito_story as c


def make_params(**kw):
    base = {"nombre": "Lucas", "edad": "5",
            "descripcion": "pelo castano rizado, peto vaquero y zapatillas rojas",
            "tema": "dinosaurios", "conflicto": "se rompe su camion",
            "paginas": "6", "estilo": "storybook watercolor"}
    base.update(kw)
    return c.parse_params(base)


def make_text(n_words, head):
    words = [head] + ["palabra%d" % i for i in range(1, n_words)]
    return " ".join(words) + "."


AD = {"pose_action": "kneeling and reaching under bed",
      "scene_environment": "bedroom with wooden floor and toys",
      "lighting_and_camera": "medium shot, eye-level, diffused daylight"}


def make_raw(n, words=20):
    pages = [{"page_number": i + 1, "dramatic_beat": "Beat%d" % i,
              "story_text": make_text(words, "Pagina%d" % i),
              "art_director": dict(AD), "mood": "calm and curious"}
             for i in range(n)]
    return json.dumps({"story_title": "El camion", "pages": pages})


class TestBeats(unittest.TestCase):
    def test_lengths_5_to_10(self):
        for n in range(5, 11):
            self.assertEqual(len(c.beats_for_n(n)), n, "beats n=%d" % n)

    def test_arc_order(self):
        b = c.beats_for_n(7)
        self.assertEqual(b[0], "Apertura")
        self.assertEqual(b[1], "Detonante")
        self.assertIn("Momento difícil", b)
        self.assertEqual(b[-1], "Cierre")

    def test_clamps(self):
        self.assertEqual(len(c.beats_for_n(99)), 10)
        self.assertEqual(len(c.beats_for_n(1)), 5)


class TestWordsAndMood(unittest.TestCase):
    def test_ranges(self):
        self.assertEqual(c.words_range_for_age(3), (15, 30))
        self.assertEqual(c.words_range_for_age(5), (15, 30))
        self.assertEqual(c.words_range_for_age(6), (35, 60))
        self.assertEqual(c.words_range_for_age("x"), (35, 60))

    def test_moods(self):
        self.assertEqual(c.mood_for_beat("Apertura"), "calm and curious")
        self.assertEqual(c.mood_for_beat("Momento difícil"), "quiet and worried")
        self.assertEqual(c.mood_for_beat("Resolución y Cierre"), "bright and proud")
        self.assertEqual(c.mood_for_beat("??"), "warm and gentle")


class TestParseParams(unittest.TestCase):
    def test_defaults(self):
        p = c.parse_params({})
        self.assertEqual(p["nombre"], "Copito")
        self.assertEqual(p["edad"], 5)
        self.assertEqual(p["paginas"], 6)

    def test_clamps(self):
        p = c.parse_params({"edad": "99", "paginas": "99"})
        self.assertEqual(p["edad"], 8)
        self.assertEqual(p["paginas"], 10)
        p = c.parse_params({"edad": "1", "paginas": "1"})
        self.assertEqual(p["edad"], 3)
        self.assertEqual(p["paginas"], 5)

    def test_aliases(self):
        p = c.parse_params({"NOMBRE_NINO": "Ana", "EDAD": "7",
                            "NUMERO_PAGINAS": "8",
                            "ESTILO_VISUAL_FIJO": "oil painting",
                            "TEMA": "luna", "CONFLICTO": "miedo"})
        self.assertEqual((p["nombre"], p["edad"], p["paginas"],
                          p["estilo"], p["tema"]), ("Ana", 7, 8, "oil painting", "luna"))

    def test_slug_stable_and_unique(self):
        p1 = make_params()
        p2 = make_params()
        self.assertEqual(c.slug_of(p1), c.slug_of(p2))
        p3 = make_params(tema="otro tema")
        self.assertNotEqual(c.slug_of(p1), c.slug_of(p3))
        self.assertTrue(c.slug_of(p1).startswith("b2"))


class TestPrompt(unittest.TestCase):
    def test_contains_inputs_and_beats(self):
        p = make_params()
        pr = c.prompt_for_v2(p)
        for needle in ("Lucas", "dinosaurios", "se rompe su camion",
                       "Apertura", "Detonante", "prosa", "story_text",
                       "art_director", "15-30"):
            self.assertIn(needle, pr)
        p7 = make_params(edad="7")
        self.assertIn("35-60", c.prompt_for_v2(p7))

    def test_page_count_anchored(self):
        # El modelo chico copiaba el ejemplo de 1 página: el conteo va
        # redundante y explícito (EXACTAMENTE N + page_number 1..N).
        p = make_params()
        pr = c.prompt_for_v2(p)
        self.assertIn("EXACTAMENTE 6", pr)
        self.assertIn("page_number del 1 al 6", pr)
        p8 = make_params(paginas="8")
        self.assertIn("EXACTAMENTE 8", c.prompt_for_v2(p8))


class TestValidate(unittest.TestCase):
    def test_ok(self):
        p = make_params()
        book, reason = c.validate_book(make_raw(6, 20), p)
        self.assertIsNotNone(book, reason)
        self.assertEqual(book["story_title"], "El camion")
        self.assertEqual(len(book["pages"]), 6)
        self.assertEqual(book["pages"][0]["page_number"], 1)

    def test_wrong_count(self):
        p = make_params()
        book, reason = c.validate_book(make_raw(5, 20), p)
        self.assertIsNone(book)
        self.assertIn("6 paginas", reason)

    def test_too_short_and_too_long(self):
        p = make_params()
        book, reason = c.validate_book(make_raw(6, 3), p)
        self.assertIsNone(book)
        self.assertIn("largo", reason)
        book, reason = c.validate_book(make_raw(6, 80), p)
        self.assertIsNone(book)
        self.assertIn("largo", reason)

    def test_missing_period(self):
        p = make_params()
        raw = json.loads(make_raw(6, 20))
        raw["pages"][2]["story_text"] = raw["pages"][2]["story_text"][:-1]
        book, reason = c.validate_book(json.dumps(raw), p)
        self.assertIsNone(book)
        self.assertIn("punto final", reason)

    def test_banned_cliche(self):
        p = make_params()
        raw = json.loads(make_raw(6, 20))
        raw["pages"][0]["story_text"] = ("Pagina0 " + "x " * 18
                                         + "un tapiz de emociones.")
        book, reason = c.validate_book(json.dumps(raw), p)
        self.assertIsNone(book)
        self.assertIn("cliche", reason)

    def test_abstract_emotion(self):
        p = make_params()
        raw = json.loads(make_raw(6, 20))
        raw["pages"][1]["story_text"] = ("Pagina1 Lucas estaba asustado "
                                         + "y " + "x " * 15 + "fin.")
        book, reason = c.validate_book(json.dumps(raw), p)
        self.assertIsNone(book)
        self.assertIn("abstracta", reason)

    def test_autonomy_adult_rejected(self):
        p = make_params()
        raw = json.loads(make_raw(6, 20))
        raw["pages"][5]["story_text"] = ("Pagina5 Lucas decide que no puede "
                                         "solo y busca la ayuda de un adulto "
                                         + "x " * 8 + "fin.")
        book, reason = c.validate_book(json.dumps(raw), p)
        self.assertIsNone(book)
        self.assertIn("adulto", reason)

    def test_autonomy_someone_help_rejected(self):
        p = make_params()
        raw = json.loads(make_raw(6, 20))
        raw["pages"][0]["story_text"] = ("Pagina0 Lucas solo podia pedirle "
                                         "a alguien para ayudarle a salir "
                                         + "x " * 10 + "fin.")
        book, reason = c.validate_book(json.dumps(raw), p)
        self.assertIsNone(book)
        self.assertIn("adulto", reason)

    def test_autonomy_innocent_adult_passes(self):
        p = make_params()
        raw = json.loads(make_raw(6, 20))
        raw["pages"][0]["story_text"] = ("Pagina0 su mama lo mira desde la "
                                         "puerta mientras juega con bloques "
                                         + "x " * 8 + "fin.")
        book, reason = c.validate_book(json.dumps(raw), p)
        self.assertIsNotNone(book, reason)

    def test_art_must_be_english(self):
        p = make_params()
        raw = json.loads(make_raw(6, 20))
        raw["pages"][0]["art_director"]["pose_action"] = "el nino corre al bosque"
        book, reason = c.validate_book(json.dumps(raw), p)
        self.assertIsNone(book)
        self.assertIn("ingles", reason)

    def test_duplicates_rejected(self):
        p = make_params()
        raw = json.loads(make_raw(6, 20))
        raw["pages"][1]["story_text"] = raw["pages"][0]["story_text"]
        book, reason = c.validate_book(json.dumps(raw), p)
        self.assertIsNone(book)
        self.assertIn("repetidas", reason)

    def test_age7_range(self):
        p = make_params(edad="7")
        book, reason = c.validate_book(make_raw(6, 45), p)
        self.assertIsNotNone(book, reason)
        book, reason = c.validate_book(make_raw(6, 10), p)
        self.assertIsNone(book)


class TestSalvageComplete(unittest.TestCase):
    def _raw_partial(self):
        # 1 página válida + 1 inválida (texto corto) de 6 pedidas
        pages = [{"page_number": 1, "dramatic_beat": "Apertura",
                  "story_text": make_text(20, "Pagina0"),
                  "art_director": dict(AD), "mood": "calm and curious"},
                 {"page_number": 2, "dramatic_beat": "Detonante",
                  "story_text": "Corto.", "art_director": dict(AD),
                  "mood": "x"}]
        return json.dumps({"story_title": "T", "pages": pages})

    def test_salvage_keeps_only_valid(self):
        p = make_params()
        title, good = c.salvage_pages(self._raw_partial(), p)
        self.assertEqual(title, "T")
        self.assertEqual(len(good), 1)
        self.assertEqual(good[0]["page_number"], 1)

    def test_salvage_unparseable(self):
        p = make_params()
        self.assertEqual(c.salvage_pages("prosa suelta", p), ("", []))

    def test_completion_prompt_names_missing(self):
        p = make_params()
        _, have = c.salvage_pages(self._raw_partial(), p)
        pr = c.prompt_for_completion(p, have, "T")
        self.assertIn("2, 3, 4", pr)  # primer bloque de 3
        self.assertIn("Detonante", pr)
        self.assertIn("Pagina0", pr)  # contexto existente
        self.assertIn("15-30", pr)

    def test_merge_validates_set(self):
        p = make_params()
        _, have = c.salvage_pages(self._raw_partial(), p)
        more = []
        for i in range(5):
            words = ["Otra%d" % i] + ["w%d" % k for k in range(1, 20)]
            more.append({"page_number": 99, "dramatic_beat": "Nudo",
                         "story_text": " ".join(words) + ".",
                         "art_director": dict(AD), "mood": "x"})
        raw = json.dumps({"pages": more})
        book, reason = c.validate_completion(raw, p, have, "T")
        self.assertIsNotNone(book, reason)
        self.assertEqual([q["page_number"] for q in book["pages"]],
                         [1, 2, 3, 4, 5, 6])

    def test_completion_dup_rejected(self):
        p = make_params()
        _, have = c.salvage_pages(self._raw_partial(), p)
        raw = json.dumps({"pages": [{"page_number": 5,
                                     "story_text": have[0]["story_text"],
                                     "art_director": dict(AD)}]})
        book, reason = c.validate_completion(raw, p, have, "T")
        self.assertIsNone(book)
        self.assertIn("valida", reason)


class TestArtFix(unittest.TestCase):
    def test_is_english(self):
        self.assertTrue(c.art_is_english("kneeling and reaching under the bed"))
        self.assertFalse(c.art_is_english("el nino corre al bosque"))
        self.assertFalse(c.art_is_english("casa grande"))

    def test_stub_per_beat(self):
        self.assertIn("wide eyes", c.stub_for_beat("Detonante")[0])
        self.assertEqual(c.stub_for_beat("??"), c.stub_for_beat("Nudo"))

    def test_fix_translates_spanish(self):
        old = c.ollama_translate_spanish
        c.ollama_translate_spanish = lambda t: "small bedroom with wooden floor"
        try:
            p = make_params()
            raw = json.dumps({"story_title": "T", "pages": [
                {"page_number": 1, "dramatic_beat": "Apertura",
                 "story_text": make_text(20, "Pagina0"),
                 "art_director": {"pose_action": "el nino se arrodilla",
                                  "scene_environment": "casa con juguetes",
                                  "lighting_and_camera": "medium shot, daylight"},
                 "mood": "x"}]})
            fraw, err = c.fix_art_english(raw, p)
            self.assertIsNotNone(fraw, err)
            book, reason = c.validate_book(
                json.dumps({"story_title": "T", "pages":
                            json.loads(fraw)["pages"] * 6}), p)
            # 6 repetidas -> falla por duplicadas, pero cada una valida:
            self.assertIn("repetidas", reason)
            one = json.loads(fraw)["pages"][0]["art_director"]
            self.assertTrue(all(c.art_is_english(one[k]) for k in one))
        finally:
            c.ollama_translate_spanish = old

    def test_fix_empty_gets_stub(self):
        p = make_params()
        raw = json.dumps({"story_title": "T", "pages": [
            {"page_number": 1, "dramatic_beat": "Nudo",
             "story_text": make_text(20, "Pagina0"),
             "art_director": {"pose_action": "", "scene_environment": "",
                              "lighting_and_camera": ""}, "mood": "x"}]})
        fraw, err = c.fix_art_english(raw, p)
        self.assertIsNotNone(fraw, err)
        ad = json.loads(fraw)["pages"][0]["art_director"]
        self.assertTrue(all(len(ad[k]) >= 8 for k in ad))

    def test_prompt_has_concrete_example(self):
        pr = c.prompt_for_v2(make_params())
        self.assertIn("kneeling and reaching under", pr)


class TestTextoLibre(unittest.TestCase):
    def test_parse_ok(self):
        raw = ('{"nombre":"Pipo","edad":5,"descripcion":"gorrion pequeno",'
               '"tema":"un gorrion que queria ser paloma",'
               '"conflicto":"quiere ser paloma","paginas":6,"es_nino":false}')
        d = c.parse_interpretation(raw, "un gorrion que queria ser paloma")
        self.assertEqual((d["nombre"], d["edad"], d["paginas"], d["es_nino"]),
                         ("Pipo", 5, 6, False))

    def test_parse_garbage_defaults(self):
        d = c.parse_interpretation("prosa suelta sin json", "un dragon")
        self.assertEqual(d["edad"], 5)
        self.assertEqual(d["paginas"], 6)
        self.assertTrue(d["es_nino"])
        self.assertIn("dragon", d["tema"])

    def test_parse_clamps(self):
        d = c.parse_interpretation('{"edad":99,"paginas":2}', "x")
        self.assertEqual((d["edad"], d["paginas"]), (8, 5))

    def test_resolve_libre(self):
        old = c.ollama_interpret
        c.ollama_interpret = lambda t: ('{"nombre":"Pipo","edad":5,'
                                        '"descripcion":"gorrion",'
                                        '"tema":"t","conflicto":"c",'
                                        '"paginas":6,"es_nino":false}')
        try:
            raw = {"texto": "un gorrion que queria ser paloma"}
            p = c.resolve_params(raw, c.parse_params(raw))
            self.assertEqual(p["modo"], "libre")
            self.assertEqual(p["nombre"], "Pipo")
            self.assertFalse(p["es_nino"])
        finally:
            c.ollama_interpret = old

    def test_resolve_explicit_wins(self):
        called = []
        old = c.ollama_interpret
        c.ollama_interpret = lambda t: calls.append(t) or "{}"
        calls = called
        try:
            raw = {"texto": "algo", "nombre": "Lucas"}
            p = c.resolve_params(raw, c.parse_params(raw))
            self.assertEqual(p["modo"], "formulario")
            self.assertEqual(p["nombre"], "Lucas")
            self.assertEqual(called, [])
        finally:
            c.ollama_interpret = old

    def test_resolve_edad_override(self):
        old = c.ollama_interpret
        c.ollama_interpret = lambda t: ('{"nombre":"P","edad":4,'
                                        '"descripcion":"d","tema":"t",'
                                        '"conflicto":"c","paginas":5,'
                                        '"es_nino":true}')
        try:
            raw = {"texto": "algo", "edad": "7"}
            p = c.resolve_params(raw, c.parse_params(raw))
            self.assertEqual(p["modo"], "libre")
            self.assertEqual(p["edad"], 7)  # explícita manda
            self.assertEqual(p["nombre"], "P")  # resto lo deduce la IA
        finally:
            c.ollama_interpret = old

    def test_anchor_animal(self):
        old = c.ollama_translate_spanish
        c.ollama_translate_spanish = lambda t: "small sparrow with round body"
        try:
            p = make_params()
            p["es_nino"] = False
            p["nombre"] = "Pipo"
            a = c.character_anchor(p)
            self.assertTrue(a.startswith("Pipo,"))
            self.assertNotIn("child", a)
        finally:
            c.ollama_translate_spanish = old

    def test_rubric_teeth(self):
        self.assertIn("busca ayuda de un adulto", c.CRITIC_RUBRIC)
        self.assertIn("FAIL", c.CRITIC_RUBRIC)

    def test_infer_es_nino(self):
        self.assertFalse(c.infer_es_nino("gorrion pequeno de pecho redondo", True))
        self.assertFalse(c.infer_es_nino("pequeño dragón verde", True))
        self.assertFalse(c.infer_es_nino("robot de lata con antenas", True))
        self.assertFalse(c.infer_es_nino("un gorrion que canta", True))
        self.assertTrue(c.infer_es_nino("niño disfrazado de oso", True))
        self.assertTrue(c.infer_es_nino("nino de pelo castano rizado", True))
        self.assertTrue(c.infer_es_nino("", True))
        self.assertFalse(c.infer_es_nino("protagonista curioso", False))

    def test_completion_chunk(self):
        p = make_params()  # 6 páginas
        _, have = c.salvage_pages(json.dumps({"pages": [
            {"page_number": 1, "dramatic_beat": "Apertura",
             "story_text": make_text(20, "Pagina0"),
             "art_director": dict(AD), "mood": "x"}]}), p)
        pr = c.prompt_for_completion(p, have, "T")
        self.assertIn("2, 3, 4", pr)  # bloque de 3, no las 5 faltantes
        self.assertNotIn("5, 6", pr)

    def test_parse_overrides_wrong_flag(self):
        raw = ('{"nombre":"Pipo","edad":5,"descripcion":"gorrion pequeno",'
               '"tema":"t","conflicto":"c","paginas":6,"es_nino":true}')
        self.assertFalse(c.parse_interpretation(raw, "x")["es_nino"])


class TestDemoMagica(unittest.TestCase):
    PATH = os.path.join(os.path.dirname(__file__), "..", "pi5", "demo.html")

    def _demo(self):
        with open(self.PATH, encoding="utf-8") as f:
            return f.read()

    def test_ambiente_magico(self):
        d = self._demo()
        for needle in ("Cuentos magicos", "stars", "loadEmoji", "LOAD_EMOJI",
                       "bigPlay", "veil", "loadloop", "karaoke",
                       "so&ntilde;ar", "Parte siguiente"):
            self.assertIn(needle, d)

    def test_cableada_a_v2(self):
        d = self._demo()
        for needle in ("/book?texto=", "story_text", "assets.jpg",
                       "assets.audio", "story_title", "reparto",
                       "recommended_pace", "optN", "optE"):
            self.assertIn(needle, d)
        self.assertNotIn("optL", d)
        self.assertNotIn("j.partes", d)
        self.assertNotIn("j.titulo", d)

    def test_servidor_sirve_demo(self):
        self.assertTrue(len(c.DEMO_PAGE) > 10000)
        self.assertIn("/book?texto=", c.DEMO_PAGE)


class TestLegacyStory(unittest.TestCase):
    def test_story_v1_compatible(self):
        doc = {
            "story_title": "El faro",
            "pages": [
                {"story_text": "Un faro alto. Punto.", "image_prompt": "a lighthouse",
                 "assets": {"img": "http://H/book/img/slug_p0", "jpg": "http://H/book/jpg/slug_p0",
                            "audio": "http://H/book/audio/slug_p0"}},
            ],
        }
        out = c.legacy_book(doc, "http://H")
        self.assertEqual(out["titulo"], "El faro")
        p = out["partes"][0]
        self.assertEqual(p["texto"], "Un faro alto. Punto.")
        self.assertEqual(p["imagen"], "a lighthouse")
        self.assertEqual(p["img"], "http://H/story/img/slug_p0")

    def test_story_legacy_dummy(self):
        p5 = c.parse_params({"topic": "un faro y las ballenas"})
        try:
            doc = {"story_title": "T", "pages": [{"story_text": "a" * 30,
                    "image_prompt": "x", "assets": {"img": "http://H/book/img/y_p0"}}]}
        except Exception:
            self.fail("parse_params fallo")
        out = c.legacy_book(doc, "http://H")
        self.assertEqual(len(out["partes"]), 1)


class TestRemoteBackend(unittest.TestCase):
    def test_post_ollama_remoto_ok(self):
        payload = {"model": c.OLLAMA_MODEL, "prompt": "x", "stream": False}
        c.OLLAMA_URL_REMOTE = "http://127.0.0.1:1/api/generate"  # choca rápido
        c._ollama_req = lambda payload, url, timeout, model: "remoto:%s" % model
        self.assertEqual(c.post_ollama(payload), "remoto:gemma4:e2b")

    def test_post_ollama_cae_a_local(self):
        payload = {"model": c.OLLAMA_MODEL, "prompt": "x", "stream": False}
        c.OLLAMA_URL_REMOTE = "http://127.0.0.1:1/api/generate"
        c._ollama_req = lambda payload, url, timeout, model: (
            (_ for _ in ()).throw(OSError("no conecta"))
            if url == "http://127.0.0.1:1/api/generate" else "local:%s" % model)
        self.assertEqual(c.post_ollama(payload), "local:qwen3.5:0.8b")

    def tearDown(self):
        c.OLLAMA_URL_REMOTE = ""


class TestCriticPrompt(unittest.TestCase):
    def test_shape(self):
        p = make_params()
        book, _ = c.validate_book(make_raw(6, 20), p)
        pr = c.critic_prompt_for(book, p)
        self.assertIn("Rubrica", pr)
        self.assertIn("score", pr)
        self.assertIn("issues", pr)
        self.assertIn("Lucas", pr)


class TestAgentic(unittest.TestCase):
    def setUp(self):
        self._enabled = c.AGENTIC_ENABLED
        self._rounds = c.CRITIC_MAX_ROUNDS
        self._critic = c.ollama_critic
        self._repair = c.ollama_repair_with_issues
        self._validate = c.validate_book

    def tearDown(self):
        c.AGENTIC_ENABLED = self._enabled
        c.CRITIC_MAX_ROUNDS = self._rounds
        c.ollama_critic = self._critic
        c.ollama_repair_with_issues = self._repair
        c.validate_book = self._validate

    def _book(self):
        p = make_params()
        book, reason = self._validate(make_raw(6, 20), p)
        self.assertIsNotNone(book, reason)
        return p, book

    def test_off_keeps_book(self):
        p, book = self._book()
        c.AGENTIC_ENABLED = False
        out, qa = c.agentic_refine(book, p)
        self.assertEqual(out, book)
        self.assertEqual(qa["verdict"], "rule_ok_no_critic")

    def test_pass_first_try(self):
        p, book = self._book()
        c.AGENTIC_ENABLED = True
        c.CRITIC_MAX_ROUNDS = 2
        c.ollama_critic = lambda bk, pp: {"pass": True, "score": 9, "issues": []}
        out, qa = c.agentic_refine(book, p)
        self.assertEqual(out, book)
        self.assertEqual(qa["verdict"], "pass")
        self.assertEqual(qa["final_score"], 9)

    def test_refine_then_pass(self):
        p, book = self._book()
        c.AGENTIC_ENABLED = True
        c.CRITIC_MAX_ROUNDS = 2
        calls = {"n": 0}

        def critic(bk, pp):
            calls["n"] += 1
            if calls["n"] == 1:
                return {"pass": False, "score": 5, "issues": ["p1: mostrar con cuerpo"]}
            return {"pass": True, "score": 9, "issues": []}

        c.ollama_critic = critic
        c.ollama_repair_with_issues = lambda bk, iss, pp: "RAW"
        c.validate_book = lambda raw, pp: ({"story_title": "T2", "pages": book["pages"]}, "ok")
        out, qa = c.agentic_refine(book, p)
        self.assertEqual(out["story_title"], "T2")
        self.assertEqual(qa["rounds"], 2)

    def test_broken_repair_keeps_best(self):
        p, book = self._book()
        c.AGENTIC_ENABLED = True
        c.CRITIC_MAX_ROUNDS = 2
        c.ollama_critic = lambda bk, pp: {"pass": False, "score": 4, "issues": ["x"]}
        c.ollama_repair_with_issues = lambda bk, iss, pp: "RAW"
        c.validate_book = lambda raw, pp: (None, "roto")
        out, qa = c.agentic_refine(book, p)
        self.assertEqual(out, book)
        self.assertIn("best_kept", qa["verdict"])


class TestBookJson(unittest.TestCase):
    def test_spec_shape(self):
        p = make_params()
        book, _ = c.validate_book(make_raw(6, 20), p)
        anchor = "Lucas, a 5-year-old child, curly brown hair"
        j = c.book_json("http://h", "b2abc", p, anchor, book, {"verdict": "pass"})
        self.assertEqual(j["story_title"], "El camion")
        self.assertEqual(j["target_age"], 5)
        self.assertEqual(j["style_bible_applied"], "storybook watercolor")
        self.assertEqual(len(j["pages"]), 6)
        pg = j["pages"][0]
        self.assertEqual(pg["page_number"], 1)
        self.assertIn("story_text", pg)
        ad = pg["art_director"]
        self.assertEqual(ad["character_anchor"], anchor)
        for k in ("pose_action", "scene_environment", "lighting_and_camera"):
            self.assertIn(k, ad)
        # Orden obligatorio: biblia + ancla + pose + entorno + cámara.
        ip = pg["image_prompt"]
        order = [p["estilo"], anchor, ad["pose_action"],
                 ad["scene_environment"], ad["lighting_and_camera"]]
        idx = [ip.find(x) for x in order]
        self.assertTrue(all(i >= 0 for i in idx), ip)
        self.assertEqual(idx, sorted(idx))
        self.assertEqual(pg["audio_cues"]["recommended_pace"], 0.88)
        self.assertIn("mood", pg["audio_cues"])
        self.assertTrue(pg["assets"]["img"].startswith("http://h/book/img/b2abc_p0"))
        self.assertEqual(j["qa"], {"verdict": "pass"})


class TestFallbacks(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self._old_cache = c.CACHE_DIR
        c.CACHE_DIR = self.tmp
        self._old = (c.comfyui_render, c.render_pollinations,
                     c.POLLINATIONS_FALLBACK)

    def tearDown(self):
        c.comfyui_render, c.render_pollinations, c.POLLINATIONS_FALLBACK = self._old
        c.CACHE_DIR = self._old_cache

    def test_comfyui_first(self):
        calls = []
        c.comfyui_render = lambda i, p, s: calls.append("comfy") or True
        c.render_pollinations = lambda i, p, s: calls.append("polli") or True
        self.assertTrue(c.render_image("x_p0", "prompt", 7))
        self.assertEqual(calls, ["comfy"])

    def test_pollinations_when_comfyui_down(self):
        calls = []
        c.comfyui_render = lambda i, p, s: calls.append("comfy") or False
        c.render_pollinations = lambda i, p, s: calls.append("polli") or True
        self.assertTrue(c.render_image("x_p0", "prompt", 7))
        self.assertEqual(calls, ["comfy", "polli"])

    def test_no_fallback_when_disabled(self):
        c.comfyui_render = lambda i, p, s: False
        c.render_pollinations = lambda i, p, s: True
        c.POLLINATIONS_FALLBACK = False
        self.assertFalse(c.render_image("x_p0", "prompt", 7))

    def test_probes_fail_closed(self):
        self.assertFalse(c.probe_tcp("http://127.0.0.1:1"))
        self.assertFalse(c.probe_http("http://127.0.0.1:1/nope"))


class TestBreathe(unittest.TestCase):
    def test_commas_get_pause(self):
        out = c.breathe("Hola, Lucas, mira eso.")
        self.assertIn(", …", out)
        self.assertTrue(len(out) <= 700)

    def test_ellipsis_kept(self):
        out = c.breathe("Y entonces... la puerta se abrio.")
        self.assertIn("…", out)


class TestLayout(unittest.TestCase):
    def test_margin(self):
        tmp = tempfile.mkdtemp()
        old = c.CACHE_DIR
        c.CACHE_DIR = tmp
        try:
            slug = "b2testlayout"
            with open(os.path.join(tmp, slug + ".json"), "w", encoding="utf-8") as f:
                json.dump({"paginas": [{}, {}]}, f)
            for i in (0, 1):
                path = os.path.join(tmp, "%s_p%d.wav" % (slug, i))
                with wave.open(path, "wb") as w:
                    w.setnchannels(1)
                    w.setsampwidth(2)
                    w.setframerate(24000)
                    w.writeframes(b"\x00\x00" * 24000)  # 1.0s
            man = c.layout_manifest(slug)
            self.assertEqual(len(man), 2)
            self.assertAlmostEqual(man[0]["audio_sec"], 1.0, places=1)
            self.assertAlmostEqual(man[0]["page_sec"] - man[0]["audio_sec"],
                                   0.15, places=2)
            self.assertIsNone(c.layout_manifest("b2noexiste"))
        finally:
            c.CACHE_DIR = old


if __name__ == "__main__":
    unittest.main()
