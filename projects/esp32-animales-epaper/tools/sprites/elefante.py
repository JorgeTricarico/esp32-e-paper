"""Generador de sprites 1-bit (168x128) para el elefantito tierno (Frame A y Frame B)."""

from pathlib import Path
import math
from PIL import Image, ImageDraw, ImageFont

WIDTH = 168
HEIGHT = 128
SCALE = 4
SW = WIDTH * SCALE
SH = HEIGHT * SCALE
STROKE = 14  # ~3.5 px al escalar 4x -> 1x


def s(val: float) -> float:
    return val * SCALE


def pt(x: float, y: float) -> tuple[float, float]:
    return (x * SCALE, y * SCALE)


def cubic_bezier(p0, p1, p2, p3, steps=40):
    pts = []
    for i in range(steps + 1):
        t = i / steps
        u = 1.0 - t
        x = (u**3) * p0[0] + 3 * (u**2) * t * p1[0] + 3 * u * (t**2) * p2[0] + (t**3) * p3[0]
        y = (u**3) * p0[1] + 3 * (u**2) * t * p1[1] + 3 * u * (t**2) * p2[1] + (t**3) * p3[1]
        pts.append((x, y))
    return pts


def draw_thick_curve(draw: ImageDraw.ImageDraw, points, width: int, fill=0):
    scaled = [pt(x, y) for x, y in points]
    draw.line(scaled, fill=fill, width=width, joint="curve")
    r = width / 2.0
    for x, y in (scaled[0], scaled[-1]):
        draw.ellipse((x - r, y - r, x + r, y + r), fill=fill)


def draw_filled_stroke_shape(draw: ImageDraw.ImageDraw, points, stroke_w=STROKE, fill=255, outline=0):
    scaled = [pt(x, y) for x, y in points]
    draw.polygon(scaled, fill=fill)
    draw.line(scaled + [scaled[0]], fill=outline, width=stroke_w, joint="curve")
    r = stroke_w / 2.0
    for x, y in scaled:
        draw.ellipse((x - r, y - r, x + r, y + r), fill=outline)


def draw_leg(draw: ImageDraw.ImageDraw, x_left: float, y_top: float, x_right: float, y_bottom: float, toenails=2):
    """Dibuja una patita redondeada con uñitas en la base."""
    # Rectángulo redondeado para la pata (sin borde superior cerrado para unir al cuerpo)
    pts = [
        (x_left, y_top),
        (x_left - 1, y_bottom - 5),
        (x_left + 2, y_bottom),
        (x_right - 2, y_bottom),
        (x_right + 1, y_bottom - 5),
        (x_right, y_top),
    ]
    scaled = [pt(x, y) for x, y in pts]
    draw.polygon(scaled, fill=255)
    # Contorno izquierdo, base y derecho
    draw_thick_curve(draw, pts, STROKE, fill=0)

    # Uñitas tiernas en la base
    leg_w = x_right - x_left
    step = leg_w / (toenails + 1)
    for i in range(1, toenails + 1):
        cx = x_left + step * i
        cy = y_bottom - 1
        rx = 2.2
        ry = 2.6
        draw.pieslice(
            (s(cx - rx), s(cy - ry), s(cx + rx), s(cy + ry)),
            start=180,
            end=360,
            fill=255,
            outline=0,
            width=max(8, STROKE - 4),
        )


def draw_tail(draw: ImageDraw.ImageDraw):
    """Colita curva con mechón tierno."""
    tail_curve = cubic_bezier((31, 76), (20, 77), (16, 86), (18, 95), steps=25)
    draw_thick_curve(draw, tail_curve, STROKE - 2, fill=0)

    # Mechón en la punta (forma de hojita/gota de 3 lóbulos suaves)
    tuft = [
        (18, 94),
        (12, 97),
        (13, 102),
        (17, 100),
        (17, 105),
        (22, 103),
        (21, 98),
        (25, 99),
        (23, 94),
    ]
    scaled = [pt(x, y) for x, y in tuft]
    draw.polygon(scaled, fill=0)


def draw_ear(draw: ImageDraw.ImageDraw, raised: bool = False):
    """Oreja grande redondeada: relajada en Frame A, un poco levantada en Frame B."""
    if not raised:
        outer = (
            cubic_bezier((74, 44), (56, 38), (46, 52), (50, 68), steps=20)
            + cubic_bezier((50, 68), (54, 82), (72, 82), (77, 68), steps=20)
        )
        inner = cubic_bezier((68, 52), (58, 52), (56, 64), (64, 71), steps=16)
    else:
        outer = (
            cubic_bezier((74, 42), (58, 28), (44, 38), (45, 55), steps=20)
            + cubic_bezier((45, 55), (46, 70), (66, 73), (76, 61), steps=20)
        )
        inner = cubic_bezier((67, 46), (57, 44), (53, 55), (60, 62), steps=16)

    scaled_outer = [pt(x, y) for x, y in outer]
    draw.polygon(scaled_outer, fill=255)
    draw.line(scaled_outer, fill=0, width=STROKE, joint="curve")
    r = STROKE / 2.0
    for x, y in (scaled_outer[0], scaled_outer[-1]):
        draw.ellipse((x - r, y - r, x + r, y + r), fill=0)

    draw_thick_curve(draw, inner, STROKE - 4, fill=0)


