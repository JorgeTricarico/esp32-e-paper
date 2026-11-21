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
        for i in range(EPD_BUFFER_SIZE):
            self.buf[i] = val

    def draw_pixel(self, x, y, color):
        if x < 0 or x >= EPD_WIDTH or y < 0 or y >= EPD_HEIGHT:
            return
        idx = y * 25 + (x >> 3)
        bit = 7 - (x & 0x07)
        if color:
            self.buf[idx] |= (1 << bit)
        else:
            self.buf[idx] &= ~(1 << bit)

    def get_pixel(self, x, y):
        if x < 0 or x >= EPD_WIDTH or y < 0 or y >= EPD_HEIGHT:
            return 1
        idx = y * 25 + (x >> 3)
        bit = 7 - (x & 0x07)
        return 1 if (self.buf[idx] & (1 << bit)) else 0

    def draw_dither_pixel(self, x, y, gray_level_16):
        if x < 0 or x >= EPD_WIDTH or y < 0 or y >= EPD_HEIGHT:
            return
        bayer = [
            [  0,  8,  2, 10 ],
            [ 12,  4, 14,  6 ],
            [  3, 11,  1,  9 ],
            [ 15,  7, 13,  5 ]
        ]
        threshold = bayer[y & 3][x & 3]
        self.draw_pixel(x, y, 1 if gray_level_16 > threshold else 0)

    def draw_line(self, x0, y0, x1, y1, color, thickness=1):
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            if thickness <= 1:
                self.draw_pixel(x0, y0, color)
            else:
                half = thickness // 2
                for ty in range(-half, half + 1):
                    for tx in range(-half, half + 1):
                        self.draw_pixel(x0 + tx, y0 + ty, color)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def draw_circle(self, xm, ym, r, color, fill=False):
        x = -r
        y = 0
        err = 2 - 2 * r
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
            if r_val <= y:
                y += 1
                err += y * 2 + 1
            if r_val > x or err > y:
                x += 1
                err += x * 2 + 1
            if x >= 0:
                break

    def draw_rect(self, x, y, w, h, color, fill=False):
        if fill:
            for cy in range(y, y + h):
                self.draw_line(x, cy, x + w - 1, cy, color, 1)
        else:
            self.draw_line(x, y, x + w - 1, y, color, 1)
            self.draw_line(x, y + h - 1, x + w - 1, y + h - 1, color, 1)
            self.draw_line(x, y, x, y + h - 1, color, 1)
            self.draw_line(x + w - 1, y, x + w - 1, y + h - 1, color, 1)

    def to_image(self, scale=3):
        img = Image.new('RGB', (EPD_WIDTH * scale, EPD_HEIGHT * scale), (250, 250, 245))
        draw = ImageDraw.Draw(img)
        for y in range(EPD_HEIGHT):
            for x in range(EPD_WIDTH):
                if self.get_pixel(x, y) == 0:
                    draw.rectangle([x * scale, y * scale, (x + 1) * scale - 1, (y + 1) * scale - 1], fill=(22, 22, 26))
        return img

# Original anime cute eye
def draw_detailed_eye(fb, cx, cy, pupilOffsetX, pupilOffsetY, blink):
    if blink == 3: # BLINK_CLOSED
        for x in range(-24, 25):
            norm = x / 24.0
            y = cy + 4 - int((1.0 - norm * norm) * 10.0)
            for t in range(4):
                fb.draw_pixel(cx + x, y + t, 0)
        return

    rx, ry = 16, 20
    if blink == 1: ry = 12
    if blink == 2: ry = 5

    for y in range(-ry, ry + 1):
        for x in range(-rx, rx + 1):
            dist = (x * x) / (rx * rx) + (y * y) / (ry * ry)
            if dist <= 1.0:
                fb.draw_pixel(cx + x, cy + y, 0)

    if blink >= 2: return
    # 3 big anime shiny bubbles
    fb.draw_circle(cx - 6 + pupilOffsetX, cy - 7 + pupilOffsetY, 6, 1, True)
    fb.draw_circle(cx + 7 + pupilOffsetX, cy + 6 + pupilOffsetY, 3, 1, True)
    fb.draw_circle(cx - 2 + pupilOffsetX, cy + 8 + pupilOffsetY, 2, 1, True)

    for x in range(-16, 17):
        ey = cy - ry - 4 - int(math.cos(x * 0.1) * 2)
        fb.draw_pixel(cx + x, ey, 0)

