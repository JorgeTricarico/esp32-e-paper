"""Generador de sprites 1-bit (168x128) para el grillito (Frame A y Frame B)."""

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
    steps: int = 24,
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


def draw_cubic_bezier(
    draw: ImageDraw.ImageDraw,
    p0: tuple[float, float],
    p1: tuple[float, float],
    p2: tuple[float, float],
    p3: tuple[float, float],
    width: int = OUTLINE,
    fill: int = 0,
    steps: int = 28,
) -> None:
    """Dibuja una curva Bezier cubica con grosor uniforme."""
    pts = []
    for i in range(steps + 1):
        t = i / steps
        inv = 1.0 - t
        x = (
            inv**3 * p0[0]
            + 3 * inv**2 * t * p1[0]
            + 3 * inv * t**2 * p2[0]
            + t**3 * p3[0]
        )
        y = (
            inv**3 * p0[1]
            + 3 * inv**2 * t * p1[1]
            + 3 * inv * t**2 * p2[1]
            + t**3 * p3[1]
        )
        pts.append((x, y))
    draw_thick_curve(draw, pts, width=width, fill=fill)


def draw_hind_leg(
    draw: ImageDraw.ImageDraw,
    hip: tuple[float, float],
    knee: tuple[float, float],
    ankle: tuple[float, float],
    foot_end: tuple[float, float],
    femur_ry: float = 5.5,
    outline: int = OUTLINE,
) -> None:
    """Dibuja una patita trasera larga doblada para saltar (femur + tibia + pie)."""
    # Femur (muslo ovalado robusto)
    cx = (hip[0] + knee[0]) / 2.0
    cy = (hip[1] + knee[1]) / 2.0
    dx = knee[0] - hip[0]
    dy = knee[1] - hip[1]
    length = math.hypot(dx, dy)
    angle_deg = math.degrees(math.atan2(dy, dx))
    draw_rotated_ellipse(
        draw,
        cx,
        cy,
        rx=length * 0.52,
        ry=femur_ry,
        angle_deg=angle_deg,
        outline=outline,
        fill=255,
    )
    # Linea interna sutil del femur para darle volumen infantil
    inner_p0 = (hip[0] + dx * 0.25, hip[1] + dy * 0.25)
    inner_p1 = (hip[0] + dx * 0.72, hip[1] + dy * 0.72)
    draw_thick_curve(draw, [inner_p0, inner_p1], width=2, fill=0)

    # Tibia larga y delgada hasta el tobillo + patita
    draw_thick_curve(draw, [knee, ankle, foot_end], width=OUTLINE, fill=0)
    # Remate limpio en la rodilla para que no quede muesca
    draw.ellipse((knee[0] - 2.5, knee[1] - 2.5, knee[0] + 2.5, knee[1] + 2.5), fill=0)


