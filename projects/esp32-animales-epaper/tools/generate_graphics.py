#!/usr/bin/env python3
"""
Generador automatizado de graficos 1-bit para "Libro de Animales Interactivo"
Placa objetivo: Waveshare ESP32-S3-ePaper-1.54 (200x200 px, 1-bit B/N).

Genera:
  - src/assets/animals_gfx.h  (bitmaps PROGMEM + tabla ANIMALS[])
  - tools/preview_sheet.png   (grilla visual 200x200 Frame A vs Frame B + Sleep)

Uso:
  python tools/generate_graphics.py
"""

from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

from PIL import Image, ImageDraw, ImageFont, ImageOps

# ==============================================================================
# CONFIGURACION DE PANTALLA Y ZONAS (Coordenadas X y W multiplos de 8!)
# ==============================================================================
EPD_W = 200
EPD_H = 200

ZONE1_H = 20                 # y = 0..19   (Indicador de pagina)
ZONE2_X = 16                 # Multiplo de 8
ZONE2_Y = 24
ZONE2_W = 168                # Multiplo de 8 (21 bytes/fila)
ZONE2_H = 128

ZONE3_Y = 156                # y = 156..199
ZONE3_W = 200                # Multiplo de 8 (25 bytes/fila)
ZONE3_H = 44

STROKE = 4                   # Grosor de trazo negro principal (alto contraste 1.54")
WHITE = 255                  # Bit 1 en EPD
BLACK = 0                    # Bit 0 en EPD

ROOT_DIR = Path(__file__).resolve().parent.parent
CUSTOM_SPRITES_DIR = ROOT_DIR / "assets" / "custom_sprites"
OUT_HEADER = ROOT_DIR / "src" / "assets" / "animals_gfx.h"
OUT_PREVIEW = ROOT_DIR / "tools" / "preview_sheet.png"

ANIMALS: List[Tuple[str, str, str]] = [
    ("perro", "PERRO", "¡GUAU!"),
    ("gato", "GATO", "¡MIAU!"),
    ("vaca", "VACA", "¡MUU!"),
    ("pato", "PATO", "¡CUAC!"),
    ("leon", "LEÓN", "¡GRRR!"),
    ("oveja", "OVEJA", "¡BEEE!"),
    ("sapo", "SAPO", "¡CROAC!"),
    ("elefante", "ELEFANTE", "¡PRUU!"),
    ("mono", "MONO", "¡UH AH!"),
    ("caballo", "CABALLO", "¡HIII!"),
    ("tigre", "TIGRE", "¡ROAR!"),
    ("grillo", "GRILLO", "¡CRI-CRI!"),
    ("cerdo", "CERDO", "¡OINC!"),
    ("esqueleto", "ESQUELETO", "¡CLAC!"),
    ("monstruo", "MONSTRUO", "¡GRRR!"),
]


# ==============================================================================
# UTILIDADES DE FUENTES Y DIBUJO 1-BIT
# ==============================================================================
def find_bold_font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    """Busca una fuente TTF bold palo seco con soporte completo de espanol."""
    candidates = [
        r"C:\Windows\Fonts\trebucbd.ttf",
        r"C:\Windows\Fonts\arialbd.ttf",
        r"C:\Windows\Fonts\tahomabd.ttf",
        r"C:\Windows\Fonts\calibrib.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/nunito/Nunito-Bold.ttf",
    ]
    for path in candidates:
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size=size)
            except OSError:
                continue
    return ImageFont.load_default()


def threshold_1bit(img: Image.Image, cutoff: int = 128) -> Image.Image:
    """Convierte a imagen L binaria pura (0 o 255) sin dithering."""
    gray = img.convert("L")
    return gray.point(lambda p: WHITE if p > cutoff else BLACK, mode="L")


def new_canvas(w: int, h: int) -> Tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("L", (w, h), WHITE)
    return img, ImageDraw.Draw(img)


def draw_bubble(draw: ImageDraw.ImageDraw, text: str) -> None:
    """Dibuja una burbuja de onomatopeya en la esquina superior derecha del sprite."""
    font = find_bold_font(13)
    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    pad_x, pad_y = 6, 4
    bw = tw + pad_x * 2
    bh = th + pad_y * 2
    x1 = ZONE2_W - 3
    x0 = max(60, x1 - bw)
    y0 = 3
    y1 = y0 + bh

    draw.rounded_rectangle([x0, y0, x1, y1], radius=6, fill=WHITE, outline=BLACK, width=2)
    # Piquito de burbuja hacia el animal
    draw.polygon([(x0 + 8, y1 - 1), (x0 + 2, y1 + 7), (x0 + 16, y1 - 1)], fill=WHITE, outline=BLACK)
    draw.line([(x0 + 9, y1 - 1), (x0 + 15, y1 - 1)], fill=WHITE, width=2)
    tx = x0 + (bw - tw) // 2 - bbox[0]
    ty = y0 + (bh - th) // 2 - bbox[1]
    draw.text((tx, ty), text, font=font, fill=BLACK)


