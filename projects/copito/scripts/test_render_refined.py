import math, os
from PIL import Image, ImageDraw

EPD_WIDTH = 200
EPD_HEIGHT = 200
EPD_BUFFER_SIZE = (EPD_WIDTH * EPD_HEIGHT) // 8

class Framebuffer:
    def __init__(self):
        self.buf = bytearray([0xFF] * EPD_BUFFER_SIZE)

    def clear(self, color=1):
        val = 0xFF if color else 0x00
        for i in range(EPD_BUFFER_SIZE): self.buf[i] = val

    def draw_pixel(self, x, y, color):
        if x < 0 or x >= EPD_WIDTH or y < 0 or y >= EPD_HEIGHT: return
        idx = y * 25 + (x >> 3)
        bit = 7 - (x & 0x07)
        if color: self.buf[idx] |= (1 << bit)
        else: self.buf[idx] &= ~(1 << bit)

    def get_pixel(self, x, y):
        if x < 0 or x >= EPD_WIDTH or y < 0 or y >= EPD_HEIGHT: return 1
        idx = y * 25 + (x >> 3)
        bit = 7 - (x & 0x07)
        return 1 if (self.buf[idx] & (1 << bit)) else 0

    def draw_dither_pixel(self, x, y, gray_level_16):
        if x < 0 or x >= EPD_WIDTH or y < 0 or y >= EPD_HEIGHT: return
        bayer = [
            [  0,  8,  2, 10 ],
            [ 12,  4, 14,  6 ],
            [  3, 11,  1,  9 ],
            [ 15,  7, 13,  5 ]
        ]
        threshold = bayer[y & 3][x & 3]
        self.draw_pixel(x, y, 1 if gray_level_16 > threshold else 0)

    def draw_line(self, x0, y0, x1, y1, color, thickness=1):
        dx = abs(x1 - x0); sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0); sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            if thickness <= 1:
                self.draw_pixel(x0, y0, color)
            else:
                half = thickness // 2
                for ty in range(-half, half + 1):
                    for tx in range(-half, half + 1):
                        self.draw_pixel(x0 + tx, y0 + ty, color)
            if x0 == x1 and y0 == y1: break
            e2 = 2 * err
            if e2 >= dy: err += dy; x0 += sx
            if e2 <= dx: err += dx; y0 += sy

    def draw_circle(self, xm, ym, r, color, fill=False):
        x = -r; y = 0; err = 2 - 2 * r
        while True:
            if fill:
                for i in range(xm + x, xm - x + 1):
                    self.draw_pixel(i, ym + y, color)
                    self.draw_pixel(i, ym - y, color)
            else:
                self.draw_pixel(xm - x, ym + y, color)
                self.draw_pixel(xm - y, ym - x, color)
                self.draw_pixel(xm + x, ym - y, color)
                self.draw_pixel(xm + y, ym + x, color)
            r_val = err
            if r_val <= y: y += 1; err += y * 2 + 1
            if r_val > x or err > y: x += 1; err += x * 2 + 1
            if x >= 0: break

    def to_image(self, scale=3):
        img = Image.new('RGB', (EPD_WIDTH * scale, EPD_HEIGHT * scale), (250, 250, 245))
        draw = ImageDraw.Draw(img)
        for y in range(EPD_HEIGHT):
            for x in range(EPD_WIDTH):
                if self.get_pixel(x, y) == 0:
                    draw.rectangle([x * scale, y * scale, (x + 1) * scale - 1, (y + 1) * scale - 1], fill=(22, 22, 26))
        return img

