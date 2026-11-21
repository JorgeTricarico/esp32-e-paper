"""Generador de sprites 1-bit (168x128) para la ovejita (Frame A y Frame B)."""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

WIDTH = 168
HEIGHT = 128
OUTLINE = 3  # Grosor de trazo limpio (3-4 px)

ROOT_DIR = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT_DIR / "assets" / "custom_sprites"


def draw_outlined_circles(
    draw: ImageDraw.ImageDraw,
    circles: list[tuple[int, int, int]],
    outline: int = OUTLINE,
) -> None:
    """Dibuja una nube de circulos con contorno negro uniforme e interior blanco puro."""
    for cx, cy, r in circles:
        draw.ellipse((cx - r - outline, cy - r - outline, cx + r + outline, cy + r + outline), fill=0)
    for cx, cy, r in circles:
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=255)


def draw_leg_standing(draw: ImageDraw.ImageDraw, x: int, y_top: int, y_bot: int, w: int = 8) -> None:
    """Dibuja una patita recta en reposo con pezuña redondeada."""
    half = w // 2
    draw.rectangle((x - half, y_top, x + half, y_bot - half), fill=0)
    draw.ellipse((x - half - 1, y_bot - w, x + half + 1, y_bot), fill=0)


def draw_leg_bent(
    draw: ImageDraw.ImageDraw,
    x_hip: int,
    y_hip: int,
    dx_knee: int,
    dy_knee: int,
    dx_hoof: int,
    dy_hoof: int,
    w: int = 8,
) -> None:
    """Dibuja una patita flexionada para el saltito."""
    x_k = x_hip + dx_knee
    y_k = y_hip + dy_knee
    x_h = x_k + dx_hoof
    y_h = y_k + dy_hoof
    draw.line((x_hip, y_hip, x_k, y_k), fill=0, width=w)
    draw.line((x_k, y_k, x_h, y_h), fill=0, width=w)
    r_k = w // 2
    r_h = w // 2 + 1
    draw.ellipse((x_k - r_k, y_k - r_k, x_k + r_k, y_k + r_k), fill=0)
    draw.ellipse((x_h - r_h, y_h - r_h, x_h + r_h, y_h + r_h), fill=0)


