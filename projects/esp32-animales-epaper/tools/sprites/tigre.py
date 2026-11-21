"""Generador de sprites 1-bit (168x128) para el tigre (tigre_a.png y tigre_b.png)."""

from pathlib import Path
from PIL import Image, ImageDraw

WIDTH = 168
HEIGHT = 128
SCALE = 4  # Supersampling 4x para curvas suaves antes de binarizar

W = WIDTH * SCALE
H = HEIGHT * SCALE
STROKE = 3.5 * SCALE
THIN_STROKE = 2.5 * SCALE


def pt(x: float, y: float) -> tuple[float, float]:
    return (x * SCALE, y * SCALE)


def draw_thick_curve(
    draw: ImageDraw.ImageDraw,
    points: list[tuple[float, float]],
    width: float = STROKE,
    fill: int = 0,
) -> None:
    """Dibuja una polilínea suave con extremos redondeados."""
    scaled = [pt(x, y) for x, y in points]
    draw.line(scaled, fill=fill, width=int(round(width)), joint="curve")
    r = width / 2.0
    for sx, sy in (scaled[0], scaled[-1]):
        draw.ellipse((sx - r, sy - r, sx + r, sy + r), fill=fill)


def quadratic_bezier(
    p0: tuple[float, float],
    p1: tuple[float, float],
    p2: tuple[float, float],
    steps: int = 24,
) -> list[tuple[float, float]]:
    pts = []
    for i in range(steps + 1):
        t = i / steps
        u = 1 - t
        x = u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0]
        y = u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]
        pts.append((x, y))
    return pts


def cubic_bezier(
    p0: tuple[float, float],
    p1: tuple[float, float],
    p2: tuple[float, float],
    p3: tuple[float, float],
    steps: int = 32,
) -> list[tuple[float, float]]:
    pts = []
    for i in range(steps + 1):
        t = i / steps
        u = 1 - t
        x = (u**3) * p0[0] + 3 * (u**2) * t * p1[0] + 3 * u * (t**2) * p2[0] + (t**3) * p3[0]
        y = (u**3) * p0[1] + 3 * (u**2) * t * p1[1] + 3 * u * (t**2) * p2[1] + (t**3) * p3[1]
        pts.append((x, y))
    return pts


def draw_filled_closed_curve(
    draw: ImageDraw.ImageDraw,
    points: list[tuple[float, float]],
    fill: int | None = 255,
    outline: int | None = 0,
    width: float = STROKE,
) -> None:
    scaled = [pt(x, y) for x, y in points]
    if fill is not None:
        draw.polygon(scaled, fill=fill)
    if outline is not None:
        draw.line(scaled + [scaled[0]], fill=outline, width=int(round(width)), joint="curve")
        r = width / 2.0
        for sx, sy in scaled:
            draw.ellipse((sx - r * 0.4, sy - r * 0.4, sx + r * 0.4, sy + r * 0.4), fill=outline)


def draw_stripe(
    draw: ImageDraw.ImageDraw,
    p0: tuple[float, float],
    p1: tuple[float, float],
    p2: tuple[float, float],
    half_width: float = 2.4,
) -> None:
    """Dibuja una raya de tigre gruesa, bien marcada y afilada hacia la punta interior."""
    curve = quadratic_bezier(p0, p1, p2, steps=16)
    left_pts = []
    right_pts = []
    n = len(curve) - 1
    for i, (x, y) in enumerate(curve):
        t = i / n
        w = half_width * (1.0 - 0.85 * (t**1.15))
        if i < n:
            dx = curve[i + 1][0] - x
            dy = curve[i + 1][1] - y
        else:
            dx = x - curve[i - 1][0]
            dy = y - curve[i - 1][1]
        length = (dx * dx + dy * dy) ** 0.5 or 1.0
        nx, ny = -dy / length, dx / length
        left_pts.append((x + nx * w, y + ny * w))
        right_pts.append((x - nx * w, y - ny * w))
    poly = [pt(x, y) for x, y in left_pts + list(reversed(right_pts))]
    draw.polygon(poly, fill=0)


