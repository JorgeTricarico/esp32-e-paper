"""Generador de sprites 1-bit (168x128) para el esqueletito (Frame A y Frame B)."""

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
    angle_deg: float = 0.0,
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


def draw_bone(
    draw: ImageDraw.ImageDraw,
    p0: tuple[float, float],
    p1: tuple[float, float],
    shaft_r: float = 1.5,
    knob_r: float = 2.0,
    outline: float = 2.5,
) -> None:
    """Dibuja un huesito limpio y estilizado de libro infantil."""
    x0, y0 = p0
    x1, y1 = p1
    dx = x1 - x0
    dy = y1 - y0
    length = math.hypot(dx, dy)
    if length < 1e-3:
        return
    nx = -dy / length
    ny = dx / length
    knob_off = 1.1

    # 1. Contorno negro exterior
    outer_w = int(round((shaft_r + outline) * 2))
    draw.line((x0, y0, x1, y1), fill=0, width=outer_w)
    r_out = knob_r + outline
    for px, py in (p0, p1):
        for sign in (-1, 1):
            kx = px + nx * knob_off * sign
            ky = py + ny * knob_off * sign
            draw.ellipse((kx - r_out, ky - r_out, kx + r_out, ky + r_out), fill=0)

    # 2. Interior blanco puro
    inner_w = int(round(shaft_r * 2))
    draw.line((x0, y0, x1, y1), fill=255, width=inner_w)
    r_in = knob_r
    for px, py in (p0, p1):
        for sign in (-1, 1):
            kx = px + nx * knob_off * sign
            ky = py + ny * knob_off * sign
            draw.ellipse((kx - r_in, ky - r_in, kx + r_in, ky + r_in), fill=255)


def draw_foot(
    draw: ImageDraw.ImageDraw,
    cx: float,
    cy: float,
    angle_deg: float = 0.0,
) -> None:
    """Dibuja un piecito de hueso redondeado y simpatico."""
    draw_rotated_ellipse(draw, cx, cy, rx=6.0, ry=3.0, angle_deg=angle_deg, outline=OUTLINE, fill=255)


def draw_hand(
    draw: ImageDraw.ImageDraw,
    cx: float,
    cy: float,
    angle_deg: float = 0.0,
) -> None:
    """Dibuja una manito redondeada y simpatica."""
    draw_rotated_ellipse(draw, cx, cy, rx=3.5, ry=2.8, angle_deg=angle_deg, outline=2.5, fill=255)


