"""Generador de sprites 1-bit (168x128) para el monito (mono_a.png y mono_b.png)."""

from pathlib import Path
from PIL import Image, ImageDraw

WIDTH = 168
HEIGHT = 128
SCALE = 4  # Supersampling 4x para curvas suaves antes de binarizar

W = WIDTH * SCALE
H = HEIGHT * SCALE
STROKE = 3.5 * SCALE  # ~3.5 px en resolución final
THIN_STROKE = 2.4 * SCALE


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


def draw_sitting_arm(
    draw: ImageDraw.ImageDraw,
    x_shoulder: float,
    x_hand: float,
    y_top: float = 74.0,
    y_bottom: float = 113.0,
    side: int = 1,
) -> None:
    """Dibuja un bracito delantero apoyado suavemente hacia adelante/abajo."""
    # side = -1 para izquierdo, +1 para derecho
    outer = cubic_bezier(
        (x_shoulder, y_top),
        (x_shoulder + side * 6.0, y_top + 12.0),
        (x_hand + side * 5.5, y_bottom - 10.0),
        (x_hand + side * 3.0, y_bottom),
    )
    bottom = quadratic_bezier(
        (x_hand + side * 3.0, y_bottom),
        (x_hand, y_bottom + 2.5),
        (x_hand - side * 4.5, y_bottom),
    )
    inner = cubic_bezier(
        (x_hand - side * 4.5, y_bottom),
        (x_hand - side * 5.0, y_bottom - 10.0),
        (x_shoulder - side * 3.0, y_top + 14.0),
        (x_shoulder - side * 4.0, y_top + 4.0),
    )
    arm_poly = outer + bottom + inner
    draw.polygon([pt(x, y) for x, y in arm_poly], fill=255)
    draw_thick_curve(draw, outer + bottom + inner, width=3.0 * SCALE, fill=0)


def draw_foot(draw: ImageDraw.ImageDraw, x_center: float, y_center: float = 111.5) -> None:
    """Dibuja un piecito redondo simpático de monito sentado."""
    rx, ry = 8.5, 5.5
    draw.ellipse(
        pt(x_center - rx, y_center - ry) + pt(x_center + rx, y_center + ry),
        fill=255,
        outline=0,
        width=int(round(3.0 * SCALE)),
    )


