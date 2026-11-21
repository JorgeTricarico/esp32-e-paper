"""Generador de sprites 1-bit (168x128) para la luciernaguita (Frame A y Frame B)."""

import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

WIDTH = 168
HEIGHT = 128
OUTLINE = 3  # Grosor de trazo limpio (3-4 px)

ROOT_DIR = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT_DIR / "assets" / "custom_sprites"


def rotated_ellipse_points(
    cx: float,
    cy: float,
    rx: float,
    ry: float,
    angle_deg: float,
    steps: int = 48,
) -> list[tuple[float, float]]:
    """Genera los vertices de una elipse rotada."""
    rad = math.radians(angle_deg)
    cos_a = math.cos(rad)
    sin_a = math.sin(rad)
    pts = []
    for i in range(steps):
        t = 2.0 * math.pi * i / steps
        x_local = rx * math.cos(t)
        y_local = ry * math.sin(t)
        x = cx + x_local * cos_a - y_local * sin_a
        y = cy + x_local * sin_a + y_local * cos_a
        pts.append((x, y))
    return pts


def draw_rotated_ellipse(
    draw: ImageDraw.ImageDraw,
    cx: float,
    cy: float,
    rx: float,
    ry: float,
    angle_deg: float,
    outline: int = OUTLINE,
    fill: int = 255,
) -> None:
    """Dibuja una elipse rotada con contorno negro grueso e interior relleno."""
    if outline > 0:
        outer_pts = rotated_ellipse_points(cx, cy, rx + outline, ry + outline, angle_deg)
        draw.polygon(outer_pts, fill=0)
    inner_pts = rotated_ellipse_points(cx, cy, rx, ry, angle_deg)
    draw.polygon(inner_pts, fill=fill)


def draw_thick_curve(
    draw: ImageDraw.ImageDraw,
    points: list[tuple[float, float]],
    width: int = OUTLINE,
    fill: int = 0,
) -> None:
    """Dibuja una curva suave con remates redondeados."""
    draw.line(points, fill=fill, width=width, joint="curve")
    r = width / 2.0
    for px, py in (points[0], points[-1]):
        draw.ellipse((px - r, py - r, px + r, py + r), fill=fill)


def draw_quad_bezier(
    draw: ImageDraw.ImageDraw,
    p0: tuple[float, float],
    p1: tuple[float, float],
    p2: tuple[float, float],
    width: int = OUTLINE,
    fill: int = 0,
    steps: int = 20,
) -> None:
    """Dibuja una curva Bezier cuadratica con grosor uniforme."""
    pts = []
    for i in range(steps + 1):
        t = i / steps
        inv = 1.0 - t
        x = inv * inv * p0[0] + 2 * inv * t * p1[0] + t * t * p2[0]
        y = inv * inv * p0[1] + 2 * inv * t * p1[1] + t * t * p2[1]
        pts.append((x, y))
    draw_thick_curve(draw, pts, width=width, fill=fill)


def draw_wing(
    draw: ImageDraw.ImageDraw,
    cx: float,
    cy: float,
    rx: float,
    ry: float,
    angle_deg: float,
) -> None:
    """Dibuja un alita ovalada transparente (interior blanco) con nervadura sutil."""
    draw_rotated_ellipse(draw, cx, cy, rx, ry, angle_deg, outline=OUTLINE, fill=255)
    # Nervadura central suave para dar textura de alita delicada
    rad = math.radians(angle_deg)
    dx = (rx * 0.55) * math.cos(rad)
    dy = (rx * 0.55) * math.sin(rad)
    draw_thick_curve(
        draw,
        [(cx - dx * 0.4, cy - dy * 0.4), (cx + dx, cy + dy)],
        width=2,
        fill=0,
    )


def draw_sparkle(draw: ImageDraw.ImageDraw, cx: int, cy: int, r: int = 5) -> None:
    """Dibuja un destello de 4 puntas en negro."""
    draw.line((cx - r, cy, cx + r, cy), fill=0, width=2)
    draw.line((cx, cy - r, cx, cy + r), fill=0, width=2)
    draw.ellipse((cx - 2, cy - 2, cx + 2, cy + 2), fill=0)


