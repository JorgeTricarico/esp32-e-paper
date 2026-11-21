"""Generador de sprites monocromaticos 1-bit (168x128) para el cachorrito."""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

WIDTH = 168
HEIGHT = 128
SCALE = 4
SW = WIDTH * SCALE
SH = HEIGHT * SCALE

BLACK = 0
WHITE = 255


def s(val: float) -> int:
    """Escala coordenadas/tamanios al lienzo 4x."""
    return int(round(val * SCALE))


def draw_thick_line(
    draw: ImageDraw.ImageDraw,
    pts: list[tuple[float, float]],
    width: float = 3.5,
    fill: int = BLACK,
) -> None:
    """Dibuja una polilinea gruesa con uniones redondeadas."""
    w = s(width)
    r = w // 2
    spts = [(s(x), s(y)) for x, y in pts]
    for i in range(len(spts) - 1):
        draw.line([spts[i], spts[i + 1]], fill=fill, width=w)
    for x, y in spts:
        draw.ellipse([x - r, y - r, x + r, y + r], fill=fill)


def draw_ellipse_outline(
    draw: ImageDraw.ImageDraw,
    cx: float,
    cy: float,
    rx: float,
    ry: float,
    fill: int = WHITE,
    outline: int = BLACK,
    width: float = 3.5,
) -> None:
    """Dibuja una elipse centrada en (cx, cy) con relleno y borde grueso."""
    bbox = [s(cx - rx), s(cy - ry), s(cx + rx), s(cy + ry)]
    draw.ellipse(bbox, fill=fill, outline=outline, width=s(width))


def draw_rotated_ellipse(
    img: Image.Image,
    cx: float,
    cy: float,
    rx: float,
    ry: float,
    angle: float,
    fill: int = WHITE,
    outline: int | None = BLACK,
    width: float = 3.5,
) -> None:
    """Dibuja una elipse rotada suavemente sobre el lienzo."""
    pad = int(max(rx, ry) * 2 + width * 2 + 10)
    box_size = s(pad * 2)
    tmp = Image.new("L", (box_size, box_size), WHITE)
    mask = Image.new("L", (box_size, box_size), 0)
    mdraw = ImageDraw.Draw(mask)
    tdraw = ImageDraw.Draw(tmp)

    c = box_size // 2
    bbox = [c - s(rx), c - s(ry), c + s(rx), c + s(ry)]

    if outline is not None:
        tdraw.ellipse(bbox, fill=fill, outline=outline, width=s(width))
        mdraw.ellipse(bbox, fill=255)
    else:
        tdraw.ellipse(bbox, fill=fill)
        mdraw.ellipse(bbox, fill=255)

    tmp_rot = tmp.rotate(angle, resample=Image.Resampling.BICUBIC, fillcolor=WHITE)
    mask_rot = mask.rotate(angle, resample=Image.Resampling.BICUBIC, fillcolor=0)

    paste_x = s(cx) - c
    paste_y = s(cy) - c
    img.paste(tmp_rot, (paste_x, paste_y), mask_rot)


def draw_tail_a(draw: ImageDraw.ImageDraw, cx: float) -> None:
    """Colita quieta curvada suavemente hacia la izquierda."""
    pts = [
        (cx - 21, 100),
        (cx - 31, 103),
        (cx - 38, 99),
        (cx - 41, 92),
    ]
    draw_thick_line(draw, pts, width=8.0, fill=BLACK)
    draw_thick_line(draw, pts, width=2.4, fill=WHITE)


def draw_tail_b(draw: ImageDraw.ImageDraw, cx: float) -> None:
    """Colita arriba moviendose con rayitas de movimiento."""
    pts = [
        (cx - 21, 97),
        (cx - 31, 90),
        (cx - 37, 80),
        (cx - 36, 70),
    ]
    draw_thick_line(draw, pts, width=8.0, fill=BLACK)
    draw_thick_line(draw, pts, width=2.4, fill=WHITE)

    # Rayitas de movimiento limpias a la izquierda y arriba de la cola
    draw_thick_line(draw, [(cx - 46, 79), (cx - 48, 71), (cx - 45, 64)], width=2.3, fill=BLACK)
    draw_thick_line(draw, [(cx - 40, 61), (cx - 35, 58), (cx - 30, 60)], width=2.3, fill=BLACK)


