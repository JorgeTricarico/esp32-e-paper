# Render the original Rudo face as currently in faces_bitmaps.h
from test_render_rudo import Framebuffer, draw_detailed_eye, draw_rudo_brows, draw_mouth_rest
import math

def draw_cigar_and_smoke_original(fb, mouthX=100, mouthY=138, iterFrame=0):
    x0 = mouthX + 14
    y0 = mouthY + 2
    x1 = mouthX + 48
    y1 = mouthY - 10

    # Draw thick cigar body
    for offset in range(-3, 4):
        ox = (offset * 1) // 3
        oy = offset
        fb.draw_line(x0 + ox, y0 + oy, x1 + ox, y1 + oy, 0, 1)

    # Cigar white band
    for offset in range(-2, 3):
        ox = (offset * 1) // 3
        oy = offset
        fb.draw_line(mouthX + 26 + ox, mouthY - 2 + oy, mouthX + 30 + ox, mouthY - 4 + oy, 1, 1)
    fb.draw_line(mouthX + 25, mouthY - 5, mouthX + 25, mouthY + 1, 0, 1)
    fb.draw_line(mouthX + 31, mouthY - 7, mouthX + 31, mouthY - 1, 0, 1)

    # Glowing burning tip
    fb.draw_pixel(x1 - 1, y1 - 2, 0)
    fb.draw_pixel(x1,     y1 - 1, 0)
    fb.draw_pixel(x1 + 1, y1 - 1, 0)
    fb.draw_pixel(x1,     y1,     1)
    fb.draw_pixel(x1 + 1, y1,     0)
    fb.draw_pixel(x1 + 2, y1,     0)
    fb.draw_pixel(x1,     y1 + 1, 0)
    fb.draw_pixel(x1 + 1, y1 + 2, 0)
    fb.draw_pixel(x1 - 1, y1 + 3, 0)

    smokeOriginX = x1 + 2
    smokeOriginY = y1 - 2
    for y in range(58):
        t = y / 58.0
        wave = math.sin(t * 6.28 + iterFrame * 0.9) * (3.0 + t * 8.0)
        sx = smokeOriginX + int(t * 18.0 + wave)
        sy = smokeOriginY - y
        if 0 <= sx < 200 and 24 <= sy < 200:
            fb.draw_pixel(sx, sy, 0)
            if y < 35: fb.draw_pixel(sx + 1, sy, 0)
            if 15 < y < 45 and ((sx + sy) % 3 != 0):
                fb.draw_pixel(sx - 1, sy, 0)

    ringY = smokeOriginY - 45 - ((iterFrame * 4) % 25)
    ringX = smokeOriginX + 18 + ((iterFrame * 2) % 8)
    if ringY >= 26:
        fb.draw_circle(ringX, ringY, 4, 0, False)
        fb.draw_circle(ringX + 3, ringY - 6, 3, 0, False)

def render_original_rudo(iterFrame=0):
    fb = Framebuffer()
    fb.draw_line(10, 18, 22, 18, 0, 2)
    fb.draw_line(164, 8, 186, 8, 0, 1)
    fb.draw_line(164, 18, 186, 18, 0, 1)

    pupilX = -4 if (iterFrame % 4 == 1) else (4 if (iterFrame % 4 == 3) else 0)
    pupilY = 0
    blinkL = 0
    blinkR = 1 if (iterFrame % 2 == 1) else 0

    draw_detailed_eye(fb, 58, 92, pupilX, pupilY, blinkL)
    draw_detailed_eye(fb, 142, 92, pupilX, pupilY, blinkR)
    draw_rudo_brows(fb, 92)
    draw_mouth_rest(fb, 100, 138)
    draw_cigar_and_smoke_original(fb, 100, 138, iterFrame)
    fb.draw_line(16, 162, 184, 162, 0, 1)
    return fb

orig_img = render_original_rudo(0).to_image(scale=3)
orig_img.save("scratch/rudo_original_frame0.png")
print("Saved scratch/rudo_original_frame0.png")
