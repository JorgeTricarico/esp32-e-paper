"""Genera los sprites monocromáticos 1-bit del pato (pato_a.png y pato_b.png).

Tamaño exacto: 168x128 px, blanco (255) y negro (0) puro, sin grises ni dithering.
"""

import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

W, H = 168, 128
SCALE = 4
SW, SH = W * SCALE, H * SCALE

WHITE = 255
BLACK = 0
STROKE = 3 * SCALE + 2  # ~3.5 px al escalar a 168x128


def sc(x: float, y: float) -> tuple[int, int]:
    """Escala coordenadas del lienzo 168x128 al lienzo supersampleado."""
    return round(x * SCALE), round(y * SCALE)


def sc_box(x0: float, y0: float, x1: float, y1: float) -> list[int]:
    """Escala un bounding box [x0, y0, x1, y1]."""
    return [round(x0 * SCALE), round(y0 * SCALE), round(x1 * SCALE), round(y1 * SCALE)]


def draw_thick_polyline(
    draw: ImageDraw.ImageDraw,
    pts: list[tuple[float, float]],
    width: int = STROKE,
    fill: int = BLACK,
) -> None:
    """Dibuja una polilínea gruesa con extremos y uniones redondeadas."""
    spts = [sc(x, y) for x, y in pts]
    draw.line(spts, fill=fill, width=width, joint="curve")
    r = width // 2
    for sx, sy in spts:
        draw.ellipse([sx - r, sy - r, sx + r, sy + r], fill=fill)