def draw_body_and_paws(draw: ImageDraw.ImageDraw, cx: float) -> None:
    """Dibuja el cuerpo sentado, patitas traseras, delanteras y collar con chapita."""
    # Patitas traseras dobladas (muslos)
    draw_ellipse_outline(draw, cx - 22, 102, 11, 13, fill=WHITE, outline=BLACK, width=3.5)
    draw_ellipse_outline(draw, cx + 22, 102, 11, 13, fill=WHITE, outline=BLACK, width=3.5)

    # Pies traseros saliendo hacia afuera
    draw_ellipse_outline(draw, cx - 25, 113, 9, 5.5, fill=WHITE, outline=BLACK, width=3.5)
    draw_ellipse_outline(draw, cx + 25, 113, 9, 5.5, fill=WHITE, outline=BLACK, width=3.5)

    # Torso central sentado
    draw_ellipse_outline(draw, cx, 92, 22, 23, fill=WHITE, outline=BLACK, width=3.5)
    draw.ellipse([s(cx - 19), s(75), s(cx + 19), s(111)], fill=WHITE)

    # Patitas delanteras rectas al frente
    for side in (-1, 1):
        px = cx + side * 10.5
        draw.rounded_rectangle(
            [s(px - 6.5), s(87), s(px + 6.5), s(115)],
            radius=s(5),
            fill=WHITE,
            outline=BLACK,
            width=s(3.3),
        )
        draw.rectangle([s(px - 4.5), s(84), s(px + 4.5), s(91)], fill=WHITE)
        draw_ellipse_outline(draw, px, 114, 7.5, 5, fill=WHITE, outline=BLACK, width=3.3)
        draw_thick_line(draw, [(px - 2.3, 114), (px - 2.3, 118)], width=2.0, fill=BLACK)
        draw_thick_line(draw, [(px + 2.3, 114), (px + 2.3, 118)], width=2.0, fill=BLACK)

    # Collarcito curvado en el cuello
    collar_pts = [(cx - 18, 70), (cx - 9, 73.5), (cx, 74.5), (cx + 9, 73.5), (cx + 18, 70)]
    draw_thick_line(draw, collar_pts, width=5.6, fill=BLACK)

    # Chapita redonda colgando al centro del collar
    draw_ellipse_outline(draw, cx, 81, 5.5, 5.5, fill=WHITE, outline=BLACK, width=2.8)
    draw_ellipse_outline(draw, cx, 81, 1.5, 1.5, fill=BLACK, outline=BLACK, width=1.0)


def draw_ears_a(img: Image.Image, cx: float) -> None:
    """Orejas caidas adorables (Frame A)."""
    draw_rotated_ellipse(img, cx - 29, 53, 8.5, 17, angle=-14, fill=WHITE, outline=BLACK, width=3.5)
    draw_rotated_ellipse(img, cx + 29, 53, 8.5, 17, angle=14, fill=WHITE, outline=BLACK, width=3.5)


def draw_ears_b(img: Image.Image, draw: ImageDraw.ImageDraw, cx: float) -> None:
    """Mismas orejas redondeadas del Frame A, abiertas/elevadas hacia los costados (aleteo alegre)."""
    draw_rotated_ellipse(img, cx - 33, 47, 8.5, 17, angle=-38, fill=WHITE, outline=BLACK, width=3.5)
    draw_rotated_ellipse(img, cx + 33, 47, 8.5, 17, angle=38, fill=WHITE, outline=BLACK, width=3.5)