def draw_thick_arc(draw: ImageDraw.ImageDraw, box: List[int], start: float, end: float, width: int = STROKE) -> None:
    draw.arc(box, start=start, end=end, fill=BLACK, width=width)


# ==============================================================================
# ILUSTRADORES VECTORIALES DE LOS 8 ANIMALES (168 x 128 px)
# ==============================================================================
def draw_perro(frame_b: bool) -> Image.Image:
    img, d = new_canvas(ZONE2_W, ZONE2_H)
    cx, cy = 74, 72

    # Cola (arriba en Frame B)
    if frame_b:
        d.line([(cx + 28, cy + 16), (cx + 46, cy - 2), (cx + 50, cy - 12)], fill=BLACK, width=4)
    else:
        d.line([(cx + 28, cy + 20), (cx + 46, cy + 14)], fill=BLACK, width=4)

    # Cuerpo sentado simple
    d.ellipse([cx - 28, cy - 2, cx + 30, cy + 44], fill=WHITE, outline=BLACK, width=STROKE)
    # Patitas delanteras
    d.rounded_rectangle([cx - 20, cy + 24, cx - 8, cy + 48], radius=4, fill=WHITE, outline=BLACK, width=STROKE)
    d.rounded_rectangle([cx + 4, cy + 24, cx + 16, cy + 48], radius=4, fill=WHITE, outline=BLACK, width=STROKE)

    # Cabeza (compartida)
    hx, hy = cx - 4, cy - 24
    # Orejas caidas (en Frame B se levantan un poco)
    if frame_b:
        d.polygon([(hx - 24, hy - 14), (hx - 42, hy - 28), (hx - 34, hy + 6)], fill=BLACK)
        d.polygon([(hx + 24, hy - 14), (hx + 42, hy - 28), (hx + 34, hy + 6)], fill=BLACK)
    else:
        d.ellipse([hx - 38, hy - 8, hx - 20, hy + 22], fill=BLACK)
        d.ellipse([hx + 20, hy - 8, hx + 38, hy + 22], fill=BLACK)

    d.ellipse([hx - 28, hy - 24, hx + 28, hy + 24], fill=WHITE, outline=BLACK, width=STROKE)
    # Mancha en un ojo
    d.ellipse([hx - 19, hy - 13, hx - 3, hy + 2], fill=BLACK)
    d.ellipse([hx - 13, hy - 8, hx - 7, hy - 2], fill=WHITE)
    # Otro ojo
    d.ellipse([hx + 8, hy - 8, hx + 16, hy], fill=BLACK)
    # Hocico y nariz
    d.ellipse([hx - 6, hy + 2, hx + 6, hy + 10], fill=BLACK)

    if frame_b:
        # Boca abierta ladrando + lenguita
        d.chord([hx - 10, hy + 8, hx + 10, hy + 22], start=0, end=180, fill=BLACK)
        d.ellipse([hx - 4, hy + 15, hx + 4, hy + 24], fill=WHITE, outline=BLACK, width=2)
        draw_bubble(d, "¡GUAU!")
    else:
        draw_thick_arc(d, [hx - 10, hy + 8, hx, hy + 16], 0, 180, 2)
        draw_thick_arc(d, [hx, hy + 8, hx + 10, hy + 16], 0, 180, 2)

    return threshold_1bit(img)