# --- PERFECTED RUDO EYE ---
def draw_rudo_eye(fb, cx, cy, pupilOffsetX=0, pupilOffsetY=0, blink=0):
    is_right = (cx > 100)
    rx = 16

    if blink == 3: # BLINK_CLOSED
        # Determined horizontal line with slight corner angle
        fb.draw_line(cx - 15, cy, cx + 15, cy, 0, 3)
        return

    # blink 0: OPEN (height ~17px), blink 1: HALF / SQUINT (height ~9px), blink 2: ALMOST (height ~5px)
    ry = 9 if blink == 0 else (4 if blink == 1 else 2)
    inner_slant = 2 if is_right else -2

    # 1. Solid bold upper eyelid (3px thick)
    for x in range(-rx, rx + 1):
        nx = x / float(rx)
        curve = (1.0 - nx * nx) * (ry * 0.75)
        slant = (x / float(rx)) * (-inner_slant)
        y_lid = cy - int(curve) + int(slant)

        fb.draw_pixel(cx + x, y_lid - 1, 0)
        fb.draw_pixel(cx + x, y_lid,     0)
        fb.draw_pixel(cx + x, y_lid + 1, 0)

        # Lower lid (clean 1px line curving up slightly)
        y_bot = cy + int((1.0 - nx * nx) * (ry * 0.45)) + int(slant * 0.4)
        fb.draw_pixel(cx + x, y_bot, 0)

    # 2. Eye Interior (Sclera & Dark Intense Iris)
    if blink < 2:
        for x in range(-rx + 1, rx):
            nx = x / float(rx)
            curve = (1.0 - nx * nx) * (ry * 0.75)
            slant = (x / float(rx)) * (-inner_slant)
            y_top = cy - int(curve) + int(slant) + 2
            y_bot = cy + int((1.0 - nx * nx) * (ry * 0.45)) + int(slant * 0.4) - 1
            for y in range(y_top, y_bot + 1):
                fb.draw_pixel(cx + x, y, 1)

        # Dark intense pupil & iris (elliptical, clipped by eyelids)
        iris_cx = cx + pupilOffsetX
        iris_cy = cy + pupilOffsetY + (1 if blink == 1 else 0)
        iris_rx = 6
        iris_ry = ry
        for iy in range(-iris_ry, iris_ry + 1):
            for ix in range(-iris_rx, iris_rx + 1):
                if (ix * ix) / float(iris_rx * iris_rx) + (iy * iy) / float(iris_ry * iris_ry) <= 1.0:
                    px = iris_cx + ix
                    py = iris_cy + iy
                    nx = (px - cx) / float(rx)
                    curve = (1.0 - nx * nx) * (ry * 0.75)
                    slant = nx * (-inner_slant)
                    y_top = cy - int(curve) + int(slant) + 1
                    y_bot = cy + int((1.0 - nx * nx) * (ry * 0.45)) + int(slant * 0.4)
                    if py >= y_top and py <= y_bot:
                        fb.draw_pixel(px, py, 0)

        # Sharp single pinpoint catchlight (1x1 dot, sharp glint)
        if blink == 0:
            fb.draw_pixel(iris_cx - 2, iris_cy - 1, 1)

    # 3. Determined Brow crease & under-eye grit fold
    # Upper crease
    for x in range(-12, 13):
        slant = (x / 12.0) * (-inner_slant)
        cy_crease = cy - ry - 4 + int(slant)
        fb.draw_pixel(cx + x, cy_crease, 0)

    # Under-eye fold (clean, subtle line under eye center)
    under_y = cy + ry + 3
    if is_right:
        fb.draw_line(cx - 8, under_y, cx + 6, under_y - 1, 0, 1)
    else:
        fb.draw_line(cx - 6, under_y - 1, cx + 8, under_y, 0, 1)

# --- PERFECTED RUDO NOSE ---
def draw_rudo_nose(fb):
    # Sharp angular nose at (100, 107..118)
    # Bridge ridge
    fb.draw_line(99, 107, 101, 116, 0, 2)
    # Under-tip sharp hook
    fb.draw_line(101, 116, 96, 118, 0, 2)
    # Left nostril shadow
    fb.draw_pixel(95, 117, 0)
    fb.draw_pixel(95, 118, 0)
    # Right nostril accent
    fb.draw_pixel(102, 116, 0)
    fb.draw_pixel(103, 117, 0)
    # Subtle dithered shadow on bridge right flank
    fb.draw_dither_pixel(101, 110, 7)
    fb.draw_dither_pixel(102, 112, 6)
    fb.draw_dither_pixel(102, 114, 5)