def draw_tiger_paw_sitting(
    draw: ImageDraw.ImageDraw,
    x_center: float,
    y_top: float = 84.0,
    y_bottom: float = 117.0,
    side: int = -1,
) -> None:
    """Dibuja una pata delantera gruesa y robusta de tigre con rayas transversales."""
    paw_fill = [
        pt(x_center - 7.5, y_top),
        pt(x_center + 7.5, y_top),
        pt(x_center + 8.5, y_bottom - 3),
        pt(x_center + 5.5, y_bottom),
        pt(x_center - 5.5, y_bottom),
        pt(x_center - 8.5, y_bottom - 3),
    ]
    draw.polygon(paw_fill, fill=255)
    paw_outline = (
        [(x_center - 7.2, y_top)]
        + quadratic_bezier((x_center - 7.2, y_top), (x_center - 8.8, y_bottom - 2), (x_center - 5.2, y_bottom))
        + [(x_center + 5.2, y_bottom)]
        + quadratic_bezier((x_center + 5.2, y_bottom), (x_center + 8.8, y_bottom - 2), (x_center + 7.2, y_top))
    )
    draw_thick_curve(draw, paw_outline, width=3.2 * SCALE, fill=0)

    # Rayitas transversales en el lado exterior de la pata
    if side < 0:
        draw_stripe(draw, (x_center - 7.0, y_top + 8), (x_center - 3.5, y_top + 8.5), (x_center - 0.5, y_top + 9.5), half_width=1.6)
        draw_stripe(draw, (x_center - 7.2, y_top + 16), (x_center - 3.5, y_top + 16.5), (x_center - 0.5, y_top + 17.0), half_width=1.6)
    else:
        draw_stripe(draw, (x_center + 7.0, y_top + 8), (x_center + 3.5, y_top + 8.5), (x_center + 0.5, y_top + 9.5), half_width=1.6)
        draw_stripe(draw, (x_center + 7.2, y_top + 16), (x_center + 3.5, y_top + 16.5), (x_center + 0.5, y_top + 17.0), half_width=1.6)

    # Deditos robustos
    draw_thick_curve(draw, [(x_center - 2.6, y_bottom - 5.0), (x_center - 2.6, y_bottom)], width=2.2 * SCALE, fill=0)
    draw_thick_curve(draw, [(x_center + 2.6, y_bottom - 5.0), (x_center + 2.6, y_bottom)], width=2.2 * SCALE, fill=0)


def draw_striped_tail(draw: ImageDraw.ImageDraw, tail_pts: list[tuple[float, float]]) -> None:
    """Dibuja una cola gruesa de tigre con anillos negros marcados y punta negra."""
    draw_thick_curve(draw, tail_pts, width=11.0 * SCALE, fill=0)
    draw_thick_curve(draw, tail_pts, width=4.8 * SCALE, fill=255)

    n = len(tail_pts)
    for idx in (int(n * 0.20), int(n * 0.42), int(n * 0.64), int(n * 0.84)):
        p = tail_pts[idx]
        p_prev = tail_pts[max(0, idx - 1)]
        p_next = tail_pts[min(n - 1, idx + 1)]
        dx = p_next[0] - p_prev[0]
        dy = p_next[1] - p_prev[1]
        length = (dx * dx + dy * dy) ** 0.5 or 1.0
        nx, ny = -dy / length, dx / length
        r = 3.8
        draw_thick_curve(
            draw,
            [(p[0] - nx * r, p[1] - ny * r), (p[0] + nx * r, p[1] + ny * r)],
            width=3.2 * SCALE,
            fill=0,
        )

    tx, ty = tail_pts[-1]
    tr = 4.5 * SCALE
    draw.ellipse((tx * SCALE - tr, ty * SCALE - tr, tx * SCALE + tr, ty * SCALE + tr), fill=0)