def draw_sheep(draw: ImageDraw.ImageDraw, dx: int = 0, dy: int = 0, jumping: bool = False) -> None:
    """Dibuja la ovejita completa con desplazamiento (dx, dy)."""
    # 1. Las 4 patitas (dibujadas antes de la nube de lana para que la lana caiga encima)
    if not jumping:
        # Traseras (ligeramente mas altas para dar profundidad)
        draw_leg_standing(draw, 39 + dx, 84 + dy, 113 + dy, w=8)
        draw_leg_standing(draw, 73 + dx, 84 + dy, 113 + dy, w=8)
        # Delanteras
        draw_leg_standing(draw, 49 + dx, 86 + dy, 117 + dy, w=8)
        draw_leg_standing(draw, 83 + dx, 86 + dy, 117 + dy, w=8)
    else:
        # Patitas flexionadas en pleno saltito alegre
        draw_leg_bent(draw, 39 + dx, 82 + dy, -7, 11, -7, 6, w=8)
        draw_leg_bent(draw, 49 + dx, 84 + dy, -5, 13, -7, 5, w=8)
        draw_leg_bent(draw, 73 + dx, 82 + dy, 5, 11, -5, 7, w=8)
        draw_leg_bent(draw, 83 + dx, 84 + dy, 8, 10, -4, 8, w=8)

    # 2. Cuerpo de nube de lana bien definido
    body_circles = [
        (25 + dx, 70 + dy, 11),  # Colita esponjosa izquierda
        (38 + dx, 61 + dy, 15),  # Lomo izquierdo
        (55 + dx, 56 + dy, 17),  # Lomo centro
        (73 + dx, 59 + dy, 16),  # Lomo derecho
        (35 + dx, 76 + dy, 14),  # Panza izquierda
        (51 + dx, 81 + dy, 16),  # Panza centro
        (69 + dx, 79 + dy, 15),  # Panza derecha
        (81 + dx, 71 + dy, 13),  # Pecho derecho
    ]
    draw_outlined_circles(draw, body_circles, outline=OUTLINE)

    # Detalles sutiles de rizos de lana en el cuerpo
    draw.arc((42 + dx, 62 + dy, 58 + dx, 76 + dy), start=20, end=155, fill=0, width=2)
    draw.arc((59 + dx, 66 + dy, 73 + dx, 79 + dy), start=15, end=145, fill=0, width=2)

    # 3. Orejitas oscuras caidas y amigables (a ambos lados de la cabeza)
    # Oreja izquierda
    draw.ellipse((68 + dx, 52 + dy, 85 + dx, 65 + dy), fill=0)
    # Oreja derecha
    draw.ellipse((109 + dx, 52 + dy, 126 + dx, 65 + dy), fill=0)

    # 4. Carita oscura amigable con borde blanco fino para separarla de orejas y lana
    face_box = (78 + dx, 44 + dy, 116 + dx, 82 + dy)
    draw.ellipse((face_box[0] - 2, face_box[1] - 2, face_box[2] + 2, face_box[3] + 2), fill=255)
    draw.ellipse(face_box, fill=0)

    # 5. Copete de lana en la frente
    tuft_circles = [
        (88 + dx, 43 + dy, 7),
        (97 + dx, 40 + dy, 8),
        (106 + dx, 43 + dy, 7),
    ]
    draw_outlined_circles(draw, tuft_circles, outline=OUTLINE)

    # 6. Ojos blancos expresivos sobre la carita oscura
    # Ojo izquierdo
    draw.ellipse((84 + dx, 52 + dy, 95 + dx, 64 + dy), fill=255)
    draw.ellipse((88 + dx, 56 + dy, 94 + dx, 62 + dy), fill=0)
    draw.ellipse((89 + dx, 57 + dy, 91 + dx, 59 + dy), fill=255)

    # Ojo derecho
    draw.ellipse((99 + dx, 52 + dy, 110 + dx, 64 + dy), fill=255)
    draw.ellipse((103 + dx, 56 + dy, 109 + dx, 62 + dy), fill=0)
    draw.ellipse((104 + dx, 57 + dy, 106 + dx, 59 + dy), fill=255)

    # 7. Naricita y boquita
    draw.line((94 + dx, 68 + dy, 97 + dx, 70 + dy, 100 + dx, 68 + dy), fill=255, width=2)
    if not jumping:
        # Sonrisa dulce
        draw.arc((90 + dx, 67 + dy, 104 + dx, 76 + dy), start=15, end=165, fill=255, width=2)
    else:
        # Boquita abierta balando
        draw.ellipse((92 + dx, 70 + dy, 102 + dx, 78 + dy), fill=255)
        draw.ellipse((94 + dx, 72 + dy, 100 + dx, 76 + dy), fill=0)

    # 8. Marquitas sutiles en el suelo debajo de la oveja saltando
    if jumping:
        draw.line((32 + dx, 118, 46 + dx, 118), fill=0, width=2)
        draw.line((56 + dx, 120, 68 + dx, 120), fill=0, width=2)
        draw.line((78 + dx, 118, 90 + dx, 118), fill=0, width=2)


def draw_speech_bubble(draw: ImageDraw.ImageDraw, text: str = "¡BEEE!") -> None:
    """Dibuja la burbuja de dialogo arriba a la derecha con trazo grueso."""
    bx0, by0, bx1, by1 = 112, 4, 165, 29
    pointer_outer = [(121, 27), (115, 38), (130, 27)]
    pointer_inner = [(123, 25), (118, 34), (128, 25)]

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

    # Frame A: reposo amigable (centrada en el lienzo)
    img_a = Image.new("L", (WIDTH, HEIGHT), 255)
    draw_a = ImageDraw.Draw(img_a)
    draw_sheep(draw_a, dx=0, dy=0, jumping=False)
    img_a = to_pure_bw(img_a)
    path_a = OUT_DIR / "oveja_a.png"
    img_a.save(path_a)

    # Frame B: misma base + saltito + boquita balando + burbuja "¡BEEE!" sin solaparse
    img_b = Image.new("L", (WIDTH, HEIGHT), 255)
    draw_b = ImageDraw.Draw(img_b)
    draw_sheep(draw_b, dx=0, dy=-6, jumping=True)
    draw_speech_bubble(draw_b, "¡BEEE!")
    img_b = to_pure_bw(img_b)
    path_b = OUT_DIR / "oveja_b.png"
    img_b.save(path_b)

    print(f"Generados: {path_a} y {path_b}")


if __name__ == "__main__":
    main()
