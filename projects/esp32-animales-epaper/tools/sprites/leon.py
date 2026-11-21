import math
import os
from PIL import Image, ImageDraw, ImageFont

WIDTH = 168
HEIGHT = 128
STROKE = 3

BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT_DIR = os.path.join(BASE_DIR, "assets", "custom_sprites")


def load_bold_font(size: int) -> ImageFont.ImageFont:
    candidates = [
        r"C:\Windows\Fonts\arialbd.ttf",
        r"C:\Windows\Fonts\calibrib.ttf",
        r"C:\Windows\Fonts\verdanab.ttf",
        r"C:\Windows\Fonts\tahomabd.ttf",
        r"C:\Windows\Fonts\DejaVuSans-Bold.ttf",
    ]
    for path in candidates:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def draw_thick_arc_poly(draw: ImageDraw.ImageDraw, points: list[tuple[float, float]], width: int = STROKE, fill: int = 0) -> None:
    if len(points) < 2:
        return
    draw.line(points, fill=fill, width=width, joint="curve")
    r = width / 2.0
    for px, py in (points[0], points[-1]):
        draw.ellipse([px - r + 0.5, py - r + 0.5, px + r - 0.5, py + r - 0.5], fill=fill)


def cubic_bezier(p0, p1, p2, p3, steps=24):
    pts = []
    for i in range(steps + 1):
        t = i / steps
        mt = 1 - t
        x = mt**3 * p0[0] + 3 * mt**2 * t * p1[0] + 3 * mt * t**2 * p2[0] + t**3 * p3[0]
        y = mt**3 * p0[1] + 3 * mt**2 * t * p1[1] + 3 * mt * t**2 * p2[1] + t**3 * p3[1]
        pts.append((x, y))
    return pts


def draw_tuft(draw: ImageDraw.ImageDraw, cx: float, cy: float, angle_deg: float, scale: float = 1.0) -> None:
    """Dibuja el mechón de la cola en forma de gota apuntando en angle_deg."""
    rad = math.radians(angle_deg)
    ca, sa = math.cos(rad), math.sin(rad)

    def rot(lx: float, ly: float) -> tuple[float, float]:
        return (cx + (lx * ca - ly * sa) * scale, cy + (lx * sa + ly * ca) * scale)

    # Contorno de gota/llama suave usando dos curvas bezier
    p_base = rot(0, 0)
    p_tip = rot(17, 0)
    top_curve = cubic_bezier(p_base, rot(4, -9), rot(12, -7), p_tip, steps=16)
    bot_curve = cubic_bezier(p_tip, rot(12, 7), rot(4, 9), p_base, steps=16)
    poly = top_curve + bot_curve[1:]
    draw.polygon(poly, fill=255)
    draw_thick_arc_poly(draw, poly, width=STROKE, fill=0)

    # Rayita interior del mechón
    draw_thick_arc_poly(draw, [rot(4, 0), rot(11, -1)], width=2, fill=0)


