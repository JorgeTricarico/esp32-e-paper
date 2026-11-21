import os
import math
from PIL import Image, ImageDraw, ImageFont

# Configuracion de lienzo final 1-bit (168x128 px)
WIDTH = 168
HEIGHT = 128
SCALE = 4  # Supersampling 4x para curvas suaves y trazos limpios
SW = WIDTH * SCALE
SH = HEIGHT * SCALE
STROKE = 12  # ~3 px al reducir 4x


def get_font(size):
    """Intenta cargar una fuente bold legible en Windows; fallback a default."""
    candidates = [
        "C:/Windows/Fonts/arialbd.ttf",
        "C:/Windows/Fonts/calibrib.ttf",
        "C:/Windows/Fonts/seguisb.ttf",
        "C:/Windows/Fonts/verdanab.ttf",
    ]
    for path in candidates:
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size)
            except Exception:
                pass
    return ImageFont.load_default()


def draw_lilypad(draw, cx, cy, rx, ry):
    """Dibuja una hoja de nenufar limpia con muesca y nervaduras."""
    bbox = [cx - rx, cy - ry, cx + rx, cy + ry]
    draw.ellipse(bbox, fill=255, outline=0, width=STROKE)

    apex = (cx + int(rx * 0.36), cy + int(ry * 0.15))
    angle1 = math.radians(14)
    angle2 = math.radians(46)
    p1 = (cx + int(rx * math.cos(angle1)), cy + int(ry * math.sin(angle1)))
    p2 = (cx + int(rx * math.cos(angle2)), cy + int(ry * math.sin(angle2)))

    p1_out = (cx + int((rx + 18) * math.cos(angle1)), cy + int((ry + 18) * math.sin(angle1)))
    p2_out = (cx + int((rx + 18) * math.cos(angle2)), cy + int((ry + 18) * math.sin(angle2)))
    draw.polygon([apex, p1_out, p2_out], fill=255)

    draw.line([apex, p1], fill=0, width=STROKE - 1, joint="curve")
    draw.line([apex, p2], fill=0, width=STROKE - 1, joint="curve")

    draw.line([(cx - int(rx * 0.65), cy + 2), (cx - int(rx * 0.25), cy + 1)], fill=0, width=6)


def draw_frog(draw, cx, cy, is_croaking=False):
    """Dibuja un sapito de libro infantil sentado sobre nenufar, con proporciones claras."""
    # 1. Patas traseras dobladas a los costados (detras del cuerpo)
    for side in (-1, 1):
        hip_x = cx + side * 28 * SCALE
        hip_y = cy + 12 * SCALE
        draw.ellipse(
            [hip_x - 15 * SCALE, hip_y - 12 * SCALE, hip_x + 15 * SCALE, hip_y + 13 * SCALE],
            fill=255, outline=0, width=STROKE
        )
        foot_x = cx + side * 37 * SCALE
        foot_y = cy + 23 * SCALE
        draw.ellipse(
            [foot_x - 11 * SCALE, foot_y - 5 * SCALE, foot_x + 11 * SCALE, foot_y + 5 * SCALE],
            fill=255, outline=0, width=STROKE - 1
        )

    # 2. Torso / panza redondeada
    torso_box = [cx - 28 * SCALE, cy - 8 * SCALE, cx + 28 * SCALE, cy + 26 * SCALE]
    draw.ellipse(torso_box, fill=255, outline=0, width=STROKE)

    # 3. Patitas delanteras cortas y abiertas a los costados (no tapan la cara ni la papada)
    for side in (-1, 1):
        shoulder_x = cx + side * 18 * SCALE
        shoulder_y = cy + 5 * SCALE
        hand_x = cx + side * 20 * SCALE
        hand_y = cy + 26 * SCALE
        # Brazo corto inclinado con contorno grueso
        draw.line([(shoulder_x, shoulder_y), (hand_x, hand_y)], fill=0, width=14 * SCALE)
        draw.line([(shoulder_x, shoulder_y), (hand_x, hand_y)], fill=255, width=8 * SCALE)
        # Manito con deditos redondeados apoyada en la hoja
        draw.ellipse(
            [hand_x - 8 * SCALE, hand_y - 4 * SCALE, hand_x + 8 * SCALE, hand_y + 5 * SCALE],
            fill=255, outline=0, width=STROKE - 2
        )

    # 4. Pancita o papada inflada croando (en el centro entre los bracitos)
    if not is_croaking:
        # Linea suave de pancita blanca
        draw.arc([cx - 15 * SCALE, cy + 4 * SCALE, cx + 15 * SCALE, cy + 22 * SCALE],
                 start=195, end=345, fill=0, width=7)
    else:
        # Saco vocal inflado grande y simpatico
        throat_box = [cx - 16 * SCALE, cy - 2 * SCALE, cx + 16 * SCALE, cy + 18 * SCALE]
        draw.ellipse(throat_box, fill=255, outline=0, width=STROKE)
        draw.arc([cx - 10 * SCALE, cy + 3 * SCALE, cx + 10 * SCALE, cy + 14 * SCALE],
                 start=20, end=160, fill=0, width=6)

    # 5. Cabeza ancha arriba del torso (tapa los hombros limpiamente)
    head_y = cy - 16 * SCALE
    head_box = [cx - 33 * SCALE, head_y - 18 * SCALE, cx + 33 * SCALE, head_y + 16 * SCALE]
    draw.ellipse(head_box, fill=255, outline=0, width=STROKE)

    # 6. Ojos saltones grandes arriba de la cabeza
    eye_dx = 18 * SCALE
    eye_y = head_y - 16 * SCALE
    eye_r = 11 * SCALE

    for side in (-1, 1):
        ex = cx + side * eye_dx
        draw.ellipse(
            [ex - eye_r, eye_y - eye_r, ex + eye_r, eye_y + eye_r],
            fill=255, outline=0, width=STROKE
        )
        # Pupila grande expresiva
        pr = 5.8 * SCALE
        px = ex - side * (1.2 * SCALE)
        py = eye_y + (0.8 * SCALE)
        draw.ellipse([px - pr, py - pr, px + pr, py + pr], fill=0)
        # Brillo en el ojo
        br = 2.0 * SCALE
        draw.ellipse([px - 2 * SCALE - br, py - 2 * SCALE - br, px - 2 * SCALE + br, py - 2 * SCALE + br], fill=255)

    # 7. Fosas nasales (dos puntitos centrados)
    for side in (-1, 1):
        nx = cx + side * 5 * SCALE
        ny = head_y - 4 * SCALE
        nr = 1.8 * SCALE
        draw.ellipse([nx - nr, ny - nr, nx + nr, ny + nr], fill=0)

    # 8. Boca / sonrisa ancha
    if not is_croaking:
        mouth_box = [cx - 22 * SCALE, head_y - 4 * SCALE, cx + 22 * SCALE, head_y + 10 * SCALE]
        draw.arc(mouth_box, start=15, end=165, fill=0, width=STROKE - 2)
    else:
        mouth_box = [cx - 12 * SCALE, head_y + 1 * SCALE, cx + 16 * SCALE, head_y + 11 * SCALE]
        draw.chord(mouth_box, start=0, end=180, fill=0, outline=0, width=STROKE - 3)
        draw.arc([cx - 20 * SCALE, head_y - 4 * SCALE, cx + 20 * SCALE, head_y + 7 * SCALE],
                 start=15, end=165, fill=0, width=STROKE - 3)