def draw_base_body(draw: ImageDraw.ImageDraw):
    """Dibuja la base corporal compartida por Frame A y Frame B."""
    # Patas traseras (fondo, un poco más arriba/atrás)
    draw_leg(draw, 46, 94, 59, 114, toenails=2)
    draw_leg(draw, 76, 94, 89, 114, toenails=2)

    # Colita detrás del cuerpo
    draw_tail(draw)

    # Cuerpo ovalado tierno
    draw.ellipse((s(29), s(58), s(91), s(103)), fill=255, outline=0, width=STROKE)

    # Patas delanteras (primer plano)
    draw_leg(draw, 34, 93, 49, 117, toenails=2)
    draw_leg(draw, 65, 93, 80, 117, toenails=2)

    # Cabeza redondeada (fusionada suavemente con el cuerpo)
    draw.ellipse((s(64), s(37), s(107), s(81)), fill=255)
    # Contorno superior y trasero de la cabeza (la parte frontal la dibujan la trompa y mejilla)
    draw.arc((s(64), s(37), s(107), s(81)), start=205, end=335, fill=0, width=STROKE)
    # Línea suave de barbilla/pecho conectando hacia la pata delantera
    chin = cubic_bezier((78, 79), (87, 81), (92, 78), (97, 73), steps=14)
    draw_thick_curve(draw, chin, STROKE, fill=0)


def draw_eye(draw: ImageDraw.ImageDraw, happy: bool = False):
    """Ojo tierno: abierto con brillo en Frame A, arco feliz en Frame B."""
    if not happy:
        cx, cy = 89, 54
        r = 3.8
        draw.ellipse((s(cx - r), s(cy - r), s(cx + r), s(cy + r)), fill=0)
        # Brillo blanco en el ojo
        draw.ellipse((s(cx - 1.6), s(cy - 2.0), s(cx + 0.4), s(cy + 0.0)), fill=255)
        # Cejita amigable
        brow = cubic_bezier((84, 46), (88, 44), (92, 45), (94, 47), steps=10)
        draw_thick_curve(draw, brow, STROKE - 5, fill=0)
    else:
        # Ojo feliz arqueado
        arc_pts = cubic_bezier((84, 56), (87, 50), (92, 50), (95, 56), steps=14)
        draw_thick_curve(draw, arc_pts, STROKE - 3, fill=0)
        # Cejita contenta bien separada del contorno superior
        brow = cubic_bezier((85, 45), (88, 43), (91, 44), (93, 46), steps=10)
        draw_thick_curve(draw, brow, STROKE - 5, fill=0)


def draw_trunk_relaxed(draw: ImageDraw.ImageDraw):
    """Trompita relajada hacia abajo con la punta suavemente curvada."""
    # Borde superior/exterior: sale de la frente (102, 50) y baja hasta la punta (118, 86)
    outer = (
        cubic_bezier((102, 50), (114, 55), (115, 72), (112, 83), steps=22)
        + cubic_bezier((112, 83), (111, 88), (118, 89), (120, 84), steps=12)
    )
    # Borde inferior/interior: sale de la mejilla (98, 65) y se une suavemente a la punta (120, 84)
    inner = (
        cubic_bezier((98, 64), (105, 65), (106, 74), (106, 83), steps=18)
        + cubic_bezier((106, 83), (107, 90), (118, 91), (120, 84), steps=14)
    )

    poly = outer + list(reversed(inner))
    draw.polygon([pt(x, y) for x, y in poly], fill=255)

    draw_thick_curve(draw, outer, STROKE, fill=0)
    draw_thick_curve(draw, inner, STROKE, fill=0)

    # Sonrisita tierna bajo la base de la trompa
    smile = cubic_bezier((90, 69), (94, 72), (98, 71), (101, 68), steps=10)
    draw_thick_curve(draw, smile, STROKE - 4, fill=0)

    # Arruguitas sutiles en la trompa
    draw_thick_curve(draw, [(106, 62), (110, 62)], STROKE - 6, fill=0)
    draw_thick_curve(draw, [(107, 70), (111, 70)], STROKE - 6, fill=0)


