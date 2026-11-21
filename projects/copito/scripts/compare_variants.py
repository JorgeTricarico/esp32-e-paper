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

# --- EYE VARIANT 1: Focused Gritty Manga/Comics Badass (Sharp almond, upper lid resting on iris, under-eye fold) ---
def draw_rudo_eye_v1(fb, cx, cy, pupilOffsetX, pupilOffsetY, blink):
    is_right = (cx > 100)
    rx = 16
    
    if blink == 3: # BLINK_CLOSED
        fb.draw_line(cx - 16, cy, cx + 16, cy, 0, 3)
        if is_right: fb.draw_line(cx + 14, cy - 1, cx + 18, cy - 3, 0, 2)
        else: fb.draw_line(cx - 14, cy - 1, cx - 18, cy - 3, 0, 2)
        return

    # Proportions: narrowed and determined
    ry = 9 if blink == 0 else 5  # 9px half-height when open (18px total!), 5px when squinting (10px total!)
    
    # Inner corner is angled slightly down towards nose
    inner_slant = 2 if is_right else -2 # positive for right eye moves left down

    # 1. Upper eyelid: Heavy bold line (3px thick)
    # Shapes the upper eye: starts higher at outer temple, slopes straight/slightly curved down to nose
    for x in range(-rx, rx + 1):
        nx = x / float(rx) # -1 to +1
        # Eyelid height profile: flatter, more determined
        curve = (1.0 - nx * nx) * (ry * 0.7)
        # Slant down towards nose
        slant = (x / float(rx)) * (-inner_slant)
        y_lid = cy - int(curve) + int(slant)
        
        # Heavy 3px upper lid
        fb.draw_pixel(cx + x, y_lid - 1, 0)
        fb.draw_pixel(cx + x, y_lid, 0)
        fb.draw_pixel(cx + x, y_lid + 1, 0)
        
        # Lower lid (1-2 px subtle curve)
        y_bot = cy + int((1.0 - nx * nx) * (ry * 0.5)) + int(slant * 0.5)
        fb.draw_pixel(cx + x, y_bot, 0)
        if abs(nx) < 0.6:
            fb.draw_pixel(cx + x, y_bot + 1, 0)

    # 2. Eye interior (Sclera & Iris)
    if blink < 2:
        # Fill sclera with white
        for x in range(-rx + 2, rx - 1):
            nx = x / float(rx)
            curve = (1.0 - nx * nx) * (ry * 0.7)
            slant = (x / float(rx)) * (-inner_slant)
            y_top = cy - int(curve) + int(slant) + 2
            y_bot = cy + int((1.0 - nx * nx) * (ry * 0.5)) + int(slant * 0.5) - 1
            for y in range(y_top, y_bot + 1):
                fb.draw_pixel(cx + x, y, 1)

        # Iris: dark focused circle/ellipse
        iris_cx = cx + pupilOffsetX
        iris_cy = cy + pupilOffsetY
        iris_rx = 6
        iris_ry = ry - 1
        for iy in range(-iris_ry, iris_ry + 1):
            for ix in range(-iris_rx, iris_rx + 1):
                if (ix * ix) / float(iris_rx * iris_rx) + (iy * iy) / float(iris_ry * iris_ry) <= 1.0:
                    px = iris_cx + ix
                    py = iris_cy + iy
                    # Keep inside eye boundaries
                    nx = (px - cx) / float(rx)
                    curve = (1.0 - nx * nx) * (ry * 0.7)
                    slant = (nx) * (-inner_slant)
                    y_top = cy - int(curve) + int(slant) + 1
                    y_bot = cy + int((1.0 - nx * nx) * (ry * 0.5)) + int(slant * 0.5)
                    if py >= y_top and py <= y_bot:
                        fb.draw_pixel(px, py, 0)

        # Intense focused pinpoint catchlight (1x1 dot, sharp glint)
        if blink == 0:
            fb.draw_pixel(iris_cx - 2, iris_cy - 2, 1)

    # 3. Character accents:
    # Upper crease line (tabique / pliegue de párpado)
    for x in range(-12, 13):
        nx = x / 12.0
        slant = nx * (-inner_slant)
        cy_crease = cy - ry - 4 + int(slant)
        fb.draw_pixel(cx + x, cy_crease, 0)

    # Under-eye grit line (pliegue inferior / cansancio rudo)
    under_y = cy + ry + 3
    if is_right:
        fb.draw_line(cx - 10, under_y, cx + 8, under_y - 1, 0, 1)
        # Crow's feet at outer temple
        fb.draw_line(cx + 17, cy - 2, cx + 22, cy - 3, 0, 1)
        fb.draw_line(cx + 16, cy + 2, cx + 21, cy + 4, 0, 1)
    else:
        fb.draw_line(cx - 8, under_y - 1, cx + 10, under_y, 0, 1)
        # Crow's feet at outer temple
        fb.draw_line(cx - 17, cy - 2, cx - 22, cy - 3, 0, 1)
        fb.draw_line(cx - 16, cy + 2, cx - 21, cy + 4, 0, 1)