def draw_monkey_base(
    draw: ImageDraw.ImageDraw,
    cx: float = 66.0,
    waving_left_arm: bool = False,
) -> None:
    """Dibuja la base corporal compartida: cuerpo sentado, piecitos, brazos, cabeza y orejas."""
    # --- Cuerpo sentado ---
    body_pts = (
        cubic_bezier((cx - 16, 65), (cx - 25, 84), (cx - 24, 112), (cx - 11, 115))
        + [(cx + 11, 115)]
        + cubic_bezier((cx + 11, 115), (cx + 24, 112), (cx + 25, 84), (cx + 16, 65))
    )
    draw_filled_closed_curve(draw, body_pts, fill=255, outline=0, width=STROKE)

    # Panzita suave en el pecho
    draw.ellipse(
        pt(cx - 12, 74) + pt(cx + 12, 106),
        fill=255,
        outline=0,
        width=int(round(2.2 * SCALE)),
    )

    # Línea de suelo sutil
    draw_thick_curve(draw, [(cx - 33, 117), (cx + 35, 117)], width=2.5 * SCALE, fill=0)

    # Piecitos sentados a izquierda y derecha
    draw_foot(draw, cx - 19.5, y_center=111.5)
    draw_foot(draw, cx + 19.5, y_center=111.5)

    # Bracito derecho siempre apoyado
    draw_sitting_arm(draw, x_shoulder=cx + 15.0, x_hand=cx + 10.0, side=1)

    if not waving_left_arm:
        # Bracito izquierdo apoyado (Frame A)
        draw_sitting_arm(draw, x_shoulder=cx - 15.0, x_hand=cx - 10.0, side=-1)
    else:
        # Bracito izquierdo levantado saludando bien afuera de la oreja (Frame B)
        waving_outer = cubic_bezier(
            (cx - 16, 79),
            (cx - 33, 82),
            (cx - 47, 73),
            (cx - 46, 59),
        )
        hand_arc = cubic_bezier(
            (cx - 46, 59),
            (cx - 45, 52),
            (cx - 36, 53),
            (cx - 36, 61),
        )
        waving_inner = cubic_bezier(
            (cx - 36, 61),
            (cx - 36, 69),
            (cx - 26, 72),
            (cx - 14, 70),
        )
        arm_poly = waving_outer + hand_arc + waving_inner
        draw.polygon([pt(x, y) for x, y in arm_poly], fill=255)
        draw_thick_curve(draw, arm_poly, width=3.0 * SCALE, fill=0)
        # Deditos saludando y rayitas de movimiento
        draw_thick_curve(draw, [(cx - 41.5, 55), (cx - 40.5, 59.5)], width=2.0 * SCALE, fill=0)
        draw_thick_curve(draw, [(cx - 38.0, 56), (cx - 37.8, 60.0)], width=2.0 * SCALE, fill=0)
        draw_thick_curve(draw, [(cx - 52, 54), (cx - 49, 57)], width=2.0 * SCALE, fill=0)
        draw_thick_curve(draw, [(cx - 53, 62), (cx - 50, 63)], width=2.0 * SCALE, fill=0)

    # --- Orejas redondas grandes (dibujadas antes de la cabeza) ---
    ear_r_outer = 11.5
    ear_r_inner = 6.0
    for side in (-1, 1):
        ex = cx + side * 24.5
        ey = 45.0
        draw.ellipse(
            pt(ex - ear_r_outer, ey - ear_r_outer) + pt(ex + ear_r_outer, ey + ear_r_outer),
            fill=255,
            outline=0,
            width=int(round(STROKE)),
        )
        # Detalle interior simétrico de la oreja
        draw.arc(
            pt(ex - ear_r_inner, ey - ear_r_inner) + pt(ex + ear_r_inner, ey + ear_r_inner),
            start=120 if side == -1 else 290,
            end=250 if side == -1 else 60,
            fill=0,
            width=int(round(THIN_STROKE)),
        )

    # --- Cabeza redonda ---
    hx0, hy0, hx1, hy1 = cx - 23.0, 26.0, cx + 23.0, 66.5
    draw.ellipse(
        pt(hx0, hy0) + pt(hx1, hy1),
        fill=255,
        outline=0,
        width=int(round(STROKE)),
    )

    # Mechoncito simpático arriba de la cabeza
    draw_thick_curve(
        draw,
        quadratic_bezier((cx - 3.5, 26.5), (cx - 1.0, 18.0), (cx + 2.5, 21.0)),
        width=2.8 * SCALE,
        fill=0,
    )
    draw_thick_curve(
        draw,
        quadratic_bezier((cx + 0.5, 26.0), (cx + 4.5, 19.5), (cx + 7.5, 23.5)),
        width=2.5 * SCALE,
        fill=0,
    )

    # Línea suave de carita de mono en la frente (arcos sobre los ojos)
    brow_left = quadratic_bezier((cx - 15.5, 42.0), (cx - 10.5, 31.5), (cx, 36.5))
    brow_right = quadratic_bezier((cx, 36.5), (cx + 10.5, 31.5), (cx + 15.5, 42.0))
    draw_thick_curve(draw, brow_left, width=2.0 * SCALE, fill=0)
    draw_thick_curve(draw, brow_right, width=2.0 * SCALE, fill=0)

    # Naricita (dos fositas ovaladas pequeñas y tiernas)
    for nx in (cx - 2.6, cx + 2.6):
        draw.ellipse(pt(nx - 1.3, 47.8) + pt(nx + 1.3, 50.2), fill=0)


