"""Generador de sprites 1-bit (168x128) para el gatito (gato_a.png y gato_b.png)."""

from pathlib import Path
from PIL import Image, ImageDraw

WIDTH = 168
HEIGHT = 128
SCALE = 4  # Supersampling 4x para curvas suaves antes de binarizar

W = WIDTH * SCALE
H = HEIGHT * SCALE
STROKE = 3.5 * SCALE  # ~3.5 px en resolución final
THIN_STROKE = 2.5 * SCALE


def pt(x: float, y: float) -> tuple[float, float]:
    return (x * SCALE, y * SCALE)


def draw_thick_curve(draw: ImageDraw.ImageDraw, points: list[tuple[float, float]], width: float = STROKE, fill: int = 0) -> None:
    """Dibuja una polilínea suave con extremos redondeados."""
    scaled = [pt(x, y) for x, y in points]
    draw.line(scaled, fill=fill, width=int(round(width)), joint="curve")
    r = width / 2.0
    for sx, sy in (scaled[0], scaled[-1]):
        draw.ellipse((sx - r, sy - r, sx + r, sy + r), fill=fill)


def quadratic_bezier(p0: tuple[float, float], p1: tuple[float, float], p2: tuple[float, float], steps: int = 24) -> list[tuple[float, float]]:
    pts = []
    for i in range(steps + 1):
        t = i / steps
        u = 1 - t
        x = u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0]
        y = u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]
        pts.append((x, y))
    return pts


def cubic_bezier(p0: tuple[float, float], p1: tuple[float, float], p2: tuple[float, float], p3: tuple[float, float], steps: int = 32) -> list[tuple[float, float]]:
    pts = []
    for i in range(steps + 1):
        t = i / steps
        u = 1 - t
        x = (u**3) * p0[0] + 3 * (u**2) * t * p1[0] + 3 * u * (t**2) * p2[0] + (t**3) * p3[0]
        y = (u**3) * p0[1] + 3 * (u**2) * t * p1[1] + 3 * u * (t**2) * p2[1] + (t**3) * p3[1]
        pts.append((x, y))
    return pts


def draw_filled_closed_curve(draw: ImageDraw.ImageDraw, points: list[tuple[float, float]], fill: int = 255, outline: int = 0, width: float = STROKE) -> None:
    scaled = [pt(x, y) for x, y in points]
    if fill is not None:
        draw.polygon(scaled, fill=fill)
    if outline is not None:
        draw.line(scaled + [scaled[0]], fill=outline, width=int(round(width)), joint="curve")
        r = width / 2.0
        for sx, sy in scaled:
            draw.ellipse((sx - r * 0.4, sy - r * 0.4, sx + r * 0.4, sy + r * 0.4), fill=outline)


def draw_sitting_paw(draw: ImageDraw.ImageDraw, x_center: float, y_top: float = 86.0, y_bottom: float = 116.0) -> None:
    """Dibuja una patita delantera apoyada que nace suavemente del pecho (sin línea superior)."""
    # Relleno blanco para tapar la línea inferior del cuerpo
    paw_fill = [
        pt(x_center - 5.5, y_top),
        pt(x_center + 5.5, y_top),
        pt(x_center + 6.0, y_bottom - 3),
        pt(x_center + 4.0, y_bottom),
        pt(x_center - 4.0, y_bottom),
        pt(x_center - 6.0, y_bottom - 3),
    ]
    draw.polygon(paw_fill, fill=255)
    # Contorno en U suave (abierto arriba)
    paw_outline = (
        [(x_center - 5.2, y_top)]
        + quadratic_bezier((x_center - 5.2, y_top), (x_center - 6.2, y_bottom - 2), (x_center - 3.5, y_bottom))
        + [(x_center + 3.5, y_bottom)]
        + quadratic_bezier((x_center + 3.5, y_bottom), (x_center + 6.2, y_bottom - 2), (x_center + 5.2, y_top))
    )
    draw_thick_curve(draw, paw_outline, width=3.0 * SCALE, fill=0)
    # Deditos
    draw_thick_curve(draw, [(x_center - 1.8, y_bottom - 4.5), (x_center - 1.8, y_bottom)], width=2.0 * SCALE, fill=0)
    draw_thick_curve(draw, [(x_center + 1.8, y_bottom - 4.5), (x_center + 1.8, y_bottom)], width=2.0 * SCALE, fill=0)


