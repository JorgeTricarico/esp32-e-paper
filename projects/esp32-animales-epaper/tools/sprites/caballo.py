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


def draw_rounded_rect_outlined(draw, bbox, radius, fill=WHITE, outline=BLACK, width=STROKE):
    x0, y0, x1, y1 = [sc(v) for v in bbox]
    draw.rounded_rectangle(
        (x0, y0, x1, y1), radius=sc(radius), fill=fill, outline=outline, width=width
    )


def draw_leg_straight(draw, x, y_top, y_bot, w=10, tilt=0, open_top=True):
    """Dibuja una patita recta de caballito con casco/pezuña negra."""
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

    # Casco redondeado en negro
    hx0 = x - w / 2 + tilt - 1.0
    hx1 = x + w / 2 + tilt + 1.0
    hy0 = y_bot - 8.5
    hy1 = y_bot + 1.5
    draw.rounded_rectangle(
        (sc(hx0), sc(hy0), sc(hx1), sc(hy1)),
        radius=sc(3.5),
        fill=BLACK,
    )


def draw_leg_trotting(draw, x, y_top):
    """Patita delantera levantada y doblada alegremente al trotar (hacia la izquierda)."""
    # Muslo saliendo hacia adelante-arriba y rodilla doblada hacia abajo
    p_hip_f = (x - 4.5, y_top)
    p_hip_b = (x + 4.5, y_top + 2)
    p_knee_f = (x - 16, y_top + 4)
    p_knee_b = (x - 10, y_top + 12)
    p_ankle_f = (x - 18, y_top + 17)
    p_ankle_b = (x - 10, y_top + 19)

    poly = [p_hip_f, p_knee_f, p_ankle_f, p_ankle_b, p_knee_b, p_hip_b]
    draw.polygon([pt(*p) for p in poly], fill=WHITE)
    draw_thick_curve(draw, [p_hip_f, p_knee_f, p_ankle_f], width=STROKE)
    draw_thick_curve(draw, [p_hip_b, p_knee_b, p_ankle_b], width=STROKE)

    # Casco inclinado
    hoof = [
        (p_ankle_f[0] - 1.5, p_ankle_f[1] - 1),
        (p_ankle_b[0] + 1.5, p_ankle_b[1] - 1),
        (p_ankle_b[0] + 2.5, p_ankle_b[1] + 5.5),
        (p_ankle_f[0] - 0.5, p_ankle_f[1] + 6.5),
    ]
    draw_filled_outlined_polygon(draw, hoof, fill=BLACK, outline=BLACK, width=5)

    # Líneas de movimiento cerca del casco
    draw_thick_curve(
        draw,
        draw_cubic_bezier((x - 24, y_top + 14), (x - 26, y_top + 18), (x - 25, y_top + 22), (x - 22, y_top + 25), steps=8),
        width=8,
    )


def draw_tail(draw, raised=False):
    """Colita larga y esponjosa a la derecha del cuerpo."""
    if not raised:
        # Cola larga cayendo con onda suave (x=118..144, y=68..109)
        top_curve = draw_cubic_bezier((117, 67), (134, 65), (144, 80), (138, 108), steps=20)
        bot_curve = draw_cubic_bezier((126, 106), (131, 90), (127, 77), (116, 75), steps=20)
        # Punta en mechón
        tip = [(138, 108), (133, 111), (129, 104), (126, 106)]
        pts = top_curve + tip + bot_curve
        draw_filled_outlined_polygon(draw, pts, fill=BLACK, outline=BLACK, width=6)
        # Rayita blanca decorativa dentro de la cola para darle textura
        tex = draw_cubic_bezier((126, 73), (134, 78), (136, 90), (133, 101), steps=12)
        draw_thick_curve(draw, tex, width=7, fill=WHITE)
    else:
        # Cola larga levantada alegremente al trotar
        top_curve = draw_cubic_bezier((117, 67), (134, 56), (148, 60), (151, 82), steps=20)
        bot_curve = draw_cubic_bezier((140, 86), (139, 72), (129, 70), (116, 75), steps=20)
        tip = [(151, 82), (147, 89), (142, 83), (140, 86)]
        pts = top_curve + tip + bot_curve
        draw_filled_outlined_polygon(draw, pts, fill=BLACK, outline=BLACK, width=6)
        tex = draw_cubic_bezier((126, 70), (137, 64), (144, 68), (145, 79), steps=12)
        draw_thick_curve(draw, tex, width=7, fill=WHITE)


