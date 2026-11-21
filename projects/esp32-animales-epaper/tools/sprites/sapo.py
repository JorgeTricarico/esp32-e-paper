import os
import math
from PIL import Image, ImageDraw, ImageFont

# Configuracion de lienzo final 1-bit (168x128 px)
WIDTH = 168
HEIGHT = 128
SCALE = 4  # Supersampling 4x para curvas suaves y trazos limpios
SW = WIDTH * SCALE
SH = HEIGHT * SCALE
STROKE = 13  # ~3.25 px al reducir 4x


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

    # Muesca triangular limpia en el lado derecho-abajo
    apex = (cx + int(rx * 0.34), cy + int(ry * 0.15))
    angle1 = math.radians(15)
    angle2 = math.radians(48)
    p1 = (cx + int(rx * math.cos(angle1)), cy + int(ry * math.sin(angle1)))
    p2 = (cx + int(rx * math.cos(angle2)), cy + int(ry * math.sin(angle2)))

    # Borrar el sector exterior de la muesca con blanco
    p1_out = (cx + int((rx + 18) * math.cos(angle1)), cy + int((ry + 18) * math.sin(angle1)))
    p2_out = (cx + int((rx + 18) * math.cos(angle2)), cy + int((ry + 18) * math.sin(angle2)))
    draw.polygon([apex, p1_out, p2_out], fill=255)

    # Trazar los dos bordes exactos de la muesca hasta el contorno
    draw.line([apex, p1], fill=0, width=STROKE - 1, joint="curve")
    draw.line([apex, p2], fill=0, width=STROKE - 1, joint="curve")

    # Nervaduras suaves sobre la hoja
    draw.arc([cx - rx + 26, cy - ry + 14, cx + 10, cy + ry - 10], start=195, end=270, fill=0, width=6)
    draw.line([(cx - int(rx * 0.55), cy + 4), (cx - int(rx * 0.15), cy + 2)], fill=0, width=6)


def draw_frog_head_and_eyes(draw, cx, cy, is_croaking=False):
    """Dibuja cabeza, ojos saltones arriba, fosas nasales y boca."""
    # Ojos saltones (sobresalen arriba de la cabeza)
    eye_dx = 22 * SCALE
    eye_y = cy - 22 * SCALE
    eye_r = 12 * SCALE

    for side in (-1, 1):
        ex = cx + side * eye_dx
        # Globo ocular blanco con contorno negro grueso
        draw.ellipse(
            [ex - eye_r, eye_y - eye_r, ex + eye_r, eye_y + eye_r],
            fill=255, outline=0, width=STROKE
        )
        # Pupila grande expresiva (mirando ligeramente al frente/arriba)
        pr = 6.2 * SCALE
        px = ex - side * (1.5 * SCALE)
        py = eye_y + (1.0 * SCALE)
        draw.ellipse([px - pr, py - pr, px + pr, py + pr], fill=0)
        # Brillo blanco en la pupila (catchlight)
        br = 2.2 * SCALE
        bx = px - 2.0 * SCALE
        by = py - 2.2 * SCALE
        draw.ellipse([bx - br, by - br, bx + br, by + br], fill=255)

    # Fosas nasales (dos puntitos)
    for side in (-1, 1):
        nx = cx + side * 6 * SCALE
        ny = cy - 12 * SCALE
        nr = 2.0 * SCALE
        draw.ellipse([nx - nr, ny - nr, nx + nr, ny + nr], fill=0)

    if not is_croaking:
        # Sonrisa ancha y amigable (limpia, sin comisuras superpuestas)
        mouth_box = [cx - 24 * SCALE, cy - 16 * SCALE, cx + 24 * SCALE, cy + 4 * SCALE]
        draw.arc(mouth_box, start=15, end=165, fill=0, width=STROKE - 1)
    else:
        # Boca abierta alegre croando hacia la derecha
        mouth_box = [cx - 12 * SCALE, cy - 10 * SCALE, cx + 18 * SCALE, cy + 2 * SCALE]
        draw.chord(mouth_box, start=0, end=180, fill=0, outline=0, width=STROKE - 3)
        draw.arc([cx - 20 * SCALE, cy - 16 * SCALE, cx + 22 * SCALE, cy - 2 * SCALE],
                 start=15, end=165, fill=0, width=STROKE - 3)


def draw_spots(draw, cx, cy):
    """Dibuja manchitas suaves redondeadas en los costados/mejillas del sapito."""
    spots = [
        (cx - 24 * SCALE, cy + 1 * SCALE, 3.6 * SCALE, 2.8 * SCALE),
        (cx - 21 * SCALE, cy + 10 * SCALE, 2.6 * SCALE, 2.2 * SCALE),
        (cx + 24 * SCALE, cy + 1 * SCALE, 3.6 * SCALE, 2.8 * SCALE),
        (cx + 21 * SCALE, cy + 10 * SCALE, 2.6 * SCALE, 2.2 * SCALE),
    ]
    for sx, sy, rx, ry in spots:
        draw.ellipse([sx - rx, sy - ry, sx + rx, sy + ry], fill=255, outline=0, width=7)