# New Rudo Badass Eye
def draw_rudo_eye(fb, cx, cy, pupilOffsetX, pupilOffsetY, blink):
    is_right = (cx > 100)
    rx = 16
    
    if blink == 3: # BLINK_CLOSED
        # Determined horizontal line with slight sharp temple flick
        fb.draw_line(cx - 16, cy, cx + 16, cy, 0, 3)
        if is_right:
            fb.draw_line(cx + 14, cy - 1, cx + 18, cy - 3, 0, 2)
        else:
            fb.draw_line(cx - 14, cy - 1, cx - 18, cy - 3, 0, 2)
        return

    ry = 10
    if blink == 1: ry = 5   # BLINK_HALF (badass squint)
    if blink == 2: ry = 3   # BLINK_ALMOST

    # 1. Dark almond socket / shape with slant
    # The upper lid slants down towards the nose bridge
    for x in range(-rx, rx + 1):
        nx = x / float(rx) # -1.0 to +1.0
        # signed direction toward nose
        to_nose = -nx if is_right else nx
        
        # Upper lid profile (higher at temple, lower at nose)
        lid_slant = int(to_nose * 2.0)
        arch = int((1.0 - nx * nx) * ry)
        
        y_top = cy - arch + lid_slant
        y_bot = cy + int(arch * 0.7) + lid_slant // 2
        
        # Draw solid black socket
        for y in range(y_top, y_bot + 1):
            fb.draw_pixel(cx + x, y, 0)

    # 2. Bold heavy upper eyelid (3px thick)
    for x in range(-rx - 2, rx + 3):
        nx = x / float(rx)
        to_nose = -nx if is_right else nx
        lid_slant = int(to_nose * 2.0)
        arch = int((1.0 - min(1.0, nx * nx)) * ry)
        y_top = cy - arch + lid_slant
        fb.draw_pixel(cx + x, y_top - 1, 0)
        fb.draw_pixel(cx + x, y_top - 2, 0)
        fb.draw_pixel(cx + x, y_top - 3, 0)

    # 3. Inner White Sclera & Focused Pupil (Piercing intense gaze)
    if blink < 2:
        # Carve white sclera inside
        sclera_ry = ry - 2
        sclera_rx = rx - 3
        for x in range(-sclera_rx, sclera_rx + 1):
            nx = x / float(sclera_rx)
            to_nose = -nx if is_right else nx
            lid_slant = int(to_nose * 2.0)
            arch = int((1.0 - nx * nx) * sclera_ry)
            y_top = cy - arch + lid_slant + 1
            y_bot = cy + int(arch * 0.6) + lid_slant // 2 - 1
            for y in range(y_top, y_bot + 1):
                fb.draw_pixel(cx + x, y, 1)

        # Intense dark iris & pupil (diameter ~11px)
        iris_cx = cx + pupilOffsetX
        iris_cy = cy + (lid_slant if blink == 1 else 0) + pupilOffsetY
        iris_r = 5 if blink == 0 else 4
        for y in range(-iris_r, iris_r + 1):
            for x in range(-iris_r, iris_r + 1):
                if x * x + y * y <= iris_r * iris_r:
                    fb.draw_pixel(iris_cx + x, iris_cy + y, 0)

        # Single pin-point cold specular glint (1x1 or 2x1 dot only, NOT huge anime balls!)
        if blink == 0:
            fb.draw_pixel(iris_cx - 2, iris_cy - 2, 1)
            fb.draw_pixel(iris_cx - 1, iris_cy - 2, 1)

    # 4. Crease / brow accent line above eye
    crease_y = cy - ry - 6
    if is_right:
        fb.draw_line(cx - 14, crease_y + 3, cx + 12, crease_y, 0, 1)
        # Crow's feet wrinkle at temple
        fb.draw_line(cx + 17, cy - 2, cx + 22, cy - 5, 0, 1)
    else:
        fb.draw_line(cx - 12, crease_y, cx + 14, crease_y + 3, 0, 1)
        fb.draw_line(cx - 17, cy - 2, cx - 22, cy - 5, 0, 1)