def draw_gato(frame_b: bool) -> Image.Image:
    img, d = new_canvas(ZONE2_W, ZONE2_H)
    cx, cy = 74, 74

    # Cola curva
    if frame_b:
        d.arc([cx + 16, cy - 20, cx + 56, cy + 36], start=270, end=90, fill=BLACK, width=4)
    else:
        d.arc([cx + 14, cy + 2, cx + 52, cy + 44], start=280, end=100, fill=BLACK, width=4)

    # Cuerpo
    d.ellipse([cx - 26, cy - 2, cx + 26, cy + 44], fill=WHITE, outline=BLACK, width=STROKE)
    # Patitas
    d.rounded_rectangle([cx - 18, cy + 26, cx - 6, cy + 46], radius=4, fill=WHITE, outline=BLACK, width=STROKE)
    d.rounded_rectangle([cx + 6, cy + 26, cx + 18, cy + 46], radius=4, fill=WHITE, outline=BLACK, width=STROKE)

    # Cabeza + orejas puntiagudas
    hx, hy = cx, cy - 24
    d.polygon([(hx - 24, hy - 8), (hx - 26, hy - 34), (hx - 6, hy - 20)], fill=WHITE, outline=BLACK)
    d.line([(hx - 24, hy - 8), (hx - 26, hy - 34), (hx - 6, hy - 20)], fill=BLACK, width=STROKE)
    d.polygon([(hx + 24, hy - 8), (hx + 26, hy - 34), (hx + 6, hy - 20)], fill=WHITE, outline=BLACK)
    d.line([(hx + 24, hy - 8), (hx + 26, hy - 34), (hx + 6, hy - 20)], fill=BLACK, width=STROKE)

    d.ellipse([hx - 28, hy - 22, hx + 28, hy + 22], fill=WHITE, outline=BLACK, width=STROKE)
    # Ojos (felices arqueados en Frame B, redondos en Frame A)
    if frame_b:
        draw_thick_arc(d, [hx - 18, hy - 10, hx - 6, hy + 2], 180, 360, 3)
        draw_thick_arc(d, [hx + 6, hy - 10, hx + 18, hy + 2], 180, 360, 3)
    else:
        d.ellipse([hx - 16, hy - 8, hx - 8, hy], fill=BLACK)
        d.ellipse([hx + 8, hy - 8, hx + 16, hy], fill=BLACK)

    # Nariz triangular
    d.polygon([(hx - 4, hy + 3), (hx + 4, hy + 3), (hx, hy + 8)], fill=BLACK)
    # Bigotes
    for dy in (-2, 4):
        d.line([(hx - 16, hy + 4 + dy), (hx - 38, hy + 2 + dy * 2)], fill=BLACK, width=2)
        d.line([(hx + 16, hy + 4 + dy), (hx + 38, hy + 2 + dy * 2)], fill=BLACK, width=2)

    if frame_b:
        d.ellipse([hx - 5, hy + 10, hx + 5, hy + 19], fill=BLACK)
        draw_bubble(d, "¡MIAU!")
    else:
        draw_thick_arc(d, [hx - 8, hy + 7, hx, hy + 13], 0, 180, 2)
        draw_thick_arc(d, [hx, hy + 7, hx + 8, hy + 13], 0, 180, 2)

    return threshold_1bit(img)


def draw_vaca(frame_b: bool) -> Image.Image:
    img, d = new_canvas(ZONE2_W, ZONE2_H)
    cx, cy = 72, 76

    # Cuerpo con manchas
    d.rounded_rectangle([cx - 34, cy - 4, cx + 36, cy + 38], radius=16, fill=WHITE, outline=BLACK, width=STROKE)
    d.ellipse([cx + 6, cy + 4, cx + 24, cy + 20], fill=BLACK)
    d.ellipse([cx - 22, cy + 12, cx - 8, cy + 26], fill=BLACK)
    # Patas
    for px in (cx - 26, cx - 10, cx + 12, cx + 26):
        d.rectangle([px - 4, cy + 34, px + 4, cy + 48], fill=BLACK)

    # Cabeza
    hx, hy = cx - 16, cy - 22
    # Cuernos
    d.arc([hx - 28, hy - 34, hx - 10, hy - 14], 120, 290, fill=BLACK, width=4)
    d.arc([hx + 10, hy - 34, hx + 28, hy - 14], 250, 60, fill=BLACK, width=4)
    # Orejas
    ear_y = hy - 14 if frame_b else hy - 8
    d.ellipse([hx - 36, ear_y, hx - 18, ear_y + 10], fill=BLACK)
    d.ellipse([hx + 18, ear_y, hx + 36, ear_y + 10], fill=BLACK)

    d.ellipse([hx - 24, hy - 22, hx + 24, hy + 22], fill=WHITE, outline=BLACK, width=STROKE)
    # Ojos
    d.ellipse([hx - 13, hy - 10, hx - 6, hy - 3], fill=BLACK)
    d.ellipse([hx + 6, hy - 10, hx + 13, hy - 3], fill=BLACK)
    # Hocico grande
    d.ellipse([hx - 20, hy + 2, hx + 20, hy + 24], fill=WHITE, outline=BLACK, width=STROKE)
    d.ellipse([hx - 10, hy + 8, hx - 5, hy + 13], fill=BLACK)
    d.ellipse([hx + 5, hy + 8, hx + 10, hy + 13], fill=BLACK)

    if frame_b:
        d.ellipse([hx - 7, hy + 15, hx + 7, hy + 23], fill=BLACK)
        draw_bubble(d, "¡MUU!")
    else:
        draw_thick_arc(d, [hx - 8, hy + 13, hx + 8, hy + 20], 10, 170, 2)

    return threshold_1bit(img)