def draw_cricket(
    draw: ImageDraw.ImageDraw,
    dx: int = 0,
    dy: int = 0,
    singing: bool = False,
) -> None:
    """Dibuja al grillito simpatico de libro infantil."""
    # 1. Patita trasera lejana (asomando detras para dar profundidad)
    if not singing:
        draw_hind_leg(
            draw,
            hip=(74 + dx, 76 + dy),
            knee=(49 + dx, 43 + dy),
            ankle=(38 + dx, 102 + dy),
            foot_end=(45 + dx, 103 + dy),
            femur_ry=4.5,
            outline=OUTLINE,
        )
    else:
        draw_hind_leg(
            draw,
            hip=(74 + dx, 74 + dy),
            knee=(49 + dx, 37 + dy),
            ankle=(38 + dx, 97 + dy),
            foot_end=(45 + dx, 99 + dy),
            femur_ry=4.5,
            outline=OUTLINE,
        )

    # 2. Patita delantera del lado lejano (sin cruzar con las cercanas)
    if not singing:
        draw_thick_curve(
            draw,
            [(102 + dx, 74 + dy), (113 + dx, 88 + dy), (118 + dx, 103 + dy), (124 + dx, 104 + dy)],
            width=OUTLINE,
            fill=0,
        )
    else:
        draw_thick_curve(
            draw,
            [(103 + dx, 73 + dy), (115 + dx, 84 + dy), (123 + dx, 95 + dy), (129 + dx, 94 + dy)],
            width=OUTLINE,
            fill=0,
        )

    # 3. Cercos (dos colitas finitas tipicas de grillo en la punta del abdomen)
    draw_quad_bezier(
        draw,
        (26 + dx, 80 + dy),
        (17 + dx, 77 + dy),
        (11 + dx, 72 + dy),
        width=2,
        fill=0,
    )
    draw_quad_bezier(
        draw,
        (26 + dx, 85 + dy),
        (17 + dx, 88 + dy),
        (12 + dx, 93 + dy),
        width=2,
        fill=0,
    )

    # 4. Abdomen segmentado
    draw_rotated_ellipse(
        draw,
        54 + dx,
        80 + dy,
        rx=28,
        ry=14,
        angle_deg=-10,
        outline=OUTLINE,
        fill=255,
    )
    # Segmentos curvos del abdomen
    for sx, sy_top, sy_bot in [
        (37, 73, 91),
        (48, 70, 91),
        (59, 68, 90),
    ]:
        draw_quad_bezier(
            draw,
            (sx + dx, sy_top + dy),
            (sx - 3 + dx, (sy_top + sy_bot) / 2 + dy),
            (sx + 1 + dx, sy_bot + dy),
            width=2,
            fill=0,
        )

    # 5. Alitas plegadas (en Frame B se levantan y vibran para cantar)
    if not singing:
        # Alita trasera apenas asomando
        draw_rotated_ellipse(
            draw, 58 + dx, 61 + dy, rx=22, ry=6.5, angle_deg=-14, outline=OUTLINE, fill=255
        )
        # Alita delantera plegada
        draw_rotated_ellipse(
            draw, 61 + dx, 65 + dy, rx=23, ry=7, angle_deg=-9, outline=OUTLINE, fill=255
        )
        # Nervaduras suaves en la alita
        draw_quad_bezier(
            draw, (44 + dx, 67 + dy), (60 + dx, 64 + dy), (76 + dx, 63 + dy), width=2, fill=0
        )
    else:
        # Alita trasera levantada
        draw_rotated_ellipse(
            draw, 57 + dx, 53 + dy, rx=22, ry=6.5, angle_deg=-29, outline=OUTLINE, fill=255
        )
        # Alita delantera levantada frotando
        draw_rotated_ellipse(
            draw, 60 + dx, 59 + dy, rx=23, ry=7, angle_deg=-21, outline=OUTLINE, fill=255
        )
        draw_quad_bezier(
            draw, (44 + dx, 66 + dy), (60 + dx, 59 + dy), (75 + dx, 53 + dy), width=2, fill=0
        )

    # 6. Torax redondeado
    draw_rotated_ellipse(
        draw,
        83 + dx,
        72 + dy,
        rx=15,
        ry=14,
        angle_deg=-6,
        outline=OUTLINE,
        fill=255,
    )

    # 7. Patitas delantera y media del lado cercano (limpias, sin cruzarse)
    if not singing:
        draw_thick_curve(
            draw,
            [(94 + dx, 80 + dy), (102 + dx, 94 + dy), (107 + dx, 108 + dy), (113 + dx, 109 + dy)],
            width=OUTLINE,
            fill=0,
        )
        draw_thick_curve(
            draw,
            [(81 + dx, 83 + dy), (85 + dx, 97 + dy), (88 + dx, 109 + dy), (94 + dx, 110 + dy)],
            width=OUTLINE,
            fill=0,
        )
    else:
        # Patita delantera levantada con entusiasmo (paralela a la lejana, sin cruzarse)
        draw_thick_curve(
            draw,
            [(94 + dx, 79 + dy), (103 + dx, 90 + dy), (110 + dx, 100 + dy), (116 + dx, 99 + dy)],
            width=OUTLINE,
            fill=0,
        )
        draw_thick_curve(
            draw,
            [(81 + dx, 83 + dy), (85 + dx, 97 + dy), (88 + dx, 108 + dy), (94 + dx, 109 + dy)],
            width=OUTLINE,
            fill=0,
        )

    # 8. Patita trasera cercana (grande, doblada para saltar)
    if not singing:
        draw_hind_leg(
            draw,
            hip=(68 + dx, 80 + dy),
            knee=(39 + dx, 45 + dy),
            ankle=(25 + dx, 107 + dy),
            foot_end=(33 + dx, 109 + dy),
            femur_ry=5.5,
            outline=OUTLINE,
        )
    else:
        # Frotando patita contra el alita levantada
        draw_hind_leg(
            draw,
            hip=(68 + dx, 78 + dy),
            knee=(38 + dx, 39 + dy),
            ankle=(24 + dx, 99 + dy),
            foot_end=(32 + dx, 102 + dy),
            femur_ry=5.5,
            outline=OUTLINE,
        )

    # 9. Cabecita redondeada en perfil amigable 3/4 mirando a la derecha
    hx, hy = 102 + dx, 57 + dy
    hrx, hry = 19, 18
    draw.ellipse(
        (hx - hrx - OUTLINE, hy - hry - OUTLINE, hx + hrx + OUTLINE, hy + hry + OUTLINE),
        fill=0,
    )
    draw.ellipse((hx - hrx, hy - hry, hx + hrx, hy + hry), fill=255)

    # 10. Antenitas largas y finas curvas (hacia arriba y atras para dejar libre la burbuja)
    if not singing:
        # Antena izquierda/trasera larga y gracil
        draw_cubic_bezier(
            draw,
            (96 + dx, 41 + dy),
            (86 + dx, 20 + dy),
            (62 + dx, 12 + dy),
            (36 + dx, 21 + dy),
            width=3,
            fill=0,
        )
        # Antena derecha/delantera
        draw_cubic_bezier(
            draw,
            (107 + dx, 41 + dy),
            (103 + dx, 18 + dy),
            (82 + dx, 9 + dy),
            (58 + dx, 12 + dy),
            width=3,
            fill=0,
        )
    else:
        # Antenitas alertas mas erguidas y vibrantes (sin tocar la burbuja x>=100, y<=29)
        draw_cubic_bezier(
            draw,
            (95 + dx, 40 + dy),
            (84 + dx, 16 + dy),
            (60 + dx, 8 + dy),
            (38 + dx, 14 + dy),
            width=3,
            fill=0,
        )
        draw_cubic_bezier(
            draw,
            (105 + dx, 40 + dy),
            (98 + dx, 16 + dy),
            (82 + dx, 7 + dy),
            (63 + dx, 8 + dy),
            width=3,
            fill=0,
        )

    # 11. Ojitos grandes expresivos (en 3/4 mirando alegremente a la derecha)
    # Ojo cercano (grande)
    draw.ellipse((96 + dx, 47 + dy, 106 + dx, 59 + dy), fill=0)
    draw.ellipse((98 + dx, 49 + dy, 102 + dx, 53 + dy), fill=255)
    draw.ellipse((102 + dx, 54 + dy, 104 + dx, 56 + dy), fill=255)

    # Ojo lejano (un poquito mas angosto por el perfil)
    draw.ellipse((110 + dx, 47 + dy, 118 + dx, 58 + dy), fill=0)
    draw.ellipse((112 + dx, 49 + dy, 115 + dx, 52 + dy), fill=255)
    draw.ellipse((115 + dx, 53 + dy, 117 + dx, 55 + dy), fill=255)

    # Mejillita tierna
    draw.line((91 + dx, 60 + dy, 95 + dx, 62 + dy), fill=0, width=2)

    # Boquita simpatica
    if not singing:
        draw_quad_bezier(
            draw,
            (105 + dx, 64 + dy),
            (111 + dx, 68 + dy),
            (116 + dx, 63 + dy),
            width=3,
            fill=0,
        )
    else:
        # Boquita abierta cantando feliz
        draw.chord((105 + dx, 60 + dy, 116 + dx, 70 + dy), start=0, end=180, fill=0)

    # 12. Rayitas de sonido/canto en Frame B alrededor de las alitas y patita frotando
    if singing:
        # Onditas de sonido arriba de las alitas/rodilla
        draw_quad_bezier(
            draw, (22 + dx, 36 + dy), (27 + dx, 28 + dy), (35 + dx, 26 + dy), width=2, fill=0
        )
        draw_quad_bezier(
            draw, (15 + dx, 32 + dy), (22 + dx, 22 + dy), (32 + dx, 19 + dy), width=2, fill=0
        )
        # Onditas de sonido detras de las alitas
        draw_quad_bezier(
            draw, (14 + dx, 54 + dy), (17 + dx, 46 + dy), (23 + dx, 42 + dy), width=2, fill=0
        )
        draw_quad_bezier(
            draw, (57 + dx, 32 + dy), (65 + dx, 30 + dy), (72 + dx, 34 + dy), width=2, fill=0
        )
        draw_quad_bezier(
            draw, (59 + dx, 25 + dy), (69 + dx, 23 + dy), (77 + dx, 27 + dy), width=2, fill=0
        )


