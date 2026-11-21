import hashlib
import re
import struct
import sys
import wave
from pathlib import Path

import pytest

ROOT_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT_DIR / "tools"))

import generate_graphics as gg  # noqa: E402
import prepare_audio as pa  # noqa: E402


EXPECTED_ANIMALS = [
    "perro", "gato", "vaca", "pato", "leon", "oveja", "sapo", "elefante",
    "mono", "caballo", "tigre", "grillo", "cerdo", "esqueleto", "monstruo",
]


def test_required_project_files_exist():
    required = [
        "platformio.ini",
        "partitions_animales_8MB.csv",
        "PINOUT_CHECK.md",
        "README.md",
        "include/board_pins.h",
        "include/epd_driver.h",
        "include/audio_es8311.h",
        "include/app_state.h",
        "include/input_manager.h",
        "src/main.cpp",
        "src/epd_driver.cpp",
        "src/audio_es8311.cpp",
        "src/app_state.cpp",
        "src/input_manager.cpp",
        "src/assets/animals_gfx.h",
        "tools/generate_graphics.py",
        "tools/prepare_audio.py",
        "tools/preview_sheet.png",
    ]
    for rel in required:
        assert (ROOT_DIR / rel).exists(), f"Falta archivo requerido: {rel}"


def test_zone_alignment_multiples_of_8():
    assert gg.ZONE2_X % 8 == 0
    assert gg.ZONE2_W % 8 == 0
    assert gg.ZONE3_W % 8 == 0
    assert gg.EPD_W % 8 == 0


def test_animals_gfx_header_structure_and_exact_mapping():
    header = (ROOT_DIR / "src" / "assets" / "animals_gfx.h").read_text(encoding="utf-8")
    assert "ANIMAL_COUNT = 15;" in header
    assert "gfx_sleep_screen[5000] PROGMEM" in header

    # Verificar que el arreglo ANIMALS[] respeta exactamente el orden y mapeo 1:1
    table_match = re.search(r"static const AnimalAsset ANIMALS\[ANIMAL_COUNT\] = \{(.*?)\};", header, re.S)
    assert table_match is not None, "No se encontro la tabla ANIMALS[]"
    rows = re.findall(
        r'\{\s*"(\w+)",\s*(gfx_\w+_a),\s*(gfx_\w+_b),\s*(gfx_\w+_name),\s*"(/sounds/\w+\.wav)"',
        table_match.group(1),
    )
    assert len(rows) == len(EXPECTED_ANIMALS)

    for idx, aid in enumerate(EXPECTED_ANIMALS):
        assert rows[idx] == (
            aid,
            f"gfx_{aid}_a",
            f"gfx_{aid}_b",
            f"gfx_{aid}_name",
            f"/sounds/{aid}.wav",
        ), f"Mapeo incorrecto en indice {idx}: {rows[idx]}"
        assert f"gfx_{aid}_a[2688] PROGMEM" in header
        assert f"gfx_{aid}_b[2688] PROGMEM" in header
        assert f"gfx_{aid}_name[1100] PROGMEM" in header


def test_animal_names_and_sprites_are_distinct_and_non_empty():
    header = (ROOT_DIR / "src" / "assets" / "animals_gfx.h").read_text(encoding="utf-8")
    name_hashes = set()
    sprite_a_hashes = set()
    sprite_b_hashes = set()

    for aid in EXPECTED_ANIMALS:
        for suffix, target_set in [("_name", name_hashes), ("_a", sprite_a_hashes), ("_b", sprite_b_hashes)]:
            m = re.search(rf"const uint8_t gfx_{aid}{suffix}\[\d+\] PROGMEM = \{{(.*?)\}};", header, re.S)
            assert m is not None, f"No se encontro array gfx_{aid}{suffix}"
            hex_bytes = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", m.group(1))]
            raw = bytes(hex_bytes)
            # No puede ser todo blanco (0xFF) ni todo negro (0x00)
            assert any(b != 0xFF for b in raw), f"gfx_{aid}{suffix} esta completamente blanco"
            assert any(b != 0x00 for b in raw), f"gfx_{aid}{suffix} esta completamente negro"
            target_set.add(hashlib.md5(raw).hexdigest())

    assert len(name_hashes) == len(EXPECTED_ANIMALS), "Hay bitmaps de nombres duplicados"
    assert len(sprite_a_hashes) == len(EXPECTED_ANIMALS), "Hay sprites Frame A duplicados"
    assert len(sprite_b_hashes) == len(EXPECTED_ANIMALS), "Hay sprites Frame B duplicados"