# Nose Variant 1: Chiseled Angular Noir Nose
def draw_rudo_nose_v1(fb):
    # Bridge ridge (tabique): from (98, 107) down-right to (101, 116)
    fb.draw_line(98, 107, 101, 116, 0, 2)
    # Under-tip angle: from (101, 116) hooking to (95, 118)
    fb.draw_line(101, 116, 95, 118, 0, 2)
    # Left nostril shadow
    fb.draw_pixel(94, 117, 0)
    fb.draw_pixel(94, 118, 0)
    # Right nostril subtle wing
    fb.draw_pixel(103, 116, 0)
    fb.draw_pixel(103, 117, 0)
    # Subtle dithered shadow on right side of bridge
    fb.draw_dither_pixel(101, 111, 7)
    fb.draw_dither_pixel(101, 113, 7)
    fb.draw_dither_pixel(102, 115, 6)

# Brows
def draw_rudo_brows(fb, cy=88):
    fb.draw_line(36, cy - 26, 76, cy - 18, 0, 3)
    fb.draw_line(36, cy - 27, 74, cy - 19, 0, 2)
    fb.draw_line(164, cy - 26, 124, cy - 18, 0, 3)
    fb.draw_line(164, cy - 27, 126, cy - 19, 0, 2)

# Mouth
def draw_rudo_mouth(fb, cx=100, cy=138):
    # Tough smirk / cigar grip
    # Left side smiles slightly up to 80, right side clamps the cigar
    for x in range(-20, 15):
        norm = (x + 3) / 18.0
        y = cy + int((1.0 - norm * norm) * 6.0)
        fb.draw_pixel(cx + x, y, 0)
        fb.draw_pixel(cx + x, y + 1, 0)
        fb.draw_pixel(cx + x, y + 2, 0)
    # Left smile corner
    fb.draw_pixel(cx - 21, cy - 1, 0)
    fb.draw_pixel(cx - 22, cy - 2, 0)