def draw_speech_bubble(draw):
    """Dibuja la burbuja de onomatopeya '¡CROAC!' arriba a la derecha."""
    bx1, by1 = 107 * SCALE, 8 * SCALE
    bx2, by2 = 164 * SCALE, 38 * SCALE

    tail = [
        (118 * SCALE, 35 * SCALE),
        (106 * SCALE, 46 * SCALE),
        (128 * SCALE, 36 * SCALE),
    ]
    draw.polygon(tail, fill=255, outline=0)
    draw.line([tail[0], tail[1]], fill=0, width=STROKE - 2)
    draw.line([tail[1], tail[2]], fill=0, width=STROKE - 2)

    draw.rounded_rectangle([bx1, by1, bx2, by2], radius=11 * SCALE, fill=255, outline=0, width=STROKE - 1)
    draw.polygon([
        (119 * SCALE, 32 * SCALE),
        (110 * SCALE, 42 * SCALE),
        (128 * SCALE, 33 * SCALE),
    ], fill=255)

    font = get_font(11 * SCALE)
    text = "¡CROAC!"
    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    tx = (bx1 + bx2 - tw) // 2 - bbox[0]
    ty = (by1 + by2 - th) // 2 - bbox[1]
    draw.text((tx, ty), text, fill=0, font=font)


def finalize_1bit(img_high):
    img_small = img_high.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    return img_small.point(lambda p: 255 if p > 127 else 0)


def main():
    out_dir = os.path.join("assets", "custom_sprites")
    os.makedirs(out_dir, exist_ok=True)

    frog_cx = 72 * SCALE
    frog_cy = 77 * SCALE
    pad_cx, pad_cy, pad_rx, pad_ry = 76 * SCALE, 106 * SCALE, 58 * SCALE, 13 * SCALE

    img_a = Image.new("L", (SW, SH), 255)
    draw_a = ImageDraw.Draw(img_a)
    draw_lilypad(draw_a, cx=pad_cx, cy=pad_cy, rx=pad_rx, ry=pad_ry)
    draw_frog(draw_a, cx=frog_cx, cy=frog_cy, is_croaking=False)
    path_a = os.path.join(out_dir, "sapo_a.png")
    finalize_1bit(img_a).save(path_a)

    img_b = Image.new("L", (SW, SH), 255)
    draw_b = ImageDraw.Draw(img_b)
    draw_lilypad(draw_b, cx=pad_cx, cy=pad_cy, rx=pad_rx, ry=pad_ry)
    draw_frog(draw_b, cx=frog_cx, cy=frog_cy, is_croaking=True)
    draw_speech_bubble(draw_b)
    path_b = os.path.join(out_dir, "sapo_b.png")
    finalize_1bit(img_b).save(path_b)

    print(f"Generados: {path_a} y {path_b}")


if __name__ == "__main__":
    main()