# --- PERFECTED RUDO BROWS ---
def draw_rudo_brows(fb, cy=88):
    fb.draw_line(36, cy - 26, 76, cy - 18, 0, 3)
    fb.draw_line(36, cy - 27, 74, cy - 19, 0, 2)
    fb.draw_line(164, cy - 26, 124, cy - 18, 0, 3)
    fb.draw_line(164, cy - 27, 126, cy - 19, 0, 2)

# --- PERFECTED MOUTH REST (SMIRK CLAMPING CIGAR) ---
def draw_rudo_mouth(fb, cx=100, cy=138, phoneme=0):
    if phoneme == 0: # MOUTH_REST
        for x in range(-20, 15):
            norm = (x + 3) / 18.0
            y = cy + int((1.0 - norm * norm) * 6.0)
            fb.draw_pixel(cx + x, y, 0)
            fb.draw_pixel(cx + x, y + 1, 0)
            fb.draw_pixel(cx + x, y + 2, 0)
        fb.draw_pixel(cx - 21, cy - 1, 0)
        fb.draw_pixel(cx - 22, cy - 2, 0)
    elif phoneme == 1: # MOUTH_E (Wide)
        for x in range(-22, 16):
            fb.draw_line(cx + x, cy - 2, cx + x, cy + 4, 0, 1)
        fb.draw_line(cx - 18, cy + 1, cx + 12, cy + 1, 1, 2)
    elif phoneme == 2: # MOUTH_O
        fb.draw_circle(cx - 4, cy + 2, 11, 0, True)
        fb.draw_circle(cx - 4, cy + 2, 7, 1, True)
    elif phoneme == 3: # MOUTH_A (Wide open)
        for y in range(0, 23):
            w = int(math.sqrt(1.0 - (y / 23.0)**2) * 22.0)
            fb.draw_line(cx - w, cy - 4 + y, cx + min(w, 14), cy - 4 + y, 0, 1)
        fb.draw_circle(cx - 2, cy + 12, 8, 1, True)
    elif phoneme == 4: # MOUTH_THINKING (Pursed)
        for x in range(-12, 12):
            norm = x / 12.0
            y = cy + 3 - int(math.cos(norm * 3.14159) * 3.0)
            fb.draw_pixel(cx + x, y, 0)
            fb.draw_pixel(cx + x, y + 1, 0)

