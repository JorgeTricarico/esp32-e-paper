"""Generador de sprites 1-bit (168x128) para el cerdito (Frame A y Frame B)."""

import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

WIDTH = 168
HEIGHT = 128
OUTLINE = 3  # Grosor de trazo limpio (3-4 px)

ROOT_DIR = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT_DIR / "assets" / "custom_sprites"


def draw_thick_ellipse(
    draw: ImageDraw.ImageDraw,
    bbox: tuple[int, int, int, int],
    fill: int = 255,
    outline: int = OUTLINE,
) -> None:
    """Dibuja un ovalo con contorno negro grueso uniforme."""
    x0, y0, x1, y1 = bbox
    draw.ellipse((x0 - outline, y0 - outline, x1 + outline, y1 + outline), fill=0)
    draw.ellipse((x0, y0, x1, y1), fill=fill)


def draw_pig_leg(
    draw: ImageDraw.ImageDraw,
    x: int,
    y_top: int,
    y_bot: int,
    w: int = 11,
    angle_dx: int = 0,
) -> None:
    """Dibuja una patita cortita y rechoncha con pezuñita negra."""
    half = w // 2
    x_bot = x + angle_dx
    # Contorno exterior negro de la pata
    draw.line((x, y_top, x_bot, y_bot - 4), fill=0, width=w + OUTLINE * 2)
    # Relleno blanco interior de la pata
    draw.line((x, y_top, x_bot, y_bot - 4), fill=255, width=w)
    # Pezuñita negra redondeada abajo
    draw.ellipse((x_bot - half - OUTLINE, y_bot - 9, x_bot + half + OUTLINE, y_bot), fill=0)
    # Pequeña muesca blanca en la base para insinuar la pezuña partida
    draw.line((x_bot, y_bot - 3, x_bot, y_bot), fill=255, width=2)


def draw_curly_tail(draw: ImageDraw.ImageDraw, x0: int, y0: int, wagging: bool = False) -> None:
    """Dibuja la colita en rulo con trazo grueso continuo."""
    # Puntos parametricos para un rulo limpio en espiral
    pts: list[tuple[int, int]] = []
    steps = 36
    for i in range(steps + 1):
        t = i / steps
        # Curva que sale hacia la izquierda, sube y hace un bucle completo
        angle = -0.3 + t * 2.35 * math.pi
        rad = 8.5 * (1.0 - 0.55 * t)
        offset_y = -4 * t if wagging else 0
        px = x0 - 9 - int(t * 9) + int(rad * math.cos(angle))
        py = y0 - 4 + int(offset_y) - int(rad * math.sin(angle))
        pts.append((px, py))

    for i in range(len(pts) - 1):
        draw.line((pts[i], pts[i + 1]), fill=0, width=OUTLINE)
    # Redondear extremos
    for px, py in (pts[0], pts[-1]):
        draw.ellipse((px - 1, py - 1, px + 1, py + 1), fill=0)

    if wagging:
        # Rayitas sutiles de movimiento junto a la colita
        draw.arc((x0 - 27, y0 - 18, x0 - 15, y0 - 6), start=130, end=230, fill=0, width=2)
        draw.arc((x0 - 24, y0 - 22, x0 - 12, y0 - 10), start=135, end=225, fill=0, width=2)


def draw_folded_ear(
    draw: ImageDraw.ImageDraw,
    pts_base: list[tuple[int, int]],
    pts_flap: list[tuple[int, int]],
) -> None:
    """Dibuja una orejita doblada adorable y limpia."""
    draw.polygon(pts_base, fill=255, outline=0, width=OUTLINE)
    draw.polygon(pts_flap, fill=255, outline=0, width=OUTLINE)


def draw_upright_ear(
    draw: ImageDraw.ImageDraw,
    pts_outer: list[tuple[int, int]],
    inner_line: tuple[int, int, int, int],
) -> None:
    """Dibuja una orejita levantada y alegre con trazo limpio."""
    draw.polygon(pts_outer, fill=255, outline=0, width=OUTLINE)
    draw.line(inner_line, fill=0, width=2)