def draw_firefly(
    draw: ImageDraw.ImageDraw,
    dx: int = 0,
    dy: int = 0,
    flapping: bool = False,
) -> None:
    """Dibuja la luciernaguita completa."""
    # 1. Alita trasera (ligeramente mas arriba/atras)
    if not flapping:
        draw_wing(draw, 50 + dx, 39 + dy, rx=21, ry=9.5, angle_deg=36)
    else:
        draw_wing(draw, 52 + dx, 31 + dy, rx=22, ry=9.5, angle_deg=54)

    # 2. Patitas tiernas recogidas en el aire (dibujadas antes del cuerpo)
    legs = [
        ((66 + dx, 88 + dy), (62 + dx, 97 + dy), (67 + dx, 102 + dy)),
        ((76 + dx, 84 + dy), (74 + dx, 95 + dy), (80 + dx, 100 + dy)),
        ((85 + dx, 78 + dy), (85 + dx, 89 + dy), (91 + dx, 93 + dy)),
    ]
    for p0, p1, p2 in legs:
        draw_quad_bezier(draw, p0, p1, p2, width=3, fill=0)

    # 3. Abdomen / colita redondeada e inclinada
    # Centro en (56, 78), inclinado -24 grados
    draw_rotated_ellipse(draw, 56 + dx, 78 + dy, rx=25, ry=16, angle_deg=-24, outline=OUTLINE, fill=255)

    # Segmentos del abdomen (lineas curvas suaves contenidas dentro del ovalo)
    draw_quad_bezier(
        draw,
        (52 + dx, 66 + dy),
        (57 + dx, 76 + dy),
        (60 + dx, 88 + dy),
        width=2,
        fill=0,
    )
    # Linea divisoria de la colita luminosa
    draw_quad_bezier(
        draw,
        (41 + dx, 72 + dy),
        (46 + dx, 81 + dy),
        (50 + dx, 91 + dy),
        width=3,
        fill=0,
    )

    # Detalle en la colita luminosa
    if not flapping:
        # En reposo: brillito tierno en la colita
        draw.ellipse((37 + dx, 81 + dy, 41 + dx, 85 + dy), fill=0)
    else:
        # En Frame B: rayitos de luz saliendo de la colita luminosa brillante
        rays = [
            ((25 + dx, 74 + dy), (15 + dx, 67 + dy)),
            ((22 + dx, 83 + dy), (9 + dx, 82 + dy)),
            ((23 + dx, 92 + dy), (11 + dx, 97 + dy)),
            ((29 + dx, 99 + dy), (20 + dx, 109 + dy)),
            ((38 + dx, 103 + dy), (35 + dx, 115 + dy)),
            ((48 + dx, 102 + dy), (50 + dx, 112 + dy)),
        ]
        for p0, p1 in rays:
            draw_thick_curve(draw, [p0, p1], width=3, fill=0)

        # Destellos extra cerca de la colita
        draw_sparkle(draw, 14 + dx, 57 + dy, r=4)
        draw_sparkle(draw, 25 + dx, 117 + dy, r=4)

    # 4. Cabeza grande y tierna de libro infantil
    hx, hy = 93 + dx, 57 + dy
    hr = 20
    draw.ellipse((hx - hr - OUTLINE, hy - hr - OUTLINE, hx + hr + OUTLINE, hy + hr + OUTLINE), fill=0)
    draw.ellipse((hx - hr, hy - hr, hx + hr, hy + hr), fill=255)

    # 5. Antenitas curvas con bolitas en las puntas
    # Antena izquierda
    draw_quad_bezier(
        draw,
        (86 + dx, 39 + dy),
        (79 + dx, 27 + dy),
        (73 + dx, 25 + dy),
        width=3,
        fill=0,
    )
    draw.ellipse((69 + dx, 21 + dy, 77 + dx, 29 + dy), fill=0)

    # Antena derecha (curvada suavemente sin acercarse a la burbuja)
    draw_quad_bezier(
        draw,
        (97 + dx, 38 + dy),
        (98 + dx, 26 + dy),
        (103 + dx, 22 + dy),
        width=3,
        fill=0,
    )
    draw.ellipse((99 + dx, 18 + dy, 107 + dx, 26 + dy), fill=0)

    # 6. Ojitos grandes felices (mirando hacia la derecha en 3/4)
    # Ojo izquierdo
    draw.ellipse((83 + dx, 48 + dy, 93 + dx, 60 + dy), fill=0)
    draw.ellipse((85 + dx, 50 + dy, 89 + dx, 54 + dy), fill=255)  # Brillito grande
    draw.ellipse((89 + dx, 55 + dy, 91 + dx, 57 + dy), fill=255)  # Brillito pequeño

    # Ojo derecho
    draw.ellipse((98 + dx, 48 + dy, 108 + dx, 60 + dy), fill=0)
    draw.ellipse((100 + dx, 50 + dy, 104 + dx, 54 + dy), fill=255)  # Brillito grande
    draw.ellipse((104 + dx, 55 + dy, 106 + dx, 57 + dy), fill=255)  # Brillito pequeño

    # Mejillita tierna (rayitas suaves)
    draw.line((79 + dx, 61 + dy, 83 + dx, 63 + dy), fill=0, width=2)
    draw.line((107 + dx, 62 + dy, 111 + dx, 61 + dy), fill=0, width=2)

    # Boquita feliz
    if not flapping:
        draw_quad_bezier(
            draw,
            (92 + dx, 65 + dy),
            (97 + dx, 70 + dy),
            (102 + dx, 65 + dy),
            width=3,
            fill=0,
        )
    else:
        # Boquita abierta cantando/zumbando alegremente
        draw.chord((92 + dx, 61 + dy, 103 + dx, 72 + dy), start=0, end=180, fill=0)

    # 7. Alita delantera (desplazada a la izquierda para no tapar los ojos)
    if not flapping:
        draw_wing(draw, 54 + dx, 49 + dy, rx=21, ry=9.5, angle_deg=18)
    else:
        draw_wing(draw, 56 + dx, 46 + dy, rx=22, ry=9.5, angle_deg=7)
        # Rayitas de movimiento alrededor de las alitas aleteando (bien separadas)
        draw_quad_bezier(
            draw,
            (31 + dx, 14 + dy),
            (38 + dx, 9 + dy),
            (47 + dx, 9 + dy),
            width=2,
            fill=0,
        )
        draw_quad_bezier(
            draw,
            (25 + dx, 24 + dy),
            (31 + dx, 19 + dy),
            (38 + dx, 18 + dy),
            width=2,
            fill=0,
        )
        draw_quad_bezier(
            draw,
            (27 + dx, 50 + dy),
            (31 + dx, 56 + dy),
            (38 + dx, 58 + dy),
            width=2,
            fill=0,
        )