def draw_pato(frame_b: bool) -> Image.Image:
    img, d = new_canvas(ZONE2_W, ZONE2_H)
    cx, cy = 72, 74

    # Agua ondulada abajo
    d.arc([20, 108, 60, 122], 180, 360, fill=BLACK, width=2)
    d.arc([60, 108, 100, 122], 180, 360, fill=BLACK, width=2)
    d.arc([100, 108, 140, 122], 180, 360, fill=BLACK, width=2)

    # Cuerpo
    d.ellipse([cx - 34, cy - 2, cx + 34, cy + 40], fill=WHITE, outline=BLACK, width=STROKE)
    # Ala (levantada en Frame B)
    if frame_b:
        d.ellipse([cx - 12, cy - 16, cx + 26, cy + 12], fill=WHITE, outline=BLACK, width=STROKE)
    else:
        d.ellipse([cx - 10, cy + 6, cx + 26, cy + 28], fill=WHITE, outline=BLACK, width=STROKE)

    # Cabeza
    hx, hy = cx - 18, cy - 24
    d.ellipse([hx - 20, hy - 20, hx + 20, hy + 20], fill=WHITE, outline=BLACK, width=STROKE)
    d.ellipse([hx - 9, hy - 8, hx - 2, hy - 1], fill=BLACK)

    # Pico hacia la izquierda
    if frame_b:
        d.polygon([(hx - 18, hy - 2), (hx - 40, hy - 10), (hx - 18, hy + 4)], fill=BLACK)
        d.polygon([(hx - 18, hy + 6), (hx - 38, hy + 14), (hx - 18, hy + 12)], fill=BLACK)
        draw_bubble(d, "¡CUAC!")
    else:
        d.polygon([(hx - 18, hy), (hx - 38, hy + 4), (hx - 18, hy + 10)], fill=BLACK)

    return threshold_1bit(img)


def draw_leon(frame_b: bool) -> Image.Image:
    img, d = new_canvas(ZONE2_W, ZONE2_H)
    cx, cy = 74, 78

    # Cuerpo
    d.ellipse([cx - 26, cy - 2, cx + 26, cy + 40], fill=WHITE, outline=BLACK, width=STROKE)
    d.rounded_rectangle([cx - 18, cy + 22, cx - 6, cy + 44], radius=4, fill=WHITE, outline=BLACK, width=STROKE)
    d.rounded_rectangle([cx + 6, cy + 22, cx + 18, cy + 44], radius=4, fill=WHITE, outline=BLACK, width=STROKE)

    # Melena grande (circulo grueso con ondas)
    hx, hy = cx, cy - 24
    r_mane = 36 if frame_b else 33
    d.ellipse([hx - r_mane, hy - r_mane, hx + r_mane, hy + r_mane], fill=BLACK)
    # Orejitas sobre melena
    d.ellipse([hx - 28, hy - 34, hx - 14, hy - 20], fill=WHITE, outline=BLACK, width=STROKE)
    d.ellipse([hx + 14, hy - 34, hx + 28, hy - 20], fill=WHITE, outline=BLACK, width=STROKE)

    # Cara
    d.ellipse([hx - 24, hy - 22, hx + 24, hy + 22], fill=WHITE, outline=BLACK, width=STROKE)
    # Ojos
    d.ellipse([hx - 13, hy - 9, hx - 6, hy - 2], fill=BLACK)
    d.ellipse([hx + 6, hy - 9, hx + 13, hy - 2], fill=BLACK)
    # Nariz
    d.polygon([(hx - 5, hy + 1), (hx + 5, hy + 1), (hx, hy + 7)], fill=BLACK)

    if frame_b:
        d.ellipse([hx - 10, hy + 8, hx + 10, hy + 20], fill=BLACK)
        draw_bubble(d, "¡GRRR!")
    else:
        draw_thick_arc(d, [hx - 8, hy + 6, hx, hy + 13], 0, 180, 2)
        draw_thick_arc(d, [hx, hy + 6, hx + 8, hy + 13], 0, 180, 2)

    return threshold_1bit(img)


