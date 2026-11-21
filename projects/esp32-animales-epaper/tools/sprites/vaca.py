import math
from pathlib import Path
from PIL import Image, ImageDraw

WIDTH, HEIGHT = 168, 128
SCALE = 4
W, H = WIDTH * SCALE, HEIGHT * SCALE
STROKE = 13  # ~3.25 px at 1x

BLACK = 0
WHITE = 255


def sc(val):
    return int(round(val * SCALE))


def pt(x, y):
    return (sc(x), sc(y))


def draw_thick_curve(draw, points, width=STROKE, fill=BLACK):
    scaled = [pt(x, y) for x, y in points]
    draw.line(scaled, fill=fill, width=width, joint="curve")
    r = width // 2
    for sx, sy in scaled:
        draw.ellipse((sx - r, sy - r, sx + r, sy + r), fill=fill)


def draw_cubic_bezier(p0, p1, p2, p3, steps=24):
    pts = []
    for i in range(steps + 1):
        t = i / steps
        u = 1 - t
        x = (
            (u ** 3) * p0[0]
            + 3 * (u ** 2) * t * p1[0]
            + 3 * u * (t ** 2) * p2[0]
            + (t ** 3) * p3[0]
        )
        y = (
            (u ** 3) * p0[1]
            + 3 * (u ** 2) * t * p1[1]
            + 3 * u * (t ** 2) * p2[1]
            + (t ** 3) * p3[1]
        )
        pts.append((x, y))
    return pts


def draw_filled_outlined_polygon(draw, points, fill=WHITE, outline=BLACK, width=STROKE):
    scaled = [pt(x, y) for x, y in points]
    draw.polygon(scaled, fill=fill)
    closed = scaled + [scaled[0]]
    draw.line(closed, fill=outline, width=width, joint="curve")
    r = width // 2
    for sx, sy in scaled:
        draw.ellipse((sx - r, sy - r, sx + r, sy + r), fill=outline)


def draw_ellipse_outlined(draw, bbox, fill=WHITE, outline=BLACK, width=STROKE):
    x0, y0, x1, y1 = [sc(v) for v in bbox]
    draw.ellipse((x0, y0, x1, y1), fill=fill, outline=outline, width=width)


def draw_rounded_rect_outlined(draw, bbox, radius, fill=WHITE, outline=BLACK, width=STROKE):
    x0, y0, x1, y1 = [sc(v) for v in bbox]
    draw.rounded_rectangle(
        (x0, y0, x1, y1), radius=sc(radius), fill=fill, outline=outline, width=width
    )


def draw_organic_spot(draw, cx, cy, rx, ry, lobes):
    """Dibuja una mancha orgánica negra suave combinando una elipse y lóbulos."""
    pts = []
    steps = 48
    for i in range(steps):
        ang = 2 * math.pi * i / steps
        mod = 1.0
        for freq, amp, phase in lobes:
            mod += amp * math.sin(freq * ang + phase)
        x = cx + rx * mod * math.cos(ang)
        y = cy + ry * mod * math.sin(ang)
        pts.append(pt(x, y))
    draw.polygon(pts, fill=BLACK)


def draw_leg(draw, x, y_top, y_bot, w=11, tilt=0, open_top=False):
    """Dibuja una patita con pezuña negra."""
    tl = (x - w / 2, y_top)
    tr = (x + w / 2, y_top)
    br = (x + w / 2 + tilt, y_bot - 5)
    bl = (x - w / 2 + tilt, y_bot - 5)
    if open_top:
        draw.polygon([pt(*tl), pt(*tr), pt(*br), pt(*bl)], fill=WHITE)
        draw_thick_curve(draw, [tl, bl], width=STROKE)
        draw_thick_curve(draw, [tr, br], width=STROKE)
    else:
        draw_filled_outlined_polygon(draw, [tl, tr, br, bl], fill=WHITE, outline=BLACK, width=STROKE)
    # Pezuña redondeada abajo
    hx0 = x - w / 2 + tilt - 0.5
    hx1 = x + w / 2 + tilt + 0.5
    hy0 = y_bot - 9
    hy1 = y_bot + 1
    draw.rounded_rectangle(
        (sc(hx0), sc(hy0), sc(hx1), sc(hy1)),
        radius=sc(3.5),
        fill=BLACK,
    )