def draw_tiger_base(draw: ImageDraw.ImageDraw, cx: float = 70.0, raised_paw: bool = False) -> None:
    """Base corporal exacta compartida entre Frame A y B: cuerpo robusto, patas gruesas, cabeza ancha con patillas, orejas cortas redondas y rayas gruesas."""
    # --- Cuerpo robusto de tigre ---
    body_pts = (
        cubic_bezier((cx - 22, 69), (cx - 33, 86), (cx - 31, 115), (cx - 15, 117))
        + [(cx + 15, 117)]
        + cubic_bezier((cx + 15, 117), (cx + 31, 115), (cx + 33, 86), (cx + 22, 69))
    )
    draw_filled_closed_curve(draw, body_pts, fill=255, outline=0, width=STROKE)

    # Rayas transversales gruesas en los costados del cuerpo
    draw_stripe(draw, (cx - 27.5, 81), (cx - 21.5, 83), (cx - 16.5, 85), half_width=2.3)
    draw_stripe(draw, (cx - 29.5, 93), (cx - 23.0, 95), (cx - 17.0, 96.5), half_width=2.5)
    draw_stripe(draw, (cx - 27.5, 105), (cx - 21.5, 106.5), (cx - 16.5, 107.5), half_width=2.2)

    draw_stripe(draw, (cx + 27.5, 81), (cx + 21.5, 83), (cx + 16.5, 85), half_width=2.3)
    draw_stripe(draw, (cx + 29.5, 93), (cx + 23.0, 95), (cx + 17.0, 96.5), half_width=2.5)
    draw_stripe(draw, (cx + 27.5, 105), (cx + 21.5, 106.5), (cx + 16.5, 107.5), half_width=2.2)

    # Línea de suelo
    draw_thick_curve(draw, [(cx - 36, 118), (cx + 38, 118)], width=2.5 * SCALE, fill=0)

    # Pata delantera derecha siempre apoyada
    draw_tiger_paw_sitting(draw, x_center=cx + 11.5, y_top=84.0, y_bottom=117.0, side=1)

    if not raised_paw:
        # Pata delantera izquierda apoyada (Frame A)
        draw_tiger_paw_sitting(draw, x_center=cx - 11.5, y_top=84.0, y_bottom=117.0, side=-1)
    else:
        # Pata delantera izquierda levantada con garritas suaves y rayas (Frame B)
        waving_fill = (
            cubic_bezier((cx - 16, 90), (cx - 28, 90), (cx - 39, 82), (cx - 37, 72))
            + cubic_bezier((cx - 37, 72), (cx - 35, 65), (cx - 24, 69), (cx - 18, 79))
        )
        draw.polygon([pt(x, y) for x, y in waving_fill], fill=255)
        waving_outline = (
            cubic_bezier((cx - 16, 90), (cx - 28, 90), (cx - 39, 82), (cx - 37, 72))
            + cubic_bezier((cx - 37, 72), (cx - 35, 65), (cx - 24, 69), (cx - 18, 79))
        )
        draw_thick_curve(draw, waving_outline, width=3.2 * SCALE, fill=0)
        # Rayitas en la pata levantada
        draw_stripe(draw, (cx - 24, 87.5), (cx - 23.5, 83.0), (cx - 22.5, 80.0), half_width=1.5)
        draw_stripe(draw, (cx - 30, 85.0), (cx - 28.5, 80.5), (cx - 27.0, 77.5), half_width=1.5)
        # Deditos
        draw_thick_curve(draw, [(cx - 31.5, 70.0), (cx - 29.8, 74.5)], width=2.2 * SCALE, fill=0)
        draw_thick_curve(draw, [(cx - 34.8, 73.2), (cx - 32.5, 77.0)], width=2.2 * SCALE, fill=0)
        # Garritas suaves de cachorro de tigre
        for gx, gy, tx, ty in [
            (cx - 32.8, 67.5, cx - 34.2, 64.5),
            (cx - 36.2, 70.2, cx - 38.8, 68.2),
            (cx - 37.8, 74.8, cx - 40.6, 74.2),
        ]:
            draw_thick_curve(draw, [(gx, gy), (tx, ty)], width=2.0 * SCALE, fill=0)

    # --- Orejas CORTAS Y REDONDAS a los costados de la cabeza ---
    for sign in (-1, 1):
        ex = cx + sign * 25.0
        ey = 28.5
        r_ear = 7.8
        draw.ellipse(
            pt(ex - r_ear, ey - r_ear) + pt(ex + r_ear, ey + r_ear),
            fill=255,
            outline=0,
            width=int(round(STROKE)),
        )
        # Interior corto oscuro en arco
        draw.ellipse(
            pt(ex - 3.2, ey - 2.6) + pt(ex + 3.2, ey + 3.4),
            fill=0,
        )

    # --- Cabeza ancha y robusta con mechones/patillas peludas en las mejillas ---
    head_pts = (
        # Frente ancha
        cubic_bezier((cx - 26, 26), (cx - 12, 20), (cx + 12, 20), (cx + 26, 26))
        # Lado derecho bajando a patillas peludas
        + quadratic_bezier((cx + 26, 26), (cx + 34, 34), (cx + 34, 46))
        + [(cx + 38, 49), (cx + 33, 53), (cx + 36, 57), (cx + 30, 61)]
        # Mandíbula / mentón ancho
        + cubic_bezier((cx + 30, 61), (cx + 24, 73), (cx + 12, 75), (cx, 75))
        + cubic_bezier((cx, 75), (cx - 12, 75), (cx - 24, 73), (cx - 30, 61))
        # Lado izquierdo con patillas peludas subiendo
        + [(cx - 36, 57), (cx - 33, 53), (cx - 38, 49), (cx - 34, 46)]
        + quadratic_bezier((cx - 34, 46), (cx - 34, 34), (cx - 26, 26))
    )
    draw_filled_closed_curve(draw, head_pts, fill=255, outline=0, width=STROKE)

    # --- 3 rayas negras gruesas en la frente ---
    draw_stripe(draw, (cx, 22.5), (cx, 29.0), (cx, 35.5), half_width=2.4)
    draw_stripe(draw, (cx - 9.0, 23.5), (cx - 7.5, 29.0), (cx - 6.0, 34.5), half_width=2.2)
    draw_stripe(draw, (cx + 9.0, 23.5), (cx + 7.5, 29.0), (cx + 6.0, 34.5), half_width=2.2)

    # --- Rayas gruesas en las mejillas (sin bigotes finos de gatito) ---
    draw_stripe(draw, (cx - 32.5, 45.0), (cx - 25.5, 46.5), (cx - 19.5, 48.0), half_width=2.2)
    draw_stripe(draw, (cx - 33.5, 53.0), (cx - 25.5, 54.0), (cx - 19.0, 55.0), half_width=2.3)
    draw_stripe(draw, (cx - 29.5, 61.0), (cx - 23.0, 61.0), (cx - 18.0, 61.0), half_width=2.0)

    draw_stripe(draw, (cx + 32.5, 45.0), (cx + 25.5, 46.5), (cx + 19.5, 48.0), half_width=2.2)
    draw_stripe(draw, (cx + 33.5, 53.0), (cx + 25.5, 54.0), (cx + 19.0, 55.0), half_width=2.3)
    draw_stripe(draw, (cx + 29.5, 61.0), (cx + 23.0, 61.0), (cx + 18.0, 61.0), half_width=2.0)

    # --- Ojitos firmes y amigables de tigre (idénticos en ambos frames) ---
    for ex in (cx - 12.0, cx + 12.0):
        ey = 43.0
        r = 4.8
        draw.ellipse(pt(ex - r, ey - r) + pt(ex + r, ey + r), fill=0)
        draw.ellipse(pt(ex - 2.6, ey - 2.8) + pt(ex - 0.1, ey - 0.3), fill=255)
        draw.ellipse(pt(ex + 1.0, ey + 1.0) + pt(ex + 2.3, ey + 2.3), fill=255)
        # Cejita corta expresiva arriba del ojo (simétrica)
        draw_thick_curve(
            draw,
            quadratic_bezier((ex - 4.5, ey - 7.0), (ex, ey - 8.5), (ex + 4.5, ey - 7.0)),
            width=2.0 * SCALE,
            fill=0,
        )

    # --- Hocico ancho de tigre + nariz ancha triangular ---
    # Línea sutil del puente del hocico
    nose_pts = [
        pt(cx - 5.8, 51.0),
        pt(cx + 5.8, 51.0),
        pt(cx, 56.5),
    ]
    draw.polygon(nose_pts, fill=0)
    # Borde redondeado superior de la nariz
    draw_thick_curve(draw, [(cx - 5.2, 51.2), (cx + 5.2, 51.2)], width=2.4 * SCALE, fill=0)