def draw_pig(draw: ImageDraw.ImageDraw, dx: int = 0, dy: int = 0, happy: bool = False) -> None:
    """Dibuja el cerdito completo con desplazamiento (dx, dy)."""
    # 1. Colita en rulo (detras del cuerpo)
    draw_curly_tail(draw, 31 + dx, 71 + dy, wagging=happy)

    # 2. Patitas traseras (mas al fondo, ligeramente mas cortas/desplazadas)
    if not happy:
        draw_pig_leg(draw, 44 + dx, 90 + dy, 115 + dy, w=9)
        draw_pig_leg(draw, 77 + dx, 91 + dy, 115 + dy, w=9)
    else:
        draw_pig_leg(draw, 43 + dx, 88 + dy, 112 + dy, w=9, angle_dx=-4)
        draw_pig_leg(draw, 76 + dx, 89 + dy, 112 + dy, w=9, angle_dx=4)

    # 3. Cuerpo rechoncho ovalado
    draw_thick_ellipse(draw, (28 + dx, 52 + dy, 96 + dx, 102 + dy), fill=255, outline=OUTLINE)

    # 4. Patitas delanteras (integradas con la pancita)
    if not happy:
        draw_pig_leg(draw, 54 + dx, 92 + dy, 119 + dy, w=10)
        draw_pig_leg(draw, 85 + dx, 92 + dy, 119 + dy, w=10)
        draw.ellipse((49 + dx, 84 + dy, 60 + dx, 97 + dy), fill=255)
        draw.ellipse((80 + dx, 84 + dy, 91 + dx, 97 + dy), fill=255)
    else:
        draw_pig_leg(draw, 54 + dx, 90 + dy, 114 + dy, w=10, angle_dx=-3)
        draw_pig_leg(draw, 86 + dx, 90 + dy, 114 + dy, w=10, angle_dx=5)
        draw.ellipse((49 + dx, 82 + dy, 60 + dx, 95 + dy), fill=255)
        draw.ellipse((81 + dx, 82 + dy, 92 + dx, 95 + dy), fill=255)

    # 5. Oreja trasera (izquierda de la cabeza) - dibujada antes de la cabeza
    if not happy:
        # Orejita doblada izquierda
        draw_folded_ear(
            draw,
            [(76 + dx, 50 + dy), (69 + dx, 36 + dy), (84 + dx, 41 + dy)],
            [(69 + dx, 36 + dy), (62 + dx, 45 + dy), (75 + dx, 44 + dy)],
        )
    else:
        # Orejita levantada izquierda
        draw_upright_ear(
            draw,
            [(75 + dx, 48 + dy), (67 + dx, 28 + dy), (85 + dx, 40 + dy)],
            (74 + dx, 43 + dy, 71 + dx, 34 + dy),
        )

    # 6. Cabeza redonda y adorable
    draw_thick_ellipse(draw, (69 + dx, 40 + dy, 119 + dx, 88 + dy), fill=255, outline=OUTLINE)

    # Suavizar union cabeza-cuerpo (nuca/pecho)
    draw.ellipse((65 + dx, 58 + dy, 83 + dx, 86 + dy), fill=255)

    # 7. Oreja delantera (derecha/superior de la cabeza)
    if not happy:
        # Orejita doblada derecha
        draw_folded_ear(
            draw,
            [(97 + dx, 42 + dy), (105 + dx, 29 + dy), (112 + dx, 45 + dy)],
            [(105 + dx, 29 + dy), (117 + dx, 36 + dy), (106 + dx, 41 + dy)],
        )
        # Limpiar la linea base interna de la oreja para que nazca limpia de la cabeza
        draw.line((98 + dx, 43 + dy, 110 + dx, 45 + dy), fill=255, width=4)
    else:
        # Orejita levantada derecha
        draw_upright_ear(
            draw,
            [(96 + dx, 42 + dy), (106 + dx, 25 + dy), (112 + dx, 45 + dy)],
            (103 + dx, 41 + dy, 106 + dx, 32 + dy),
        )
        # Limpiar la linea base interna de la oreja para que nazca limpia de la cabeza
        draw.line((97 + dx, 43 + dy, 110 + dx, 45 + dy), fill=255, width=4)

    # 8. Ojos expresivos
    draw.ellipse((82 + dx, 50 + dy, 90 + dx, 58 + dy), fill=0)
    draw.ellipse((84 + dx, 52 + dy, 86 + dx, 54 + dy), fill=255)
    draw.ellipse((103 + dx, 50 + dy, 111 + dx, 58 + dy), fill=0)
    draw.ellipse((105 + dx, 52 + dy, 107 + dx, 54 + dy), fill=255)

    # 9. Boquita (debajo del hocico, con espacio limpio)
    if not happy:
        # Sonrisa dulce
        draw.arc((91 + dx, 71 + dy, 111 + dx, 82 + dy), start=20, end=160, fill=0, width=OUTLINE)
    else:
        # Boquita abierta feliz
        draw.chord((93 + dx, 72 + dy, 110 + dx, 84 + dy), start=0, end=180, fill=0, outline=0)
        draw.chord((97 + dx, 77 + dy, 106 + dx, 83 + dy), start=0, end=180, fill=255)

    # 10. Hocico ovalado prominente con dos orificios
    snout_box = (88 + dx, 59 + dy, 114 + dx, 74 + dy)
    draw_thick_ellipse(draw, snout_box, fill=255, outline=OUTLINE)
    draw.ellipse((94 + dx, 63 + dy, 99 + dx, 70 + dy), fill=0)
    draw.ellipse((103 + dx, 63 + dy, 108 + dx, 70 + dy), fill=0)

    # 11. Marquitas sutiles de saltito en el suelo para Frame B
    if happy:
        draw.line((38 + dx, 120, 54 + dx, 120), fill=0, width=2)
        draw.line((70 + dx, 122, 90 + dx, 122), fill=0, width=2)


