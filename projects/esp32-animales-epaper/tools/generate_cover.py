#!/usr/bin/env python3
"""
Genera la portada ilustrada 200x200 (1-bit B/N) para la pantalla de reposo/Deep Sleep
del Libro de Animales Interactivo.
Salida:
  - src/assets/cover_gfx.h (static const uint8_t gfx_cover[5000] PROGMEM)
  - tools/cover_preview.png
"""

from __future__ import annotations

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT_DIR = Path(__file__).resolve().parent.parent
SPRITES_DIR = ROOT_DIR / "assets" / "custom_sprites"
OUT_HEADER = ROOT_DIR / "src" / "assets" / "cover_gfx.h"
OUT_PREVIEW = ROOT_DIR / "tools" / "cover_preview.png"

EPD_W = 200
EPD_H = 200


def load_font(size: int) -> ImageFont.FreeTypeFont:
    for path in [
        "C:/Windows/Fonts/arialbd.ttf",
        "C:/Windows/Fonts/calibrib.ttf",
        "C:/Windows/Fonts/seguisb.ttf",
    ]:
        p = Path(path)
        if p.exists():
            try:
                return ImageFont.truetype(str(p), size)
            except Exception:
                pass
    return ImageFont.load_default()


def crop_sprite_content(path: Path) -> Image.Image:
    img = Image.open(path).convert("L")
    inv = ImageOps.invert(img)
    bbox = inv.getbbox()
    if bbox:
        img = img.crop(bbox)
    return img


def pack_1bit(img: Image.Image) -> bytes:
    """Empaqueta 200x200 en 5000 bytes MSB-first (1=Blanco, 0=Negro)."""
    px = img.load()
    out = bytearray()
    for y in range(EPD_H):
        for bx in range(EPD_W // 8):
            byte = 0
            for bit in range(8):
                x = bx * 8 + bit
                if px[x, y] > 127:
                    byte |= (0x80 >> bit)
            out.append(byte)
    return bytes(out)


def main() -> int:
    img = Image.new("L", (EPD_W, EPD_H), 255)
    draw = ImageDraw.Draw(img)

    # Marco de tapa de libro
    draw.rounded_rectangle([3, 3, 196, 196], radius=10, fill=255, outline=0, width=2)

    # Banner superior negro con titulo en blanco
    draw.rounded_rectangle([9, 8, 190, 44], radius=7, fill=0)
    f_small = load_font(11)
    f_big = load_font(17)

    t1 = "MI LIBRO DE"
    b1 = draw.textbbox((0, 0), t1, font=f_small)
    draw.text(((EPD_W - (b1[2] - b1[0])) // 2 - b1[0], 11 - b1[1]), t1, fill=255, font=f_small)

    t2 = "ANIMALES"
    b2 = draw.textbbox((0, 0), t2, font=f_big)
    draw.text(((EPD_W - (b2[2] - b2[0])) // 2 - b2[0], 24 - b2[1]), t2, fill=255, font=f_big)

    # Grilla 3x2 de 6 personajes bien legibles (perro, leon, elefante / gato, sapo, monstruo)
    featured = [
        "perro", "leon", "elefante",
        "gato", "sapo", "monstruo",
    ]
    cell_w = 58
    cell_h = 62
    start_x = 12
    start_y = 50
    gap_x = 3
    gap_y = 4

    for idx, name in enumerate(featured):
        row = idx // 3
        col = idx % 3
        cx = start_x + col * (cell_w + gap_x)
        cy = start_y + row * (cell_h + gap_y)

        sp = crop_sprite_content(SPRITES_DIR / f"{name}_a.png")
        # Escalar preservando aspecto dentro de cell_w x cell_h
        scale = min((cell_w - 2) / sp.width, (cell_h - 2) / sp.height)
        nw = max(1, int(round(sp.width * scale)))
        nh = max(1, int(round(sp.height * scale)))
        sp_resized = sp.resize((nw, nh), Image.Resampling.LANCZOS)
        # Umbral un poco mas oscuro (<175 -> negro) para que los trazos finos al reducir no se borren
        sp_1bit = sp_resized.point(lambda p: 255 if p > 170 else 0)
        ox = cx + (cell_w - nw) // 2
        oy = cy + (cell_h - nh) // 2
        img.paste(sp_1bit, (ox, oy))

    # Pie sutil de portada
    draw.line([(16, 180), (183, 180)], fill=0, width=1)
    f_foot = load_font(10)
    t3 = "15 PERSONAJES  •  VOZ Y LETRAS"
    b3 = draw.textbbox((0, 0), t3, font=f_foot)
    draw.text(((EPD_W - (b3[2] - b3[0])) // 2 - b3[0], 184 - b3[1]), t3, fill=0, font=f_foot)

    final = img.point(lambda p: 255 if p > 127 else 0)
    final.save(OUT_PREVIEW)

    data = pack_1bit(final)
    assert len(data) == 5000
    hex_lines = []
    for i in range(0, len(data), 16):
        hex_lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",")

    OUT_HEADER.write_text(
        "#ifndef COVER_GFX_H\n"
        "#define COVER_GFX_H\n\n"
        "#include <Arduino.h>\n\n"
        "static const uint8_t gfx_cover[5000] PROGMEM = {\n"
        + "\n".join(hex_lines) + "\n"
        "};\n\n"
        "#endif // COVER_GFX_H\n",
        encoding="utf-8",
    )
    print(f"[OK] Portada generada: {OUT_PREVIEW} y {OUT_HEADER}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