def draw_oveja(frame_b: bool) -> Image.Image:
    img, d = new_canvas(ZONE2_W, ZONE2_H)
    cx, cy = 76, 74
    dy = -4 if frame_b else 0

    # Patitas
    for px in (cx - 20, cx - 6, cx + 10, cx + 22):
        d.rectangle([px - 3, cy + 28 + dy, px + 3, cy + 46 + dy], fill=BLACK)

    # Cuerpo esponjoso (nube de circulos blancos con contorno)
    puffs = [
        (cx - 24, cy + 8 + dy, 18),
        (cx + 22, cy + 8 + dy, 18),
        (cx - 12, cy - 8 + dy, 20),
        (cx + 12, cy - 8 + dy, 20),
        (cx, cy + 14 + dy, 22),
    ]
    for px, py, r in puffs:
        d.ellipse([px - r, py - r, px + r, py + r], fill=WHITE, outline=BLACK, width=STROKE)
    # Relleno interior blanco para unir las nubes
    d.ellipse([cx - 28, cy - 14 + dy, cx + 28, cy + 28 + dy], fill=WHITE)

    # Cabeza negra/oscura amigable a la izquierda
    hx, hy = cx - 24, cy - 12 + dy
    # Orejas
    d.ellipse([hx - 26, hy - 6, hx - 12, hy + 4], fill=BLACK)
    d.ellipse([hx + 12, hy - 6, hx + 26, hy + 4], fill=BLACK)
    # Cara
    d.ellipse([hx - 18, hy - 18, hx + 18, hy + 18], fill=BLACK)
    # Copete de lana
    d.ellipse([hx - 12, hy - 24, hx + 12, hy - 10], fill=WHITE, outline=BLACK, width=2)
    # Ojos blancos con pupila
    d.ellipse([hx - 10, hy - 8, hx - 3, hy - 1], fill=WHITE)
    d.ellipse([hx + 3, hy - 8, hx + 10, hy - 1], fill=WHITE)
    d.ellipse([hx - 8, hy - 6, hx - 5, hy - 3], fill=BLACK)
    d.ellipse([hx + 5, hy - 6, hx + 8, hy - 3], fill=BLACK)

    if frame_b:
        d.ellipse([hx - 5, hy + 5, hx + 5, hy + 13], fill=WHITE)
        draw_bubble(d, "¡BEEE!")
    else:
        d.arc([hx - 6, hy + 4, hx + 6, hy + 11], 0, 180, fill=WHITE, width=2)

    return threshold_1bit(img)


def draw_sapo(frame_b: bool) -> Image.Image:
    img, d = new_canvas(ZONE2_W, ZONE2_H)
    cx, cy = 76, 82
    dy = -10 if frame_b else 0

    # Hoja de nenufar abajo
    d.ellipse([26, 102, 130, 122], fill=WHITE, outline=BLACK, width=STROKE)

    # Patas traseras dobladas
    d.ellipse([cx - 40, cy + 2 + dy, cx - 14, cy + 26 + dy], fill=WHITE, outline=BLACK, width=STROKE)
    d.ellipse([cx + 14, cy + 2 + dy, cx + 40, cy + 26 + dy], fill=WHITE, outline=BLACK, width=STROKE)

    # Cuerpo ancho
    d.ellipse([cx - 32, cy - 18 + dy, cx + 32, cy + 26 + dy], fill=WHITE, outline=BLACK, width=STROKE)

    # Ojos saltones arriba
    for ex in (cx - 16, cx + 16):
        d.ellipse([ex - 11, cy - 34 + dy, ex + 11, cy - 12 + dy], fill=WHITE, outline=BLACK, width=STROKE)
        d.ellipse([ex - 5, cy - 28 + dy, ex + 5, cy - 18 + dy], fill=BLACK)

    # Boca ancha + papada inflada en Frame B
    if frame_b:
        d.ellipse([cx - 18, cy + 2 + dy, cx + 18, cy + 24 + dy], fill=WHITE, outline=BLACK, width=2)
        d.chord([cx - 16, cy - 6 + dy, cx + 16, cy + 10 + dy], 0, 180, fill=BLACK)
        draw_bubble(d, "¡CROAC!")
    else:
        draw_thick_arc(d, [cx - 20, cy - 8 + dy, cx + 20, cy + 8 + dy], 10, 170, 3)

    return threshold_1bit(img)