def draw_speech_bubble(draw: ImageDraw.ImageDraw, text: str = "¡OINC!") -> None:
    """Dibuja la burbuja de dialogo arriba a la derecha con trazo grueso."""
    bx0, by0, bx1, by1 = 113, 3, 165, 27
    pointer_outer = [(122, 25), (116, 35), (131, 25)]
    pointer_inner = [(124, 23), (119, 31), (129, 23)]

    draw.rounded_rectangle((bx0, by0, bx1, by1), radius=9, fill=255, outline=0, width=OUTLINE)
    draw.polygon(pointer_outer, fill=0)
    draw.rounded_rectangle((bx0 + OUTLINE, by0 + OUTLINE, bx1 - OUTLINE, by1 - OUTLINE), radius=6, fill=255)
    draw.polygon(pointer_inner, fill=255)

    font = None
    for font_path in ("C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/calibrib.ttf"):
        if Path(font_path).exists():
            font = ImageFont.truetype(font_path, 12)
            break
    if font is None:
        font = ImageFont.load_default()

    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    tx = bx0 + (bx1 - bx0 - tw) // 2 - bbox[0]
    ty = by0 + (by1 - by0 - th) // 2 - bbox[1]
    draw.text((tx, ty), text, fill=0, font=font)


def to_pure_bw(img: Image.Image) -> Image.Image:
    """Convierte a blanco (255) y negro (0) puro con umbral >127, sin grises."""
    gray = img.convert("L")
    return gray.point(lambda p: 255 if p > 127 else 0, mode="L")


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    # Frame A: cerdito en reposo amigable
    img_a = Image.new("L", (WIDTH, HEIGHT), 255)
    draw_a = ImageDraw.Draw(img_a)
    draw_pig(draw_a, dx=2, dy=0, happy=False)
    img_a = to_pure_bw(img_a)
    path_a = OUT_DIR / "cerdo_a.png"
    img_a.save(path_a)

    # Frame B: misma base + orejas levantadas + boquita abierta + colita moviendose + burbuja "¡OINC!"
    img_b = Image.new("L", (WIDTH, HEIGHT), 255)
    draw_b = ImageDraw.Draw(img_b)
    draw_pig(draw_b, dx=2, dy=-4, happy=True)
    draw_speech_bubble(draw_b, "¡OINC!")
    img_b = to_pure_bw(img_b)
    path_b = OUT_DIR / "cerdo_b.png"
    img_b.save(path_b)

    print(f"Generados: {path_a} y {path_b}")


if __name__ == "__main__":
    main()