def draw_cat_base(draw: ImageDraw.ImageDraw, cx: float = 72.0, waving_left_paw: bool = False) -> None:
    """Dibuja la base corporal compartida: cuerpo sentado, cabeza, orejas, nariz, bigotes y patitas."""
    # --- Cuerpo sentado ---
    body_pts = (
        cubic_bezier((cx - 17, 68), (cx - 26, 86), (cx - 25, 114), (cx - 12, 116))
        + [(cx + 12, 116)]
        + cubic_bezier((cx + 12, 116), (cx + 25, 114), (cx + 26, 86), (cx + 17, 68))
    )
    draw_filled_closed_curve(draw, body_pts, fill=255, outline=0, width=STROKE)

    # Línea de suelo sutil bajo el gatito
    draw_thick_curve(draw, [(cx - 32, 117), (cx + 34, 117)], width=2.5 * SCALE, fill=0)

    # Patita delantera derecha (del espectador) siempre apoyada
    draw_sitting_paw(draw, x_center=cx + 7.5, y_top=86.0, y_bottom=116.0)

    if not waving_left_paw:
        # Patita delantera izquierda apoyada (Frame A)
        draw_sitting_paw(draw, x_center=cx - 7.5, y_top=86.0, y_bottom=116.0)
    else:
        # Patita delantera izquierda levantada saludando hacia la izquierda (Frame B)
        # Así queda totalmente libre sin cruzarse con la cola de la derecha
        waving_fill = (
            cubic_bezier((cx - 13, 88), (cx - 24, 88), (cx - 33, 80), (cx - 31, 72))
            + cubic_bezier((cx - 31, 72), (cx - 29, 66), (cx - 20, 70), (cx - 15, 78))
        )
        draw.polygon([pt(x, y) for x, y in waving_fill], fill=255)
        waving_outline = (
            cubic_bezier((cx - 13, 88), (cx - 24, 88), (cx - 33, 80), (cx - 31, 72))
            + cubic_bezier((cx - 31, 72), (cx - 29, 66), (cx - 20, 70), (cx - 15, 78))
        )
        draw_thick_curve(draw, waving_outline, width=3.0 * SCALE, fill=0)
        # Deditos saludando
        draw_thick_curve(draw, [(cx - 26, 70), (cx - 24.5, 74)], width=2.0 * SCALE, fill=0)
        draw_thick_curve(draw, [(cx - 29, 73), (cx - 27, 76.5)], width=2.0 * SCALE, fill=0)

    # --- Cabeza y orejas ---
    # Oreja izquierda
    left_ear = (
        quadratic_bezier((cx - 24, 42), (cx - 27, 20), (cx - 22, 15))
        + quadratic_bezier((cx - 22, 15), (cx - 16, 14), (cx - 7, 31))
    )
    draw_filled_closed_curve(draw, left_ear, fill=255, outline=0, width=STROKE)
    left_ear_in = [
        (cx - 20, 36),
        (cx - 20, 21),
        (cx - 11, 31),
    ]
    draw_filled_closed_curve(draw, left_ear_in, fill=255, outline=0, width=THIN_STROKE)

    # Oreja derecha
    right_ear = (
        quadratic_bezier((cx + 7, 31), (cx + 16, 14), (cx + 22, 15))
        + quadratic_bezier((cx + 22, 15), (cx + 27, 20), (cx + 24, 42))
    )
    draw_filled_closed_curve(draw, right_ear, fill=255, outline=0, width=STROKE)
    right_ear_in = [
        (cx + 11, 31),
        (cx + 20, 21),
        (cx + 20, 36),
    ]
    draw_filled_closed_curve(draw, right_ear_in, fill=255, outline=0, width=THIN_STROKE)

    # Cabeza ovalada tierna
    hx0, hy0 = pt(cx - 27, 28)
    hx1, hy1 = pt(cx + 27, 72)
    draw.ellipse((hx0, hy0, hx1, hy1), fill=255, outline=0, width=int(round(STROKE)))

    # --- Nariz en triangulito ---
    nose_pts = [
        pt(cx - 3.5, 54),
        pt(cx + 3.5, 54),
        pt(cx, 58.5),
    ]
    draw.polygon(nose_pts, fill=0)

    # --- Bigotes (3 a cada lado) ---
    draw_thick_curve(draw, quadratic_bezier((cx - 19, 53), (cx - 28, 51), (cx - 36, 49)), width=THIN_STROKE, fill=0)
    draw_thick_curve(draw, quadratic_bezier((cx - 20, 57), (cx - 29, 57), (cx - 37, 57)), width=THIN_STROKE, fill=0)
    draw_thick_curve(draw, quadratic_bezier((cx - 19, 61), (cx - 28, 63), (cx - 35, 65)), width=THIN_STROKE, fill=0)

    draw_thick_curve(draw, quadratic_bezier((cx + 19, 53), (cx + 28, 51), (cx + 36, 49)), width=THIN_STROKE, fill=0)
    draw_thick_curve(draw, quadratic_bezier((cx + 20, 57), (cx + 29, 57), (cx + 37, 57)), width=THIN_STROKE, fill=0)
    draw_thick_curve(draw, quadratic_bezier((cx + 19, 61), (cx + 28, 63), (cx + 35, 65)), width=THIN_STROKE, fill=0)


