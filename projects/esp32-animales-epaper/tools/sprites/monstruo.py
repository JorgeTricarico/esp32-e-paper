"""Generador de sprites 1-bit (168x128) para el monstruo amigable (Frame A y Frame B).

Inspirado en el monstruo de "Ahora no, Bernardo" de David McKee.
"""

import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

WIDTH = 168
HEIGHT = 128
OUTLINE = 3  # Grosor de trazo limpio (3-4 px)

ROOT_DIR = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT_DIR / "assets" / "custom_sprites"


def draw_thick_polyline(
    draw: ImageDraw.ImageDraw,
    pts: list[tuple[float, float]],
    width: int = OUTLINE,
    closed: bool = False,
    fill: int = 0,
) -> None:
    """Dibuja una polilinea con uniones redondeadas limpias."""
    if len(pts) < 2:
        return
    r = max(1, width // 2)
    seq = pts + [pts[0]] if closed else pts
    for i in range(len(seq) - 1):
        draw.line((seq[i][0], seq[i][1], seq[i + 1][0], seq[i + 1][1]), fill=fill, width=width)
    for x, y in pts:
        draw.ellipse((x - r, y - r, x + r, y + r), fill=fill)


def build_furry_body_points(cx: float, cy: float) -> list[tuple[float, float]]:
    """Genera el contorno de cuerpo rechoncho (forma de pera) con mechones suaves."""
    pts: list[tuple[float, float]] = []
    num_tufts = 20
    steps = num_tufts * 2
    for i in range(steps):
        angle = (2.0 * math.pi * i) / steps - math.pi / 2.0
        cos_a = math.cos(angle)
        sin_a = math.sin(angle)

        # Forma de pera rechoncha: mas angosta arriba (cabeza), mas ancha abajo (panza)
        pear_factor = 1.0 + 0.18 * sin_a
        rx = 32.0 * pear_factor
        ry = 39.0

        # Mechon suave alternando picos y valles
        is_peak = (i % 2 == 0)
        tuft = 4.2 if is_peak else -1.2

        # En la parte superior de la cabeza y base de pies, mechones ligeramente mas suaves
        if sin_a < -0.75:
            tuft *= 0.7

        x = cx + (rx + tuft) * cos_a
        y = cy + (ry + tuft) * sin_a
        pts.append((round(x), round(y)))
    return pts


def draw_horn(
    draw: ImageDraw.ImageDraw,
    base_x: int,
    base_y: int,
    tip_x: int,
    tip_y: int,
    half_w: int = 5,
) -> None:
    """Dibuja un cuernito pequeno y simpatico con contorno negro e interior blanco."""
    # Vector perpendicular para el ancho de la base
    dx = tip_x - base_x
    dy = tip_y - base_y
    length = math.hypot(dx, dy) or 1.0
    nx = -dy / length
    ny = dx / length

    p1 = (round(base_x + nx * half_w), round(base_y + ny * half_w))
    p2 = (round(base_x - nx * half_w), round(base_y - ny * half_w))
    tip = (tip_x, tip_y)

    draw.polygon([p1, tip, p2], fill=255)
    draw_thick_polyline(draw, [p1, tip, p2], width=OUTLINE, closed=False, fill=0)
    # Rayita decorativa en el cuerno
    mid_x = (base_x + tip_x) // 2
    mid_y = (base_y + tip_y) // 2
    draw.line(
        (
            round(mid_x + nx * (half_w * 0.45)),
            round(mid_y + ny * (half_w * 0.45)),
            round(mid_x - nx * (half_w * 0.45)),
            round(mid_y - ny * (half_w * 0.45)),
        ),
        fill=0,
        width=2,
    )


def draw_tail(draw: ImageDraw.ImageDraw, cx: int, cy: int, wag_up: bool = False) -> None:
    """Dibuja una colita peluda saliendo por el lado izquierdo, debajo del brazo."""
    if not wag_up:
        pts = [
            (cx - 28, cy + 23),
            (cx - 42, cy + 25),
            (cx - 49, cy + 22),
            (cx - 45, cy + 29),
            (cx - 48, cy + 34),
            (cx - 37, cy + 33),
            (cx - 26, cy + 31),
        ]
    else:
        pts = [
            (cx - 28, cy + 18),
            (cx - 46, cy + 14),
            (cx - 54, cy + 5),
            (cx - 47, cy + 17),
            (cx - 51, cy + 25),
            (cx - 38, cy + 27),
            (cx - 26, cy + 28),
        ]
    draw.polygon(pts, fill=255)
    draw_thick_polyline(draw, pts, width=OUTLINE, closed=False, fill=0)


def draw_foot(draw: ImageDraw.ImageDraw, x: int, y_top: int, y_bot: int, facing_right: bool = True) -> None:
    """Dibuja una patita corta y rechoncha con tres garritas."""
    dir_x = 1 if facing_right else -1
    # Pierna corta
    draw.rectangle((x - 7, y_top, x + 7, y_bot - 4), fill=255)
    draw.line((x - 7, y_top, x - 7, y_bot - 4), fill=0, width=OUTLINE)
    draw.line((x + 7, y_top, x + 7, y_bot - 4), fill=0, width=OUTLINE)

    # Pie redondeado
    fx0 = x - 9 if dir_x > 0 else x - 12
    fx1 = x + 12 if dir_x > 0 else x + 9
    draw.ellipse((fx0, y_bot - 10, fx1, y_bot), fill=255, outline=0, width=OUTLINE)

    # Dos rayitas de garritas/dedos
    toe_x = fx1 - 5 if dir_x > 0 else fx0 + 5
    draw.line((toe_x, y_bot - 6, toe_x + dir_x * 3, y_bot - 1), fill=0, width=2)
    draw.line((toe_x - dir_x * 4, y_bot - 5, toe_x - dir_x * 2, y_bot - 1), fill=0, width=2)


def draw_arm_resting(draw: ImageDraw.ImageDraw, x_sh: int, y_sh: int, side: int) -> None:
    """Dibuja un bracito corto en reposo (side=-1 izquierdo, +1 derecho)."""
    x_el = x_sh + side * 12
    y_el = y_sh + 11
    x_hand = x_sh + side * 14
    y_hand = y_sh + 20

    # Contorno blanco grueso + trazo negro para que se separe limpio del cuerpo
    draw.line((x_sh, y_sh, x_el, y_el), fill=0, width=10)
    draw.line((x_el, y_el, x_hand, y_hand), fill=0, width=9)
    draw.ellipse((x_hand - 5, y_hand - 5, x_hand + 5, y_hand + 5), fill=0)

    draw.line((x_sh, y_sh, x_el, y_el), fill=255, width=4)
    draw.line((x_el, y_el, x_hand, y_hand), fill=255, width=4)
    draw.ellipse((x_hand - 2, y_hand - 2, x_hand + 2, y_hand + 2), fill=255)

    # 3 garritas simpaticas abajo
    for dx in (-3, 0, 3):
        draw.line((x_hand + dx, y_hand + 3, x_hand + dx + side, y_hand + 7), fill=0, width=2)


def draw_arm_raised(draw: ImageDraw.ImageDraw, x_sh: int, y_sh: int, side: int) -> None:
    """Dibuja un bracito levantado haciendo garritas simpaticas (Frame B)."""
    x_el = x_sh + side * 13
    y_el = y_sh - 7
    x_hand = x_sh + side * 17
    y_hand = y_sh - 17

    draw.line((x_sh, y_sh, x_el, y_el), fill=0, width=10)
    draw.line((x_el, y_el, x_hand, y_hand), fill=0, width=9)
    draw.ellipse((x_hand - 5, y_hand - 5, x_hand + 5, y_hand + 5), fill=0)

    draw.line((x_sh, y_sh, x_el, y_el), fill=255, width=4)
    draw.line((x_el, y_el, x_hand, y_hand), fill=255, width=4)
    draw.ellipse((x_hand - 2, y_hand - 2, x_hand + 2, y_hand + 2), fill=255)

    # 3 garritas hacia arriba/afuera
    for idx, (gx, gy) in enumerate([(-4, -5), (0, -7), (4, -5)]):
        draw.line(
            (x_hand + gx, y_hand - 3, x_hand + gx + side * (idx - 1), y_hand + gy),
            fill=0,
            width=2,
        )


def draw_monster(draw: ImageDraw.ImageDraw, roaring: bool = False) -> None:
    """Dibuja al monstruo completo en Frame A (roaring=False) o Frame B (roaring=True)."""
    cx = 72
    cy = 67 if not roaring else 65

    # 1. Colita peluda detras del cuerpo
    draw_tail(draw, cx, cy, wag_up=roaring)

    # 2. Patitas cortas y rechonchas
    draw_foot(draw, cx - 15, cy + 34, cy + 51, facing_right=False)
    draw_foot(draw, cx + 15, cy + 34, cy + 51, facing_right=True)

    # 3. Dos cuernitos pequenos en la cabeza
    draw_horn(draw, cx - 14, cy - 35, cx - 22, cy - 50, half_w=5)
    draw_horn(draw, cx + 14, cy - 35, cx + 22, cy - 50, half_w=5)

    # 4. Cuerpo rechoncho peludo (contorno con mechones suaves, interior blanco)
    body_pts = build_furry_body_points(cx, cy)
    draw.polygon(body_pts, fill=255)
    draw_thick_polyline(draw, body_pts, width=OUTLINE, closed=True, fill=0)

    # Textura limpia de mechoncitos suaves de pelo para no empastar en 1-bit
    tuft_marks = [
        (cx - 19, cy + 21),
        (cx + 18, cy + 21),
        (cx - 1, cy + 28),
    ]
    for tx, ty in tuft_marks:
        draw.line((tx - 3, ty, tx, ty + 4, tx + 3, ty), fill=0, width=2)

    # 5. Bracitos cortos (reposo en A, levantados haciendo garritas en B)
    if not roaring:
        draw_arm_resting(draw, cx - 29, cy - 2, side=-1)
        draw_arm_resting(draw, cx + 29, cy - 2, side=1)
    else:
        draw_arm_raised(draw, cx - 27, cy + 1, side=-1)
        draw_arm_raised(draw, cx + 27, cy + 1, side=1)

    # 6. Boca ancha con dientes triangulares prominentes y divertidos
    if not roaring:
        # Sonrisa ancha cerrada/semiabierta con dientes triangulares divertidos
        mouth_box = (cx - 21, cy - 1, cx + 21, cy + 17)
        draw.chord(mouth_box, start=0, end=180, fill=0, outline=0, width=OUTLINE)
        # Dientes triangulares blancos arriba bajando sobre la boca negra
        for tx in (cx - 13, cx - 4, cx + 5, cx + 13):
            draw.polygon([(tx - 4, cy + 8), (tx + 4, cy + 8), (tx, cy + 15)], fill=255)
        # Labio superior limpio
        draw.arc(mouth_box, start=5, end=175, fill=0, width=OUTLINE)
        draw.line((cx - 21, cy + 8, cx + 21, cy + 8), fill=0, width=OUTLINE)
    else:
        # Boca bien abierta rugiendo alegre
        mouth_box = (cx - 22, cy - 3, cx + 22, cy + 25)
        draw.ellipse(mouth_box, fill=0)
        # Lenguita simpatica en el centro inferior con contorno negro limpio
        draw.chord((cx - 9, cy + 15, cx + 9, cy + 25), start=180, end=360, fill=255)
        draw.arc((cx - 9, cy + 15, cx + 9, cy + 25), start=180, end=360, fill=0, width=2)
        # Dientes triangulares superiores bien marcados
        for tx in (cx - 13, cx - 4, cx + 4, cx + 13):
            draw.polygon([(tx - 4, cy - 1), (tx + 4, cy - 1), (tx, cy + 8)], fill=255)
        # Dientes triangulares inferiores a ambos lados de la lengua
        for tx in (cx - 12, cx + 12):
            draw.polygon([(tx - 4, cy + 21), (tx + 4, cy + 21), (tx, cy + 13)], fill=255)
        # Contorno limpio de la boca
        draw.ellipse(mouth_box, outline=0, width=OUTLINE)

    # 7. Ojos pizarrones expresivos (grandes, juntos, con pupilas picaras)
    eye_y = cy - 20
    # Ojo izquierdo
    draw.ellipse((cx - 16, eye_y - 8, cx - 1, eye_y + 8), fill=255, outline=0, width=OUTLINE)
    draw.ellipse((cx - 10, eye_y - 3, cx - 3, eye_y + 4), fill=0)
    draw.ellipse((cx - 8, eye_y - 2, cx - 6, eye_y), fill=255)
    # Ojo derecho
    draw.ellipse((cx + 1, eye_y - 8, cx + 16, eye_y + 8), fill=255, outline=0, width=OUTLINE)
    draw.ellipse((cx + 4, eye_y - 3, cx + 11, eye_y + 4), fill=0)
    draw.ellipse((cx + 6, eye_y - 2, cx + 8, eye_y), fill=255)

    # Cejitas expresivas
    if not roaring:
        draw.arc((cx - 16, eye_y - 13, cx - 2, eye_y - 6), start=190, end=340, fill=0, width=2)
        draw.arc((cx + 2, eye_y - 13, cx + 16, eye_y - 6), start=200, end=350, fill=0, width=2)
    else:
        draw.line((cx - 15, eye_y - 11, cx - 3, eye_y - 9), fill=0, width=3)
        draw.line((cx + 3, eye_y - 9, cx + 15, eye_y - 11), fill=0, width=3)

    # 8. Nariz larga/peculiar estilo David McKee (dibujada al final para sobresalir)
    nose_y = cy - 10
    nose_pts = [
        (cx - 2, nose_y - 3),
        (cx + 13, nose_y - 5),
        (cx + 20, nose_y - 2),
        (cx + 21, nose_y + 2),
        (cx + 18, nose_y + 5),
        (cx + 10, nose_y + 6),
        (cx - 2, nose_y + 4),
    ]
    draw.polygon(nose_pts, fill=255)
    draw_thick_polyline(draw, nose_pts, width=OUTLINE, closed=False, fill=0)
    # Punta redondeada y fosita nasal
    draw.ellipse((cx + 14, nose_y - 1, cx + 17, nose_y + 2), fill=0)


def draw_speech_bubble(draw: ImageDraw.ImageDraw, text: str = "¡GRRR!") -> None:
    """Dibuja la burbuja de dialogo arriba a la derecha con trazo grueso."""
    bx0, by0, bx1, by1 = 110, 4, 165, 29
    pointer_outer = [(119, 27), (112, 38), (128, 27)]
    pointer_inner = [(121, 25), (115, 34), (126, 25)]

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

    # Frame A: monstruo rechoncho en reposo amigable
    img_a = Image.new("L", (WIDTH, HEIGHT), 255)
    draw_a = ImageDraw.Draw(img_a)
    draw_monster(draw_a, roaring=False)
    img_a = to_pure_bw(img_a)
    path_a = OUT_DIR / "monstruo_a.png"
    img_a.save(path_a)

    # Frame B: misma base + bracitos levantados + boca rugiendo + burbuja "¡GRRR!"
    img_b = Image.new("L", (WIDTH, HEIGHT), 255)
    draw_b = ImageDraw.Draw(img_b)
    draw_monster(draw_b, roaring=True)
    draw_speech_bubble(draw_b, "¡GRRR!")
    img_b = to_pure_bw(img_b)
    path_b = OUT_DIR / "monstruo_b.png"
    img_b.save(path_b)

    print(f"Generados: {path_a} y {path_b}")


if __name__ == "__main__":
    main()