def catmull_rom(points: list[tuple[float, float]], steps_per_seg: int = 16) -> list[tuple[float, float]]:
    """Interpola una curva suave Catmull-Rom a través de los puntos de control."""
    if len(points) < 4:
        return points
    out: list[tuple[float, float]] = []
    for i in range(1, len(points) - 2):
        p0, p1, p2, p3 = points[i - 1], points[i], points[i + 1], points[i + 2]
        for step in range(steps_per_seg):
            t = step / steps_per_seg
            t2 = t * t
            t3 = t2 * t
            x = 0.5 * (
                (2 * p1[0])
                + (-p0[0] + p2[0]) * t
                + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2
                + (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3
            )
            y = 0.5 * (
                (2 * p1[1])
                + (-p0[1] + p2[1]) * t
                + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2
                + (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3
            )
            out.append((x, y))
    out.append(points[-2])
    return out


def draw_waves(draw: ImageDraw.ImageDraw) -> None:
    """Dibuja olitas suaves debajo del patito y recorta limpiamente la parte sumergida."""
    water_y = 96.0
    wave_pts: list[tuple[float, float]] = []
    for i in range(0, 145):
        x = 12.0 + i
        y = water_y + 2.4 * math.sin((x - 12.0) * 0.15)
        wave_pts.append((x, y))

    poly = [sc(12.0, 128.0)] + [sc(x, y) for x, y in wave_pts] + [sc(156.0, 128.0)]
    draw.polygon(poly, fill=WHITE)
    draw_thick_polyline(draw, wave_pts, width=STROKE, fill=BLACK)

    # Dos olitas secundarias suaves abajo para dar contexto de estanque
    for start_x, end_x, base_y, phase in [(26.0, 74.0, 109.0, 0.0), (88.0, 138.0, 112.0, 1.8)]:
        pts = []
        steps = 40
        for i in range(steps + 1):
            x = start_x + (end_x - start_x) * (i / steps)
            y = base_y + 2.0 * math.sin((x - start_x) * 0.18 + phase)
            pts.append((x, y))
        draw_thick_polyline(draw, pts, width=int(STROKE * 0.85), fill=BLACK)


def draw_duck_base(draw: ImageDraw.ImageDraw, open_beak: bool) -> None:
    """Dibuja la silueta unificada del patito (cuerpo, cola, cabeza, copete, ojo, pico)."""
    # Contorno continuo unificado (sentido horario desde la base sumergida izquierda)
    # Así evitamos cualquier línea interna o astilla en el cuello/pecho.
    silhouette_ctrl = [
        (30, 102),
        (31, 95),
        (26, 85),
        (19, 77),  # punta inferior de la colita
        (25, 72),  # muesca de la colita
        (17, 60),  # punta superior de la colita
        (35, 68),  # inicio del lomo
        (52, 62),  # lomo
        (61, 51),  # nuca
        (62, 36),  # parte trasera de la cabeza
        (72, 24),  # coronilla izquierda
        (85, 23),  # coronilla derecha
        (98, 30),  # frente
        (102, 43), # zona del pico
        (100, 56), # barbilla
        (103, 72), # pecho superior
        (100, 88), # pecho inferior
        (93, 98),  # base sumergida derecha
        (86, 104),
    ]
    silhouette_pts = catmull_rom(silhouette_ctrl, steps_per_seg=14)

    # Relleno blanco de toda la silueta
    draw.polygon([sc(x, y) for x, y in silhouette_pts], fill=WHITE)

    # Contorno exterior suave (dividido en: espalda+cola+cabeza, y pecho, dejando abierta la zona del pico)
    # 1) De la base izquierda pasando por cola, lomo, cabeza hasta la frente (donde empieza el pico)
    back_head_ctrl = [
        (30, 102),
        (31, 95),
        (26, 85),
        (19, 77),
        (25, 72),
        (17, 60),
        (35, 68),
        (52, 62),
        (61, 51),
        (62, 36),
        (72, 24),
        (85, 23),
        (98, 30),
        (102, 43),
        (103, 48),
    ]
    # Para la colita con picos definidos, dibujamos lomo+cola y cabeza por tramos limpios
    tail_and_back = catmull_rom([(32, 102), (31, 95), (27, 85), (19, 77), (25, 72), (24, 70)], steps_per_seg=10)
    draw_thick_polyline(draw, tail_and_back, width=STROKE, fill=BLACK)
    draw_thick_polyline(draw, [(25, 72), (17, 60), (35, 68)], width=STROKE, fill=BLACK)

    back_to_forehead = catmull_rom([
        (32, 66), (35, 68), (52, 62), (61, 51), (62, 36), (72, 24), (85, 23), (97, 30), (102, 42), (103, 46)
    ], steps_per_seg=14)
    draw_thick_polyline(draw, back_to_forehead, width=STROKE, fill=BLACK)

    # 2) Pecho derecho (desde debajo del pico hasta el agua)
    chest_ctrl = [(100, 52), (100, 56), (103, 72), (100, 88), (94, 97), (90, 102)]
    draw_thick_polyline(draw, catmull_rom(chest_ctrl, steps_per_seg=14), width=STROKE, fill=BLACK)

    # 3) Copete / plumita simpática en la cabeza (dos plumitas curvas suaves)
    tuft_left = catmull_rom([(76, 25), (75, 23), (71, 16), (65, 14), (69, 12), (75, 16), (78, 22), (79, 24)])
    tuft_right = catmull_rom([(79, 24), (79, 22), (79, 13), (84, 9), (87, 13), (84, 19), (82, 23), (82, 25)])
    draw.polygon([sc(x, y) for x, y in tuft_left], fill=WHITE)
    draw.polygon([sc(x, y) for x, y in tuft_right], fill=WHITE)
    draw_thick_polyline(draw, tuft_left, width=int(STROKE * 0.85), fill=BLACK)
    draw_thick_polyline(draw, tuft_right, width=int(STROKE * 0.85), fill=BLACK)

    # 4) Ojo grande expresivo con brillo + ceja + mejilla tierna
    eye_cx, eye_cy, eye_r = 88, 41, 4.8
    draw.ellipse(sc_box(eye_cx - eye_r, eye_cy - eye_r, eye_cx + eye_r, eye_cy + eye_r), fill=BLACK)
    draw.ellipse(sc_box(eye_cx - 2.3, eye_cy - 2.6, eye_cx + 0.3, eye_cy + 0.0), fill=WHITE)
    brow = catmull_rom([(83, 33), (85, 32), (91, 32), (94, 34)])
    draw_thick_polyline(draw, brow, width=int(STROKE * 0.7), fill=BLACK)
    draw_thick_polyline(draw, [(78, 48), (76, 51)], width=int(STROKE * 0.65), fill=BLACK)
    draw_thick_polyline(draw, [(82, 49), (80, 52)], width=int(STROKE * 0.65), fill=BLACK)

    # 5) Pico
    if not open_beak:
        # Frame A: pico cerrado sonriente
        beak_ctrl = [(99, 44), (101, 44), (114, 45), (121, 49), (117, 54), (102, 55), (99, 54)]
        beak_pts = catmull_rom(beak_ctrl)
        draw.polygon([sc(x, y) for x, y in beak_pts], fill=WHITE)
        draw_thick_polyline(draw, beak_pts, width=STROKE, fill=BLACK)
        # Línea de sonrisa del pico
        smile = catmull_rom([(99, 50), (101, 50), (111, 51), (119, 49), (120, 48)])
        draw_thick_polyline(draw, smile, width=int(STROKE * 0.8), fill=BLACK)
    else:
        # Frame B: pico bien abierto graznando (comisura bien cerrada)
        mouth_interior = [sc(98, 46), sc(120, 42), sc(109, 51), sc(118, 60), sc(98, 56)]
        draw.polygon(mouth_interior, fill=BLACK)
        # Lengüita blanca en la parte inferior del interior
        draw.ellipse(sc_box(102, 51, 111, 57), fill=WHITE)

        # Mandíbula superior (inclinada hacia arriba)
        upper_pts = catmull_rom([(98, 46), (100, 45), (112, 40), (121, 41), (119, 45), (106, 49), (99, 50)])
        draw.polygon([sc(x, y) for x, y in upper_pts], fill=WHITE)
        draw_thick_polyline(draw, upper_pts, width=STROKE, fill=BLACK)

        # Mandíbula inferior (inclinada hacia abajo, conectada en la comisura x=99, y=51)
        lower_pts = catmull_rom([(99, 51), (101, 52), (114, 57), (119, 60), (114, 63), (102, 58), (98, 56)])
        draw.polygon([sc(x, y) for x, y in lower_pts], fill=WHITE)
        draw_thick_polyline(draw, lower_pts, width=STROKE, fill=BLACK)


def draw_wing_folded(draw: ImageDraw.ImageDraw) -> None:
    """Dibuja el ala plegada sobre el cuerpo (Frame A)."""
    wing_pts = catmull_rom([
        (60, 69), (64, 68), (75, 69), (80, 77), (72, 85), (54, 86), (45, 79), (50, 71), (64, 68), (70, 69)
    ])
    draw.polygon([sc(x, y) for x, y in wing_pts], fill=WHITE)
    draw_thick_polyline(draw, wing_pts, width=STROKE, fill=BLACK)
    draw_thick_polyline(draw, [(55, 78), (64, 78), (69, 76)], width=int(STROKE * 0.75), fill=BLACK)


def draw_wing_flapping(draw: ImageDraw.ImageDraw) -> None:
    """Dibuja el ala batida hacia arriba con rayitas de movimiento (Frame B)."""
    wing_curve = catmull_rom([
        (56, 68), (52, 65), (38, 52), (33, 39), (42, 34), (53, 41), (63, 55), (67, 65), (68, 68)
    ])
    draw.polygon([sc(x, y) for x, y in wing_curve], fill=WHITE)
    draw_thick_polyline(draw, wing_curve, width=STROKE, fill=BLACK)

    # Pluma interna en el ala levantada
    draw_thick_polyline(draw, [(45, 45), (51, 53), (57, 60)], width=int(STROKE * 0.75), fill=BLACK)

    # Rayitas de movimiento alrededor del ala
    draw_thick_polyline(draw, [(26, 42), (24, 50), (27, 57)], width=int(STROKE * 0.75), fill=BLACK)
    draw_thick_polyline(draw, [(20, 46), (19, 52), (21, 57)], width=int(STROKE * 0.65), fill=BLACK)
    draw_thick_polyline(draw, [(46, 28), (52, 31), (57, 36)], width=int(STROKE * 0.7), fill=BLACK)


def load_comic_font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    """Carga una fuente bold legible en Windows para la burbuja de diálogo."""
    candidates = [
        "C:/Windows/Fonts/comicbd.ttf",
        "C:/Windows/Fonts/arialbd.ttf",
        "C:/Windows/Fonts/verdanab.ttf",
        "C:/Windows/Fonts/seguisb.ttf",
    ]
    for path in candidates:
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def draw_speech_bubble(draw: ImageDraw.ImageDraw, text: str = "¡CUAC!") -> None:
    """Dibuja la burbuja de onomatopeya arriba a la derecha (Frame B)."""
    bx0, by0, bx1, by1 = 105, 4, 163, 31
    tail = [sc(121, 30), sc(118, 36), sc(130, 30)]

    draw.rounded_rectangle(sc_box(bx0, by0, bx1, by1), radius=9 * SCALE, fill=WHITE, outline=BLACK, width=STROKE)
    # Relleno blanco de la colita que tapa el borde inferior del globo justo en su base
    draw.polygon([sc(121, 29.5), sc(118, 36), sc(129, 29.5)], fill=WHITE)
    draw_thick_polyline(draw, [(121, 31), (118, 36), (129, 31)], width=STROKE, fill=BLACK)

    font = load_comic_font(12 * SCALE)
    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    cx = (bx0 + bx1) * SCALE / 2
    cy = (by0 + by1) * SCALE / 2
    tx = cx - tw / 2 - bbox[0]
    ty = cy - th / 2 - bbox[1]
    draw.text((tx, ty), text, fill=BLACK, font=font)


def finalize_1bit(img: Image.Image) -> Image.Image:
    """Reduce a 168x128 y aplica umbral estricto >127 para blanco/negro puro sin dithering."""
    small = img.resize((W, H), Image.Resampling.LANCZOS)
    return small.point(lambda p: WHITE if p > 127 else BLACK, mode="L")


def render_frame_a(out_path: Path) -> None:
    img = Image.new("L", (SW, SH), WHITE)
    draw = ImageDraw.Draw(img)
    draw_duck_base(draw, open_beak=False)
    draw_wing_folded(draw)
    draw_waves(draw)
    finalize_1bit(img).save(out_path)


def render_frame_b(out_path: Path) -> None:
    img = Image.new("L", (SW, SH), WHITE)
    draw = ImageDraw.Draw(img)
    draw_duck_base(draw, open_beak=True)
    draw_wing_flapping(draw)
    draw_waves(draw)
    draw_speech_bubble(draw, "¡CUAC!")
    finalize_1bit(img).save(out_path)


def main() -> None:
    project_root = Path(__file__).resolve().parents[2]
    out_dir = project_root / "assets" / "custom_sprites"
    out_dir.mkdir(parents=True, exist_ok=True)

    path_a = out_dir / "pato_a.png"
    path_b = out_dir / "pato_b.png"

    render_frame_a(path_a)
    render_frame_b(path_b)
    print(f"Generado: {path_a}")
    print(f"Generado: {path_b}")


if __name__ == "__main__":
    main()