def draw_body_and_legs(draw: ImageDraw.ImageDraw, cx: float) -> None:
    """Dibuja el cuerpo sentado del leoncito (compartido entre Frame A y B)."""
    # Patitas traseras (asoman a los lados)
    for side in (-1, 1):
        hx = cx + side * 20
        hy = 104
        # Óvalo de pata trasera
        draw.ellipse([hx - 10, hy - 9, hx + 10, hy + 11], fill=255, outline=0, width=STROKE)
        # Deditos de pata trasera
        for d in (-3, 1):
            tx = hx + side * (3 + d * 0.4)
            draw_thick_arc_poly(draw, [(tx, hy + 6), (tx, hy + 10)], width=2, fill=0)

    # Torso / pancita redondeada
    torso_box = [cx - 21, 72, cx + 21, 115]
    draw.ellipse(torso_box, fill=255, outline=0, width=STROKE)

    # Patitas delanteras rectas y gorditas apoyadas adelante
    for side in (-1, 1):
        px = cx + side * 10
        # Brazo vertical
        draw.rectangle([px - 6, 86, px + 6, 111], fill=255)
        draw_thick_arc_poly(draw, [(px - 6, 86), (px - 6, 111)], width=STROKE, fill=0)
        draw_thick_arc_poly(draw, [(px + 6, 86), (px + 6, 111)], width=STROKE, fill=0)
        # Zarpa / patita inferior redondeada
        draw.ellipse([px - 7, 106, px + 7, 118], fill=255, outline=0, width=STROKE)
        # Tapa la línea superior de la elipse dentro del brazo para unirlo limpio
        draw.rectangle([px - 4, 104, px + 4, 110], fill=255)
        # Deditos
        draw_thick_arc_poly(draw, [(px - 2, 113), (px - 2, 117)], width=2, fill=0)
        draw_thick_arc_poly(draw, [(px + 2, 113), (px + 2, 117)], width=2, fill=0)

    # Pequeña curva de pancita entre las patitas delanteras
    draw_thick_arc_poly(
        draw,
        cubic_bezier((cx - 4, 109), (cx - 1, 111), (cx + 1, 111), (cx + 4, 109), steps=10),
        width=STROKE,
        fill=0,
    )


def draw_tail(draw: ImageDraw.ImageDraw, cx: float, raised: bool) -> None:
    """Dibuja la colita al costado izquierdo."""
    if not raised:
        # Frame A: colita tranquila haciendo curva suave hacia la izquierda
        p0 = (cx - 21, 103)
        p1 = (cx - 38, 105)
        p2 = (cx - 45, 92)
        p3 = (cx - 42, 82)
        pts = cubic_bezier(p0, p1, p2, p3, steps=20)
        draw_thick_arc_poly(draw, pts, width=STROKE + 1, fill=0)
        draw_tuft(draw, p3[0], p3[1], angle_deg=-115, scale=0.95)
    else:
        # Frame B: colita levantada enérgicamente en forma de S
        p0 = (cx - 21, 100)
        p1 = (cx - 40, 96)
        p2 = (cx - 36, 72)
        p3 = (cx - 46, 60)
        pts = cubic_bezier(p0, p1, p2, p3, steps=20)
        draw_thick_arc_poly(draw, pts, width=STROKE + 1, fill=0)
        draw_tuft(draw, p3[0], p3[1], angle_deg=-130, scale=0.95)
        # Dos rayitas cortas de movimiento junto a la cola
        draw_thick_arc_poly(draw, [(cx - 56, 52), (cx - 60, 47)], width=2, fill=0)
        draw_thick_arc_poly(draw, [(cx - 58, 60), (cx - 63, 58)], width=2, fill=0)


def draw_mane_and_ears(draw: ImageDraw.ImageDraw, cx: float, cy: float, bristled: bool) -> None:
    """Dibuja las orejitas redondas y la melena festoneada en flor/sol."""
    # Orejitas redondas asomando detrás de la melena
    ear_positions = [(-22, -26), (22, -26)] if not bristled else [(-23, -27), (23, -27)]
    for ex_off, ey_off in ear_positions:
        ex, ey = cx + ex_off, cy + ey_off
        r_out = 9
        draw.ellipse([ex - r_out, ey - r_out, ex + r_out, ey + r_out], fill=255, outline=0, width=STROKE)
        r_in = 4
        draw.ellipse([ex - r_in, ey - r_in, ex + r_in, ey + r_in], fill=0)

    num_petals = 12
    if not bristled:
        # Frame A: melena festoneada suave en forma de flor/sol (lóbulos redondos)
        ring_r = 25.5
        petal_r = 9.5
        for i in range(num_petals):
            ang = math.radians(i * (360 / num_petals) - 90)
            px = cx + ring_r * math.cos(ang)
            py = cy + ring_r * math.sin(ang)
            draw.ellipse([px - petal_r, py - petal_r, px + petal_r, py + petal_r], fill=255, outline=0, width=STROKE)
        inner_clean = 26
        draw.ellipse([cx - inner_clean, cy - inner_clean, cx + inner_clean, cy + inner_clean], fill=255)
    else:
        # Frame B: misma base festoneada pero levemente erizada (pétalos más salidos con punta suave)
        pts = []
        r_tip = 37.5
        r_mid = 33.5
        r_valley = 26.5
        for i in range(num_petals):
            a0 = math.radians(i * (360 / num_petals) - 90)
            a_left = a0 - math.radians(8)
            a_right = a0 + math.radians(8)
            a_val = a0 + math.radians(15)
            pts.append((cx + r_mid * math.cos(a_left), cy + r_mid * math.sin(a_left)))
            pts.append((cx + r_tip * math.cos(a0), cy + r_tip * math.sin(a0)))
            pts.append((cx + r_mid * math.cos(a_right), cy + r_mid * math.sin(a_right)))
            pts.append((cx + r_valley * math.cos(a_val), cy + r_valley * math.sin(a_val)))
        draw.polygon(pts, fill=255)
        draw_thick_arc_poly(draw, pts + [pts[0]], width=STROKE, fill=0)