# --- PERFECTED ROBUST HABANO & RIGHT-MARGIN SMOKE ---
def draw_cigar_and_smoke(fb, mouthX=100, mouthY=138, iterFrame=0):
    x0 = mouthX + 13   # 113
    y0 = mouthY + 1    # 139
    x1 = 165           # 165
    y1 = mouthY - 5    # 133

    dx = x1 - x0
    dy = y1 - y0

    # 1. Thick Cigar Body (11px thickness: offset -5 to +5)
    for x in range(x0, x1 + 1):
        t = (x - x0) / float(dx)
        yc = int(y0 + t * dy)

        # Crisp contour outlines top and bottom (2px thick)
        fb.draw_pixel(x, yc - 5, 0)
        fb.draw_pixel(x, yc - 6, 0)
        fb.draw_pixel(x, yc + 5, 0)
        fb.draw_pixel(x, yc + 6, 0)

        if x < 127: # Base tobacco wrapper
            for y in range(yc - 4, yc + 5):
                if y == yc - 2: fb.draw_dither_pixel(x, y, 8)
                else: fb.draw_pixel(x, y, 0)
        elif 127 <= x <= 135: # Anilla (Cigar Band): High contrast white band
            if x == 127 or x == 135:
                for y in range(yc - 4, yc + 5): fb.draw_pixel(x, y, 0)
            else:
                for y in range(yc - 4, yc + 5): fb.draw_pixel(x, y, 1) # Pure white
                # Cigar band brand seal / gold medal
                if 130 <= x <= 132:
                    fb.draw_pixel(x, yc, 0)
                    if x == 131:
                        fb.draw_pixel(x, yc - 2, 0)
                        fb.draw_pixel(x, yc - 1, 0)
                        fb.draw_pixel(x, yc + 1, 0)
                        fb.draw_pixel(x, yc + 2, 0)
        elif 136 <= x <= 157: # Outer tobacco body
            for y in range(yc - 4, yc + 5):
                if y == yc - 2: fb.draw_dither_pixel(x, y, 8)
                else: fb.draw_pixel(x, y, 0)
        else: # x >= 158: Ash & Burning Ember
            if x <= 160: # Hot burning core
                for y in range(yc - 4, yc + 5):
                    if abs(y - yc) <= 1: fb.draw_pixel(x, y, 1) # Bright ember
                    else: fb.draw_pixel(x, y, 0)
            else: # Dithered gray ash tip
                for y in range(yc - 4, yc + 5):
                    fb.draw_dither_pixel(x, y, 9)

    # Flat ash tip at x1
    yc_end = int(y0 + dy)
    for y in range(yc_end - 5, yc_end + 6):
        fb.draw_pixel(x1, y, 0)

    # Tiny glowing sparks near ash tip
    if iterFrame % 2 == 1:
        fb.draw_pixel(x1 + 1, yc_end - 2, 0)
        fb.draw_pixel(x1 + 2, yc_end + 1, 0)

    # 2. Animated Smoke: STRICTLY on the right margin (x: 166..195, never < 162)
    smokeOriginX = x1 + 2  # 167
    smokeOriginY = yc_end - 2 # 131

    smoke_height = smokeOriginY - 36
    for y_step in range(smoke_height):
        sy = smokeOriginY - y_step
        t = y_step / float(smoke_height) # 0.0 at ash tip, 1.0 at y=36

        # Organic wave undulating towards top-right
        # Starts smoothly at 167 with t*t easing at the base
        base_offset = (t ** 0.6) * 7.0 # 0 at bottom, ~7px right at top
        wave = math.sin(t * 7.2 - iterFrame * 0.85) * (1.5 + t * 6.5)
        sx = int(smokeOriginX + base_offset + wave)

        # STRICT SAFETY BOUNDARIES:
        if sx < 166: sx = 166
        if sx > 194: sx = 194

        fb.draw_pixel(sx, sy, 0)
        if y_step < 20:
            if sx + 1 <= 195: fb.draw_pixel(sx + 1, sy, 0)
        elif y_step < 55:
            if (sx + sy) % 3 != 0 and sx + 1 <= 195: fb.draw_pixel(sx + 1, sy, 0)
            if (sx * sy) % 4 == 0 and sx - 1 >= 166: fb.draw_pixel(sx - 1, sy, 0)
        else:
            fb.draw_dither_pixel(sx, sy, 6)
            if sx + 1 <= 195: fb.draw_dither_pixel(sx + 1, sy, 8)
            if sx - 1 >= 166: fb.draw_dither_pixel(sx - 1, sy, 8)

    # Secondary floating smoke ring at top right
    ring_phase = (iterFrame * 4) % 24
    ringY = 60 - ring_phase
    ringX = 181 + ((iterFrame * 2) % 6)
    if 172 <= ringX and ringX + 4 <= 195 and ringY >= 36:
        fb.draw_circle(ringX, ringY, 4, 0, False)
        fb.draw_circle(ringX + 2, ringY - 4, 3, 0, False)