def draw_frame_a() -> Image.Image:
    img = Image.new("L", (W, H), 255)
    draw = ImageDraw.Draw(img)
    cx = 72.0  # Mismo centro en ambos frames para animación estable

    # Colita enroscada (Frame A)
    tail_pts = (
        cubic_bezier((cx + 18, 108), (cx + 38, 111), (cx + 46, 90), (cx + 34, 83))
        + cubic_bezier((cx + 34, 83), (cx + 25, 78), (cx + 22, 88), (cx + 30, 92))
    )
    draw_thick_curve(draw, tail_pts, width=9.5 * SCALE, fill=0)
    draw_thick_curve(draw, tail_pts, width=3.5 * SCALE, fill=255)

    draw_cat_base(draw, cx=cx, waving_left_paw=False)

    # Ojitos grandes con brillo (Frame A)
    for ex in (cx - 11.5, cx + 11.5):
        ey = 46.5
        r = 5.5
        draw.ellipse(pt(ex - r, ey - r) + pt(ex + r, ey + r), fill=0)
        # Brillo principal arriba-izquierda
        draw.ellipse(pt(ex - 3.2, ey - 3.4) + pt(ex - 0.2, ey - 0.4), fill=255)
        # Brillo secundario abajo-derecha
        draw.ellipse(pt(ex + 1.2, ey + 1.2) + pt(ex + 2.8, ey + 2.8), fill=255)

    # Boquita tierna (:3)
    draw_thick_curve(draw, quadratic_bezier((cx - 6, 60), (cx - 3, 64), (cx, 59)), width=THIN_STROKE, fill=0)
    draw_thick_curve(draw, quadratic_bezier((cx, 59), (cx + 3, 64), (cx + 6, 60)), width=THIN_STROKE, fill=0)

    return finalize_1bit(img)