def draw_frame_a() -> Image.Image:
    img = Image.new("L", (W, H), 255)
    draw = ImageDraw.Draw(img)
    cx = 66.0

    # --- Colita larga enroscada (Frame A) ---
    tail_a = (
        cubic_bezier((cx + 18, 105), (cx + 35, 107), (cx + 49, 102), (cx + 48, 88))
        + cubic_bezier((cx + 48, 88), (cx + 47, 77), (cx + 34, 78), (cx + 34, 88))
        + cubic_bezier((cx + 34, 88), (cx + 34, 94), (cx + 41, 94), (cx + 41, 89))
    )
    draw_thick_curve(draw, tail_a, width=7.8 * SCALE, fill=0)
    draw_thick_curve(draw, tail_a, width=2.4 * SCALE, fill=255)
    # Puntita negra redondeada al final del rulo
    r_tip = 3.2 * SCALE
    tx, ty = pt(cx + 41, 89)
    draw.ellipse((tx - r_tip, ty - r_tip, tx + r_tip, ty + r_tip), fill=0)

    draw_monkey_base(draw, cx=cx, waving_left_arm=False)

    # Ojitos redondos expresivos con brillo blanco
    for ex in (cx - 8.5, cx + 8.5):
        ey = 42.5
        draw.ellipse(pt(ex - 3.0, ey - 3.6) + pt(ex + 3.0, ey + 3.6), fill=0)
        draw.ellipse(pt(ex - 1.4, ey - 2.0) + pt(ex + 0.4, ey - 0.2), fill=255)

    # Sonrisa amplia y amigable
    smile = quadratic_bezier((cx - 9.0, 53.5), (cx, 60.5), (cx + 9.0, 53.5))
    draw_thick_curve(draw, smile, width=2.6 * SCALE, fill=0)
    # Pequeñas comisuras
    draw_thick_curve(draw, [(cx - 10.2, 54.5), (cx - 8.2, 52.5)], width=2.0 * SCALE, fill=0)
    draw_thick_curve(draw, [(cx + 8.2, 52.5), (cx + 10.2, 54.5)], width=2.0 * SCALE, fill=0)

    return finalize_1bit(img)


def draw_uh_ah_text(draw: ImageDraw.ImageDraw, x0: float, y0: float) -> None:
    """Dibuja '¡UH AH!' con trazos vectoriales limpios y nítidos."""
    w = 2.2 * SCALE
    # '¡'
    draw.ellipse(pt(x0 + 0.2, y0 + 0.5) + pt(x0 + 2.6, y0 + 2.9), fill=0)
    draw_thick_curve(draw, [(x0 + 1.4, y0 + 5.0), (x0 + 1.4, y0 + 11.5)], width=w, fill=0)

    # 'U'
    ux = x0 + 5.8
    draw_thick_curve(
        draw,
        [(ux, y0 + 0.8), (ux, y0 + 8.5)]
        + quadratic_bezier((ux, y0 + 8.5), (ux + 3.5, y0 + 12.2), (ux + 7.0, y0 + 8.5))
        + [(ux + 7.0, y0 + 0.8)],
        width=w,
        fill=0,
    )

    # 'H'
    hx1 = x0 + 16.5
    draw_thick_curve(draw, [(hx1, y0 + 0.8), (hx1, y0 + 11.5)], width=w, fill=0)
    draw_thick_curve(draw, [(hx1 + 7.0, y0 + 0.8), (hx1 + 7.0, y0 + 11.5)], width=w, fill=0)
    draw_thick_curve(draw, [(hx1, y0 + 6.0), (hx1 + 7.0, y0 + 6.0)], width=w, fill=0)

    # 'A'
    ax = x0 + 30.5
    draw_thick_curve(draw, [(ax, y0 + 11.5), (ax + 3.8, y0 + 0.8), (ax + 7.6, y0 + 11.5)], width=w, fill=0)
    draw_thick_curve(draw, [(ax + 1.4, y0 + 8.0), (ax + 6.2, y0 + 8.0)], width=w * 0.9, fill=0)

    # 'H'
    hx2 = x0 + 41.8
    draw_thick_curve(draw, [(hx2, y0 + 0.8), (hx2, y0 + 11.5)], width=w, fill=0)
    draw_thick_curve(draw, [(hx2 + 7.0, y0 + 0.8), (hx2 + 7.0, y0 + 11.5)], width=w, fill=0)
    draw_thick_curve(draw, [(hx2, y0 + 6.0), (hx2 + 7.0, y0 + 6.0)], width=w, fill=0)

    # '!'
    ex = x0 + 52.8
    draw_thick_curve(draw, [(ex, y0 + 0.8), (ex, y0 + 7.0)], width=w, fill=0)
    draw.ellipse(pt(ex - 1.2, y0 + 9.3) + pt(ex + 1.2, y0 + 11.7), fill=0)