# Robust Habano & Right-margin Smoke
def draw_cigar_and_smoke_v1(fb, mouthX=100, mouthY=138, iterFrame=0):
    x0 = mouthX + 13   # 113
    y0 = mouthY + 1    # 139
    x1 = 165           # 165
    y1 = mouthY - 5    # 133

    dx = x1 - x0
    dy = y1 - y0

    # 1. 11px Thick Cigar Body
    for x in range(x0, x1 + 1):
        t = (x - x0) / float(dx)
        yc = int(y0 + t * dy)

        # Bold outlines top and bottom (2px)
        fb.draw_pixel(x, yc - 5, 0)
        fb.draw_pixel(x, yc - 6, 0)
        fb.draw_pixel(x, yc + 5, 0)
        fb.draw_pixel(x, yc + 6, 0)

        if x < 127: # Base tobacco
            for y in range(yc - 4, yc + 5):
                if y == yc - 2: fb.draw_dither_pixel(x, y, 8)
                else: fb.draw_pixel(x, y, 0)
        elif 127 <= x <= 135: # Anilla (Cigar Band)
            if x == 127 or x == 135:
                for y in range(yc - 4, yc + 5): fb.draw_pixel(x, y, 0)
            else:
                for y in range(yc - 4, yc + 5): fb.draw_pixel(x, y, 1) # White contrast band
                # Diamond gold emblem
                if (x == 130 or x == 131 or x == 132):
                    fb.draw_pixel(x, yc, 0)
                    if x == 131:
                        fb.draw_pixel(x, yc - 2, 0)
                        fb.draw_pixel(x, yc - 1, 0)
                        fb.draw_pixel(x, yc + 1, 0)
                        fb.draw_pixel(x, yc + 2, 0)
        elif 136 <= x <= 157: # Outer body tobacco
            for y in range(yc - 4, yc + 5):
                if y == yc - 2: fb.draw_dither_pixel(x, y, 8)
                else: fb.draw_pixel(x, y, 0)
        else: # x >= 158: Ash & Ember
            if x <= 160: # Hot burning ember
                for y in range(yc - 4, yc + 5):
                    if abs(y - yc) <= 1: fb.draw_pixel(x, y, 1) # White burning coal
                    else: fb.draw_pixel(x, y, 0)
            else: # Ash
                for y in range(yc - 4, yc + 5):
                    fb.draw_dither_pixel(x, y, 9)

    # Flat ash tip at x1
    yc_end = int(y0 + dy)
    for y in range(yc_end - 5, yc_end + 6):
        fb.draw_pixel(x1, y, 0)

    # 2. Right-Margin Smoke (x strictly 166..195, never < 162)
    smokeOriginX = x1 + 2  # 167
    smokeOriginY = yc_end - 2 # 131

    for y_step in range(smokeOriginY - 36):
        sy = smokeOriginY - y_step
        t = y_step / float(smokeOriginY - 36)
        
        # Smooth organic drift
        center_x = 171.0 + t * 9.0
        wave = math.sin(t * 7.0 - iterFrame * 0.8) * (2.0 + t * 6.0)
        sx = int(center_x + wave)
        
        # Clamp strictly
        if sx < 166: sx = 166
        if sx > 194: sx = 194
        
        fb.draw_pixel(sx, sy, 0)
        if y_step < 25:
            if sx + 1 <= 195: fb.draw_pixel(sx + 1, sy, 0)
        elif y_step < 55:
            if (sx + sy) % 3 != 0 and sx + 1 <= 195: fb.draw_pixel(sx + 1, sy, 0)
            if (sx * sy) % 5 == 0 and sx - 1 >= 166: fb.draw_pixel(sx - 1, sy, 0)
        else:
            fb.draw_dither_pixel(sx, sy, 6)
            if sx + 1 <= 195: fb.draw_dither_pixel(sx + 1, sy, 8)
            if sx - 1 >= 166: fb.draw_dither_pixel(sx - 1, sy, 8)

    # Floating smoke ring at top right
    ring_phase = (iterFrame * 4) % 24
    ringY = 58 - ring_phase
    ringX = 181 + ((iterFrame * 2) % 6)
    if ringX >= 172 and ringX + 4 <= 195 and ringY >= 36:
        fb.draw_circle(ringX, ringY, 4, 0, False)
        fb.draw_circle(ringX + 2, ringY - 4, 3, 0, False)

# Render Full Face
fb = Framebuffer()
# Top bar
fb.draw_line(10, 18, 22, 18, 0, 2)
fb.draw_line(164, 8, 186, 8, 0, 1)
fb.draw_line(164, 18, 186, 18, 0, 1)

draw_rudo_eye_v1(fb, 58, 92, 0, 0, 0)       # Left eye open
draw_rudo_eye_v1(fb, 142, 92, 0, 0, 1)      # Right eye squint (winking badass)
draw_rudo_brows(fb, 92)
draw_rudo_nose_v1(fb)
draw_rudo_mouth(fb, 100, 138)
draw_cigar_and_smoke_v1(fb, 100, 138, 0)
fb.draw_line(16, 162, 184, 162, 0, 1)

img = fb.to_image(scale=3)
img.save("scratch/rudo_v1_idle0.png")
print("Saved scratch/rudo_v1_idle0.png")

# Also render with both eyes open
fb2 = Framebuffer()
fb2.draw_line(10, 18, 22, 18, 0, 2)
fb2.draw_line(164, 8, 186, 8, 0, 1)
fb2.draw_line(164, 18, 186, 18, 0, 1)
draw_rudo_eye_v1(fb2, 58, 92, 0, 0, 0)
draw_rudo_eye_v1(fb2, 142, 92, 0, 0, 0)
draw_rudo_brows(fb2, 92)
draw_rudo_nose_v1(fb2)
draw_rudo_mouth(fb2, 100, 138)
draw_cigar_and_smoke_v1(fb2, 100, 138, 1)
fb2.draw_line(16, 162, 184, 162, 0, 1)
img2 = fb2.to_image(scale=3)
img2.save("scratch/rudo_v1_both_open.png")
print("Saved scratch/rudo_v1_both_open.png")