def draw_frame_a() -> Image.Image:
    img = Image.new("L", (W, H), 255)
    draw = ImageDraw.Draw(img)
    cx = 70.0

    # Colita gruesa de tigre en reposo abajo a la derecha (Frame A)
    tail_pts = (
        cubic_bezier((cx + 22, 108), (cx + 44, 111), (cx + 52, 90), (cx + 40, 82))
        + cubic_bezier((cx + 40, 82), (cx + 31, 77), (cx + 28, 87), (cx + 35, 91))
    )
    draw_striped_tail(draw, tail_pts)

    draw_tiger_base(draw, cx=cx, raised_paw=False)

    # Hocico ancho con boca tranquila en Frame A
    draw_thick_curve(draw, [(cx, 56.0), (cx, 60.5)], width=2.5 * SCALE, fill=0)
    draw_thick_curve(draw, quadratic_bezier((cx - 8.5, 61.0), (cx - 4.0, 66.0), (cx, 60.5)), width=2.8 * SCALE, fill=0)
    draw_thick_curve(draw, quadratic_bezier((cx, 60.5), (cx + 4.0, 66.0), (cx + 8.5, 61.0)), width=2.8 * SCALE, fill=0)
    # Puntitos de hocico de tigre (no bigotes finos)
    for px, py in [
        (cx - 8.5, 57.5), (cx - 11.0, 59.0),
        (cx + 8.5, 57.5), (cx + 11.0, 59.0),
    ]:
        draw.ellipse(pt(px - 1.1, py - 1.1) + pt(px + 1.1, py + 1.1), fill=0)

    return finalize_1bit(img)