def draw_frame_b() -> Image.Image:
    img = Image.new("L", (W, H), 255)
    draw = ImageDraw.Draw(img)
    cx = 66.0

    # --- Colita levantada alegre (Frame B) ---
    tail_b = (
        cubic_bezier((cx + 18, 104), (cx + 35, 102), (cx + 42, 85), (cx + 34, 73))
        + cubic_bezier((cx + 34, 73), (cx + 27, 62), (cx + 38, 49), (cx + 49, 53))
    )
    draw_thick_curve(draw, tail_b, width=7.8 * SCALE, fill=0)
    draw_thick_curve(draw, tail_b, width=2.4 * SCALE, fill=255)
    r_tip = 3.2 * SCALE
    tx, ty = pt(cx + 49, 53)
    draw.ellipse((tx - r_tip, ty - r_tip, tx + r_tip, ty + r_tip), fill=0)

    draw_monkey_base(draw, cx=cx, waving_left_arm=True)

    # Ojitos arqueados felices ^ ^
    for ex in (cx - 8.5, cx + 8.5):
        ey = 43.5
        draw_thick_curve(
            draw,
            quadratic_bezier((ex - 4.2, ey), (ex, ey - 6.5), (ex + 4.2, ey)),
            width=2.8 * SCALE,
            fill=0,
        )

    # Boca abierta gritando alegre
    mouth_pts = (
        [(cx - 6.5, 53.0), (cx + 6.5, 53.0)]
        + quadratic_bezier((cx + 6.5, 53.0), (cx + 6.5, 62.5), (cx, 62.5))
        + quadratic_bezier((cx, 62.5), (cx - 6.5, 62.5), (cx - 6.5, 53.0))
    )
    draw_filled_closed_curve(draw, mouth_pts, fill=0, outline=0, width=2.0 * SCALE)
    # Lengüita blanca abajo dentro de la boca
    draw.ellipse(pt(cx - 3.2, 58.2) + pt(cx + 3.2, 61.5), fill=255)

    # --- Burbuja de diálogo arriba a la derecha: "¡UH AH!" ---
    bx0, by0, bx1, by1 = 97.0, 4.0, 165.0, 30.0
    tail_bubble = [pt(108, 27), pt(100, 36), pt(117, 28)]
    draw.polygon(tail_bubble, fill=255)
    draw.rounded_rectangle(
        pt(bx0, by0) + pt(bx1, by1),
        radius=10 * SCALE,
        fill=255,
        outline=0,
        width=int(round(2.8 * SCALE)),
    )
    draw.polygon(tail_bubble, fill=255)
    draw_thick_curve(draw, [(108, 29), (100, 36), (117, 29)], width=2.8 * SCALE, fill=0)
    draw.rounded_rectangle(pt(bx0 + 3, by0 + 3) + pt(bx1 - 3, by1 - 3), radius=8 * SCALE, fill=255)

    draw_uh_ah_text(draw, x0=104.0, y0=11.0)

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

    path_a = out_dir / "mono_a.png"
    path_b = out_dir / "mono_b.png"
    frame_a.save(path_a)
    frame_b.save(path_b)
    print(f"Guardados: {path_a} ({frame_a.size}, colors={set(frame_a.get_flattened_data())})")
    print(f"Guardados: {path_b} ({frame_b.size}, colors={set(frame_b.get_flattened_data())})")


if __name__ == "__main__":
    main()