# Rudo Brows
def draw_rudo_brows(fb, cy=88):
    fb.draw_line(36, cy - 26, 76, cy - 18, 0, 3)
    fb.draw_line(36, cy - 27, 74, cy - 19, 0, 2)
    fb.draw_line(164, cy - 26, 124, cy - 18, 0, 3)
    fb.draw_line(164, cy - 27, 126, cy - 19, 0, 2)

# Character Nose
def draw_rudo_nose(fb):
    # Angular bridge & nostril shadow at (100, 108..120)
    # Bridge slant: from (99, 107) down-right to (101, 116)
    fb.draw_line(99, 107, 101, 116, 0, 2)
    # Under-nose base angle: from (101, 116) back to (96, 118)
    fb.draw_line(101, 116, 96, 118, 0, 2)
    # Nostril accents
    fb.draw_pixel(95, 117, 0)
    fb.draw_pixel(95, 118, 0)
    fb.draw_pixel(102, 117, 0)

# Mouth (MOUTH_REST)
def draw_mouth_rest(fb, cx=100, cy=138):
    for x in range(-20, 21):
        norm = x / 20.0
        y = cy + int((1.0 - norm * norm) * 7.0)
        fb.draw_pixel(cx + x, y, 0)
        fb.draw_pixel(cx + x, y + 1, 0)
        fb.draw_pixel(cx + x, y + 2, 0)
    fb.draw_pixel(cx - 21, cy - 1, 0)
    fb.draw_pixel(cx - 22, cy - 2, 0)
    fb.draw_pixel(cx + 21, cy - 1, 0)
    fb.draw_pixel(cx + 22, cy - 2, 0)