def draw_skeleton(
    draw: ImageDraw.ImageDraw,
    cx: float = 72,
    dy: float = 0,
    dancing: bool = False,
) -> None:
    """Dibuja el esqueletito simpatico de libro infantil."""
    head_tilt = -5.0 if dancing else 0.0
    head_dx = -2.0 if dancing else 0.0

    # --- 1. PIERNITAS ---
    pelvis_y = 89 + dy
    if not dancing:
        # Pierna izquierda
        l_hip = (cx - 8, pelvis_y + 3)
        l_knee = (cx - 10, 104 + dy)
        l_ankle = (cx - 10, 117 + dy)
        draw_bone(draw, l_hip, l_knee)
        draw_bone(draw, l_knee, l_ankle)
        draw_foot(draw, l_ankle[0] - 3, l_ankle[1] + 2, angle_deg=0)

        # Pierna derecha
        r_hip = (cx + 8, pelvis_y + 3)
        r_knee = (cx + 10, 104 + dy)
        r_ankle = (cx + 10, 117 + dy)
        draw_bone(draw, r_hip, r_knee)
        draw_bone(draw, r_knee, r_ankle)
        draw_foot(draw, r_ankle[0] + 3, r_ankle[1] + 2, angle_deg=0)
    else:
        # Pierna izquierda apoyada
        l_hip = (cx - 8, pelvis_y + 3)
        l_knee = (cx - 12, 104 + dy)
        l_ankle = (cx - 10, 118 + dy)
        draw_bone(draw, l_hip, l_knee)
        draw_bone(draw, l_knee, l_ankle)
        draw_foot(draw, l_ankle[0] - 3, l_ankle[1] + 2, angle_deg=-5)

        # Pierna derecha levantada bailando alegre
        r_hip = (cx + 8, pelvis_y + 2)
        r_knee = (cx + 22, 95 + dy)
        r_ankle = (cx + 27, 108 + dy)
        draw_bone(draw, r_hip, r_knee)
        draw_bone(draw, r_knee, r_ankle)
        draw_foot(draw, r_ankle[0] + 4, r_ankle[1] - 1, angle_deg=-22)

    # --- 2. BRACITOS ---
    shoulder_y = 66 + dy
    if not dancing:
        # Brazo izquierdo relajado
        l_sh = (cx - 16, shoulder_y)
        l_el = (cx - 25, 77 + dy)
        l_wr = (cx - 23, 89 + dy)
        draw_bone(draw, l_sh, l_el, shaft_r=1.4, knob_r=1.9)
        draw_bone(draw, l_el, l_wr, shaft_r=1.4, knob_r=1.9)
        draw_hand(draw, l_wr[0], l_wr[1] + 2, angle_deg=15)

        # Brazo derecho relajado
        r_sh = (cx + 16, shoulder_y)
        r_el = (cx + 25, 77 + dy)
        r_wr = (cx + 23, 89 + dy)
        draw_bone(draw, r_sh, r_el, shaft_r=1.4, knob_r=1.9)
        draw_bone(draw, r_el, r_wr, shaft_r=1.4, knob_r=1.9)
        draw_hand(draw, r_wr[0], r_wr[1] + 2, angle_deg=-15)
    else:
        # Brazo izquierdo doblado en jarra / bailando
        l_sh = (cx - 16, shoulder_y)
        l_el = (cx - 28, 72 + dy)
        l_wr = (cx - 22, 83 + dy)
        draw_bone(draw, l_sh, l_el, shaft_r=1.4, knob_r=1.9)
        draw_bone(draw, l_el, l_wr, shaft_r=1.4, knob_r=1.9)
        draw_hand(draw, l_wr[0] + 1, l_wr[1] + 2, angle_deg=-20)

        # Brazo derecho levantado saludando/bailando alegre
        r_sh = (cx + 16, shoulder_y)
        r_el = (cx + 27, 60 + dy)
        r_wr = (cx + 32, 47 + dy)
        draw_bone(draw, r_sh, r_el, shaft_r=1.4, knob_r=1.9)
        draw_bone(draw, r_el, r_wr, shaft_r=1.4, knob_r=1.9)
        draw_hand(draw, r_wr[0] + 1, r_wr[1] - 2, angle_deg=-25)

    # --- 3. COLUMNA, COSTILLAS Y PELVIS ---
    # Columna vertebral central
    draw.line((cx, 57 + dy, cx, pelvis_y), fill=0, width=4)

    # Costillas limpias (3 pares simetricos)
    rib_specs = [
        (67 + dy, 13.5, 2.2),
        (73.5 + dy, 11.5, 1.8),
        (80 + dy, 8.5, 1.4),
    ]
    for ry, rw, curve in rib_specs:
        draw_quad_bezier(
            draw,
            (cx - rw, ry + curve),
            (cx, ry - curve),
            (cx + rw, ry + curve),
            width=4,
            fill=0,
        )

    # Pelvis redondeada (dos lobulos unidos limpiamente)
    draw_rotated_ellipse(draw, cx - 6.0, pelvis_y, rx=6.5, ry=4.2, angle_deg=-10, outline=OUTLINE, fill=255)
    draw_rotated_ellipse(draw, cx + 6.0, pelvis_y, rx=6.5, ry=4.2, angle_deg=10, outline=OUTLINE, fill=255)
    draw.ellipse((cx - 5, pelvis_y - 2.5, cx + 5, pelvis_y + 2.5), fill=255)

    # --- 4. CALAVERA REDONDEADA Y AMIGABLE ---
    hcx = cx + head_dx
    cranium_y = 31 + dy
    jaw_y = 48 + dy

    # Contorno exterior unido (craneo + mandibula)
    draw.polygon(rotated_ellipse_points(hcx, cranium_y, 25 + OUTLINE, 21 + OUTLINE, head_tilt), fill=0)
    draw.polygon(rotated_ellipse_points(hcx, jaw_y, 15 + OUTLINE, 9.5 + OUTLINE, head_tilt), fill=0)

    # Interior blanco unido
    draw.polygon(rotated_ellipse_points(hcx, cranium_y, 25, 21, head_tilt), fill=255)
    draw.polygon(rotated_ellipse_points(hcx, jaw_y, 15, 9.5, head_tilt), fill=255)

    # Ojitos grandes oscuros con brillito blanco
    eye_dx = 10.0
    eye_y = 33 + dy
    eye_r = 6.0
    for side in (-1, 1):
        ex = hcx + side * eye_dx
        ey = eye_y + (side * -0.8 if dancing else 0.0)
        draw.ellipse((ex - eye_r, ey - eye_r, ex + eye_r, ey + eye_r), fill=0)
        # Brillito blanco principal
        draw.ellipse((ex - 3.5, ey - 3.5, ex - 0.3, ey - 0.3), fill=255)
        # Brillito secundario
        draw.ellipse((ex + 1.2, ey + 1.2, ex + 2.9, ey + 2.9), fill=255)

    # Naricita diminuta y simpatica
    ny = 41 + dy
    draw.polygon([(hcx, ny - 2.0), (hcx - 2.2, ny + 1.6), (hcx + 2.2, ny + 1.6)], fill=0)

    # Sonrisita alegre de dientes cuadrados
    mouth_y = 46 + dy
    draw_quad_bezier(
        draw,
        (hcx - 9.5, mouth_y),
        (hcx, mouth_y + 4.2),
        (hcx + 9.5, mouth_y),
        width=2,
        fill=0,
    )
    draw_quad_bezier(
        draw,
        (hcx - 8.5, mouth_y + 5.2),
        (hcx, mouth_y + 9.4),
        (hcx + 8.5, mouth_y + 5.2),
        width=2,
        fill=0,
    )
    for tx in (-9.0, -4.5, 0.0, 4.5, 9.0):
        t_norm = tx / 9.5
        y_top = mouth_y + 2.0 * (1.0 - t_norm * t_norm)
        y_bot = y_top + 4.9
        draw.line((hcx + tx, y_top, hcx + tx, y_bot), fill=0, width=2)

    # --- 5. RAYITAS DE MOVIMIENTO / TRAQUETEO (solo Frame B) ---
    if dancing:
        # Cerca del brazo levantado (separadas del contorno)
        draw_quad_bezier(draw, (cx + 40, 39 + dy), (cx + 43, 44 + dy), (cx + 42, 50 + dy), width=2, fill=0)
        draw_quad_bezier(draw, (cx + 45, 42 + dy), (cx + 47, 46 + dy), (cx + 46, 51 + dy), width=2, fill=0)

        # Cerca de la pierna levantada
        draw_quad_bezier(draw, (cx + 36, 94 + dy), (cx + 40, 99 + dy), (cx + 39, 105 + dy), width=2, fill=0)
        draw_quad_bezier(draw, (cx + 41, 97 + dy), (cx + 44, 101 + dy), (cx + 43, 106 + dy), width=2, fill=0)

        # Rayitas de traqueteo junto a la cabecita
        draw_quad_bezier(draw, (cx - 33, 21 + dy), (cx - 36, 26 + dy), (cx - 35, 31 + dy), width=2, fill=0)
        draw_quad_bezier(draw, (cx - 38, 24 + dy), (cx - 40, 28 + dy), (cx - 39, 32 + dy), width=2, fill=0)


def draw_speech_bubble(draw: ImageDraw.ImageDraw, text: str = "¡CLAC!") -> None:
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
    draw_skeleton(draw_a, cx=72, dy=0, dancing=False)
    img_a = to_pure_bw(img_a)
    path_a = OUT_DIR / "esqueleto_a.png"
    img_a.save(path_a)

    # Frame B: bailando alegre + rayitas de traqueteo + burbuja "¡CLAC!"
    img_b = Image.new("L", (WIDTH, HEIGHT), 255)
    draw_b = ImageDraw.Draw(img_b)
    draw_skeleton(draw_b, cx=72, dy=-2, dancing=True)
    draw_speech_bubble(draw_b, "¡CLAC!")
    img_b = to_pure_bw(img_b)
    path_b = OUT_DIR / "esqueleto_b.png"
    img_b.save(path_b)

    print(f"Generados: {path_a} y {path_b}")


if __name__ == "__main__":
    main()
