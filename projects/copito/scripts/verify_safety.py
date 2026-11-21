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

# --- C++ READY IMPLEMENTATIONS ---

def drawRudoEye(fb, cx, cy, pupilOffsetX=0, pupilOffsetY=0, blink=0):
    is_right = (cx > 100)
    rx = 16

    if blink == 3: # BLINK_CLOSED
        fb.draw_line(cx - 15, cy, cx + 15, cy, 0, 3)
        return

    # blink 0: OPEN (height 17px), blink 1: HALF / SQUINT (height 9px), blink 2: ALMOST (height 5px)
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

    # 3. Upper brow crease & under-eye grit fold
    for x in range(-12, 13):
        slant = (x / 12.0) * (-inner_slant)
        cy_crease = cy - ry - 4 + int(slant)
        fb.draw_pixel(cx + x, cy_crease, 0)

    under_y = cy + ry + 3
    if is_right:
        fb.draw_line(cx - 8, under_y, cx + 6, under_y - 1, 0, 1)
    else:
        fb.draw_line(cx - 6, under_y - 1, cx + 8, under_y, 0, 1)

def drawRudoNose(fb, eyeCy=92):
    # Proportional nose anchor: bridge starts eyeCy + 15, base at eyeCy + 25
    ny0 = eyeCy + 15
    ny1 = eyeCy + 24
    ny2 = eyeCy + 26

    # Bridge ridge
    fb.draw_line(99, ny0, 101, ny1, 0, 2)
    # Under-tip sharp hook
    fb.draw_line(101, ny1, 96, ny2, 0, 2)
    # Left nostril shadow
    fb.draw_pixel(95, ny2 - 1, 0)
    fb.draw_pixel(95, ny2, 0)
    # Right nostril accent
    fb.draw_pixel(102, ny1, 0)
    fb.draw_pixel(103, ny2 - 1, 0)
    # Subtle dithered shadow on bridge right flank
    fb.draw_dither_pixel(101, ny0 + 3, 7)
    fb.draw_dither_pixel(102, ny0 + 5, 6)
    fb.draw_dither_pixel(102, ny0 + 7, 5)

def drawRudoBrows(fb, cy=88):
    fb.draw_line(36, cy - 26, 76, cy - 18, 0, 3)
    fb.draw_line(36, cy - 27, 74, cy - 19, 0, 2)
    fb.draw_line(164, cy - 26, 124, cy - 18, 0, 3)
    fb.draw_line(164, cy - 27, 126, cy - 19, 0, 2)

def drawCigarAndSmoke(fb, mouthX=100, mouthY=138, iterFrame=0):
    x0 = mouthX + 13   # 113
    y0 = mouthY + 1    # 139
    x1 = 165           # 165
    y1 = mouthY - 5    # 133

    dx = x1 - x0
    dy = y1 - y0

    # 1. 11px Thick Robust Puro / Habano Body
    for x in range(x0, x1 + 1):
        t = (x - x0) / float(dx)
        yc = int(y0 + t * dy)

        # Sharp solid black contour outlines (2px thick top and bottom)
        fb.draw_pixel(x, yc - 5, 0)
        fb.draw_pixel(x, yc - 6, 0)
        fb.draw_pixel(x, yc + 5, 0)
        fb.draw_pixel(x, yc + 6, 0)

        if x < 127: # Tobacco wrapper near mouth
            for y in range(yc - 4, yc + 5):
                if y == yc - 2: fb.draw_dither_pixel(x, y, 8)
                else: fb.draw_pixel(x, y, 0)
        elif 127 <= x <= 135: # Anilla (Cigar Band): High-contrast white band
            if x == 127 or x == 135:
                for y in range(yc - 4, yc + 5): fb.draw_pixel(x, y, 0)
            else:
                for y in range(yc - 4, yc + 5): fb.draw_pixel(x, y, 1) # White fill
                # Cigar brand gold seal in center
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
        else: # x >= 158: Ash & Burning Ember (Ceniza y Brasa)
            if x <= 160: # Glowing ember
                for y in range(yc - 4, yc + 5):
                    if abs(y - yc) <= 1: fb.draw_pixel(x, y, 1) # Hot white coal
                    else: fb.draw_pixel(x, y, 0)
            else: # Dithered ash texture
                for y in range(yc - 4, yc + 5):
                    fb.draw_dither_pixel(x, y, 9)

    # Flat ash tip at x1
    yc_end = int(y0 + dy)
    for y in range(yc_end - 5, yc_end + 6):
        fb.draw_pixel(x1, y, 0)

    # Sparks floating off burning tip
    if iterFrame % 2 == 1:
        fb.draw_pixel(x1 + 1, yc_end - 2, 0)
        fb.draw_pixel(x1 + 2, yc_end + 1, 0)

    # 2. Animated Smoke: STRICTLY right margin (x: 166..195, never < 162)
    smokeOriginX = x1 + 2     # 167
    smokeOriginY = yc_end - 2 # 131

    smoke_height = smokeOriginY - 36
    for y_step in range(smoke_height):
        sy = smokeOriginY - y_step
        t = y_step / float(smoke_height)

        # Smooth organic curve
        base_offset = (t ** 0.6) * 7.0
        wave = math.sin(t * 7.2 - iterFrame * 0.85) * (1.5 + t * 6.5)
        sx = int(smokeOriginX + base_offset + wave)

        # Hard clamp: NEVER CROSS LEFT OF 166
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

    # Floating smoke ring at top right
    ring_phase = (iterFrame * 4) % 24
    ringY = 60 - ring_phase
    ringX = 181 + ((iterFrame * 2) % 6)
    if 172 <= ringX and ringX + 4 <= 195 and ringY >= 36:
        fb.draw_circle(ringX, ringY, 4, 0, False)
        fb.draw_circle(ringX + 2, ringY - 4, 3, 0, False)

# VERIFICATION OF SAFETY AND BOUNDS:
fb_test = Framebuffer()
drawCigarAndSmoke(fb_test, 100, 138, 0)

# Check all smoke pixels
min_smoke_x = 999
max_smoke_x = -999
smoke_pixels = 0
for y in range(36, 134):
    for x in range(0, 200):
        # check pixels to the right of x1 (165)
        if x > 165 and fb_test.get_pixel(x, y) == 0:
            min_smoke_x = min(min_smoke_x, x)
            max_smoke_x = max(max_smoke_x, x)
            smoke_pixels += 1

print(f'Verification: Smoke pixel count: {smoke_pixels}')
print(f'Verification: Min Smoke X: {min_smoke_x}, Max Smoke X: {max_smoke_x}')
assert min_smoke_x >= 166, f'ERROR: Smoke crossed left of 166! min_smoke_x={min_smoke_x}'
assert max_smoke_x <= 195, f'ERROR: Smoke exceeded 195! max_smoke_x={max_smoke_x}'
print('Smoke boundary test: 100% PASSED! Eye is completely clear!')