def draw_head(img: Image.Image, draw: ImageDraw.ImageDraw, cx: float, frame: str) -> None:
    """Dibuja la cabeza, manchita en el ojo, ojitos con brillo, nariz y boca."""
    # Base de cabeza redondeada con mejillas suaves
    draw_ellipse_outline(draw, cx, 46, 28, 23, fill=WHITE, outline=BLACK, width=3.5)
    draw_ellipse_outline(draw, cx - 17, 52, 13, 12, fill=WHITE, outline=BLACK, width=3.5)
    draw_ellipse_outline(draw, cx + 17, 52, 13, 12, fill=WHITE, outline=BLACK, width=3.5)
    draw.ellipse([s(cx - 25.5), s(25.5), s(cx + 25.5), s(66.5)], fill=WHITE)
    draw.ellipse([s(cx - 27.5), s(42), s(cx + 27.5), s(62)], fill=WHITE)

    # Manchita organica alrededor del ojo izquierdo
    draw_rotated_ellipse(img, cx - 12.5, 43, 10.2, 8.8, angle=15, fill=BLACK, outline=None)

    # Ojo izquierdo (dentro de la manchita negra): aro blanco + pupila negra + brillo blanco
    draw_ellipse_outline(draw, cx - 12, 43, 5.2, 5.4, fill=WHITE, outline=WHITE, width=1.0)
    draw_ellipse_outline(draw, cx - 11.5, 43, 3.6, 3.9, fill=BLACK, outline=BLACK, width=1.0)
    draw_ellipse_outline(draw, cx - 12.8, 41.4, 1.3, 1.3, fill=WHITE, outline=WHITE, width=0.5)

    # Ojo derecho (sobre fondo blanco): ojo negro expresivo con brillo blanco
    draw_ellipse_outline(draw, cx + 12, 43, 4.4, 4.9, fill=BLACK, outline=BLACK, width=1.0)
    draw_ellipse_outline(draw, cx + 10.7, 41.3, 1.5, 1.5, fill=WHITE, outline=WHITE, width=0.5)

    # Naricita negra redondeada con puntito de brillo blanco
    nose_pts = [(cx - 4.8, 49.5), (cx + 4.8, 49.5), (cx, 54.5)]
    draw.polygon([(s(x), s(y)) for x, y in nose_pts], fill=BLACK)
    draw_thick_line(draw, nose_pts + [nose_pts[0]], width=2.2, fill=BLACK)
    draw_ellipse_outline(draw, cx - 1.4, 50.8, 1.1, 0.8, fill=WHITE, outline=WHITE, width=0.5)

    # Boquita
    if frame == "a":
        draw_thick_line(draw, [(cx, 54.5), (cx, 57.5)], width=2.4, fill=BLACK)
        draw_thick_line(draw, [(cx - 6.5, 57.2), (cx - 3.0, 60.0), (cx, 57.5)], width=2.4, fill=BLACK)
        draw_thick_line(draw, [(cx, 57.5), (cx + 3.0, 60.0), (cx + 6.5, 57.2)], width=2.4, fill=BLACK)
    else:
        # Boquita abierta alegre con lengua afuera, bien separada del collar
        draw_thick_line(draw, [(cx, 54.0), (cx, 55.5)], width=2.4, fill=BLACK)
        # Cavidad bucal negra
        draw.chord(
            [s(cx - 7.5), s(49.5), s(cx + 7.5), s(61.5)],
            start=0,
            end=180,
            fill=BLACK,
            outline=BLACK,
            width=s(2.4),
        )
        # Lengua afuera (blanca con contorno negro y rayita central)
        draw_ellipse_outline(draw, cx, 60.5, 4.2, 3.8, fill=WHITE, outline=BLACK, width=2.3)
        draw_thick_line(draw, [(cx, 58.6), (cx, 62.0)], width=1.4, fill=BLACK)


def draw_speech_bubble(draw: ImageDraw.ImageDraw) -> None:
    """Dibuja la burbuja arriba a la derecha con '¡GUAU!' en Frame B."""
    bx1, by1, bx2, by2 = 111, 5, 164, 34
    draw.rounded_rectangle(
        [s(bx1), s(by1), s(bx2), s(by2)],
        radius=s(8),
        fill=WHITE,
        outline=BLACK,
        width=s(3.0),
    )

    # Piquito apuntando abajo-izquierda hacia el cachorrito
    tail_poly = [(121, 32), (114, 38), (129, 32)]
    draw.polygon([(s(x), s(y)) for x, y in tail_poly], fill=WHITE)
    draw_thick_line(draw, [(121, 34), (114, 38), (128, 34)], width=3.0, fill=BLACK)
    # Limpiar el borde inferior de la burbuja justo donde nace el piquito
    draw.polygon([(s(122.0), s(31.5)), (s(116.5), s(36.2)), (s(127.2), s(31.5))], fill=WHITE)

    font_path = "C:/Windows/Fonts/comicbd.ttf"
    try:
        font = ImageFont.truetype(font_path, s(11.8))
    except OSError:
        font = ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf", s(11.8))

    text = "¡GUAU!"
    cx = s((bx1 + bx2) / 2)
    cy = s((by1 + by2) / 2)
    draw.text((cx, cy), text, fill=BLACK, font=font, anchor="mm")


def render_frame(frame: str) -> Image.Image:
    """Renderiza un frame ('a' o 'b') y lo convierte a 168x128 blanco y negro puro."""
    img = Image.new("L", (SW, SH), WHITE)
    draw = ImageDraw.Draw(img)

    cx = 70.0

    if frame == "a":
        draw_tail_a(draw, cx)
        draw_body_and_paws(draw, cx)
        draw_ears_a(img, cx)
        draw_head(img, draw, cx, frame="a")
    else:
        draw_tail_b(draw, cx)
        draw_body_and_paws(draw, cx)
        draw_ears_b(img, draw, cx)
        draw_head(img, draw, cx, frame="b")
        draw_speech_bubble(draw)

    small = img.resize((WIDTH, HEIGHT), resample=Image.Resampling.BILINEAR)
    binary = small.point(lambda p: WHITE if p > 127 else BLACK, mode="L")
    return binary


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    out_dir = root / "assets" / "custom_sprites"
    out_dir.mkdir(parents=True, exist_ok=True)

    for frame in ("a", "b"):
        out_path = out_dir / f"perro_{frame}.png"
        im = render_frame(frame)
        im.save(out_path, format="PNG")
        print(
            f"Guardado: {out_path} ({im.size}, mode={im.mode}, "
            f"valores={sorted(set(im.get_flattened_data()))})"
        )


if __name__ == "__main__":
    main()