def draw_roar_text(draw: ImageDraw.ImageDraw, x0: float, y0: float) -> None:
    """Dibuja '¡ROAR!' con trazos vectoriales gruesos y nítidos."""
    w = 2.4 * SCALE
    # '¡'
    draw.ellipse(pt(x0 + 0.3, y0 + 1.0) + pt(x0 + 2.7, y0 + 3.4), fill=0)
    draw_thick_curve(draw, [(x0 + 1.5, y0 + 5.5), (x0 + 1.5, y0 + 12.0)], width=w, fill=0)

    # 'R'
    rx = x0 + 6.5
    draw_thick_curve(draw, [(rx, y0 + 12.0), (rx, y0 + 1.0), (rx + 4.5, y0 + 1.0)], width=w, fill=0)
    draw_thick_curve(
        draw,
        quadratic_bezier((rx + 4.5, y0 + 1.0), (rx + 8.0, y0 + 3.8), (rx + 4.5, y0 + 6.5)) + [(rx, y0 + 6.5)],
        width=w,
        fill=0,
    )
    draw_thick_curve(draw, [(rx + 3.5, y0 + 6.5), (rx + 7.2, y0 + 12.0)], width=w, fill=0)

    # 'O'
    ox = x0 + 17.5
    draw.ellipse(pt(ox, y0 + 1.0) + pt(ox + 7.5, y0 + 12.0), outline=0, width=int(round(w)))

    # 'A'
    ax = x0 + 28.0
    draw_thick_curve(draw, [(ax, y0 + 12.0), (ax + 4.0, y0 + 1.0), (ax + 8.0, y0 + 12.0)], width=w, fill=0)
    draw_thick_curve(draw, [(ax + 1.5, y0 + 8.5), (ax + 6.5, y0 + 8.5)], width=w * 0.9, fill=0)

    # 'R'
    r2x = x0 + 39.0
    draw_thick_curve(draw, [(r2x, y0 + 12.0), (r2x, y0 + 1.0), (r2x + 4.5, y0 + 1.0)], width=w, fill=0)
    draw_thick_curve(
        draw,
        quadratic_bezier((r2x + 4.5, y0 + 1.0), (r2x + 8.0, y0 + 3.8), (r2x + 4.5, y0 + 6.5)) + [(r2x, y0 + 6.5)],
        width=w,
        fill=0,
    )
    draw_thick_curve(draw, [(r2x + 3.5, y0 + 6.5), (r2x + 7.2, y0 + 12.0)], width=w, fill=0)

    # '!'
    ex = x0 + 49.8
    draw_thick_curve(draw, [(ex, y0 + 1.0), (ex, y0 + 7.5)], width=w, fill=0)
    draw.ellipse(pt(ex - 1.3, y0 + 9.8) + pt(ex + 1.3, y0 + 12.4), fill=0)