def draw_horn(draw, base_x, base_y, side=1):
    """Cuernito curvo adorable. side=-1 izquierdo, side=1 derecho."""
    p_outer = draw_cubic_bezier(
        (base_x, base_y),
        (base_x + side * 6, base_y - 6),
        (base_x + side * 8, base_y - 13),
        (base_x + side * 2, base_y - 17),
        steps=16,
    )
    p_inner = draw_cubic_bezier(
        (base_x + side * 2, base_y - 17),
        (base_x + side * 1, base_y - 11),
        (base_x - side * 1, base_y - 6),
        (base_x - side * 5, base_y - 2),
        steps=16,
    )
    pts = p_outer + p_inner
    draw_filled_outlined_polygon(draw, pts, fill=WHITE, outline=BLACK, width=STROKE - 1)


def draw_ear(draw, base_x, base_y, side=1, raised=False):
    """Orejita de vaca. raised=False caída/relajada, raised=True levantada alegre."""
    if not raised:
        # Oreja relajada cayendo suavemente hacia abajo-afuera
        p1 = draw_cubic_bezier(
            (base_x, base_y),
            (base_x + side * 9, base_y - 1),
            (base_x + side * 16, base_y + 5),
            (base_x + side * 15, base_y + 12),
            steps=16,
        )
        p2 = draw_cubic_bezier(
            (base_x + side * 15, base_y + 12),
            (base_x + side * 10, base_y + 14),
            (base_x + side * 4, base_y + 10),
            (base_x, base_y + 5),
            steps=16,
        )
        pts = p1 + p2
        draw_filled_outlined_polygon(draw, pts, fill=WHITE, outline=BLACK, width=STROKE)
        # Detalle interior de la oreja
        draw_thick_curve(
            draw,
            [(base_x + side * 4, base_y + 3.5), (base_x + side * 10.5, base_y + 8)],
            width=STROKE - 4,
        )
    else:
        # Oreja levantada hacia arriba-afuera
        p1 = draw_cubic_bezier(
            (base_x, base_y),
            (base_x + side * 9, base_y - 8),
            (base_x + side * 17, base_y - 10),
            (base_x + side * 18, base_y - 4),
            steps=16,
        )
        p2 = draw_cubic_bezier(
            (base_x + side * 18, base_y - 4),
            (base_x + side * 13, base_y + 2),
            (base_x + side * 6, base_y + 4),
            (base_x, base_y + 4),
            steps=16,
        )
        pts = p1 + p2
        draw_filled_outlined_polygon(draw, pts, fill=WHITE, outline=BLACK, width=STROKE)
        draw_thick_curve(
            draw,
            [(base_x + side * 4, base_y + 0.5), (base_x + side * 12, base_y - 3)],
            width=STROKE - 4,
        )


def draw_cowbell(draw, cx, cy, tilted=False):
    """Dibuja el collar y el cencerro en el cuello."""
    # Tira del collar
    draw_thick_curve(
        draw,
        [(cx - 10, cy - 8), (cx, cy - 2), (cx + 12, cy - 9)],
        width=STROKE + 1,
        fill=BLACK,
    )
    dx = 2 if tilted else 0
    # Anilla
    draw_ellipse_outlined(
        draw, (cx - 2.5 + dx * 0.3, cy - 3, cx + 2.5 + dx * 0.3, cy + 2),
        fill=WHITE, outline=BLACK, width=STROKE - 3
    )
    # Cuerpo del cencerro (trapecio redondeado)
    pts = [
        (cx - 4 + dx * 0.5, cy + 2),
        (cx + 4 + dx * 0.5, cy + 2),
        (cx + 6.5 + dx, cy + 13),
        (cx - 6.5 + dx, cy + 13),
    ]
    draw_filled_outlined_polygon(draw, pts, fill=WHITE, outline=BLACK, width=STROKE - 1)
    # Badajo (bolita abajo)
    draw.ellipse(
        (sc(cx - 2.5 + dx), sc(cy + 12), sc(cx + 2.5 + dx), sc(cy + 17)),
        fill=BLACK,
    )
    if tilted:
        # Pequeñas líneas de tintineo
        draw_thick_curve(draw, [(cx + 10, cy + 5), (cx + 13, cy + 3)], width=8)
        draw_thick_curve(draw, [(cx + 11, cy + 10), (cx + 14, cy + 10)], width=8)