def draw_speech_bubble(draw: ImageDraw.ImageDraw, text: str = "¡BZZZ!") -> None:
    """Dibuja la burbuja de dialogo arriba a la derecha con trazo grueso."""
    bx0, by0, bx1, by1 = 113, 4, 165, 29
    pointer_outer = [(123, 27), (114, 38), (133, 27)]
    pointer_inner = [(126, 25), (119, 33), (130, 25)]

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

    # Frame A: reposo amigable
    img_a = Image.new("L", (WIDTH, HEIGHT), 255)
    draw_a = ImageDraw.Draw(img_a)
    draw_firefly(draw_a, dx=2, dy=2, flapping=False)
    img_a = to_pure_bw(img_a)
    path_a = OUT_DIR / "luciernaga_a.png"
    img_a.save(path_a)

    # Frame B: aleteando + colita luminosa brillante + burbuja "¡BZZZ!"
    img_b = Image.new("L", (WIDTH, HEIGHT), 255)
    draw_b = ImageDraw.Draw(img_b)
    draw_firefly(draw_b, dx=2, dy=-2, flapping=True)
    draw_speech_bubble(draw_b, "¡BZZZ!")
    img_b = to_pure_bw(img_b)
    path_b = OUT_DIR / "luciernaga_b.png"
    img_b.save(path_b)

    print(f"Generados: {path_a} y {path_b}")


if __name__ == "__main__":
    main()