def draw_mane(draw, dy=0):
    """Crin despeinada en el cuello y mechoncito en la frente."""
    # Crin despeinada a lo largo de la parte trasera del cuello (x=64..84, y=28..66)
    mane_pts = [
        (61, 29 + dy),
        (71, 26 + dy),
        (68, 34 + dy),
        (78, 35 + dy),
        (73, 43 + dy),
        (83, 46 + dy),
        (77, 53 + dy),
        (86, 58 + dy),
        (78, 64 + dy),
        (70, 62 + dy),
        (65, 48 + dy),
    ]
    draw_filled_outlined_polygon(draw, mane_pts, fill=BLACK, outline=BLACK, width=6)

    # Mechón despeinado en la frente (flequillo entre orejas y ojos)
    tuft = [
        (48, 25 + dy),
        (41, 20 + dy),
        (44, 28 + dy),
        (35, 27 + dy),
        (41, 34 + dy),
        (51, 32 + dy),
    ]
    draw_filled_outlined_polygon(draw, tuft, fill=BLACK, outline=BLACK, width=5)


def draw_ear(draw, bx, by, tilt_x=0, filled_inner=True):
    """Orejita parada puntiaguda y tierna."""
    outer = [
        (bx - 5.5, by + 2),
        (bx + tilt_x, by - 14),
        (bx + 6.5, by + 1),
    ]
    draw_filled_outlined_polygon(draw, outer, fill=WHITE, outline=BLACK, width=STROKE - 1)
    if filled_inner:
        inner = [
            (bx - 2, by),
            (bx + tilt_x * 0.7, by - 8),
            (bx + 2.5, by),
        ]
        draw.polygon([pt(*p) for p in inner], fill=BLACK)


def draw_speech_bubble(draw):
    """Burbuja de diálogo arriba a la derecha con texto ¡HIII! nítido."""
    # Contorno de la burbuja: x=92..163, y=6..36
    x0, y0, x1, y1 = 92, 6, 163, 36
    r = 10
    draw_rounded_rect_outlined(draw, (x0, y0, x1, y1), r, fill=WHITE, outline=BLACK, width=STROKE - 1)

    # Piquito de la burbuja apuntando hacia la cabecita sin chocar con la crin
    tail_pts = [(97, 29), (85, 36), (105, 34)]
    draw.polygon([pt(x, y) for x, y in tail_pts], fill=WHITE)
    draw_thick_curve(draw, [(97, 30), (85, 36), (105, 35)], width=STROKE - 1, fill=BLACK)
    draw.polygon([pt(99, 28), pt(88, 34), pt(106, 33)], fill=WHITE)

    # Texto "¡HIII!" centrado en x=92..163 (centro x=127.5), y=13..28
    w_txt = 10  # ~2.5 px en 1x
    # '¡' (x=101)
    draw.ellipse((sc(99.5), sc(13), sc(102.5), sc(16)), fill=BLACK)
    draw_thick_curve(draw, [(101, 19), (101, 28)], width=w_txt)

    # 'H' (x=109..119)
    draw_thick_curve(draw, [(109, 14), (109, 28)], width=w_txt)
    draw_thick_curve(draw, [(119, 14), (119, 28)], width=w_txt)
    draw_thick_curve(draw, [(109, 21), (119, 21)], width=w_txt)

    # 'I' (x=126)
    draw_thick_curve(draw, [(126, 14), (126, 28)], width=w_txt)

    # 'I' (x=134)
    draw_thick_curve(draw, [(134, 14), (134, 28)], width=w_txt)

    # 'I' (x=142)
    draw_thick_curve(draw, [(142, 14), (142, 28)], width=w_txt)

    # '!' (x=151)
    draw_thick_curve(draw, [(151, 14), (151, 23)], width=w_txt)
    draw.ellipse((sc(149.5), sc(26), sc(152.5), sc(29)), fill=BLACK)