def test_pack_1bit_and_threshold_pure_binary():
    from PIL import Image

    img = Image.new("L", (16, 2), 255)
    img.putpixel((0, 0), 0)
    img.putpixel((8, 1), 100)
    binary = gg.threshold_1bit(img)
    assert set(binary.get_flattened_data()).issubset({0, 255})
    packed = gg.pack_1bit(binary)
    assert len(packed) == 4
    assert packed[0] == 0x7F
    assert packed[1] == 0xFF
    assert packed[2] == 0xFF
    assert packed[3] == 0x7F


def test_generated_wavs_format_distinctness_and_energy():
    total_bytes = 0
    wav_hashes = set()
    for aid in EXPECTED_ANIMALS:
        wav_path = ROOT_DIR / "data" / "sounds" / f"{aid}.wav"
        assert wav_path.exists(), f"Falta audio: {wav_path}"
        total_bytes += wav_path.stat().st_size

        with wave.open(str(wav_path), "rb") as wf:
            assert wf.getnchannels() == 1, f"{aid}.wav debe ser Mono"
            assert wf.getsampwidth() == 2, f"{aid}.wav debe ser 16-bit"
            assert wf.getframerate() == 16000, f"{aid}.wav debe ser 16 kHz"
            nframes = wf.getnframes()
            dur = nframes / float(wf.getframerate())
            assert 0.2 <= dur <= 2.5, f"{aid}.wav duracion fuera de rango: {dur}s"
            raw = wf.readframes(nframes)
            wav_hashes.add(hashlib.md5(raw).hexdigest())
            samples = struct.unpack(f"<{nframes}h", raw)
            peak = max(abs(s) for s in samples)
            rms = (sum(x * x for x in samples) / float(nframes)) ** 0.5
            assert peak > 10000, f"{aid}.wav tiene pico muy bajo (peak={peak})"
            assert rms >= 2400, f"{aid}.wav tiene volumen RMS muy bajo (rms={rms:.1f})"

    assert len(wav_hashes) == len(EXPECTED_ANIMALS), "Hay archivos WAV duplicados entre animales"
    assert total_bytes < 2 * 1024 * 1024


def test_letter_wavs_exist_and_valid_format():
    for ch in "abcdefghijklmnopqrstuvwxyz":
        p = ROOT_DIR / "data" / "letters" / f"{ch}.wav"
        assert p.exists(), f"Falta audio de letra: {p}"
        with wave.open(str(p), "rb") as wf:
            assert wf.getnchannels() == 1
            assert wf.getsampwidth() == 2
            assert wf.getframerate() == 16000
            n = wf.getnframes()
            assert n >= 4000, f"{ch}.wav demasiado corto ({n} frames)"
            s = struct.unpack(f"<{n}h", wf.readframes(n))
            rms = (sum(x * x for x in s) / float(n)) ** 0.5
            assert rms >= 3200, f"Letra {ch}.wav muy baja (rms={rms:.1f})"

    for aid in EXPECTED_ANIMALS:
        p = ROOT_DIR / "data" / "names" / f"{aid}.wav"
        assert p.exists(), f"Falta audio de nombre: {p}"
        with wave.open(str(p), "rb") as wf:
            assert wf.getnchannels() == 1
            assert wf.getsampwidth() == 2
            assert wf.getframerate() == 16000
            n = wf.getnframes()
            assert n >= 4800, f"name {aid}.wav demasiado corto"
            s = struct.unpack(f"<{n}h", wf.readframes(n))
            rms = (sum(x * x for x in s) / float(n)) ** 0.5
            assert rms >= 3200, f"Nombre {aid}.wav muy bajo (rms={rms:.1f})"


def test_partitions_littlefs_at_least_2mb():
    csv_text = (ROOT_DIR / "partitions_animales_8MB.csv").read_text(encoding="utf-8")
    m = re.search(r"spiffs,\s*data,\s*spiffs,\s*0x[0-9a-fA-F]+,\s*(0x[0-9a-fA-F]+)", csv_text)
    assert m is not None, "No se encontro particion spiffs/littlefs en default_8MB.csv"
    size_bytes = int(m.group(1), 16)
    assert size_bytes >= 2 * 1024 * 1024