def draw_frame_b() -> Image.Image:
    img = Image.new("L", (W, H), 255)
    draw = ImageDraw.Draw(img)
    cx = 70.0

    # Colita gruesa de tigre levantada en S (Frame B), debajo de la burbuja
    tail_s = (
        cubic_bezier((cx + 22, 108), (cx + 42, 106), (cx + 50, 90), (cx + 41, 77))
        + cubic_bezier((cx + 41, 77), (cx + 33, 66), (cx + 42, 54), (cx + 53, 51))
    )
    draw_striped_tail(draw, tail_s)

    draw_tiger_base(draw, cx=cx, raised_paw=True)

    # Boca abierta rugiendo simpático (misma cabeza y ojos que Frame A)
    mouth_pts = (
        [(cx - 9.0, 59.5), (cx + 9.0, 59.5)]
        + quadratic_bezier((cx + 9.0, 59.5), (cx + 9.0, 71.0), (cx, 71.0))
        + quadratic_bezier((cx, 71.0), (cx - 9.0, 71.0), (cx - 9.0, 59.5))
    )
    draw_filled_closed_curve(draw, mouth_pts, fill=0, outline=0, width=2.2 * SCALE)
    # Colmillitos simpáticos blancos arriba
    draw.polygon([pt(cx - 6.8, 59.5), pt(cx - 4.0, 59.5), pt(cx - 5.4, 63.2)], fill=255)
    draw.polygon([pt(cx + 4.0, 59.5), pt(cx + 6.8, 59.5), pt(cx + 5.4, 63.2)], fill=255)
    # Lengüita blanca abajo
    draw.ellipse(pt(cx - 4.2, 66.2) + pt(cx + 4.2, 70.2), fill=255)

    # --- Burbuja de diálogo arriba a la derecha: "¡ROAR!" ---
    bx0, by0, bx1, by1 = 100.0, 5.0, 164.0, 33.0
    tail_bubble = [pt(113, 29), pt(104, 38), pt(123, 31)]
    draw.polygon(tail_bubble, fill=255)
    draw.rounded_rectangle(
        pt(bx0, by0) + pt(bx1, by1),
        radius=10 * SCALE,
        fill=255,
        outline=0,
        width=int(round(3.0 * SCALE)),
    )
    draw.polygon(tail_bubble, fill=255)
    draw_thick_curve(draw, [(112, 32), (104, 38), (122, 32)], width=3.0 * SCALE, fill=0)
    draw.rounded_rectangle(pt(bx0 + 3, by0 + 3) + pt(bx1 - 3, by1 - 3), radius=8 * SCALE, fill=255)

    draw_roar_text(draw, x0=106.5, y0=12.5)

    return finalize_1bit(img)


def finalize_1bit(img: Image.Image) -> Image.Image:
    """Reduce a 168x128 y aplica umbral binario estricto >127 (solo 0 y 255)."""
    small = img.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    return small.point(lambda p: 255 if p > 127 else 0, mode="L")


def main() -> None:
    out_dir = Path(__file__).resolve().parents[2] / "assets" / "custom_sprites"
    out_dir.mkdir(parents=True, exist_ok=True)

    frame_a = draw_frame_a()
    frame_b = draw_frame_b()

    path_a = out_dir / "tigre_a.png"
    path_b = out_dir / "tigre_b.png"
    frame_a.save(path_a)
    frame_b.save(path_b)
    print(f"Guardados: {path_a} ({frame_a.size}, colors={set(frame_a.get_flattened_data())})")
    print(f"Guardados: {path_b} ({frame_b.size}, colors={set(frame_b.get_flattened_data())})")


if __name__ == "__main__":
    main()