def render_horse(frame_b=False):
    img = Image.new("L", (W, H), WHITE)
    draw = ImageDraw.Draw(img)

    # Desplazamiento vertical de la cabeza en Frame B (cabecita un poco alzada)
    dy = -4.5 if frame_b else 0.0

    # 1. Colita larga (detrás del cuerpo)
    draw_tail(draw, raised=frame_b)

    # 2. Patitas traseras (lado lejano primero, ligeramente desplazadas)
    draw_leg_straight(draw, 103, 92, 117, w=8.5, tilt=2, open_top=False)
    if not frame_b:
        draw_leg_straight(draw, 69, 92, 117, w=8.5, tilt=-2, open_top=False)
    else:
        # En Frame B la patita delantera lejana apoya firme mientras la cercana trota
        draw_leg_straight(draw, 71, 92, 118, w=9, tilt=1, open_top=False)

    # 3. Cuerpo redondeado y tierno (x=55..121, y=62..99)
    body_top = draw_cubic_bezier((57, 74), (66, 61), (108, 60), (119, 71), steps=20)
    body_right = draw_cubic_bezier((119, 71), (124, 80), (121, 93), (109, 97), steps=14)
    body_bot = draw_cubic_bezier((109, 97), (92, 100), (74, 100), (60, 95), steps=16)
    body_left = draw_cubic_bezier((60, 95), (53, 90), (52, 81), (57, 74), steps=12)
    body_poly = body_top + body_right + body_bot + body_left
    draw_filled_outlined_polygon(draw, body_poly, fill=WHITE, outline=BLACK, width=STROKE)

    # 4. Patitas cercanas (integradas suavemente al cuerpo)
    draw_leg_straight(draw, 111, 92, 119, w=9.5, tilt=0, open_top=True)
    if not frame_b:
        draw_leg_straight(draw, 61, 92, 119, w=9.5, tilt=0, open_top=True)
    else:
        draw_leg_trotting(draw, 60, 89)

    # 5. Crin despeinada (detrás del cuello y cabeza)
    draw_mane(draw, dy=dy)

    # 6. Cuello conectando suavemente cabeza y cuerpo
    neck_poly = [
        (49, 44 + dy),
        (67, 41 + dy),
        (76, 68),
        (54, 80),
    ]
    draw.polygon([pt(*p) for p in neck_poly], fill=WHITE)
    draw_thick_curve(draw, [(67, 41 + dy), (76, 68)], width=STROKE)
    draw_thick_curve(draw, [(49, 46 + dy), (53, 72), (55, 80)], width=STROKE)

    # 7. Orejitas paradas (detrás de la cabeza)
    draw_ear(draw, 58, 28 + dy, tilt_x=2, filled_inner=True)
    draw_ear(draw, 46, 29 + dy, tilt_x=-2, filled_inner=True)

    # 8. Cabeza + hocico ancho de perfil amigable (mirando a la izquierda)
    # Contorno unificado suave de cabeza y hocico ancho
    head_pts = (
        draw_cubic_bezier((45, 28 + dy), (58, 25 + dy), (68, 34 + dy), (66, 48 + dy), steps=14)
        + draw_cubic_bezier((66, 48 + dy), (64, 58 + dy), (54, 63 + dy), (43, 62 + dy), steps=14)
        + draw_cubic_bezier((43, 62 + dy), (32, 62 + dy), (21, 59 + dy), (20, 50 + dy), steps=14)
        + draw_cubic_bezier((20, 50 + dy), (20, 41 + dy), (30, 39 + dy), (35, 35 + dy), steps=12)
        + draw_cubic_bezier((35, 35 + dy), (38, 30 + dy), (41, 28 + dy), (45, 28 + dy), steps=10)
    )
    draw_filled_outlined_polygon(draw, head_pts, fill=WHITE, outline=BLACK, width=STROKE)

    # Línea suave separando el hocico ancho
    muzzle_line = draw_cubic_bezier((35, 41 + dy), (39, 47 + dy), (39, 54 + dy), (36, 60 + dy), steps=12)
    draw_thick_curve(draw, muzzle_line, width=STROKE - 4, fill=BLACK)

    # 9. Ojo grande con brillo de libro infantil
    ex, ey = 46, 41 + dy
    draw.ellipse((sc(ex - 4.0), sc(ey - 4.5), sc(ex + 4.0), sc(ey + 4.5)), fill=BLACK)
    draw.ellipse((sc(ex - 2.2), sc(ey - 2.6), sc(ex + 0.3), sc(ey - 0.1)), fill=WHITE)

    # Fosa nasal ovalada en el hocico ancho
    draw.ellipse((sc(25), sc(45 + dy), sc(29.5), sc(49 + dy)), fill=BLACK)

    # 10. Boca: Frame A sonrisa tierna, Frame B boca abierta relinchando
    if not frame_b:
        smile = draw_cubic_bezier((22, 54 + dy), (27, 57.5 + dy), (32, 56.5 + dy), (35, 53.5 + dy), steps=12)
        draw_thick_curve(draw, smile, width=STROKE - 3, fill=BLACK)
    else:
        # Boca abierta ovalada/triangular hacia la izquierda relinchando
        mouth_pts = [
            (20, 51 + dy),
            (32.5, 52 + dy),
            (31.5, 62 + dy),
            (22, 60 + dy),
        ]
        draw.polygon([pt(*p) for p in mouth_pts], fill=BLACK)
        # Lengüita blanca adentro para alto contraste 1-bit
        draw.chord((sc(22.5), sc(55.5 + dy), sc(30.5), sc(62 + dy)), start=180, end=360, fill=WHITE)
        draw_thick_curve(draw, [(20, 51 + dy), (32.5, 52 + dy), (31.5, 62 + dy), (22, 60 + dy), (20, 51 + dy)], width=6, fill=BLACK)

    # 11. Burbuja "¡HIII!" en Frame B
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

    frame_a = render_horse(frame_b=False)
    frame_b = render_horse(frame_b=True)

    path_a = out_dir / "caballo_a.png"
    path_b = out_dir / "caballo_b.png"

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