def draw_tail(draw, happy=False):
    """Colita a la derecha del cuerpo."""
    if not happy:
        # Cola tranquila cayendo hacia abajo
        curve = draw_cubic_bezier(
            (121, 74), (130, 76), (132, 87), (128, 95), steps=18
        )
        draw_thick_curve(draw, curve, width=STROKE - 2, fill=BLACK)
        # Mechoncito en forma de gota
        pts = [
            (128, 94),
            (132, 98),
            (129, 104),
            (125, 102),
            (124, 97),
        ]
        draw_filled_outlined_polygon(draw, pts, fill=BLACK, outline=BLACK, width=6)
    else:
        # Cola levantada moviéndose feliz
        curve = draw_cubic_bezier(
            (121, 74), (131, 68), (135, 59), (141, 55), steps=18
        )
        draw_thick_curve(draw, curve, width=STROKE - 2, fill=BLACK)
        # Mechoncito en la punta
        pts = [
            (140, 55),
            (146, 51),
            (149, 55),
            (145, 59),
            (141, 58),
        ]
        draw_filled_outlined_polygon(draw, pts, fill=BLACK, outline=BLACK, width=6)
        # Líneas de movimiento
        draw_thick_curve(
            draw,
            draw_cubic_bezier((131, 53), (134, 48), (139, 46), (143, 46), steps=10),
            width=8,
        )
        draw_thick_curve(
            draw,
            draw_cubic_bezier((142, 65), (147, 63), (150, 59), (151, 55), steps=10),
            width=8,
        )


def draw_speech_bubble(draw):
    """Burbuja de diálogo arriba a la derecha con texto ¡MUU! nítido."""
    # Contorno de la burbuja: x=101..163, y=6..36
    x0, y0, x1, y1 = 101, 6, 163, 36
    r = 10
    draw_rounded_rect_outlined(draw, (x0, y0, x1, y1), r, fill=WHITE, outline=BLACK, width=STROKE - 1)

    # Piquito de la burbuja apuntando al hocico
    tail_pts = [(111, 33), (101, 44), (120, 34)]
    draw.polygon([pt(x, y) for x, y in tail_pts], fill=WHITE)
    draw_thick_curve(draw, [(111, 35), (101, 44), (120, 35)], width=STROKE - 1, fill=BLACK)
    draw.polygon([pt(112, 32), pt(104, 41), pt(119, 33)], fill=WHITE)

    # Texto "¡MUU!" centrado en x=101..163 (centro x=132), y=21
    w_txt = 11  # ~2.75 px en 1x
    # '¡' (x=110)
    draw.ellipse((sc(108.5), sc(14), sc(111.5), sc(17)), fill=BLACK)
    draw_thick_curve(draw, [(110, 20), (110, 28)], width=w_txt)

    # 'M' (x=116..126)
    draw_thick_curve(
        draw,
        [(116, 28), (116, 15), (121, 23), (126, 15), (126, 28)],
        width=w_txt,
    )

    # 'U' 1 (x=131.5..138.5)
    u1 = [(131.5, 15), (131.5, 24)] + draw_cubic_bezier(
        (131.5, 24), (131.5, 29), (138.5, 29), (138.5, 24), steps=10
    ) + [(138.5, 15)]
    draw_thick_curve(draw, u1, width=w_txt)

    # 'U' 2 (x=143.5..150.5)
    u2 = [(143.5, 15), (143.5, 24)] + draw_cubic_bezier(
        (143.5, 24), (143.5, 29), (150.5, 29), (150.5, 24), steps=10
    ) + [(150.5, 15)]
    draw_thick_curve(draw, u2, width=w_txt)

    # '!' (x=155.5)
    draw_thick_curve(draw, [(155.5, 14.5), (155.5, 23)], width=w_txt)
    draw.ellipse((sc(154), sc(25.5), sc(157), sc(28.5)), fill=BLACK)