def draw_elefante(frame_b: bool) -> Image.Image:
    img, d = new_canvas(ZONE2_W, ZONE2_H)
    cx, cy = 78, 76

    # Cuerpo grande
    d.ellipse([cx - 30, cy - 8, cx + 42, cy + 38], fill=WHITE, outline=BLACK, width=STROKE)
    # Patas gruesas
    d.rectangle([cx - 20, cy + 26, cx - 6, cy + 48], fill=WHITE, outline=BLACK, width=STROKE)
    d.rectangle([cx + 18, cy + 26, cx + 32, cy + 48], fill=WHITE, outline=BLACK, width=STROKE)

    # Oreja gigante
    d.ellipse([cx - 12, cy - 36, cx + 26, cy + 14], fill=WHITE, outline=BLACK, width=STROKE)

    # Cabeza
    hx, hy = cx - 20, cy - 16
    d.ellipse([hx - 22, hy - 22, hx + 22, hy + 22], fill=WHITE, outline=BLACK, width=STROKE)
    # Ojo
    d.ellipse([hx - 10, hy - 8, hx - 3, hy - 1], fill=BLACK)

    # Trompa (abajo en reposo, levantada en Frame B!)
    if frame_b:
        d.line([(hx - 18, hy + 2), (hx - 36, hy - 12), (hx - 32, hy - 32)], fill=BLACK, width=7)
        d.ellipse([hx - 37, hy - 36, hx - 27, hy - 26], fill=BLACK)
        draw_bubble(d, "¡PRUU!")
    else:
        d.line([(hx - 18, hy + 2), (hx - 32, hy + 16), (hx - 26, hy + 32)], fill=BLACK, width=7)

    return threshold_1bit(img)


BUILTIN_DRAWERS: Dict[str, Callable[[bool], Image.Image]] = {
    "perro": draw_perro,
    "gato": draw_gato,
    "vaca": draw_vaca,
    "pato": draw_pato,
    "leon": draw_leon,
    "oveja": draw_oveja,
    "sapo": draw_sapo,
    "elefante": draw_elefante,
}


# ==============================================================================
# CARGA DE SPRITES CUSTOM Y RENDER DE NOMBRES / PANTALLA SLEEP
# ==============================================================================
def load_sprite(animal_id: str, frame_b: bool) -> Image.Image:
    suffix = "b" if frame_b else "a"
    custom_path = CUSTOM_SPRITES_DIR / f"{animal_id}_{suffix}.png"
    if custom_path.exists():
        raw = Image.open(custom_path).convert("L")
        fitted = ImageOps.contain(raw, (ZONE2_W, ZONE2_H), method=Image.Resampling.BILINEAR)
        canvas = Image.new("L", (ZONE2_W, ZONE2_H), WHITE)
        ox = (ZONE2_W - fitted.width) // 2
        oy = (ZONE2_H - fitted.height) // 2
        canvas.paste(fitted, (ox, oy))
        return threshold_1bit(canvas)

    return BUILTIN_DRAWERS[animal_id](frame_b)


def render_name_bitmap(name_upper: str) -> Tuple[Image.Image, List[Tuple[int, int]]]:
    """Pre-renderiza el nombre en MAYUSCULAS con tracking en 200x44 px 1-bit y retorna (img, [(x, w), ...])."""
    img, d = new_canvas(ZONE3_W, ZONE3_H)
    tracking = 4 if len(name_upper) <= 6 else 2

    for size in (30, 26, 22, 18):
        font = find_bold_font(size)
        widths = [d.textbbox((0, 0), ch, font=font)[2] - d.textbbox((0, 0), ch, font=font)[0] for ch in name_upper]
        total_w = sum(widths) + tracking * max(0, len(name_upper) - 1)
        if total_w <= ZONE3_W - 12:
            break

    # Medir altura global con el texto completo
    full_bbox = d.textbbox((0, 0), name_upper, font=font)
    th = full_bbox[3] - full_bbox[1]
    x = (ZONE3_W - total_w) // 2
    y = (ZONE3_H - th) // 2 - full_bbox[1]

    letter_boxes: List[Tuple[int, int]] = []
    for idx, ch in enumerate(name_upper):
        cb = d.textbbox((0, 0), ch, font=font)
        cw = cb[2] - cb[0]
        d.text((x - cb[0], y), ch, font=font, fill=BLACK)
        letter_boxes.append((max(0, x), max(1, cw)))
        x += cw + tracking

    return threshold_1bit(img, cutoff=140), letter_boxes