def draw_face(draw: ImageDraw.ImageDraw, cx: float, cy: float, roaring: bool) -> None:
    """Dibuja la carita del leoncito."""
    # Contorno de la cara
    rx, ry = 21.5, 19.5
    draw.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=255, outline=0, width=STROKE)

    # Ojos
    eye_y = cy - 4
    for side in (-1, 1):
        ex = cx + side * 8.5
        if not roaring:
            # Ojitos redondos tiernos y amigables con pequeño brillo
            er = 4
            draw.ellipse([ex - er, eye_y - er, ex + er, eye_y + er], fill=0)
            draw.rectangle([ex - 2, eye_y - 2, ex - 1, eye_y - 1], fill=255)
        else:
            # Ojitos alegres/expresivos entrecerrados (arcos felices hacia arriba)
            arc_pts = cubic_bezier(
                (ex - 4, eye_y + 1),
                (ex - 2, eye_y - 4),
                (ex + 2, eye_y - 4),
                (ex + 4, eye_y + 1),
                steps=12,
            )
            draw_thick_arc_poly(draw, arc_pts, width=STROKE, fill=0)

    # Cejitas suaves y amigables (arqueadas hacia arriba)
    for side in (-1, 1):
        bx = cx + side * 8.5
        by = cy - 11.5
        if not roaring:
            brow_pts = cubic_bezier(
                (bx - 3, by + 1),
                (bx - 1, by - 1),
                (bx + 1, by - 1),
                (bx + 3, by + 1),
                steps=8,
            )
            draw_thick_arc_poly(draw, brow_pts, width=2, fill=0)
        else:
            brow_pts = cubic_bezier(
                (bx - 3, by),
                (bx - 1, by - 2),
                (bx + 1, by - 2),
                (bx + 3, by),
                steps=8,
            )
            draw_thick_arc_poly(draw, brow_pts, width=2, fill=0)

    # Naricita triangular redondeada
    ny = cy + 2
    nose_poly = [(cx - 4.5, ny - 2), (cx + 4.5, ny - 2), (cx, ny + 3)]
    draw.polygon(nose_poly, fill=0)
    draw_thick_arc_poly(draw, nose_poly + [nose_poly[0]], width=2, fill=0)

    # Boca
    if not roaring:
        # Sonrisa tranquila ":3"
        draw_thick_arc_poly(draw, [(cx, ny + 3), (cx, ny + 7)], width=2, fill=0)
        left_smile = cubic_bezier((cx - 6, ny + 6), (cx - 4, ny + 10), (cx - 1, ny + 9), (cx, ny + 7), steps=10)
        right_smile = cubic_bezier((cx, ny + 7), (cx + 1, ny + 9), (cx + 4, ny + 10), (cx + 6, ny + 6), steps=10)
        draw_thick_arc_poly(draw, left_smile, width=2, fill=0)
        draw_thick_arc_poly(draw, right_smile, width=2, fill=0)
    else:
        # Boca abierta rugiendo con expresión simpática
        mouth_box = [cx - 8, ny + 4, cx + 8, ny + 15]
        draw.ellipse(mouth_box, fill=0)
        # Lengüita blanca abajo dentro de la boca
        draw.ellipse([cx - 4.5, ny + 10, cx + 4.5, ny + 14.5], fill=255)
        # Dos pequeños colmillitos blancos arriba
        draw.polygon([(cx - 5, ny + 5), (cx - 3, ny + 5), (cx - 4, ny + 8)], fill=255)
        draw.polygon([(cx + 3, ny + 5), (cx + 5, ny + 5), (cx + 4, ny + 8)], fill=255)
        # Contorno limpio de la boca
        draw.ellipse(mouth_box, outline=0, width=2)

    # Bigotitos cortos a los costados de las mejillas
    for side in (-1, 1):
        wx = cx + side * 14
        wy = cy + 4
        draw_thick_arc_poly(draw, [(wx, wy - 1), (wx + side * 6, wy - 2)], width=2, fill=0)
        draw_thick_arc_poly(draw, [(wx, wy + 2), (wx + side * 6, wy + 3)], width=2, fill=0)