# Robust Cigar (10-12px) & Safe Right-Margin Smoke
def draw_cigar_and_smoke(fb, mouthX=100, mouthY=138, iterFrame=0):
    x0 = mouthX + 14   # 114
    y0 = mouthY + 1    # 139
    x1 = 165           # 165 (well clear of eye at 126..158)
    y1 = mouthY - 4    # 134

    # 1. Thick Cigar Body (11 px thick: offset -5 to +5)
    dx = x1 - x0
    dy = y1 - y0
    
    for x in range(x0, x1 + 1):
        t = (x - x0) / float(dx)
        yc = int(y0 + t * dy)
        
        # Solid black top and bottom contour (2px thick)
        fb.draw_pixel(x, yc - 5, 0)
        fb.draw_pixel(x, yc - 6, 0)
        fb.draw_pixel(x, yc + 5, 0)
        fb.draw_pixel(x, yc + 6, 0)
        
        # Cigar body fill
        if x < 128:
            # Tobacco wrapper near mouth: dark with subtle leaf highlight
            for y in range(yc - 4, yc + 5):
                if y == yc - 2:
                    fb.draw_dither_pixel(x, y, 8) # subtle specular highlight
                else:
                    fb.draw_pixel(x, y, 0)
        elif x >= 128 and x <= 135:
            # Cigar Band (Anilla de puro): HIGH CONTRAST WHITE BAND
            # Black vertical borders
            if x == 128 or x == 135:
                for y in range(yc - 4, yc + 5):
                    fb.draw_pixel(x, y, 0)
            else:
                # White fill
                for y in range(yc - 4, yc + 5):
                    fb.draw_pixel(x, y, 1)
                # Golden / black diamond seal emblem in center
                if (x == 131 or x == 132) and abs(y - yc) <= 1:
                    fb.draw_pixel(x, yc, 0)
                    fb.draw_pixel(x, yc - 1, 0)
                    fb.draw_pixel(x, yc + 1, 0)
        elif x > 135 and x < 158:
            # Main cigar body: dark tobacco wrapper
            for y in range(yc - 4, yc + 5):
                if y == yc - 2:
                    fb.draw_dither_pixel(x, y, 8)
                else:
                    fb.draw_pixel(x, y, 0)
        else: # x >= 158: Ash & Ember (Ceniza y Brasa)
            # Burning ember interface at x=158..160
            if x <= 160:
                for y in range(yc - 4, yc + 5):
                    if abs(y - yc) <= 1:
                        fb.draw_pixel(x, y, 1) # Hot white glowing ember
                    else:
                        fb.draw_pixel(x, y, 0) # Charred rim
            else: # x = 161..165: Ash tip
                for y in range(yc - 4, yc + 5):
                    # Dithered gray ash texture
                    fb.draw_dither_pixel(x, y, 9)

    # Flat ash tip vertical edge at x1
    t_end = 1.0
    yc_end = int(y0 + t_end * dy)
    for y in range(yc_end - 5, yc_end + 6):
        fb.draw_pixel(x1, y, 0)
    # A couple of ash crumb / burning speckles
    fb.draw_pixel(x1 + 1, yc_end - 2, 0)
    fb.draw_pixel(x1 + 2, yc_end + 1, 0)

    # 2. Animated Smoke: STRICTLY on the right margin (x: 166..195, NEVER < 162)
    smokeOriginX = x1 + 2  # 167
    smokeOriginY = yc_end - 2 # 132

    # Billowing undulating smoke column rising to y=36
    smoke_height = smokeOriginY - 36
    for y_step in range(smoke_height):
        t = y_step / float(smoke_height)
        sy = smokeOriginY - y_step
        
        # Smoke drifts slightly rightwards as it rises: 170 -> 180
        center_x = 171.0 + t * 9.0
        wave = math.sin(t * 7.5 - iterFrame * 0.85) * (2.0 + t * 6.5)
        sx = int(center_x + wave)
        
        # HARD CLAMP: NEVER CROSS LEFT OF 166, NEVER EXCEED 195
        if sx < 166: sx = 166
        if sx > 195: sx = 195
        
        fb.draw_pixel(sx, sy, 0)
        # Smoke thickness varies:
        if y_step < 20:
            # Base thin stream
            if sx + 1 <= 195: fb.draw_pixel(sx + 1, sy, 0)
        elif y_step < 50:
            # Mid-body curling tendril
            if (sx + sy) % 3 != 0:
                if sx + 1 <= 195: fb.draw_pixel(sx + 1, sy, 0)
                if sx - 1 >= 166: fb.draw_pixel(sx - 1, sy, 0)
        else:
            # High billows: airy dithered texture
            fb.draw_dither_pixel(sx, sy, 6)
            if sx + 1 <= 195: fb.draw_dither_pixel(sx + 1, sy, 8)
            if sx - 1 >= 166: fb.draw_dither_pixel(sx - 1, sy, 8)

    # Secondary drifting smoke ring puff at top right
    ring_phase = (iterFrame * 4) % 24
    ringY = 62 - ring_phase
    ringX = 180 + ((iterFrame * 2) % 6)
    if ringX >= 170 and ringX + 4 <= 195 and ringY >= 36:
        fb.draw_circle(ringX, ringY, 4, 0, False)
        fb.draw_circle(ringX + 2, ringY - 5, 3, 0, False)

# Full frame render: Rudo Idle
def render_rudo_idle(frame_idx=0):
    fb = Framebuffer()
    # Simple top bar
    fb.draw_line(10, 18, 22, 18, 0, 2)
    fb.draw_line(164, 8, 186, 8, 0, 1)
    fb.draw_line(164, 18, 186, 18, 0, 1)

    pupilX = -3 if (frame_idx % 4 == 1) else (3 if (frame_idx % 4 == 3) else 0)
    pupilY = 0
    blinkL = 0
    blinkR = 1 if (frame_idx % 2 == 1) else 0 # Badass squint on right eye!

    # Eyes
    draw_rudo_eye(fb, 58, 92, pupilX, pupilY, blinkL)
    draw_rudo_eye(fb, 142, 92, pupilX, pupilY, blinkR)

    # Brows
    draw_rudo_brows(fb, 92)

    # Nose
    draw_rudo_nose(fb)

    # Mouth & Habano
    draw_mouth_rest(fb, 100, 138)
    draw_cigar_and_smoke(fb, 100, 138, frame_idx)

    # Bottom label
    fb.draw_line(16, 162, 184, 162, 0, 1)
    return fb

os.makedirs("scratch", exist_ok=True)
img0 = render_rudo_idle(0).to_image(scale=3)
img0.save("scratch/rudo_idle_frame0.png")
img1 = render_rudo_idle(1).to_image(scale=3)
img1.save("scratch/rudo_idle_frame1.png")
print("Rendered scratch/rudo_idle_frame0.png and frame1.png successfully!")