def render_sleep_screen() -> Image.Image:
    """Pantalla completa 200x200 para reposo: luna creciente, estrellas y texto."""
    img, d = new_canvas(EPD_W, EPD_H)

    # Luna creciente centrada arriba
    mx, my, r = 100, 74, 34
    d.ellipse([mx - r, my - r, mx + r, my + r], fill=BLACK)
    d.ellipse([mx - r + 18, my - r - 8, mx + r + 14, my + r - 8], fill=WHITE)

    # Estrellitas alrededor
    stars = [(44, 48, 7), (154, 44, 8), (52, 108, 5), (156, 98, 6)]
    for sx, sy, sr in stars:
        d.line([(sx - sr, sy), (sx + sr, sy)], fill=BLACK, width=2)
        d.line([(sx, sy - sr), (sx, sy + sr)], fill=BLACK, width=2)

    # Texto "¡HASTA PRONTO!"
    font = find_bold_font(20)
    msg = "¡HASTA PRONTO!"
    bb = d.textbbox((0, 0), msg, font=font)
    tw = bb[2] - bb[0]
    tx = (EPD_W - tw) // 2 - bb[0]
    d.text((tx, 146 - bb[1]), msg, font=font, fill=BLACK)

    return threshold_1bit(img, cutoff=140)


# ==============================================================================
# EMPAQUETADO A BYTES 1-BIT (MSB first, 1=Blanco, 0=Negro) Y EXPORTACION C
# ==============================================================================
def pack_1bit(img: Image.Image) -> bytes:
    w, h = img.size
    assert w % 8 == 0, f"Ancho {w} debe ser multiplo de 8"
    pixels = img.load()
    out = bytearray()
    for y in range(h):
        for xb in range(0, w, 8):
            byte = 0
            for bit in range(8):
                if pixels[xb + bit, y] > 127:
                    byte |= 1 << (7 - bit)
            out.append(byte)
    return bytes(out)


def format_c_array(name: str, data: bytes) -> str:
    lines = [f"const uint8_t {name}[{len(data)}] PROGMEM = {{"]
    for i in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02X}" for b in data[i : i + 16])
        lines.append(f"    {chunk},")
    lines.append("};")
    return "\n".join(lines)


def compose_full_screen(index: int, total: int, sprite: Image.Image, name_bmp: Image.Image) -> Image.Image:
    """Compone una pantalla simulada 200x200 identica a la del ESP32."""
    screen, d = new_canvas(EPD_W, EPD_H)

    # Zona 1: Paginador (puntos adaptativos)
    spacing = 11 if total > 13 else (13 if total > 10 else 16)
    start_x = (EPD_W - (total - 1) * spacing) // 2
    for i in range(total):
        px = start_x + i * spacing
        py = 10
        if i == index:
            d.ellipse([px - 4, py - 4, px + 4, py + 4], fill=BLACK)
        else:
            d.ellipse([px - 2, py - 2, px + 2, py + 2], fill=WHITE, outline=BLACK, width=1)

    # Linea separadora sutil opcional bajo paginador
    d.line([(16, 20), (183, 20)], fill=BLACK, width=1)

    # Zona 2: Sprite 168x128 en (16, 24)
    screen.paste(sprite, (ZONE2_X, ZONE2_Y))

    # Linea separadora sobre nombre
    d.line([(16, 154), (183, 154)], fill=BLACK, width=1)

    # Zona 3: Nombre 200x44 en (0, 156)
    screen.paste(name_bmp, (0, ZONE3_Y))
    return screen


def build_preview_sheet(screens: List[Tuple[str, Image.Image, Image.Image]], sleep_img: Image.Image) -> None:
    cols = 2
    rows = len(screens) + 1
    pad = 16
    label_h = 20
    cell_w = EPD_W + pad * 2
    cell_h = EPD_H + label_h + pad

    sheet = Image.new("RGB", (cols * cell_w, rows * cell_h + pad), (235, 235, 235))
    d = ImageDraw.Draw(sheet)
    font = find_bold_font(12)

    for idx, (label, scr_a, scr_b) in enumerate(screens):
        for col, (tag, scr) in enumerate([("Frame A (Reposo)", scr_a), ("Frame B (Sonido)", scr_b)]):
            ox = col * cell_w + pad
            oy = idx * cell_h + pad
            d.text((ox, oy), f"{label} - {tag}", font=font, fill=(30, 30, 30))
            sheet.paste(scr.convert("RGB"), (ox, oy + label_h))
            d.rectangle([ox - 1, oy + label_h - 1, ox + EPD_W, oy + label_h + EPD_H], outline=(100, 100, 100))

    # Ultima fila: Sleep Screen
    oy = len(screens) * cell_h + pad
    d.text((pad, oy), "SLEEP SCREEN (Pulsacion larga PWR)", font=font, fill=(30, 30, 30))
    sheet.paste(sleep_img.convert("RGB"), (pad, oy + label_h))
    d.rectangle([pad - 1, oy + label_h - 1, pad + EPD_W, oy + label_h + EPD_H], outline=(100, 100, 100))

    OUT_PREVIEW.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT_PREVIEW)