# --- RENDER ALL 4 MAIN FRAMES FOR RUDO ---
def render_rudo_frame(state="idle", iter_frame=0, phoneme=0):
    fb = Framebuffer()
    # Top bar
    fb.draw_line(10, 18, 22, 18, 0, 2)
    fb.draw_line(164, 8, 186, 8, 0, 1)
    fb.draw_line(164, 18, 186, 18, 0, 1)

    cy_eyes = 92
    mouth_y = 138
    pupilX = 0
    pupilY = 0
    blinkL = 0
    blinkR = 0

    if state == "idle":
        pupilX = -3 if (iter_frame % 4 == 1) else (3 if (iter_frame % 4 == 3) else 0)
        blinkR = 1 if (iter_frame % 2 == 1) else 0 # Squint winking against smoke
        draw_rudo_eye(fb, 58, cy_eyes, pupilX, pupilY, blinkL)
        draw_rudo_eye(fb, 142, cy_eyes, pupilX, pupilY, blinkR)
        draw_rudo_brows(fb, cy_eyes)
        draw_rudo_nose(fb)
        draw_rudo_mouth(fb, 100, mouth_y, 0)
        draw_cigar_and_smoke(fb, 100, mouth_y, iter_frame)
        fb.draw_line(16, 162, 184, 162, 0, 1)

    elif state == "listening":
        cy_eyes = 86
        mouth_y = 134
        blinkR = 1 # focused squint
        draw_rudo_eye(fb, 58, cy_eyes, 0, 0, 0)
        draw_rudo_eye(fb, 142, cy_eyes, 0, 0, blinkR)
        draw_rudo_brows(fb, cy_eyes)
        draw_rudo_nose(fb)
        draw_rudo_mouth(fb, 100, mouth_y, 0)
        draw_cigar_and_smoke(fb, 100, mouth_y, iter_frame)
        # Sound ripples
        for arc in range(3):
            r = 16 + arc * 6
            for a in range(-40, 41, 8):
                rad = a * 0.0174533
                fb.draw_pixel(34 - int(math.cos(rad) * r), 86 + int(math.sin(rad) * r), 0)
        fb.draw_line(16, 162, 184, 162, 0, 1)

    elif state == "thinking":
        cy_eyes = 88
        mouth_y = 136
        blinkR = 1
        # Looking up-right thoughtfully
        draw_rudo_eye(fb, 58, cy_eyes, 4, -3, 0)
        draw_rudo_eye(fb, 142, cy_eyes, 4, -3, blinkR)
        draw_rudo_brows(fb, cy_eyes)
        draw_rudo_nose(fb)
        draw_rudo_mouth(fb, 100, mouth_y, 4) # Thinking mouth
        draw_cigar_and_smoke(fb, 100, mouth_y, iter_frame)
        # Thought bubbles
        fb.draw_circle(164, 60, 3, 0, False)
        fb.draw_circle(174, 46, 5, 0, False)
        fb.draw_circle(185, 30, 7, 0, False)
        fb.draw_line(16, 162, 184, 162, 0, 1)

    elif state == "speaking":
        cy_eyes = 88
        mouth_y = 138
        blinkR = 1
        draw_rudo_eye(fb, 58, cy_eyes, 0, 0, 0)
        draw_rudo_eye(fb, 142, cy_eyes, 0, 0, blinkR)
        draw_rudo_brows(fb, cy_eyes)
        draw_rudo_nose(fb)
        draw_rudo_mouth(fb, 100, mouth_y, phoneme)
        draw_cigar_and_smoke(fb, 100, mouth_y, iter_frame)
        fb.draw_line(16, 162, 184, 162, 0, 1)

    return fb

# Render and save
render_rudo_frame("idle", 0).to_image(scale=3).save("scratch/rudo_idle_f0_perfect.png")
render_rudo_frame("idle", 1).to_image(scale=3).save("scratch/rudo_idle_f1_perfect.png")
render_rudo_frame("listening", 0).to_image(scale=3).save("scratch/rudo_listening_perfect.png")
render_rudo_frame("thinking", 0).to_image(scale=3).save("scratch/rudo_thinking_perfect.png")
render_rudo_frame("speaking", 0, phoneme=1).to_image(scale=3).save("scratch/rudo_speaking_perfect.png")
print("All perfected states rendered successfully!")