def render_cow(frame_b=False):
    img = Image.new("L", (W, H), WHITE)
    draw = ImageDraw.Draw(img)

    # 1. Patitas traseras (detrás del cuerpo)
    draw_leg(draw, 73, 94, 117, w=10, tilt=-1)
    draw_leg(draw, 108, 94, 117, w=10, tilt=1)

    # 2. Colita
    draw_tail(draw, happy=frame_b)

    # 3. Cuerpo rechoncho y adorable (x=54..124, y=62..105)
    draw_ellipse_outlined(draw, (53, 62, 124, 104), fill=WHITE, outline=BLACK, width=STROKE)

    # Manchas orgánicas negras en el cuerpo
    draw_organic_spot(
        draw, cx=93, cy=74, rx=13, ry=9,
        lobes=[(2, 0.18, 0.4), (3, 0.14, 1.2)]
    )
    draw_organic_spot(
        draw, cx=111, cy=88, rx=7.5, ry=6.5,
        lobes=[(2, 0.15, 1.8), (3, 0.12, 0.3)]
    )
    draw_organic_spot(
        draw, cx=78, cy=90, rx=6.5, ry=5.5,
        lobes=[(2, 0.16, 0.9), (3, 0.12, 2.1)]
    )

    # 4. Patitas delanteras (integradas suavemente al cuerpo)
    draw_leg(draw, 66, 95, 119, w=10.5, tilt=0, open_top=True)
    draw_leg(draw, 99, 95, 119, w=10.5, tilt=0, open_top=True)

    # 5. Collar y cencerro en el cuello
    draw_cowbell(draw, 57, 81, tilted=frame_b)

    # 6. Cuernos y orejas (detrás de la cabeza)
    draw_ear(draw, 35, 44, side=-1, raised=frame_b)
    draw_ear(draw, 79, 44, side=1, raised=frame_b)
    draw_horn(draw, 42, 34, side=-1)
    draw_horn(draw, 72, 34, side=1)

    # 7. Cabeza (redondeada y expresiva: x=33..81, y=31..72)
    draw_ellipse_outlined(draw, (33, 31, 81, 72), fill=WHITE, outline=BLACK, width=STROKE)

    # Mancha orgánica en la parte superior derecha de la cabeza (sin tapar el ojo)
    draw_organic_spot(
        draw, cx=69, cy=40, rx=8.5, ry=6.5,
        lobes=[(2, 0.18, 0.5), (3, 0.12, 1.4)]
    )
    # Pequeña manchita en la mejilla izquierda
    draw_organic_spot(
        draw, cx=39, cy=54, rx=4.2, ry=3.8,
        lobes=[(2, 0.15, 1.1)]
    )

    # 8. Ojos grandes con brillo de libro infantil
    for ex in (46, 68):
        ey = 48
        draw.ellipse((sc(ex - 4.2), sc(ey - 4.8), sc(ex + 4.2), sc(ey + 4.8)), fill=BLACK)
        # Brillo blanco superior izquierdo
        draw.ellipse((sc(ex - 2.3), sc(ey - 2.8), sc(ex + 0.3), sc(ey - 0.2)), fill=WHITE)

    # 9. Hocico ancho y tierno (x=31..83, y=54..80)
    draw_ellipse_outlined(draw, (31, 54, 83, 81), fill=WHITE, outline=BLACK, width=STROKE)

    # Fosas nasales ovaladas suaves
    draw.ellipse((sc(43), sc(60), sc(48), sc(64.5)), fill=BLACK)
    draw.ellipse((sc(66), sc(60), sc(71), sc(64.5)), fill=BLACK)

    # Boca: Frame A sonrisa tranquila, Frame B boca abierta mugiendo feliz
    if not frame_b:
        smile = draw_cubic_bezier((47, 68.5), (52, 74), (62, 74), (67, 68.5), steps=14)
        draw_thick_curve(draw, smile, width=STROKE - 2, fill=BLACK)
    else:
        # Boca abierta ovalada negra con lengüita blanca abajo
        draw.ellipse((sc(48), sc(66), sc(66), sc(77.5)), fill=BLACK)
        # Lengüita curvilínea adentro (en blanco para alto contraste 1-bit)
        draw.chord((sc(51), sc(70.5), sc(63), sc(78)), start=180, end=360, fill=WHITE)
        # Re-trazar borde inferior de la boca para que quede cerrado y limpio
        draw.ellipse((sc(48), sc(66), sc(66), sc(77.5)), outline=BLACK, width=STROKE - 4)

    # 10. Burbuja "¡MUU!" en Frame B
    if frame_b:
        draw_speech_bubble(draw)

    # Reducir a 168x128 y binarizar estrictamente a 0 y 255 (umbral > 127)
    out = img.resize((WIDTH, HEIGHT), Image.Resampling.BILINEAR)
    out = out.point(lambda p: 255 if p > 127 else 0, mode="L")
    return out


def main():
    base_dir = Path(__file__).resolve().parents[2]
    out_dir = base_dir / "assets" / "custom_sprites"
    out_dir.mkdir(parents=True, exist_ok=True)

    frame_a = render_cow(frame_b=False)
    frame_b = render_cow(frame_b=True)

    path_a = out_dir / "vaca_a.png"
    path_b = out_dir / "vaca_b.png"

    frame_a.save(path_a, format="PNG")
    frame_b.save(path_b, format="PNG")

    # Verificación estricta de dimensiones y colores puros (0 y 255)
    for p in (path_a, path_b):
        im = Image.open(p)
        colors = set(im.get_flattened_data())
        assert im.size == (168, 128), f"{p} tamaño inválido: {im.size}"
        assert colors <= {0, 255}, f"{p} contiene grises: {colors}"
        print(f"OK: {p} ({im.size}, colores={sorted(colors)})")


if __name__ == "__main__":
    main()