def draw_miau_text(draw: ImageDraw.ImageDraw, x0: float, y0: float) -> None:
    """Dibuja '¡MIAU!' con trazos vectoriales limpios y nítidos."""
    w = 2.4 * SCALE
    # '¡'
    draw.ellipse(pt(x0 + 0.3, y0 + 1) + pt(x0 + 2.8, y0 + 3.5), fill=0)
    draw_thick_curve(draw, [(x0 + 1.5, y0 + 5.5), (x0 + 1.5, y0 + 12)], width=w, fill=0)

    # 'M'
    mx = x0 + 6.0
    draw_thick_curve(draw, [(mx, y0 + 12), (mx, y0 + 1), (mx + 4.5, y0 + 7), (mx + 9, y0 + 1), (mx + 9, y0 + 12)], width=w, fill=0)

    # 'I' (con buen espacio respecto a M y A)
    ix = x0 + 19.8
    draw_thick_curve(draw, [(ix, y0 + 1), (ix, y0 + 12)], width=w, fill=0)

    # 'A'
    ax = x0 + 24.2
    draw_thick_curve(draw, [(ax, y0 + 12), (ax + 4, y0 + 1), (ax + 8, y0 + 12)], width=w, fill=0)
    draw_thick_curve(draw, [(ax + 1.5, y0 + 8.5), (ax + 6.5, y0 + 8.5)], width=w * 0.9, fill=0)

    # 'U'
    ux = x0 + 36.2
    draw_thick_curve(draw, [(ux, y0 + 1), (ux, y0 + 9)] + quadratic_bezier((ux, y0 + 9), (ux + 3.5, y0 + 13), (ux + 7, y0 + 9)) + [(ux + 7, y0 + 1)], width=w, fill=0)

    # '!'
    ex = x0 + 47.5
    draw_thick_curve(draw, [(ex, y0 + 1), (ex, y0 + 7.5)], width=w, fill=0)
    draw.ellipse(pt(ex - 1.3, y0 + 9.8) + pt(ex + 1.3, y0 + 12.4), fill=0)


def draw_frame_b() -> Image.Image:
    img = Image.new("L", (W, H), 255)
    draw = ImageDraw.Draw(img)
    cx = 72.0  # Mismo centro que Frame A

    # Colita erguida en S (Frame B) totalmente libre a la derecha
    tail_s = (
        cubic_bezier((cx + 18, 108), (cx + 35, 106), (cx + 42, 91), (cx + 33, 80))
        + cubic_bezier((cx + 33, 80), (cx + 24, 69), (cx + 34, 56), (cx + 46, 54))
    )
    draw_thick_curve(draw, tail_s, width=9.5 * SCALE, fill=0)
    draw_thick_curve(draw, tail_s, width=3.5 * SCALE, fill=255)

    draw_cat_base(draw, cx=cx, waving_left_paw=True)

    # Ojitos arqueados felices ^ ^
    for ex in (cx - 11.5, cx + 11.5):
        ey = 48.0
        draw_thick_curve(draw, quadratic_bezier((ex - 5.5, ey), (ex, ey - 8.5), (ex + 5.5, ey)), width=3.2 * SCALE, fill=0)

    # Boquita abierta maullando
    mouth_pts = (
        [(cx - 5, 60), (cx + 5, 60)]
        + quadratic_bezier((cx + 5, 60), (cx + 5, 68), (cx, 68))
        + quadratic_bezier((cx, 68), (cx - 5, 68), (cx - 5, 60))
    )
    draw_filled_closed_curve(draw, mouth_pts, fill=0, outline=0, width=2.0 * SCALE)
    # Lengüita blanca abajo dentro de la boca
    draw.ellipse(pt(cx - 2.5, 64.5) + pt(cx + 2.5, 67.2), fill=255)

    # --- Burbuja de diálogo arriba a la derecha: "¡MIAU!" ---
    bx0, by0, bx1, by1 = 102.0, 5.0, 164.0, 33.0
    tail_bubble = [pt(113, 29), pt(99, 43), pt(123, 31)]
    draw.polygon(tail_bubble, fill=255)
    draw.rounded_rectangle(pt(bx0, by0) + pt(bx1, by1), radius=10 * SCALE, fill=255, outline=0, width=int(round(3.0 * SCALE)))
    draw.polygon(tail_bubble, fill=255)
    draw_thick_curve(draw, [(112, 32), (99, 43), (122, 32)], width=3.0 * SCALE, fill=0)
    draw.rounded_rectangle(pt(bx0 + 3, by0 + 3) + pt(bx1 - 3, by1 - 3), radius=8 * SCALE, fill=255)

    draw_miau_text(draw, x0=108.5, y0=12.5)

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

    path_a = out_dir / "gato_a.png"
    path_b = out_dir / "gato_b.png"
    frame_a.save(path_a)
    frame_b.save(path_b)
    print(f"Guardados: {path_a} ({frame_a.size}, colors={set(frame_a.get_flattened_data())})")
    print(f"Guardados: {path_b} ({frame_b.size}, colors={set(frame_b.get_flattened_data())})")


if __name__ == "__main__":
    main()