def normalize_letter_keys(name_upper: str) -> str:
    repl = str.maketrans("ÁÉÍÓÚÜ", "AEIOUU")
    return name_upper.translate(repl).lower()


def main() -> int:
    OUT_HEADER.parent.mkdir(parents=True, exist_ok=True)

    arrays_c: List[str] = []
    entries_c: List[str] = []
    preview_screens: List[Tuple[str, Image.Image, Image.Image]] = []

    for idx, (animal_id, name_upper, _ono) in enumerate(ANIMALS):
        spr_a = load_sprite(animal_id, frame_b=False)
        spr_b = load_sprite(animal_id, frame_b=True)
        name_bmp, letter_boxes = render_name_bitmap(name_upper)

        arrays_c.append(format_c_array(f"gfx_{animal_id}_a", pack_1bit(spr_a)))
        arrays_c.append(format_c_array(f"gfx_{animal_id}_b", pack_1bit(spr_b)))
        arrays_c.append(format_c_array(f"gfx_{animal_id}_name", pack_1bit(name_bmp)))

        lx_vals = [str(b[0]) for b in letter_boxes] + ["0"] * (12 - len(letter_boxes))
        lw_vals = [str(b[1]) for b in letter_boxes] + ["0"] * (12 - len(letter_boxes))
        lkeys = normalize_letter_keys(name_upper)

        entries_c.append(
            f'    {{ "{animal_id}", gfx_{animal_id}_a, gfx_{animal_id}_b, '
            f'gfx_{animal_id}_name, "/sounds/{animal_id}.wav", '
            f'{len(letter_boxes)}, {{ {", ".join(lx_vals)} }}, {{ {", ".join(lw_vals)} }}, "{lkeys}" }},'
        )

        scr_a = compose_full_screen(idx, len(ANIMALS), spr_a, name_bmp)
        scr_b = compose_full_screen(idx, len(ANIMALS), spr_b, name_bmp)
        preview_screens.append((name_upper, scr_a, scr_b))

    sleep_img = render_sleep_screen()
    arrays_c.append(format_c_array("gfx_sleep_screen", pack_1bit(sleep_img)))

    header_content = f"""#ifndef ANIMALS_GFX_H
#define ANIMALS_GFX_H

// ==============================================================================
// ARCHIVO GENERADO AUTOMATICAMENTE POR tools/generate_graphics.py
// NO EDITAR MANUALMENTE - Volver a ejecutar el script ante cambios de sprites.
// ==============================================================================

#include <stdint.h>
#ifdef ARDUINO
#include <pgmspace.h>
#else
#ifndef PROGMEM
#define PROGMEM
#endif
#endif

#define GFX_ZONE2_X         {ZONE2_X}
#define GFX_ZONE2_Y         {ZONE2_Y}
#define GFX_ZONE2_W         {ZONE2_W}
#define GFX_ZONE2_H         {ZONE2_H}
#define GFX_ZONE2_BYTES     ({ZONE2_W} * {ZONE2_H} / 8)

#define GFX_ZONE3_Y         {ZONE3_Y}
#define GFX_ZONE3_W         {ZONE3_W}
#define GFX_ZONE3_H         {ZONE3_H}
#define GFX_ZONE3_BYTES     ({ZONE3_W} * {ZONE3_H} / 8)

typedef struct {{
    const char* id;
    const uint8_t* frame_a;      // 168 x 128 px (1-bit)
    const uint8_t* frame_b;      // 168 x 128 px (1-bit)
    const uint8_t* name_bitmap;  // 200 x 44 px (1-bit, MAYUSCULAS)
    const char* audio_path;      // ej. "/sounds/perro.wav"
    uint8_t letter_count;        // Cantidad de letras en el nombre
    uint8_t letter_x[12];        // Posicion X de cada letra en Zona 3
    uint8_t letter_w[12];        // Ancho de cada letra en Zona 3
    const char* letter_keys;     // Letras en minuscula ASCII para /letters/<c>.wav
}} AnimalAsset;

{chr(10).join(arrays_c)}

static const uint8_t ANIMAL_COUNT = {len(ANIMALS)};

static const AnimalAsset ANIMALS[ANIMAL_COUNT] = {{
{chr(10).join(entries_c)}
}};

#endif // ANIMALS_GFX_H
"""
    OUT_HEADER.write_text(header_content, encoding="utf-8")
    build_preview_sheet(preview_screens, sleep_img)

    print(f"[OK] Generado: {OUT_HEADER}")
    print(f"[OK] Preview:  {OUT_PREVIEW}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