def draw_trunk_trumpet(draw: ImageDraw.ImageDraw):
    """Trompa bien levantada hacia arriba en forma de S tocando trompeta."""
    # Borde exterior/inferior de la S: sale de la mejilla (97, 73) y sube en S hasta (107, 22)
    outer_s = (
        cubic_bezier((97, 73), (112, 70), (116, 53), (106, 42), steps=22)
        + cubic_bezier((106, 42), (98, 34), (100, 26), (107, 22), steps=20)
    )
    # Borde interior/superior de la S: sale de la frente (100, 49) y sube paralelo hasta (98, 20)
    inner_s = (
        cubic_bezier((100, 49), (105, 51), (104, 47), (99, 43), steps=16)
        + cubic_bezier((99, 43), (91, 35), (92, 26), (98, 20), steps=18)
    )
    # Punta ensanchada tipo trompeta entre (98, 20) y (107, 22)
    bell = cubic_bezier((98, 20), (102, 18), (105, 19), (107, 22), steps=10)

    poly = outer_s + list(reversed(bell)) + list(reversed(inner_s))
    draw.polygon([pt(x, y) for x, y in poly], fill=255)

    draw_thick_curve(draw, outer_s, STROKE, fill=0)
    draw_thick_curve(draw, inner_s, STROKE, fill=0)
    draw_thick_curve(draw, bell, STROKE - 2, fill=0)

    # Sonrisita alegre
    smile = cubic_bezier((89, 68), (93, 71), (97, 70), (100, 67), steps=10)
    draw_thick_curve(draw, smile, STROKE - 4, fill=0)

    # Arruguitas en la curva de la trompa
    draw_thick_curve(draw, [(105, 56), (109, 55)], STROKE - 6, fill=0)
    draw_thick_curve(draw, [(99, 34), (103, 33)], STROKE - 6, fill=0)

    # Rayitas de sonido saliendo de la trompa
    draw_thick_curve(draw, [(102, 14), (103, 7)], STROKE - 4, fill=0)
    draw_thick_curve(draw, [(109, 16), (114, 11)], STROKE - 4, fill=0)
    draw_thick_curve(draw, [(112, 22), (116, 21)], STROKE - 4, fill=0)


def draw_speech_bubble(draw: ImageDraw.ImageDraw, text: str = "¡PRUU!"):
    """Burbuja de onomatopeya arriba a la derecha."""
    # Burbuja en x=119..165, y=6..33 con colita apuntando hacia la trompa
    bx0, by0, bx1, by1 = 119, 6, 165, 33
    rad = 9

    # Cuerpo de la burbuja redondeada
    draw.rounded_rectangle(
        (s(bx0), s(by0), s(bx1), s(by1)),
        radius=s(rad),
        fill=255,
        outline=0,
        width=STROKE - 2,
    )

    # Colita de la burbuja apuntando abajo-izquierda hacia la trompa
    tail_poly = [(123, 29), (113, 36), (129, 32)]
    draw.polygon([pt(x, y) for x, y in tail_poly], fill=255)
    draw_thick_curve(draw, [(122, 31), (113, 36), (128, 32)], STROKE - 3, fill=0)
    # Limpiar línea interna donde la colita se une a la burbuja
    draw.polygon([pt(122, 28), pt(129, 28), pt(127, 32), pt(121, 31)], fill=255)

    # Texto centrado dentro de la burbuja
    font = None
    for font_name in ("arialbd.ttf", "DejaVuSans-Bold.ttf", "calibrib.ttf", "seguiemj.ttf"):
        try:
            font = ImageFont.truetype(font_name, int(s(10.5)))
            break
        except OSError:
            continue
    if font is None:
        font = ImageFont.load_default()

    cx = (bx0 + bx1) / 2.0
    cy = (by0 + by1) / 2.0
    draw.text(pt(cx, cy), text, fill=0, font=font, anchor="mm")


def finalize_1bit(img_4x: Image.Image) -> Image.Image:
    """Reduce a 168x128 y aplica umbral duro (>127) para blanco (255) y negro (0) puro."""
    img_1x = img_4x.resize((WIDTH, HEIGHT), resample=Image.Resampling.BILINEAR)
    # Umbral estricto: >127 -> 255, <=127 -> 0, sin grises ni dithering
    return img_1x.point(lambda p: 255 if p > 127 else 0, mode="L")


def generate_frame_a(out_path: Path):
    img = Image.new("L", (SW, SH), 255)
    draw = ImageDraw.Draw(img)

    draw_base_body(draw)
    draw_ear(draw, raised=False)
    draw_eye(draw, happy=False)
    draw_trunk_relaxed(draw)

    final = finalize_1bit(img)
    final.save(out_path, format="PNG")


def generate_frame_b(out_path: Path):
    img = Image.new("L", (SW, SH), 255)
    draw = ImageDraw.Draw(img)

    draw_base_body(draw)
    draw_ear(draw, raised=True)
    draw_eye(draw, happy=True)
    draw_trunk_trumpet(draw)
    draw_speech_bubble(draw, "¡PRUU!")

    final = finalize_1bit(img)
    final.save(out_path, format="PNG")


def main():
    root = Path(__file__).resolve().parents[2]
    out_dir = root / "assets" / "custom_sprites"
    out_dir.mkdir(parents=True, exist_ok=True)

    frame_a = out_dir / "elefante_a.png"
    frame_b = out_dir / "elefante_b.png"

    generate_frame_a(frame_a)
    generate_frame_b(frame_b)

    for p in (frame_a, frame_b):
        im = Image.open(p)
        vals = set(im.getdata())
        print(f"{p.name}: size={im.size}, mode={im.mode}, values={sorted(vals)}")


if __name__ == "__main__":
    main()