def draw_frog(draw, cx, cy, is_croaking=False):
    """Dibuja al sapito sentado sobre la hoja de nenufar.

    Mantiene exactamente la misma posicion de cuerpo, ojos, manchitas y patas
    en ambos frames para minimizar el refresco parcial en e-Paper.
    """
    # Patas traseras dobladas a los costados (detras del torso)
    for side in (-1, 1):
        hip_x = cx + side * 24 * SCALE
        hip_y = cy + 14 * SCALE
        draw.ellipse(
            [hip_x - 16 * SCALE, hip_y - 12 * SCALE, hip_x + 16 * SCALE, hip_y + 14 * SCALE],
            fill=255, outline=0, width=STROKE
        )
        foot_x = cx + side * 32 * SCALE
        foot_y = cy + 25 * SCALE
        draw.ellipse(
            [foot_x - 10 * SCALE, foot_y - 5 * SCALE, foot_x + 10 * SCALE, foot_y + 5 * SCALE],
            fill=255, outline=0, width=STROKE - 1
        )

    # Cuerpo principal regordete (cabeza + torso integrados)
    body_box = [cx - 32 * SCALE, cy - 22 * SCALE, cx + 32 * SCALE, cy + 26 * SCALE]
    draw.ellipse(body_box, fill=255, outline=0, width=STROKE)

    # Manchitas suaves en los costados
    draw_spots(draw, cx, cy)

    if not is_croaking:
        # Linea suave de pancita en reposo
        draw.arc([cx - 20 * SCALE, cy + 3 * SCALE, cx + 20 * SCALE, cy + 26 * SCALE],
                 start=200, end=340, fill=0, width=7)
    else:
        # Papada inflada croando (globo en la garganta)
        throat_box = [cx - 18 * SCALE, cy - 4 * SCALE, cx + 18 * SCALE, cy + 16 * SCALE]
        draw.ellipse(throat_box, fill=255, outline=0, width=STROKE)
        draw.arc([cx - 12 * SCALE, cy + 1 * SCALE, cx + 12 * SCALE, cy + 13 * SCALE],
                 start=20, end=160, fill=0, width=6)

    # Patitas delanteras apoyadas al frente (identicas en ambos frames)
    for side in (-1, 1):
        arm_x = cx + side * 12 * SCALE
        draw.rounded_rectangle(
            [arm_x - 5 * SCALE, cy + 6 * SCALE, arm_x + 5 * SCALE, cy + 28 * SCALE],
            radius=4 * SCALE, fill=255, outline=0, width=STROKE - 1
        )
        hx = arm_x + side * 1 * SCALE
        hy = cy + 27 * SCALE
        draw.ellipse([hx - 7 * SCALE, hy - 4 * SCALE, hx + 7 * SCALE, hy + 4 * SCALE],
                     fill=255, outline=0, width=STROKE - 2)

    # Cabeza, ojos y boca (encima del cuerpo para que los ojos queden limpios)
    draw_frog_head_and_eyes(draw, cx, cy, is_croaking=is_croaking)


def draw_speech_bubble(draw):
    """Dibuja la burbuja de onomatopeya '¡CROAC!' arriba a la derecha."""
    bx1, by1 = 107 * SCALE, 8 * SCALE
    bx2, by2 = 164 * SCALE, 38 * SCALE

    # Colita de la burbuja apuntando hacia la boca del sapo sin tocar el ojo
    tail = [
        (118 * SCALE, 35 * SCALE),
        (108 * SCALE, 45 * SCALE),
        (128 * SCALE, 36 * SCALE),
    ]
    draw.polygon(tail, fill=255, outline=0)
    draw.line([tail[0], tail[1]], fill=0, width=STROKE - 2)
    draw.line([tail[1], tail[2]], fill=0, width=STROKE - 2)

    # Globo redondeado
    draw.rounded_rectangle([bx1, by1, bx2, by2], radius=11 * SCALE, fill=255, outline=0, width=STROKE - 1)
    # Limpiar la interseccion interior de la colita con blanco
    draw.polygon([
        (119 * SCALE, 32 * SCALE),
        (111 * SCALE, 41 * SCALE),
        (128 * SCALE, 33 * SCALE),
    ], fill=255)

    # Texto "¡CROAC!" centrado
    font = get_font(11 * SCALE)
    text = "¡CROAC!"
    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    tx = (bx1 + bx2 - tw) // 2 - bbox[0]
    ty = (by1 + by2 - th) // 2 - bbox[1]
    draw.text((tx, ty), text, fill=0, font=font)


def finalize_1bit(img_high):
    """Reduce 4x y aplica umbral estricto (>127) para blanco (255) y negro (0) puro."""
    img_small = img_high.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    return img_small.point(lambda p: 255 if p > 127 else 0)


def main():
    out_dir = os.path.join("assets", "custom_sprites")
    os.makedirs(out_dir, exist_ok=True)

    frog_cx = 72 * SCALE
    frog_cy = 77 * SCALE
    pad_cx, pad_cy, pad_rx, pad_ry = 76 * SCALE, 107 * SCALE, 58 * SCALE, 13 * SCALE

    # Frame A: Sapito alegre sentado sobre hoja de nenufar
    img_a = Image.new("L", (SW, SH), 255)
    draw_a = ImageDraw.Draw(img_a)
    draw_lilypad(draw_a, cx=pad_cx, cy=pad_cy, rx=pad_rx, ry=pad_ry)
    draw_frog(draw_a, cx=frog_cx, cy=frog_cy, is_croaking=False)
    final_a = finalize_1bit(img_a)
    path_a = os.path.join(out_dir, "sapo_a.png")
    final_a.save(path_a)

    # Frame B: Misma posicion sentado sobre la hoja, papada inflada croando y burbuja ¡CROAC!
    img_b = Image.new("L", (SW, SH), 255)
    draw_b = ImageDraw.Draw(img_b)
    draw_lilypad(draw_b, cx=pad_cx, cy=pad_cy, rx=pad_rx, ry=pad_ry)
    draw_frog(draw_b, cx=frog_cx, cy=frog_cy, is_croaking=True)
    draw_speech_bubble(draw_b)
    final_b = finalize_1bit(img_b)
    path_b = os.path.join(out_dir, "sapo_b.png")
    final_b.save(path_b)

    print(f"Generados: {path_a} y {path_b}")


if __name__ == "__main__":
    main()