def draw_speech_bubble(draw: ImageDraw.ImageDraw, text: str = "¡CRI-CRI!") -> None:
    """Dibuja la burbuja de dialogo arriba a la derecha con trazo grueso."""
    bx0, by0, bx1, by1 = 100, 2, 165, 25
    pointer_outer = [(115, 23), (112, 32), (125, 23)]
    pointer_inner = [(118, 21), (115, 28), (122, 21)]

    draw.rounded_rectangle((bx0, by0, bx1, by1), radius=9, fill=255, outline=0, width=OUTLINE)
    draw.polygon(pointer_outer, fill=0)
    draw.rounded_rectangle(
        (bx0 + OUTLINE, by0 + OUTLINE, bx1 - OUTLINE, by1 - OUTLINE), radius=6, fill=255
    )
    draw.polygon(pointer_inner, fill=255)

    font = None
    for font_path in ("C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/calibrib.ttf"):
        if Path(font_path).exists():
            font = ImageFont.truetype(font_path, 11)
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

    # Frame A: grillito en reposo amigable
    img_a = Image.new("L", (WIDTH, HEIGHT), 255)
    draw_a = ImageDraw.Draw(img_a)
    draw_cricket(draw_a, dx=2, dy=2, singing=False)
    img_a = to_pure_bw(img_a)
    path_a = OUT_DIR / "grillo_a.png"
    img_a.save(path_a)

    # Frame B: frotando patitas/alitas con sonido + burbuja "¡CRI-CRI!"
    img_b = Image.new("L", (WIDTH, HEIGHT), 255)
    draw_b = ImageDraw.Draw(img_b)
    draw_cricket(draw_b, dx=2, dy=0, singing=True)
    draw_speech_bubble(draw_b, "¡CRI-CRI!")
    img_b = to_pure_bw(img_b)
    path_b = OUT_DIR / "grillo_b.png"
    img_b.save(path_b)

    print(f"Generados: {path_a} y {path_b}")


if __name__ == "__main__":
    main()