def draw_speech_bubble(draw: ImageDraw.ImageDraw, text: str = "¡GRRR!") -> None:
    """Dibuja la burbuja de diálogo arriba a la derecha con texto nítido."""
    # Caja redondeada en x=[106..165], y=[4..32]
    x0, y0, x1, y1 = 106, 4, 165, 32
    draw.rounded_rectangle([x0, y0, x1, y1], radius=9, fill=255, outline=0, width=STROKE)

    # Colita apuntando hacia el leoncito sin tocar la melena
    tail_poly = [(117, 30), (108, 40), (125, 31)]
    draw.polygon(tail_poly, fill=255)
    draw_thick_arc_poly(draw, [(117, 31), (108, 40), (125, 31)], width=STROKE, fill=0)
    # Limpiar línea interna de la unión de la cola con la burbuja
    draw.line([(118, 30), (124, 30)], fill=255, width=4)

    # Texto "¡GRRR!" centrado
    font = load_bold_font(12)
    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    tx = (x0 + x1) / 2 - tw / 2 - bbox[0]
    ty = (y0 + y1) / 2 - th / 2 - bbox[1]
    draw.text((tx, ty), text, fill=0, font=font)


def binarize_and_save(img: Image.Image, out_path: str) -> None:
    # Umbral estricto > 127 sin grises ni dithering
    bw = img.convert("L").point(lambda p: 255 if p > 127 else 0)
    unique_vals = set(bw.tobytes())
    assert unique_vals.issubset({0, 255}), f"Valores no binarios encontrados: {unique_vals}"
    assert bw.size == (WIDTH, HEIGHT), f"Tamaño incorrecto: {bw.size}"
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    bw.save(out_path, format="PNG")
    print(f"Guardado {out_path} ({bw.size[0]}x{bw.size[1]}), valores={sorted(unique_vals)}")


def render_leon(roaring: bool, out_path: str) -> None:
    img = Image.new("L", (WIDTH, HEIGHT), 255)
    draw = ImageDraw.Draw(img)

    # Centrado ligeramente a la izquierda para dejar espacio cómodo a la burbuja en Frame B
    cx = 74
    cy = 48

    draw_tail(draw, cx, raised=roaring)
    draw_body_and_legs(draw, cx)
    draw_mane_and_ears(draw, cx, cy, bristled=roaring)
    draw_face(draw, cx, cy, roaring=roaring)

    if roaring:
        draw_speech_bubble(draw, "¡GRRR!")

    binarize_and_save(img, out_path)


def main() -> None:
    render_leon(roaring=False, out_path=os.path.join(OUT_DIR, "leon_a.png"))
    render_leon(roaring=True, out_path=os.path.join(OUT_DIR, "leon_b.png"))


if __name__ == "__main__":
    main()
